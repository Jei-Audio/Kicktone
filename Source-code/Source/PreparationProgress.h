#pragma once
// v0.5.19 PREPARATION PROGRESS (user request: a bar that shows how long it is
// until all 37 keys are ready). Portable (no JUCE). The preparation worker
// reports what it is doing; the editor reads a snapshot on its timer.
//
// Time left = the expected cost of the batches still to render x the measured
// wall-clock seconds per unit of cost. Keys are not equally expensive: a key
// below the root renders a longer transposed stream and a key far above it
// more cycles (virus-kick-3.wav, Extreme v5 + Root Render, relative to a key
// next to the root: about 2x at -4 st, 5x at -12 st, 1.6x at +20 st). The
// tracker learns each key's cost per configuration from its render time
// (learnKey); keyWeight() is only the first guess for keys not measured yet.
// A batch shares the render threads, see batchCost().
// The speed is measured while keys render (it includes the parallel threads,
// the Background rendering setting, other instances taking turns and the
// worker's own overhead between batches) and is remembered per engine
// configuration, normalised by the work of one key (tail seconds x stretch),
// so a newly loaded tail has an estimate at once.
// Nothing here runs on the audio thread. Times are seconds from any steady
// clock, passed in so the tests can drive them.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

namespace pt::progress {
enum class Phase { NoTail, Settling, Setup, Rendering, Paused, Ready };
enum class Activity { None, Punch, Crunch };

// First-guess render cost of a key `shift` semitones from the root (1 = the root).
inline double keyWeight(double shift) { return shift>=0?std::exp2(shift/24.0):std::exp2(-shift/5.5); }
// Expected wall time of one batch of keys with these weights on `threads`
// threads, in units of one upward key on one thread: pt::parallel::forEach
// hands the keys out in order to whichever thread is free, so a batch is never
// faster than its most expensive key and a small batch leaves threads idle.
inline double batchCost(const double* weights,size_t count,unsigned threads) {
    std::array<double,64> busy{};
    const size_t used=(std::min)((std::min)(size_t((std::max)(1u,threads)),busy.size()),(std::max)(size_t(1),count));
    for(size_t i=0;i<count;++i)*std::min_element(busy.begin(),busy.begin()+std::ptrdiff_t(used))+=(std::max)(0.0,weights[i]);
    return *std::max_element(busy.begin(),busy.begin()+std::ptrdiff_t(used));
}

struct Snapshot {
    Phase phase=Phase::NoTail;
    Activity activity=Activity::None;
    bool waitingTurn=false;   // another Tailtone holds the shared preparation turn
    int ready=0, total=37;
    double secondsLeft=-1;    // < 0: not known yet
    double startsIn=0;        // Paused: seconds until the remaining keys start
    bool complete() const { return phase!=Phase::NoTail && total>0 && ready>=total; }
    float fraction() const { return total>0?float(std::clamp(ready,0,total))/float(total):0.f; }
};

class Tracker {
public:
    static constexpr int kRates=8;
    static constexpr double kDecay=.75;     // older batches count less (the machine load changes)
    static constexpr double kGapSeconds=.5; // a batch starting this soon after the last one also counts the gap
    static constexpr int kMaxShift=48;       // learned key costs for shifts -48..+48 semitones
    // rateKey: the configuration whose speed is measured (engine, Root Render,
    // crunch, thread count). workPerKey: the work of one key (> 0).
    void setConfig(uint64_t rateKey,double workPerKey) {
        const std::lock_guard<std::mutex> l(m);
        if(rateKey!=key)lastCommit=-1;
        key=rateKey;work=workPerKey>1.e-6?workPerKey:1.e-6;
    }
    void setPhase(Phase p,double startsInSeconds=0) {
        const std::lock_guard<std::mutex> l(m);
        phase=p;startsIn=(std::max)(0.0,startsInSeconds);waiting=false;
    }
    // remainingWeight: expected cost of the missing keys (batchCost() of the batches
    // they will be rendered in; < 0: one per key).
    void setReady(int readyKeys,int totalKeys,double remainingWeight=-1) {
        const std::lock_guard<std::mutex> l(m);
        total=(std::max)(0,totalKeys);ready=std::clamp(readyKeys,0,total);
        remaining=remainingWeight>=0?remainingWeight:double(total-ready);
    }
    void setWaiting(bool w) { const std::lock_guard<std::mutex> l(m); waiting=w; }
    void setActivity(Activity a) { const std::lock_guard<std::mutex> l(m); activity=a; }
    // One batch of `keys` keys (expected cost `weight`) starts rendering;
    // keyDone() with each key's share of that cost as it finishes (any thread);
    // endBatch() when it was published (committed) or cancelled.
    void beginBatch(int keys,double weight,double now) {
        const std::lock_guard<std::mutex> l(m);
        inBatch=keys>0;batchKeys=(std::max)(0,keys);batchDone=0;batchWeight=(std::max)(0.0,weight);batchDoneWeight=0;
        batchReady=ready;batchRemaining=(std::max)(remaining,batchWeight);
        batchStart=lastCommit>=0 && now-lastCommit>=0 && now-lastCommit<kGapSeconds?lastCommit:now;
        phase=Phase::Rendering;waiting=false;provisional=storedSecondsPerWeight();
    }
    void keyDone(double weight,double now) {
        const std::lock_guard<std::mutex> l(m);
        if(!inBatch || batchDone>=batchKeys)return;
        ++batchDone;batchDoneWeight+=(std::max)(0.0,weight);
        // Fixed until the next key finishes, so the countdown runs smoothly.
        const auto* r=find(key);
        const double seconds=(r?r->seconds:0.0)+(std::max)(0.0,now-batchStart);
        const double units=(r?r->work:0.0)+batchDoneWeight*work;
        if(units>0)provisional=seconds/units*work;
    }
    void endBatch(bool committed,double now) {
        const std::lock_guard<std::mutex> l(m);
        if(!inBatch)return;
        inBatch=false;
        if(!committed || batchKeys<=0){ready=batchReady;remaining=batchRemaining;lastCommit=-1;return;}
        if(batchWeight>0) {
            auto& r=slot(key);
            r.seconds=r.seconds*kDecay+(std::max)(0.0,now-batchStart);
            r.work=r.work*kDecay+batchWeight*work;
        }
        ready=std::clamp(batchReady+batchKeys,0,total);remaining=(std::max)(0.0,batchRemaining-batchWeight);
        lastCommit=now;
    }
    // A key `shift` semitones from the root took `seconds` to render (worker
    // render thread). Kept per configuration and unit of work.
    void learnKey(double shift,double seconds) {
        const std::lock_guard<std::mutex> l(m);
        const int i=int(std::lround(shift))+kMaxShift;
        if(i<0 || i>2*kMaxShift || !(seconds>0))return;
        auto& r=slot(key);
        const float v=float(seconds/work);
        r.keySeconds[size_t(i)]=r.keySeconds[size_t(i)]>0?.5f*(r.keySeconds[size_t(i)]+v):v;
    }
    // Expected cost of a key `shift` semitones from the root: its learned render
    // time where measured (scaled to keyWeight() units), else keyWeight().
    double weightFor(double shift) const {
        const std::lock_guard<std::mutex> l(m);
        const auto* r=findAny(key);
        const int i=int(std::lround(shift))+kMaxShift;
        if(!r || i<0 || i>2*kMaxShift || !(r->keySeconds[size_t(i)]>0))return keyWeight(shift);
        double logSum=0;int n=0;   // scale: geometric mean of measured / first guess
        for(int k=0;k<=2*kMaxShift;++k)if(r->keySeconds[size_t(k)]>0){logSum+=std::log(r->keySeconds[size_t(k)]/keyWeight(k-kMaxShift));++n;}
        return r->keySeconds[size_t(i)]/std::exp(logSum/n);
    }
    // Wall-clock seconds for one key of weight 1 in the current configuration (< 0: unknown).
    double secondsPerKey() const { const std::lock_guard<std::mutex> l(m); return currentSecondsPerWeight(); }
    Snapshot snapshot(double now) const {
        const std::lock_guard<std::mutex> l(m);
        Snapshot s;
        s.phase=phase;s.activity=activity;s.waitingTurn=waiting;s.total=total;s.startsIn=startsIn;
        s.ready=std::clamp(inBatch?batchReady+batchDone:ready,0,total);
        if(s.phase==Phase::NoTail){s.ready=0;return s;}
        if(s.complete()){s.secondsLeft=0;return s;}
        const double spw=currentSecondsPerWeight();
        if(spw<0)return s;
        double left=0;
        if(inBatch) {
            const double elapsed=(std::max)(0.0,now-batchStart);
            // A batch running late still shows a little time for its unfinished keys.
            left=(std::max)(batchWeight*spw-elapsed,.2*spw*(batchWeight-batchDoneWeight))+(std::max)(0.0,batchRemaining-batchWeight)*spw;
        } else left=remaining*spw;
        if(s.phase==Phase::Paused)left+=startsIn;
        s.secondsLeft=left;
        return s;
    }
private:
    struct Rate { uint64_t key=0; double seconds=0, work=0; unsigned used=0; std::array<float,2*kMaxShift+1> keySeconds{}; };
    mutable std::mutex m;
    std::array<Rate,kRates> rates{};
    unsigned clock=0;
    uint64_t key=0; double work=1;
    Phase phase=Phase::NoTail; Activity activity=Activity::None; bool waiting=false;
    int ready=0, total=37; double remaining=37, startsIn=0;
    bool inBatch=false; int batchKeys=0, batchDone=0, batchReady=0;
    double batchWeight=0, batchDoneWeight=0, batchRemaining=0, batchStart=0, provisional=-1, lastCommit=-1;
    const Rate* find(uint64_t k) const {   // with a measured speed
        for(const auto& r:rates)if(r.used>0 && r.key==k && r.work>0)return &r;
        return nullptr;
    }
    const Rate* findAny(uint64_t k) const {
        for(const auto& r:rates)if(r.used>0 && r.key==k)return &r;
        return nullptr;
    }
    Rate& slot(uint64_t k) {   // the least recently used entry is replaced
        Rate* oldest=&rates[0];
        for(auto& r:rates) {
            if(r.used>0 && r.key==k){r.used=++clock;return r;}
            if(r.used<oldest->used)oldest=&r;
        }
        *oldest=Rate{};oldest->key=k;oldest->used=++clock;
        return *oldest;
    }
    double storedSecondsPerWeight() const { const auto* r=find(key); return r?r->seconds/r->work*work:-1.0; }
    double currentSecondsPerWeight() const {
        if(inBatch && batchDone>0)return provisional;
        return storedSecondsPerWeight();
    }
};

// "about 25 s", "about 1 min 20 s", "about 12 min".
inline std::string formatSeconds(double s) {
    if(!(s>=0))return {};
    if(s<1.0)return "under 1 s";
    if(s<60.0)return "about "+std::to_string(int(std::ceil(s)))+" s";
    if(s<600.0) {
        const int total=int(std::lround(s/5.0))*5;
        const int sec=total%60;
        return "about "+std::to_string(total/60)+" min"+(sec>0?" "+std::to_string(sec)+" s":std::string());
    }
    return "about "+std::to_string(int(std::lround(s/60.0)))+" min";
}

// The line shown on the bar.
inline std::string describe(const Snapshot& s) {
    const std::string count=std::to_string(s.ready)+"/"+std::to_string(s.total)+" keys";
    const auto withActivity=[&](std::string text) {
        if(s.activity==Activity::Punch)text+=" | preparing punch notes...";
        else if(s.activity==Activity::Crunch)text+=" | preparing crunch layers...";
        return text;
    };
    if(s.phase==Phase::NoTail)return "Load a tail to prepare its "+std::to_string(s.total)+" keys";
    if(s.complete())return withActivity("All "+std::to_string(s.total)+" keys ready");
    const std::string left=s.secondsLeft>=0?formatSeconds(s.secondsLeft)+" left":std::string("estimating time left...");
    if(s.waitingTurn)return withActivity(count+" | waiting for another Tailtone to finish");
    switch(s.phase) {
        case Phase::Settling: return withActivity(count+" | starting...");
        case Phase::Setup:    return withActivity(count+" | preparing the engine | "+left);
        case Phase::Paused: {
            const std::string starts="the other keys start in "+std::to_string(int(std::ceil(s.startsIn)))+" s";
            return withActivity(count+" | "+starts+(s.secondsLeft>=0?" | "+formatSeconds(s.secondsLeft)+" left":std::string()));
        }
        default:              return withActivity(count+" | "+left);
    }
}
} // namespace pt::progress
