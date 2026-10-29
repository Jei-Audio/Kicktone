#pragma once
// v0.4.65: parallel note rendering on the preparation worker.
//
// Every note of a bank (tail pitches, Root Render composite notes, punch
// lanes, crunch layers) is an independent pure function of the immutable
// source and the prepared analysis, so the notes can be rendered on several
// threads at once. Each note's output is bit-identical to the sequential
// render: the per-note code is unchanged, only the order in which notes are
// computed differs, and the results are written to fixed slots.
//
// Rules: this runs only on the preparation worker (the
// worker thread blocks here until every task has finished), never in the
// audio callback; no shared mutable state is touched by the tasks except
// their own output slot; cancellation is polled between notes as before.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_WIN32)
 // v0.5.11: NO <windows.h> here. This header reaches PluginProcessor.cpp and
 // PluginEditor.cpp, and windows.h would pour its macros (near, far, small,
 // DrawText, ...) into Tailtone's own code. Only these two kernel32 functions
 // are needed; the declarations match windows.h exactly, so both may coexist.
 extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread(void);
 extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void* thread,int priority);
#elif defined(__APPLE__)
 #include <pthread.h>
 #include <pthread/qos.h>
#elif defined(__linux__)
 #include <sys/resource.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

namespace pt::parallel {
// v0.5.10 PREPARATION CPU (user report: "CPU usage skyrockets every time I load
// a new sample"). Loading a sample renders 37 pitched tail notes, the Root
// Render wet bank, the crunch layers and up to 50 punch lanes; that work is
// unavoidable, but how many cores it takes at once is a choice:
//   Quiet    - 1 thread: slowest, barely visible (default since v0.5.12).
//   Balanced - half the hardware threads (the v0.5.10..v0.5.11 default).
//   Fast     - all hardware threads but one (the v0.4.65..v0.5.9 behaviour).
// Every render thread also runs BELOW normal priority, so the host's audio
// and UI threads win whenever they compete. Stored per computer by the editor
// (JeiAudio/Tailtone.settings "prepEffort"); the processor applies it.
enum Effort { Quiet=0, Balanced=1, Fast=2 };
inline std::atomic<int>& effort() { static std::atomic<int> value{Quiet}; return value; }   // v0.5.12: Quiet by default (user request)
inline unsigned threadsForEffort(int e,unsigned hw) {
    if(hw<=1 || e==Quiet)return 1;
    const unsigned n=e==Fast?hw-1:(std::max)(1u,hw/2);
    return (std::min)(8u,n);   // memory: every in-flight note holds its 4x streams
}
// Worker threads used for note rendering, never fewer than 1, never more than 8.
inline unsigned defaultThreads() {
    return threadsForEffort(effort().load(),std::thread::hardware_concurrency());
}
// Below-normal priority for the calling thread (render pools and the
// preparation worker). Best effort; a failure changes nothing.
inline void lowerCurrentThreadPriority() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(),-1);   // THREAD_PRIORITY_BELOW_NORMAL
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY,0);
#elif defined(__linux__)
    setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),10);
#endif
}
// Test hook / override (0 = defaultThreads()).
inline std::atomic<unsigned>& threadOverride() { static std::atomic<unsigned> value{0}; return value; }
inline unsigned threads() { const unsigned o=threadOverride().load(); return o>0?o:defaultThreads(); }

// Run body(index) for index in [0,count) on up to threads() threads
// (the calling thread is one of them). Tasks are handed out through an atomic
// counter so a few slow notes do not leave idle threads. An exception thrown
// by a task stops the hand-out and is rethrown on the calling thread after
// every running task has finished; tasks that want per-item failure
// bookkeeping (dry fallback per note) catch inside body.
inline void forEach(size_t count,const std::function<void(size_t)>& body,unsigned threadCount=0) {
    if(count==0)return;
    const unsigned wanted=threadCount>0?threadCount:threads();
    const unsigned n=(unsigned)(std::min<size_t>)(wanted,count);
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::atomic<bool> errorStored{false};
    const auto work=[&] {
        for(;;) {
            if(failed.load())return;
            const size_t i=next.fetch_add(1);
            if(i>=count)return;
            try { body(i); }
            catch(...) {
                failed.store(true);
                bool expected=false;
                if(errorStored.compare_exchange_strong(expected,true))error=std::current_exception();
                return;
            }
        }
    };
    if(n<=1){work();}
    else {
        std::vector<std::thread> pool;pool.reserve(n-1);
        const auto pooled=[&]{lowerCurrentThreadPriority();work();};
        try { for(unsigned t=1;t<n;++t)pool.emplace_back(pooled); }
        catch(...) { failed.store(true); }   // thread creation failed: finish on this thread
        if(!failed.load() || pool.empty())work();
        for(auto& t:pool)t.join();
        if(failed.load() && !error) {            // only thread creation failed: run the rest here
            failed.store(false);
            work();
        }
    }
    if(error)std::rethrow_exception(error);
}

