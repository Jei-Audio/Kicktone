// v0.5.19 PREPARATION PROGRESS (progress bar with the time left until all 37
// tail keys are ready).
//  1. pt::progress::Tracker (portable): no estimate before anything was
//     measured; speed per unit of key cost (keys below the root cost more); a smooth countdown inside a
//     batch; committed batches are remembered per configuration and scaled by
//     the work of one key (a new tail has an estimate at once); a cancelled
//     batch restores the count; the background pause adds its wait; texts.
//  2. A real processor: no tail -> "Load a tail"; loading a tail with one
//     played key shows the count rising to 37/37, the pause before the other
//     keys, an estimate while rendering that is in the right range, and the
//     final state matches the published bank; an instance waiting for another
//     one's turn says so on the bar.
//  3. A heavier setup (Extreme v5 + Root Render + crunch): the estimate at a
//     quarter, half and three quarters is within 0.5..2x of the real time left.
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
static void ck(bool b,const std::string& m){if(!b)throw std::runtime_error(m);}
static bool approx(double a,double b,double tol){return std::abs(a-b)<=tol;}
using namespace pt::progress;

static void tracker() {
    // First guess of a key's cost: 1 at the root, growing both ways, steeply downward.
    ck(keyWeight(0)==1 && keyWeight(-12)>4 && keyWeight(-12)<5.5 && keyWeight(12)>1.2 && keyWeight(12)<1.6 && keyWeight(-3)>keyWeight(3) &&
       keyWeight(-14)>keyWeight(-12) && keyWeight(20)>keyWeight(12),"key cost first guess");
    // Learned key costs replace the first guess per configuration (scaled to its units).
    {Tracker l;l.setConfig(9,2.0);
     ck(l.weightFor(-12)==keyWeight(-12) && l.weightFor(5)==keyWeight(5),"unmeasured keys use the first guess");
     l.learnKey(0,.2);l.learnKey(-12,1.8);   // per unit of work: .1 and .9
     ck(approx(l.weightFor(-12)/l.weightFor(0),9,1e-5),"learned key costs keep their measured ratio");
     ck(approx(l.weightFor(-12)*l.weightFor(0),keyWeight(-12)*keyWeight(0),1e-4),"learned key costs are scaled to the first guess units");
     ck(l.weightFor(5)==keyWeight(5),"a key not measured keeps the first guess");
     l.learnKey(0,.4);ck(approx(l.weightFor(-12)/l.weightFor(0),9/1.5,1e-5),"a new measurement is averaged in");
     l.setConfig(10,2.0);ck(l.weightFor(-12)==keyWeight(-12),"another configuration has its own key costs");
     l.setConfig(9,4.0);ck(approx(l.weightFor(-12)/l.weightFor(0),6,1e-5),"key costs are kept per unit of work");
     l.learnKey(99,1);l.learnKey(1,0);l.learnKey(2,-1);ck(l.weightFor(2)==keyWeight(2),"out-of-range or empty measurements are ignored");}
    // A batch shares the threads in order (as pt::parallel::forEach hands keys out).
    {const double four[]={1,1,1,1},heavyFirst[]={4,1,1},heavyLast[]={1,1,4};
     ck(batchCost(four,4,8)==1 && batchCost(four,4,2)==2 && batchCost(four,4,1)==4,"batch cost: threads shared");
     ck(batchCost(heavyFirst,3,2)==4 && batchCost(heavyLast,3,2)==5,"batch cost: never faster than its most expensive key, in hand-out order");
     ck(batchCost(four,0,4)==0 && batchCost(four,4,0)==4,"batch cost edge cases");}
    Tracker t;
    t.setConfig(1,2.0);t.setReady(1,37);t.setPhase(Phase::Rendering);
    auto s=t.snapshot(0);
    ck(s.ready==1 && s.total==37 && s.secondsLeft<0 && !s.complete(),"no estimate before a key was measured");
    ck(describe(s)=="1/37 keys | estimating time left...","unknown estimate text: "+describe(s));
    // A batch of 6 keys; one key takes 0.5 s (sequential).
    t.beginBatch(6,6.0,10.0);
    ck(t.snapshot(10.2).secondsLeft<0,"still unknown until the first key finished");
    t.keyDone(1,10.5);
    s=t.snapshot(10.5);
    ck(s.ready==2,"finished keys count at once");
    ck(approx(t.secondsPerKey(),.5,1e-9),"seconds per key from the first key");
    // batch: 6 x .5 - .5 elapsed = 2.5; after it: 37 - 1 - 6 = 30 keys x .5 = 15
    ck(approx(s.secondsLeft,17.5,1e-9),"time left inside a batch");
    ck(t.snapshot(11.0).secondsLeft<s.secondsLeft && approx(t.snapshot(11.0).secondsLeft,17.0,1e-9),"countdown runs between finished keys");
    // A late batch keeps a little time for its unfinished keys.
    ck(t.snapshot(30.0).secondsLeft>=.2*.5*5+30*.5-1e-9,"a late batch must not count down to zero");
    for(int k=0;k<5;++k)t.keyDone(1,11.0+k*.5);
    t.endBatch(true,13.2);   // 3.2 s for 6 keys (crunch / phase map included)
    s=t.snapshot(13.2);
    ck(s.ready==7,"committed batch count");
    ck(approx(t.secondsPerKey(),3.2/6,1e-9),"committed speed includes the whole batch");
    ck(approx(s.secondsLeft,30*3.2/6,1e-9),"time left between batches");
    // The pause before the other keys adds its wait.
    t.setPhase(Phase::Paused,4.5);
    s=t.snapshot(14);
    ck(approx(s.secondsLeft,4.5+30*3.2/6,1e-9) && s.startsIn==4.5,"paused estimate includes the wait");
    ck(describe(s)=="7/37 keys | the other keys start in 5 s | about 21 s left","paused text: "+describe(s));
    // A cancelled batch restores the count and teaches nothing.
    t.beginBatch(4,4.0,20);t.keyDone(1,21);ck(t.snapshot(21).ready==8,"key counted during the batch");
    t.endBatch(false,22);
    ck(t.snapshot(22).ready==7 && approx(t.secondsPerKey(),3.2/6,1e-9),"cancelled batch must restore the count and keep the speed");
    // A new tail twice as long (same configuration): an estimate at once, twice the time per key.
    t.setConfig(1,4.0);t.setReady(1,37);t.setPhase(Phase::Setup);
    s=t.snapshot(30);
    ck(approx(s.secondsLeft,36*2*3.2/6,1e-9),"the speed is remembered per unit of tail length");
    ck(describe(s).rfind("1/37 keys | preparing the engine | about ",0)==0,"setup text: "+describe(s));
    // Another configuration (e.g. Extreme v5 or Root Render): unknown until measured.
    t.setConfig(2,4.0);ck(t.snapshot(30).secondsLeft<0,"a different configuration has its own speed");
    t.setConfig(1,4.0);ck(t.snapshot(30).secondsLeft>0,"configurations are kept side by side");
    // Waiting for another instance's turn.
    t.setWaiting(true);s=t.snapshot(31);
    ck(s.waitingTurn && describe(s)=="1/37 keys | waiting for another Tailtone to finish","waiting text");
    t.setPhase(Phase::Rendering);ck(!t.snapshot(31).waitingTurn,"a new phase clears the waiting notice");
    // Complete and the separate punch / crunch work.
    t.setReady(37,37);t.setPhase(Phase::Ready);s=t.snapshot(40);
    ck(s.complete() && s.secondsLeft==0 && s.fraction()==1.f && describe(s)=="All 37 keys ready","complete state");
    t.setActivity(Activity::Punch);ck(describe(t.snapshot(40))=="All 37 keys ready | preparing punch notes...","punch activity text");
    t.setActivity(Activity::None);
    t.setPhase(Phase::NoTail);s=t.snapshot(41);
    ck(!s.complete() && s.ready==0 && describe(s)=="Load a tail to prepare its 37 keys","no tail state");
    // The rate table keeps the 8 most recently used configurations.
    for(uint64_t k=10;k<20;++k){t.setConfig(k,1);t.setReady(0,37);t.beginBatch(1,1,0);t.keyDone(1,1);t.endBatch(true,1);}
    t.setConfig(19,1);ck(t.snapshot(2).secondsLeft>0,"recent configuration kept");
    t.setConfig(10,1);ck(t.snapshot(2).secondsLeft<0,"oldest configuration replaced");
    // Durations.
    ck(formatSeconds(.4)=="under 1 s" && formatSeconds(7.2)=="about 8 s" && formatSeconds(59.5)=="about 60 s" &&
       formatSeconds(61)=="about 1 min" && formatSeconds(83)=="about 1 min 25 s" && formatSeconds(1250)=="about 21 min" && formatSeconds(-1).empty(),"duration texts");
    // keyDone from several threads (the render pool) is counted exactly once each.
    t.setConfig(5,1);t.setReady(0,37);t.beginBatch(32,32,0);
    {std::vector<std::thread> pool;for(int i=0;i<8;++i)pool.emplace_back([&]{for(int k=0;k<4;++k)t.keyDone(1,1);});for(auto& th:pool)th.join();}
    ck(t.snapshot(1).ready==32,"parallel keyDone counted wrongly");
    t.keyDone(1,2);ck(t.snapshot(2).ready==32,"keys beyond the batch must not count");
    t.endBatch(true,2);
    // Weighted keys: one upward key measured, the expensive -14 st key left costs 4x.
    {Tracker w;w.setConfig(8,1);w.setReady(35,37,1+4);w.setPhase(Phase::Rendering);
     w.beginBatch(1,1,0);w.keyDone(1,1);w.endBatch(true,1);
     auto x=w.snapshot(1);ck(x.ready==36 && approx(x.secondsLeft,4,1e-9),"the time left must follow the cost of the missing keys");
     w.beginBatch(1,4,10);w.keyDone(4,11);ck(w.snapshot(11).ready==37,"weighted key counted");
     w.endBatch(false,12);x=w.snapshot(12);ck(x.ready==36 && approx(x.secondsLeft,4,1e-9),"cancel restores the weight left");}
    // A batch that starts right after the previous one also counts the gap (worker overhead).
    {Tracker g;g.setConfig(7,1);g.setReady(0,37);g.setPhase(Phase::Rendering);
     g.beginBatch(1,1,0);g.keyDone(1,1);g.endBatch(true,1);ck(approx(g.secondsPerKey(),1,1e-9),"first batch");
     g.beginBatch(1,1,1.3);g.keyDone(1,2.3);g.endBatch(true,2.3);
     ck(approx(g.secondsPerKey(),(1*.75+1.3)/(1*.75+1),1e-9),"a batch right after the last one must include the gap");
     g.beginBatch(1,1,5.0);g.keyDone(1,6);g.endBatch(true,6);
     ck(approx(g.secondsPerKey(),(2.05*.75+1)/(1.75*.75+1),1e-9),"a batch after a pause must not count the pause");}
    std::cout<<"  tracker: cost model, estimates, countdown, memory per configuration, cancellation, gaps, texts\n";
}

