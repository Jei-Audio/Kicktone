// v0.4.52: with Root Render + Pitch the played tail is the composite (tail with
// the reverb baked in). Auto Match must be measured on that composite, not the
// plain tail, or the punch/tail join can phase out on pitched notes.
#include "LegacyTestProcessor.h"
#include "../Source/PhaseAlignment.h"
#include "WaveFixture.h"
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}

struct TailtoneRootRenderPhaseTest {
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static void run(const char* punchPath,const char* tailPath) {
    LegacyTestProcessor p;stop(p);
    set(p,"phaseAuto",1);set(p,"root",36);set(p,"stretch",100);set(p,"trim",40);set(p,"crossfade",10);set(p,"muteReverb",0);
    set(p,"tailVerbOn",1);set(p,"tailVerbRootPitch",1);set(p,"tailVerbMix",40);set(p,"tailVerbSpace",300);set(p,"tailVerbDistance",20);set(p,"tailVerbTailSend",1);
    auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample=loadFloatWave(punchPath);
    auto tail=std::make_shared<PunchTailProcessor::Asset>();tail->sample=loadFloatWave(tailPath);
    {const juce::ScopedLock lock(p.assetLock);p.assets[0]=punch;p.assets[1]=tail;}
    p.startThread();
    std::shared_ptr<const PunchTailProcessor::PhaseMap> map;
    for(int i=0;i<4800;++i){
        {const juce::ScopedLock lock(p.assetLock);
         if(p.phaseMap&&p.phaseMap->composite&&p.tailWetBank==p.phaseMap->composite&&p.bank&&p.bank->prepared&&p.phaseMap->pitches==p.bank&&!p.rebuilding){map=p.phaseMap;break;}}
        juce::Thread::sleep(25);}
    check(bool(map),"Phase map never used the Root Render + Pitch composite");
    check(p.compositeUsable(map->composite.get(),map->pitches.get()),"Composite in the phase map is not the playable one");
    // The stored solution equals an analysis of the actually played composite note.
    int differs=0;
    for(int n:{30,33,39,41,45}) {
        auto s=p.settings();s.trimMs=0;s.punchTimeScale=map->punch->forNote(n).punchTimeScale;
        const auto want=pt::phase::analyse(&map->punch->forNote(n).sample,map->composite->notes[size_t(n)].get(),s,n,true);
        const auto& got=map->notes[size_t(n)];
        check(got.valid==want.valid&&std::abs(got.offsetMs-want.offsetMs)<1e-9&&std::abs(got.after-want.after)<1e-9,
              "Auto Match was not measured on the composite for note "+std::to_string(n));
        auto d=p.settings();d.trimMs=map->trimMs;d.punchTimeScale=s.punchTimeScale;
        const auto plain=pt::phase::analyse(&map->punch->forNote(n).sample,&map->pitches->notes[size_t(n)],d,n,true);
        if(std::abs(plain.offsetMs-want.offsetMs)>.05)++differs;
    }
    check(differs>0,"Fixture does not exercise the composite/plain difference");
    // A played note uses that composite solution.
    p.prepareToPlay(44100,128);
    juce::AudioBuffer<float> block(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,41+12,1.f),0);
    p.processBlock(block,midi);
    PunchTailProcessor::PlayingVoice* v=nullptr;for(auto& voice:p.voices)if(voice.active)v=&voice;
    check(v&&v->compositeTail,"Note did not play the composite tail");
    check(std::abs(v->settings.phaseShiftMs-(p.settings().phaseShiftMs+map->notes[41].offsetMs))<1e-9,"Played note did not use the composite Auto Match offset");
    // Root Render off: back to measuring the plain tail.
    set(p,"tailVerbRootPitch",0);
    std::shared_ptr<const PunchTailProcessor::PhaseMap> plainMap;
    for(int i=0;i<4800;++i){{const juce::ScopedLock lock(p.assetLock);if(p.phaseMap&&!p.phaseMap->composite){plainMap=p.phaseMap;break;}}juce::Thread::sleep(25);}
    check(bool(plainMap),"Phase map did not return to the plain tail when Root Render was turned off");
    stop(p);
 }
};

int main(int argc,char** argv) try {
    if(argc<3)throw std::runtime_error("Usage: RootRenderPhaseTests punch.wav tail.wav");
    juce::ScopedJuceInitialiser_GUI init;
    TailtoneRootRenderPhaseTest::run(argv[1],argv[2]);
    std::cout<<"PASS Root Render + Pitch: Auto Match measured on the played composite tail, used at note-on, plain tail again when off\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
