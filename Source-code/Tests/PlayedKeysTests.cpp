// v0.5.15 PLAYED KEYS FIRST + prepared notes saved in the project.
//  1. Played keys are rendered and published before the other keys.
//  2. The final banks (dry tail, Root Render composite, crunch layers, phase
//     map) are bit-identical to a run without played keys: only the order and
//     the moments of publication change.
//  3. Played keys and their rendered notes round-trip through the project;
//     a reopened project has them at once, bit-identical, and the cache is
//     ignored when a render setting changed.
//  4. An offline render (export) waits for the keys it plays.
//  5. v0.5.16: a reopened project publishes its saved keys without preparing
//     the engine or the Root Render setup (base notes are saved too, also with
//     a stretch); an export never waits for another instance's turn, and an
//     instance waiting for its turn says so; three instances loading at once
//     share the turns.
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <thread>
#include <stdexcept>
static void ck(bool b,const std::string& m){if(!b)throw std::runtime_error(m);}
struct TailtoneKeyMappingTest {
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}
static std::shared_ptr<const PunchTailProcessor::Bank> dry(PunchTailProcessor& p){const juce::ScopedLock l(p.assetLock);return p.bank;}
static std::shared_ptr<const PunchTailProcessor::TailWetBank> wet(PunchTailProcessor& p){const juce::ScopedLock l(p.assetLock);return p.tailWetBank;}
static std::shared_ptr<const PunchTailProcessor::CrunchBank> crunch(PunchTailProcessor& p){const juce::ScopedLock l(p.assetLock);return p.crunchBank;}
static std::shared_ptr<const PunchTailProcessor::PhaseMap> phase(PunchTailProcessor& p){const juce::ScopedLock l(p.assetLock);return p.phaseMap;}
static void configure(PunchTailProcessor& p,bool rootRender,float stretch=100) {
    if(rootRender){set(p,"tailVerbOn",1);set(p,"tailVerbRootPitch",1);set(p,"tailVerbMix",40);set(p,"muteReverb",0);}
    set(p,"crunchMix",40);
    if(stretch!=100)set(p,"stretch",stretch);
}
// Everything final: dry complete, composite complete (Root Render), crunch and phase map bound to them.
static bool settled(PunchTailProcessor& p,bool rootRender) {
    const juce::ScopedLock l(p.assetLock);
    if(!p.bank || !p.bank->prepared)return false;
    if(rootRender && (!p.tailWetBank || !p.tailWetBank->prepared || p.tailWetBank->dryBank!=p.bank))return false;
    if(!p.crunchBank || p.crunchBank->dry!=p.bank)return false;
    if(!p.phaseMap || p.phaseMap->pitches!=p.bank)return false;
    return true;
}
static void waitSettled(PunchTailProcessor& p,bool rootRender,const char* what) {
    for(int i=0;i<120000;++i){if(settled(p,rootRender)){juce::Thread::sleep(300);if(settled(p,rootRender))return;}juce::Thread::sleep(2);}
    throw std::runtime_error(std::string(what)+" never finished: "+p.status().toStdString());
}
static void same(const pt::Sample& a,const pt::Sample& b,const std::string& what){ck(a.rate==b.rate&&a.left==b.left&&a.right==b.right,what+" differs");}
static void compare(PunchTailProcessor& a,PunchTailProcessor& b,bool rootRender) {
    auto da=dry(a),db=dry(b);
    ck(da->key==db->key,"content keys differ");
    for(int n=0;n<128;++n){same(da->notes[size_t(n)],db->notes[size_t(n)],"dry note "+std::to_string(n));ck(da->noteStretches[size_t(n)]==db->noteStretches[size_t(n)],"note stretch "+std::to_string(n));}
    ck(da->pitched==db->pitched&&da->fallback==db->fallback&&da->fallback==0,"pitched/fallback counts");
    if(rootRender){auto wa=wet(a),wb=wet(b);
        for(int n=0;n<128;++n){same(*wa->notes[size_t(n)],*wb->notes[size_t(n)],"composite note "+std::to_string(n));ck(wa->dryTailDurations[size_t(n)]==wb->dryTailDurations[size_t(n)],"composite duration");}}
    auto ca=crunch(a),cb=crunch(b);
    for(int n=0;n<128;++n){
        ck(bool(ca->dryLayers[size_t(n)])==bool(cb->dryLayers[size_t(n)]),"crunch layer presence "+std::to_string(n));
        if(ca->dryLayers[size_t(n)])same(*ca->dryLayers[size_t(n)],*cb->dryLayers[size_t(n)],"crunch layer "+std::to_string(n));
        ck(ca->drives[size_t(n)]==cb->drives[size_t(n)],"crunch drive "+std::to_string(n));
        if(rootRender && ca->wetLayers[size_t(n)])same(*ca->wetLayers[size_t(n)],*cb->wetLayers[size_t(n)],"crunch wet layer "+std::to_string(n));
    }
    auto pa=phase(a),pb=phase(b);
    for(int n=0;n<pt::kPunchIndexCount;++n){const auto& x=pa->notes[size_t(n)];const auto& y=pb->notes[size_t(n)];
        ck(x.valid==y.valid&&x.offsetMs==y.offsetMs&&x.before==y.before&&x.after==y.after,"phase index "+std::to_string(n));}
}
static void run(const juce::File& punch,const juce::File& tail,bool rootRender,float stretch=100) {
    const std::string label=std::string(rootRender?"Root Render + crunch":"dry + crunch")+(stretch!=100?" @ "+std::to_string(int(stretch))+" %":"");
    // Reference: nothing played (the order is irrelevant to the result).
    PunchTailProcessor ref;configure(ref,rootRender,stretch);ref.loadFile(0,punch);ref.loadFile(1,tail);
    waitSettled(ref,rootRender,"reference");
    // Played keys first.
    PunchTailProcessor p;configure(p,rootRender,stretch);
    const std::array<int,3> played{{20,27,44}};
    for(int n:played)p.markPlayed(n);
    p.loadFile(0,punch);p.loadFile(1,tail);
    bool sawPartial=false;std::shared_ptr<const PunchTailProcessor::Bank> last;
    for(int i=0;i<120000 && !settled(p,rootRender);++i) {
        auto b=dry(p);
        if(b && b!=last && b->usable() && b->key!=0) {
            last=b;
            if(!b->prepared && !sawPartial) {
                sawPartial=true;
                for(int n:played)ck(b->real[size_t(n)],"a published partial bank must already hold every played key ("+std::to_string(n)+")");
                int real=0;for(int n=pt::kLowestPitchNote;n<=pt::kHighestPitchNote;++n)real+=b->real[size_t(n)]?1:0;
                ck(real<pt::kPitchNoteCount,"the first partial bank must not be complete");
                if(rootRender){auto w=wet(p);ck(w && w->dryBank==b,"the composite is published with the bank");for(int n:played)ck(w->hasNote(n),"composite played key missing from the first batch");}
                auto c=crunch(p);ck(c && c->dry==b,"crunch layers are published with the bank");
                for(int n:played)ck(c->dryLayers[size_t(n)]!=nullptr,"crunch layer of a played key missing from the first batch");
                int unfinished=-1;for(int n=pt::kLowestPitchNote;n<=pt::kHighestPitchNote;++n)if(!b->real[size_t(n)]){unfinished=n;break;}
                ck(unfinished>=0 && b->notes[size_t(unfinished)].left==b->notes[size_t(std::lround(b->root))].left,"an unfinished key must hold the dry tail");
            }
        }
        juce::Thread::sleep(1);
    }
    ck(sawPartial,label+": no partial bank with the played keys was published first");
    waitSettled(p,rootRender,"played-first");
    compare(ref,p,rootRender);
    std::cout<<"  "<<label<<": played keys published first; final banks bit-identical\n";
    // Project round trip: played keys and their notes come back at once.
    juce::MemoryBlock saved;p.getStateInformation(saved);
    juce::MemoryBlock bare;ref.getStateInformation(bare);
    ck(saved.getSize()>bare.getSize()+100000,"the project must carry the prepared notes of the played keys");
    {const auto t=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
     ck(t.getProperty("playedKeys").toString()=="20,27,44","played keys not saved");
     ck(t.getChildWithName("PreparedNotes").getNumChildren()==3,"prepared notes of exactly the played keys");}
    {PunchTailProcessor q;q.setStateInformation(saved.getData(),int(saved.getSize()));
     for(int n:played)ck(q.wasPlayed(n),"played keys not restored");
     ck(!q.wasPlayed(30),"unplayed key restored as played");
     std::shared_ptr<const PunchTailProcessor::Bank> first;
     for(int i=0;i<60000;++i){auto b=dry(q);if(b && b->usable() && b->key!=0){first=b;break;}juce::Thread::sleep(1);}
     ck(first!=nullptr,"reopened project: no bank");
     // v0.5.16: built from the saved notes alone (the setup follows with the first missing key).
     ck(q.enginePrepares.load()==0,"reopened project prepared the tail engine before publishing its saved keys");
     ck(q.wetPrepares.load()==0,"reopened project ran the Root Render setup before publishing its saved keys");
     auto reference=dry(p);
     ck(first->key==reference->key,"reopened project: different content key");
     for(int n:played){ck(first->real[size_t(n)],"reopened project: played key "+std::to_string(n)+" not available at once");
         same(first->notes[size_t(n)],reference->notes[size_t(n)],"restored note "+std::to_string(n));}
     if(rootRender){auto w=wet(q);ck(w && w->dryBank==first,"reopened project: composite not published with the bank");
         for(int n:played){ck(w->hasNote(n),"reopened project: composite key missing");same(*w->notes[size_t(n)],*wet(p)->notes[size_t(n)],"restored composite "+std::to_string(n));}}
     waitSettled(q,rootRender,"reopened");
     ck(q.enginePrepares.load()==1,"the engine must be prepared once, for the keys that were not saved");
     ck(!rootRender || q.wetPrepares.load()==1,"the Root Render setup must run once, for the keys that were not saved");
     compare(p,q,rootRender);
     ck(q.pendingCache==nullptr,"the restored notes must be released once the bank is complete");}
    // A changed setting ignores the saved notes (they would sound different).
    {PunchTailProcessor q;q.setStateInformation(saved.getData(),int(saved.getSize()));set(q,"stretch",110);
     for(int i=0;i<60000;++i){auto b=dry(q);if(b && b->usable() && b->key!=0)break;juce::Thread::sleep(1);}
     auto b=dry(q);ck(b && b->key!=dry(p)->key,"stretch must change the content key");
     for(int n:played)ck(!b->real[size_t(n)] || b->notes[size_t(n)].left!=dry(p)->notes[size_t(n)].left,"saved notes used for different settings");}
    std::cout<<"  "<<label<<": project round trip restores the played keys at once (bit-identical); stale notes ignored\n";
}
static void offline(const juce::File& punch,const juce::File& tail) {
    // v0.5.16: every turn is taken by another (simulated) instance; the export
    // must not wait for it, while a realtime instance waits and says so.
    pt::parallel::instanceSlotsOverride().store(1);pt::parallel::foregroundSlotsOverride().store(1);
    pt::parallel::PreparationSlot blocker;ck(blocker.claim(pt::parallel::Priority::Foreground),"blocker turn");
    auto waiting=std::make_unique<PunchTailProcessor>();waiting->loadFile(1,tail);
    PunchTailProcessor p;p.setNonRealtime(true);p.prepareToPlay(48000,256);
    p.loadFile(0,punch);p.loadFile(1,tail);
    for(int i=0;i<60000;++i){auto b=dry(p);if(b && b->usable() && b->key!=0)break;juce::Thread::sleep(1);}
    // FL Studio key G#4 = internal 32 (keys sound an octave lower).
    const int key=44,note=pt::tailNoteForKey(key,false);
    juce::AudioBuffer<float> buffer(2,256);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,key,1.f),0);
    p.processBlock(buffer,midi);
    auto b=dry(p);ck(b && b->hasNote(note),"an offline render must wait for the key it plays");
    ck(p.wasPlayed(note),"an offline note must be remembered as played");
    p.releaseResources();
    bool said=false;for(int i=0;i<2000 && !said;++i){said=waiting->status()==PunchTailProcessor::waitingForTurnText();juce::Thread::sleep(1);}
    ck(said,"an instance waiting for its turn must say so: "+waiting->status().toStdString());
    ck(!dry(*waiting),"the waiting instance prepared without a turn");
    blocker.release();
    bool loaded=false;for(int i=0;i<60000 && !loaded;++i){auto b=dry(*waiting);loaded=b && b->usable();juce::Thread::sleep(1);}
    ck(loaded,"the waiting instance never got its turn");
    ck(waiting->status()!=PunchTailProcessor::waitingForTurnText(),"the waiting notice stayed after the turn came");
    pt::parallel::instanceSlotsOverride().store(0);pt::parallel::foregroundSlotsOverride().store(0);
    std::cout<<"  offline render waited for its key, not for another instance's turn; a waiting instance says so\n";
}
// v0.5.16: the named locks behind the cross-process limit. A slot taken on one
// thread is busy for every other thread (and process) until it is released.
// Uses the last slot so a running FL Studio is not disturbed. Windows only: on
// POSIX the lock files are per process, and the in-process gate covers threads.
static void namedSlots() {
   #if defined(_WIN32)
    auto& slots=PunchTailProcessor::namedPreparationSlots();
    const unsigned last=pt::parallel::kMaxSlots-1;
    // A second lock object with the same name stands for another process.
    juce::InterProcessLock otherProcess("JeiAudio_Tailtone_Preparation_"+juce::String(int(last)));
    ck(slots.tryEnter(last),"named slot not available");
    bool other=true;std::thread([&]{other=otherProcess.enter(0);if(other)otherProcess.exit();}).join();
    ck(!other,"a named slot held here was taken by another process");
    slots.exit(last);
    std::thread([&]{other=otherProcess.enter(0);if(other)otherProcess.exit();}).join();
    ck(other,"a released named slot stayed busy");
    std::cout<<"  named preparation slots exclude other processes and release\n";
   #endif
}
// v0.5.16: several instances loading at once take turns (never more preparing
// than the shared limit), and the unplayed keys wait for the idle delay.
static void sharedLimit(const juce::File& punch,const juce::File& tail) {
    pt::parallel::backgroundDelayOverride().store(1500);
    auto& gate=pt::parallel::preparationGate();
    std::array<std::unique_ptr<PunchTailProcessor>,3> instances;
    for(auto& p:instances){p=std::make_unique<PunchTailProcessor>();configure(*p,true);p->markPlayed(27);}
    for(auto& p:instances){p->loadFile(0,punch);p->loadFile(1,tail);}
    unsigned maxActive=0;
    std::array<std::shared_ptr<const PunchTailProcessor::Bank>,3> firstPartial;
    std::array<uint32_t,3> partialAt{};
    bool heldPartial=false,sawDeferredStatus=false;
    for(int i=0;i<240000;++i) {
        maxActive=(std::max)(maxActive,gate.activeCount());
        bool all=true;
        for(size_t k=0;k<instances.size();++k) {
            auto& p=*instances[k];
            auto b=dry(p);
            if(!firstPartial[k] && b && b->usable() && b->key!=0 && b->real[27] && !b->prepared){firstPartial[k]=b;partialAt[k]=juce::Time::getMillisecondCounter();}
            // The idle delay: 1 s after its played key was published, the bank is still partial.
            if(firstPartial[k] && juce::Time::getMillisecondCounter()-partialAt[k]>1000 && juce::Time::getMillisecondCounter()-partialAt[k]<1100 && b && !b->prepared)heldPartial=true;
            if(p.status().contains("the other keys follow after a few seconds"))sawDeferredStatus=true;
            all&=settled(p,true);
        }
        if(all)break;
        juce::Thread::sleep(1);
    }
    for(auto& p:instances)ck(settled(*p,true),"shared limit: an instance never finished: "+p->status().toStdString());
    ck(maxActive>=1 && maxActive<=pt::parallel::foregroundSlots(),"more instances prepared at once than the shared limit");
    ck(sawDeferredStatus,"the status line never said the other keys wait for an idle moment");
    for(auto& b:firstPartial)ck(b!=nullptr,"shared limit: played key not published before the rest");
    ck(heldPartial,"unplayed keys did not wait for the idle delay");
    compare(*instances[0],*instances[2],true);
    pt::parallel::backgroundDelayOverride().store(-1);
    std::cout<<"  3 instances: at most "<<maxActive<<" prepared at once; unplayed keys waited for the idle delay; banks identical\n";
}
};
int main(int argc,char** argv) try {
    juce::ScopedJuceInitialiser_GUI init;
    ck(argc>2,"usage: PlayedKeysTests <punch.wav> <tail.wav>");
    const juce::File punch(juce::String::fromUTF8(argv[1])),tail(juce::String::fromUTF8(argv[2]));
    TailtoneKeyMappingTest::run(punch,tail,false);
    TailtoneKeyMappingTest::run(punch,tail,true);
    TailtoneKeyMappingTest::run(punch,tail,true,112);   // v0.5.16: base notes with a stretch
    TailtoneKeyMappingTest::offline(punch,tail);
    TailtoneKeyMappingTest::sharedLimit(punch,tail);
    TailtoneKeyMappingTest::namedSlots();
    std::cout<<"PASS played keys first + prepared notes in the project\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