struct TailtoneKeyMappingTest {
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}
static std::shared_ptr<const PunchTailProcessor::Bank> dry(PunchTailProcessor& p){const juce::ScopedLock l(p.assetLock);return p.bank;}
static void processor(const juce::File& punch,const juce::File& tail) {
    pt::parallel::backgroundDelayOverride().store(800);
    auto owner=std::make_unique<PunchTailProcessor>();auto& p=*owner;
    {bool noTail=false;for(int i=0;i<3000 && !noTail;++i){noTail=p.preparationProgress().phase==Phase::NoTail;juce::Thread::sleep(1);}
     ck(noTail && describe(p.preparationProgress())=="Load a tail to prepare its 37 keys","no tail: "+describe(p.preparationProgress()));}
    p.markPlayed(27);
    p.loadFile(0,punch);p.loadFile(1,tail);
    bool sawPaused=false,sawEstimate=false,sawLoading=false,monotonic=true;
    int last=-1;double halfEstimate=-1;uint32_t halfAt=0;
    const uint32_t start=juce::Time::getMillisecondCounter();
    for(int i=0;i<240000;++i) {
        const auto s=p.preparationProgress();
        auto b=dry(p);
        if(s.complete() && b && b->prepared)break;
        if(s.phase!=Phase::NoTail && s.ready<37)sawLoading=true;
        if(s.phase==Phase::Paused && !s.waitingTurn && s.startsIn>0 && s.ready<37 && describe(s).find("the other keys start in")!=std::string::npos)sawPaused=true;
        if(s.phase==Phase::Rendering && s.secondsLeft>0 && s.ready<37)sawEstimate=true;
        if(b && b->key!=0 && s.phase!=Phase::Setup) {   // after the new tail's first bank
            if(last>=0 && s.ready<last)monotonic=false;
            last=s.ready;
        }
        if(halfEstimate<0 && s.phase==Phase::Rendering && s.ready>=19 && s.secondsLeft>0){halfEstimate=s.secondsLeft;halfAt=juce::Time::getMillisecondCounter();}
        juce::Thread::sleep(1);
    }
    const uint32_t end=juce::Time::getMillisecondCounter();
    const auto s=p.preparationProgress();
    ck(s.complete() && s.ready==37 && s.secondsLeft==0 && describe(s)=="All 37 keys ready","final state: "+describe(s));
    ck(dry(p)->prepared,"bar complete before the bank");
    ck(sawLoading,"the bar never showed the keys being prepared");
    ck(sawPaused,"the bar never showed the pause before the other keys");
    ck(sawEstimate,"the bar never showed a time left while rendering");
    ck(monotonic,"the ready count went backwards");
    if(halfEstimate>0) {   // generous: the machine load varies, the order of magnitude must be right
        const double actual=(end-halfAt)*.001;
        std::cout<<"  halfway estimate "<<halfEstimate<<" s, actual "<<actual<<" s (whole load "<<(end-start)*.001<<" s)\n";
        ck(actual<.3 || (halfEstimate>actual/3 && halfEstimate<actual*3),"halfway estimate far off");
    }
    // A setting change starts again from fewer keys and has an estimate (speed remembered).
    set(p,"stretch",110);
    bool restarted=false,estimated=false;
    for(int i=0;i<240000;++i) {
        const auto x=p.preparationProgress();auto b=dry(p);
        if(x.ready<37 && x.phase!=Phase::Settling)restarted=true;
        if(restarted && x.ready<37 && x.secondsLeft>0)estimated=true;
        if(restarted && x.complete() && b && b->prepared && std::abs(b->requestedStretch-1.1)<1e-6)break;
        juce::Thread::sleep(1);
    }
    ck(restarted && estimated,"after a stretch change the bar must restart with an estimate");
    ck(p.preparationProgress().complete(),"stretch change never completed");
    pt::parallel::backgroundDelayOverride().store(-1);
    std::cout<<"  processor: count to 37/37, pause, estimate, final state, restart after a change\n";
}
// A heavier setup (Extreme v5 + Root Render + crunch on a distorted kick): the
// estimate at a quarter, half and three quarters against the real time left.
static void accuracy(const juce::File& punch,const juce::File& tail) {
    pt::parallel::backgroundDelayOverride().store(300);
    auto owner=std::make_unique<PunchTailProcessor>();auto& p=*owner;
    set(p,"pitchExtremeV5",1);set(p,"tailVerbOn",1);set(p,"tailVerbRootPitch",1);set(p,"tailVerbMix",40);set(p,"muteReverb",0);set(p,"crunchMix",40);
    p.loadFile(0,punch);p.loadFile(1,tail);
    std::array<double,3> estimate{{-1,-1,-1}};std::array<uint32_t,3> at{};const std::array<int,3> marks{{10,19,28}};
    for(int i=0;i<600000;++i) {
        const auto s=p.preparationProgress();auto b=dry(p);
        if(s.complete() && b && b->prepared)break;
        for(size_t k=0;k<marks.size();++k)
            if(estimate[k]<0 && s.phase==Phase::Rendering && s.ready>=marks[k] && s.secondsLeft>0){estimate[k]=s.secondsLeft;at[k]=juce::Time::getMillisecondCounter();}
        juce::Thread::sleep(1);
    }
    const uint32_t end=juce::Time::getMillisecondCounter();
    ck(p.preparationProgress().complete(),"heavy setup never completed: "+p.status().toStdString());
    for(size_t k=0;k<marks.size();++k) {
        ck(estimate[k]>0,"no estimate at "+std::to_string(marks[k])+"/37");
        const double actual=(end-at[k])*.001;
        std::cout<<"  Extreme v5 + Root Render + crunch, at "<<marks[k]<<"/37: estimated "<<estimate[k]<<" s, actual "<<actual<<" s\n";
        ck(actual<.3 || (estimate[k]>actual/2 && estimate[k]<actual*2),"estimate far off at "+std::to_string(marks[k])+"/37");
    }
    pt::parallel::backgroundDelayOverride().store(-1);
}
// An instance waiting for another one's turn says so on the bar.
static void waiting(const juce::File& tail) {
    pt::parallel::instanceSlotsOverride().store(1);pt::parallel::foregroundSlotsOverride().store(1);
    pt::parallel::PreparationSlot blocker;ck(blocker.claim(pt::parallel::Priority::Foreground),"blocker turn");
    auto p=std::make_unique<PunchTailProcessor>();p->loadFile(1,tail);
    bool said=false;for(int i=0;i<3000 && !said;++i){const auto s=p->preparationProgress();said=s.waitingTurn && describe(s).find("waiting for another Tailtone")!=std::string::npos;juce::Thread::sleep(1);}
    blocker.release();
    ck(said,"waiting instance: "+describe(p->preparationProgress()));
    bool cleared=false;for(int i=0;i<60000 && !cleared;++i){cleared=!p->preparationProgress().waitingTurn && dry(*p) && dry(*p)->usable();juce::Thread::sleep(1);}
    ck(cleared,"the waiting notice stayed after the turn came");
    pt::parallel::instanceSlotsOverride().store(0);pt::parallel::foregroundSlotsOverride().store(0);
    std::cout<<"  waiting for another instance's turn is shown on the bar\n";
}
};

int main(int argc,char** argv) try {
    tracker();
    juce::ScopedJuceInitialiser_GUI init;
    ck(argc>3,"usage: PreparationProgressTests <punch.wav> <tail.wav> <heavy-tail.wav>");
    const juce::File punch(juce::String::fromUTF8(argv[1])),tail(juce::String::fromUTF8(argv[2])),heavy(juce::String::fromUTF8(argv[3]));
    TailtoneKeyMappingTest::processor(punch,tail);
    TailtoneKeyMappingTest::accuracy(punch,heavy);
    TailtoneKeyMappingTest::waiting(tail);
    std::cout<<"PASS preparation progress\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