// v0.5.16 SHARED PREPARATION LIMIT (user report: CPU load and temperature
// shoot up while an .flp with several Tailtone instances opens). PREPARATION
// CPU used to limit each instance on its own, so N instances took N times the
// cores. All instances now share one budget, handed out in turns:
//   Background (keys nobody played): at most instanceSlots() workers, each with
//     threads() render threads, so their total never exceeds the setting's
//     thread budget. A background claim never queues: it only takes a free
//     turn while no foreground claim waits, for one small batch.
//   Foreground (a sample load, fresh bank, played keys, punch lanes, crunch,
//     phase): first come, first served (ticket queue), up to foregroundSlots()
//     at once - on Quiet two instances (one core each), so one heavy instance
//     cannot hold up every other instance's played keys.
//   Urgent (offline render / export): never waits and is not counted, so an
//     export never waits for other instances (the CPU is the export's anyway).
// A worker claims only when it has real work (cheap passes never wait), keeps
// the turn for one preparation pass, and waiting is cancellable: cancel() is
// polled every 20 ms outside the gate's lock (it may take other locks).
// One gate per loaded Tailtone binary (static in this header-only module).
// Optional SystemSlots (set by the plugin) extend the limit to every process
// on the computer: a limited turn also needs system slot `index` (a named
// lock, released automatically when a process dies). Ordering is per process.
enum class Priority { Background=0, Foreground=1, Urgent=2 };
inline unsigned budgetThreadsForEffort(int e,unsigned hw) {
    if(hw<=1 || e==Quiet)return 1;
    return e==Fast?hw-1:(std::max)(1u,hw/2);
}
constexpr unsigned kMaxSlots=16;
inline std::atomic<unsigned>& instanceSlotsOverride() { static std::atomic<unsigned> value{0}; return value; }   // test hook
inline std::atomic<unsigned>& foregroundSlotsOverride() { static std::atomic<unsigned> value{0}; return value; } // test hook
// Background turns: the setting's thread budget divided by each worker's threads.
inline unsigned instanceSlots() {
    if(const unsigned o=instanceSlotsOverride().load())return (std::min)(o,kMaxSlots);
    return (std::min)(kMaxSlots,(std::max)(1u,budgetThreadsForEffort(effort().load(),std::thread::hardware_concurrency())/threads()));
}
// Foreground turns: as many, but at least two on Quiet (one core each) when the
// machine has two hardware threads.
inline unsigned foregroundSlots() {
    if(const unsigned o=foregroundSlotsOverride().load())return (std::min)(o,kMaxSlots);
    const unsigned slots=instanceSlots();
    if(effort().load()==Quiet && std::thread::hardware_concurrency()>=2)return (std::max)(slots,2u);
    return slots;
}

// v0.5.16 BACKGROUND DELAY (user request): the keys nobody has played wait
// until no Tailtone instance in the process has run foreground preparation
// for backgroundDelayMs(), so opening a project first finishes every
// instance's played keys and then lets the machine settle before the rest
// follows. Played keys, offline renders and setting changes are never delayed.
inline std::atomic<int>& backgroundDelayOverride() { static std::atomic<int> value{-1}; return value; }   // test hook, -1 = default
constexpr int kBackgroundDelayMs=5000;
inline int backgroundDelayMs() { const int o=backgroundDelayOverride().load(); return o>=0?o:kBackgroundDelayMs; }
inline long long steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Cross-process slots (named locks). tryEnter must not block; enter and exit
// of one index happen on the same thread (the worker that holds the turn).
struct SystemSlots {
    virtual ~SystemSlots()=default;
    virtual bool tryEnter(unsigned index)=0;
    virtual void exit(unsigned index)=0;
};

