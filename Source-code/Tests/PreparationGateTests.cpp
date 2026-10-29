// v0.5.16 shared preparation limit + background delay (portable).
//  1. Turn counts: background turns never exceed the PREPARATION CPU thread
//     budget; Quiet allows two foreground turns (one core each).
//  2. Never more workers than turns prepare at once.
//  3. Foreground claims are served first come, first served; a worker that
//     releases and claims again queues behind the waiting ones.
//  4. A background claim never queues and never takes a turn while a
//     foreground claim waits.
//  5. Urgent claims (offline render) never wait.
//  6. A waiting claim is cancellable, leaves the queue and reports onWait once.
//  7. Background keys wait for backgroundDelayMs() after the last foreground
//     or urgent work, and the full delay while it runs or waits.
//  8. System slots (other processes): a turn also needs a free system slot,
//     each enter is matched by an exit on the same thread.
#include "../Source/ParallelRender.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
using namespace pt::parallel;
static void sleepMs(int ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
static bool waitFor(const std::function<bool()>& f,int ms=2000){for(int i=0;i<ms;++i){if(f())return true;sleepMs(1);}return f();}
static void setTurns(unsigned background,unsigned foreground){instanceSlotsOverride().store(background);foregroundSlotsOverride().store(foreground);}

// Simulates other processes holding system slots.
struct FakeSystemSlots final : SystemSlots {
    std::mutex m; std::map<unsigned,std::thread::id> owner; std::vector<bool> external=std::vector<bool>(kMaxSlots,false);
    int enters=0, exits=0; bool wrongThread=false;
    bool tryEnter(unsigned i) override {
        const std::lock_guard<std::mutex> l(m);
        if(external[i] || owner.count(i))return false;
        owner[i]=std::this_thread::get_id();++enters;return true;
    }
    void exit(unsigned i) override {
        const std::lock_guard<std::mutex> l(m);
        if(!owner.count(i) || owner[i]!=std::this_thread::get_id())wrongThread=true;
        owner.erase(i);++exits;
    }
};

int main() {
  try {
    auto& gate=preparationGate();
    // 1. turn counts
    check(effort().load()==Quiet && instanceSlots()==1,"Quiet: one background turn");
    if(std::thread::hardware_concurrency()>=2)check(foregroundSlots()==2,"Quiet: two foreground turns");
    check(budgetThreadsForEffort(Quiet,16)==1 && budgetThreadsForEffort(Balanced,16)==8 && budgetThreadsForEffort(Fast,32)==31,"budget per effort");
    for(int e:{Quiet,Balanced,Fast})for(unsigned hw:{1u,2u,4u,8u,12u,16u,32u,64u}) {
        const unsigned perInstance=threadsForEffort(e,hw);
        const unsigned slots=(std::max)(1u,budgetThreadsForEffort(e,hw)/perInstance);
        check(slots>=1 && (slots==1 || slots*perInstance<=budgetThreadsForEffort(e,hw)),"background turns * threads exceed the budget");
    }
    effort().store(Balanced);check(foregroundSlots()==instanceSlots(),"Balanced: foreground turns = background turns");
    effort().store(Quiet);
    backgroundDelayOverride().store(0);
    // 2. eight foreground workers compete; at most the foreground turns run at once.
    for(unsigned turns:{1u,2u}) {
        setTurns(1,turns);
        std::atomic<int> running{0},maxRunning{0},done{0};
        std::vector<std::thread> workers;
        for(int i=0;i<8;++i)workers.emplace_back([&]{
            PreparationSlot slot;
            check(slot.claim(Priority::Foreground,[]{return false;}),"claim failed without cancellation");
            const int now=++running;int seen=maxRunning.load();
            while(now>seen && !maxRunning.compare_exchange_weak(seen,now)){}
            sleepMs(15);--running;slot.release();++done;
        });
        for(auto& t:workers)t.join();
        check(done==8,"every instance must get its turn");
        check(maxRunning.load()<=int(turns),"more instances prepared at once than the shared limit");
        check(gate.activeCount()==0 && gate.waitingCount()==0,"turns or queue entries leaked");
    }
    // 3. first come, first served; a releaser that claims again queues last.
    {
        setTurns(1,1);
        PreparationSlot a;check(a.claim(Priority::Foreground),"A");
        std::mutex orderLock;std::vector<int> order;
        const auto worker=[&](int id){PreparationSlot s;check(s.claim(Priority::Foreground,[]{return false;}),"queued claim");
            {const std::lock_guard<std::mutex> l(orderLock);order.push_back(id);}sleepMs(5);};
        std::thread f1(worker,1);check(waitFor([&]{return gate.waitingCount()==1;}),"F1 queued");
        std::thread f2(worker,2);check(waitFor([&]{return gate.waitingCount()==2;}),"F2 queued");
        a.release();
        check(a.claim(Priority::Foreground,[]{return false;}),"A again");   // must wait behind F1 and F2
        {const std::lock_guard<std::mutex> l(orderLock);check(order.size()==2 && order[0]==1 && order[1]==2,"turns not served in arrival order (or A jumped the queue)");}
        a.release();f1.join();f2.join();
        check(gate.activeCount()==0 && gate.waitingCount()==0,"leak after ordering test");
    }
    // 4. background never queues and never jumps a waiting foreground claim.
    {
        setTurns(1,2);
        PreparationSlot b0;check(b0.claim(Priority::Background),"free background turn");
        PreparationSlot b1;check(!b1.claim(Priority::Background),"second background turn beyond the background limit");
        PreparationSlot f;check(f.claim(Priority::Foreground),"foreground uses the second turn");
        PreparationSlot f2;std::atomic<bool> f2Has{false};
        std::thread t([&]{check(f2.claim(Priority::Foreground,[]{return false;}),"F2");f2Has=true;while(f2Has)sleepMs(1);f2.release();});
        check(waitFor([&]{return gate.waitingCount()==1;}),"F2 queued");
        b0.release();
        PreparationSlot b2;check(!b2.claim(Priority::Background),"background took a turn while a foreground claim waits");
        check(waitFor([&]{return f2Has.load();}),"F2 never got the freed turn");
        f2Has=false;t.join();f.release();
        check(gate.activeCount()==0,"leak after background test");
    }
    // 5. urgent never waits, even when every turn is taken.
    {
        setTurns(1,1);
        PreparationSlot a;check(a.claim(Priority::Foreground),"A3");
        PreparationSlot u;const auto t0=std::chrono::steady_clock::now();
        check(u.claim(Priority::Urgent,[]{return false;}),"urgent claim");
        check(std::chrono::steady_clock::now()-t0<std::chrono::milliseconds(50),"urgent claim waited");
        check(gate.activeCount()==2 && gate.limitedCount()==1,"urgent turn is counted apart");
        u.release();a.release();
    }
    // 6. cancellation and onWait.
    {
        setTurns(1,1);
        int immediateWaits=0;PreparationSlot free;check(free.claim(Priority::Foreground,{},[&]{++immediateWaits;}),"free turn");
        check(immediateWaits==0,"onWait called for a turn that was free");
        std::atomic<bool> cancel{false},returned{false},got{true};std::atomic<int> waits{0};
        std::thread w([&]{PreparationSlot s;got=s.claim(Priority::Foreground,[&]{return cancel.load();},[&]{++waits;});returned=true;});
        check(waitFor([&]{return waits.load()==1;}),"onWait not called for a waiting claim");
        sleepMs(60);check(!returned && waits.load()==1,"onWait must run once; claim returned while the turn was busy");
        cancel=true;check(waitFor([&]{return returned.load();}),"cancelled claim never returned");
        w.join();check(!got,"a cancelled claim must return false");
        check(gate.waitingCount()==0,"a cancelled claim stayed in the queue");
        free.release();
        PreparationSlot b;check(b.claim(Priority::Background),"a cancelled foreground claim still blocks background work");
        b.release();
    }
    // 7. background delay
    {
        setTurns(1,1);backgroundDelayOverride().store(300);
        PreparationSlot f;check(f.claim(Priority::Foreground),"F7");
        check(gate.backgroundWaitMs()>=300,"background keys must wait while foreground work runs");
        f.release();
        const int left=gate.backgroundWaitMs();
        check(left>200 && left<=300,"the delay starts when the foreground work ends");
        sleepMs(150);check(gate.backgroundWaitMs()>0,"background keys started before the delay");
        sleepMs(250);check(gate.backgroundWaitMs()==0,"background keys never allowed after the delay");
        PreparationSlot u;check(u.claim(Priority::Urgent),"U7");
        check(gate.backgroundWaitMs()>=300,"an urgent (export) turn counts as foreground");
        u.release();sleepMs(350);
        PreparationSlot b;check(b.claim(Priority::Background),"B7");
        check(gate.backgroundWaitMs()==0,"background work alone does not delay background keys");
        check(b.claim(Priority::Foreground),"promote");
        check(gate.backgroundWaitMs()>=300,"promoted turn must count as foreground");
        b.release();
        check(gate.backgroundWaitMs()>200,"promoted turn restarts the delay");
        check(kBackgroundDelayMs==5000,"default background delay");
        backgroundDelayOverride().store(0);
    }
    // 8. system slots
    {
        FakeSystemSlots sys;gate.setSystemSlots(&sys);
        setTurns(1,2);
        sys.external[0]=true;   // another process holds slot 0
        PreparationSlot f;check(f.claim(Priority::Foreground),"foreground takes system slot 1");
        PreparationSlot b;check(!b.claim(Priority::Background),"background may only use slot 0, held by another process");
        std::atomic<bool> got{false};
        std::thread t([&]{PreparationSlot s;check(s.claim(Priority::Foreground,[]{return false;}),"waits for a system slot");got=true;s.release();});
        sleepMs(80);check(!got,"a turn was granted without a free system slot");
        {const std::lock_guard<std::mutex> l(sys.m);sys.external[0]=false;}   // the other process ends
        check(waitFor([&]{return got.load();}),"the freed system slot was never used");
        t.join();f.release();
        PreparationSlot u;check(u.claim(Priority::Urgent),"urgent ignores system slots");u.release();
        check(sys.enters==sys.exits && sys.enters==2 && !sys.wrongThread,"system slot enter/exit unbalanced or on another thread");
        gate.setSystemSlots(nullptr);
    }
    setTurns(0,0);backgroundDelayOverride().store(-1);
    std::cout<<"PASS shared preparation turns (FIFO, urgent, Quiet x2, system slots) + background delay\n";
    return 0;
  } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
