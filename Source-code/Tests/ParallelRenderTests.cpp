// v0.4.65 parallel note rendering: bit-identical to the sequential render,
// exception and cancellation behaviour, and the composite bank path.
#include "WaveFixture.h"
#include "../Source/ParallelRender.h"
#include "../Source/KickCharacterPitch.h"
#include "../Source/TailReverbPitchBank.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}

int main(int argc,char** argv) {
  try {
    check(argc>1,"usage: ParallelRenderTests <virus-kick-3.wav>");
    const auto x=loadFloatWave(argv[1]);
    check(pt::parallel::defaultThreads()>=1 && pt::parallel::defaultThreads()<=8,"thread count out of range");
    // v0.5.10 PREPARATION CPU: Quiet = 1 (default since v0.5.12), Balanced = half, Fast = all but one, 1..8.
    using namespace pt::parallel;
    check(effort().load()==Quiet&&defaultThreads()==1,"Quiet (1 core) must be the default effort");
    check(threadsForEffort(Quiet,16)==1&&threadsForEffort(Balanced,16)==8&&threadsForEffort(Fast,16)==8,"16 threads: 1 / 8 / 8");
    check(threadsForEffort(Quiet,8)==1&&threadsForEffort(Balanced,8)==4&&threadsForEffort(Fast,8)==7,"8 threads: 1 / 4 / 7");
    check(threadsForEffort(Balanced,2)==1&&threadsForEffort(Fast,2)==1&&threadsForEffort(Fast,1)==1&&threadsForEffort(Balanced,0)==1,"small machines use 1 thread");
    check(threadsForEffort(Balanced,12)==6&&threadsForEffort(Fast,12)==8,"12 threads: 6 / 8 (capped)");
    lowerCurrentThreadPriority();   // best effort, must not throw or change results
    {std::vector<std::atomic<int>> hits(5);for(auto& h:hits)h=0;forEach(5,[&](size_t i){++hits[i];});for(auto& h:hits)check(h==1,"forEach after priority change");}
    // forEach visits every index exactly once, on every thread count.
    for(unsigned t:{1u,2u,3u,8u}) {
        std::vector<std::atomic<int>> hits(37);for(auto& h:hits)h=0;
        pt::parallel::forEach(37,[&](size_t i){++hits[i];},t);
        for(auto& h:hits)check(h==1,"index not visited exactly once");
    }
    pt::parallel::forEach(0,[&](size_t){check(false,"body called for an empty range");});
    // An exception in one task stops the hand-out and is rethrown once.
    {
        std::atomic<int> ran{0};bool thrown=false;
        // Index 0 fails at once; the other tasks take a moment, so the hand-out
        // must stop long before all 64 have run.
        try { pt::parallel::forEach(64,[&](size_t i){++ran;if(i==0)throw std::runtime_error("boom");std::this_thread::sleep_for(std::chrono::milliseconds(2));},4); }
        catch(const std::runtime_error& e){thrown=std::string(e.what())=="boom";}
        check(thrown,"task exception was not rethrown");
        check(ran<64,"hand-out continued after a failure");
    }
    // Tail bank renders: every note identical for 1, 2 and 4 threads (Highest
    // Fidelity v4 at 100%, 80% and 150%, plus a cancel mid-way).
    const double root=36;
    const auto analysis=pt::cycle::analyzeWithManualRoot(x,440*std::pow(2.,(root-69)/12));
    for(double stretch:{1.0,.8,1.5}) {
        const auto prepared=pt::character::prepare(x,analysis,stretch);
        std::array<pt::Sample,128> reference;
        for(int note=pt::kLowestPitchNote;note<=pt::kHighestPitchNote;++note)
            if(note!=36)reference[size_t(note)]=pt::character::render(x,note-root,prepared);
        for(unsigned threads:{2u,4u}) {
            std::array<pt::Sample,128> notes;
            pt::parallel::forEach(size_t(pt::kPitchNoteCount),[&](size_t i){
                const int note=pt::kLowestPitchNote+int(i);if(note==36)return;
                notes[size_t(note)]=pt::character::render(x,note-root,prepared);
            },threads);
            for(int note=pt::kLowestPitchNote;note<=pt::kHighestPitchNote;++note) {
                if(note==36)continue;
                check(notes[size_t(note)].left==reference[size_t(note)].left && notes[size_t(note)].right==reference[size_t(note)].right,
                      "parallel render differs from the sequential render");
            }
        }
    }
    // Cancellation inside a task propagates as the usual cancel exception.
    {
        const auto prepared=pt::character::prepare(x,analysis,1.0);
        std::atomic<int> started{0};bool cancelled=false;
        try {
            pt::parallel::forEach(size_t(pt::kPitchNoteCount),[&](size_t i){
                ++started;
                pt::character::render(x,double(pt::kLowestPitchNote+int(i))-root,prepared,[&]{return started>2;});
            },2);
        } catch(const std::runtime_error& e){cancelled=std::string(e.what()).find("cancelled")!=std::string::npos;}
        check(cancelled,"cancel did not propagate");
        check(started<pt::kPitchNoteCount,"cancel did not stop the hand-out");
    }
    // Root Render composite bank (parallel inside): identical for 1 and 4 threads.
    {
        pt::TailReverbSettings s;s.enabled=true;s.tailSend=true;s.mix=30;s.tailRootPitch=true;s.spaceMs=400;s.engine=1;
        pt::TailCompositeRenderSettings wp;
        pt::parallel::threadOverride().store(1);
        const auto a=pt::renderRootTailCompositeBank(x,root,0,s,1.0,4,false,wp);
        pt::parallel::threadOverride().store(4);
        const auto b=pt::renderRootTailCompositeBank(x,root,0,s,1.0,4,false,wp);
        pt::parallel::threadOverride().store(0);
        for(int n=0;n<128;++n) {
            check(bool(a.notes[size_t(n)])==bool(b.notes[size_t(n)]),"composite note presence differs");
            if(a.notes[size_t(n)])check(a.notes[size_t(n)]->left==b.notes[size_t(n)]->left && a.notes[size_t(n)]->right==b.notes[size_t(n)]->right &&
                                        a.dryTailDurations[size_t(n)]==b.dryTailDurations[size_t(n)],"composite bank differs across thread counts");
        }
        // The composite's cancel is honoured by the parallel notes too.
        bool thrown=false;std::atomic<int> polls{0};
        try { pt::renderRootTailCompositeBank(x,root,0,s,1.0,4,false,wp,[&]{return ++polls>60;}); }
        catch(const std::runtime_error&){thrown=true;}
        check(thrown,"composite cancel was ignored");
    }
    std::cout<<"ParallelRender OK ("<<pt::parallel::defaultThreads()<<" default threads)\n";return 0;
  } catch(const std::exception& e) { std::cerr<<"FAILED: "<<e.what()<<"\n";return 1; }
}