class PreparationGate {
public:
    static constexpr unsigned kNoIndex=~0u;
    // Background: one try, never waits. Foreground: waits for its turn in
    // arrival order. Urgent: immediate. onWait() runs once (outside the lock)
    // when a foreground claim has to wait. Returns false when cancel() turned true.
    bool acquire(Priority priority,const std::function<bool()>& cancel,unsigned& index,const std::function<void()>& onWait={}) {
        index=kNoIndex;
        std::unique_lock<std::mutex> lock(mutex);
        if(priority==Priority::Urgent){++urgentActive;++foregroundActive;return true;}
        const bool foreground=priority==Priority::Foreground;
        uint64_t ticket=0;
        if(foreground){ticket=nextTicket++;queue.push_back(ticket);}
        bool waited=false;
        for(;;) {
            const bool myTurn=foreground?queue.front()==ticket:queue.empty();
            if(myTurn) {
                const unsigned cap=foreground?foregroundSlots():instanceSlots();
                if(limited<cap) {
                    const unsigned i=takeIndex(cap);
                    if(i!=kNoIndex) {
                        ++limited;index=i;
                        if(foreground){queue.pop_front();++foregroundActive;changed.notify_all();}
                        return true;
                    }
                }
            }
            if(!foreground)return false;   // background never queues
            lock.unlock();
            if(!waited){waited=true;if(onWait)onWait();}
            const bool cancelled=cancel && cancel();
            lock.lock();
            if(cancelled) {
                queue.erase(std::find(queue.begin(),queue.end(),ticket));
                changed.notify_all();
                return false;
            }
            changed.wait_for(lock,std::chrono::milliseconds(20));
        }
    }
    void release(Priority priority,unsigned index) {
        {   const std::lock_guard<std::mutex> lock(mutex);
            if(priority==Priority::Urgent){if(urgentActive>0)--urgentActive;}
            else {
                if(limited>0)--limited;
                if(index<kMaxSlots && busy[index]){busy[index]=false;if(system)system->exit(index);}
            }
            if(priority!=Priority::Background){if(foregroundActive>0)--foregroundActive;lastForeground=steadyMs();}
        }
        changed.notify_all();
    }
    // A held background turn that now does foreground work.
    void promote() { const std::lock_guard<std::mutex> lock(mutex); ++foregroundActive; }
    // Milliseconds until background keys may start (0 = now): the full delay
    // while foreground work runs or waits, else what is left of it.
    int backgroundWaitMs() {
        const std::lock_guard<std::mutex> lock(mutex);
        const int delay=backgroundDelayMs();
        if(foregroundActive>0 || !queue.empty())return (std::max)(1,delay);
        if(lastForeground==0)return 0;
        const long long remaining=lastForeground+delay-steadyMs();
        return remaining>0?int(remaining):0;
    }
    void setSystemSlots(SystemSlots* s) { const std::lock_guard<std::mutex> lock(mutex); system=s; }
    unsigned activeCount() { const std::lock_guard<std::mutex> lock(mutex); return limited+urgentActive; }   // tests
    unsigned limitedCount() { const std::lock_guard<std::mutex> lock(mutex); return limited; }               // tests
    size_t waitingCount() { const std::lock_guard<std::mutex> lock(mutex); return queue.size(); }            // tests
private:
    unsigned takeIndex(unsigned cap) {   // under mutex
        for(unsigned i=0;i<(std::min)(cap,kMaxSlots);++i)
            if(!busy[i] && (!system || system->tryEnter(i))){busy[i]=true;return i;}
        return kNoIndex;
    }
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<uint64_t> queue;
    uint64_t nextTicket=1;
    std::array<bool,kMaxSlots> busy{};
    SystemSlots* system=nullptr;
    unsigned limited=0, urgentActive=0, foregroundActive=0;
    long long lastForeground=0;
};
inline PreparationGate& preparationGate() { static PreparationGate gate; return gate; }

// One worker's turn on the shared gate, held for one preparation pass.
// claim() is idempotent while held; a foreground claim on a held background
// turn promotes it (it then delays the background keys as well).
class PreparationSlot {
public:
    ~PreparationSlot() { release(); }
    bool claim(Priority p,const std::function<bool()>& cancel={},const std::function<void()>& onWait={}) {
        if(held) {
            if(p!=Priority::Background && priority==Priority::Background){preparationGate().promote();priority=Priority::Foreground;}
            return true;
        }
        held=preparationGate().acquire(p,cancel,index,onWait);
        if(held)priority=p;
        return held;
    }
    void release() { if(held){held=false;preparationGate().release(priority,index);index=PreparationGate::kNoIndex;} }
    bool isHeld() const { return held; }
    Priority heldPriority() const { return priority; }
private:
    bool held=false;
    Priority priority=Priority::Background;
    unsigned index=PreparationGate::kNoIndex;
};
} // namespace pt::parallel
