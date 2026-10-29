// v0.4.48: Kick Character in the real processor — worker bank, playback,
// note switching without hitches, state/migration, punch independence.
#include "../Source/PluginProcessor.h"
#include "../Source/ExtremeV5.h"
#include "../Source/TailReverbPitchBank.h"
#include "../Source/KickCharacterPitch.h"
#include "../Source/AdaptiveStretch.h"
#include "../Source/PitchDrop.h"
#include "WaveFixture.h"
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}

struct TailtoneKickCharacterTest {
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static std::shared_ptr<const PunchTailProcessor::Bank> waitForBank(PunchTailProcessor& p,int quality,double stretch) {
    std::shared_ptr<const PunchTailProcessor::Bank> bank;
    for(int i=0;i<4800;++i){bank=p.previewBank();
        if(bank&&bank->prepared&&bank->quality==quality&&std::abs(bank->requestedStretch-stretch)<1e-5&&!p.rebuilding)return bank;
        juce::Thread::sleep(25);}
    // v0.5.1: a wait that never matches is a test error, not a pass after 2 minutes
    // (the v0.4.68 "stillV4" wait asked for 125 % after stretch was set back to 100 %).
    throw std::runtime_error("Expected bank (engine "+std::to_string(quality)+", stretch "+std::to_string(stretch)+") never finished");
 }
 static void run(const char* fixture) {
    PunchTailProcessor p;stop(p);
    set(p,"phaseAuto",0);set(p,"root",36);set(p,"stretch",100);set(p,"pitchCharacter",1);
    check(p.qualityValue()==4,"pitchCharacter does not select the Kick Character engine");
    auto tail=std::make_shared<PunchTailProcessor::Asset>();tail->sample=loadFloatWave(fixture);
    {const juce::ScopedLock lock(p.assetLock);p.assets[1]=tail;}
    p.startThread();
    auto bank=waitForBank(p,4,1.0);
    check(bank&&bank->prepared&&bank->quality==4,"Kick Character bank never finished");
    check(bank->fallback==0&&bank->pitched==pt::kPitchNoteCount-1,"Kick Character notes fell back to the dry tail");
    check(bank->notes[36].left==tail->sample.left,"Root note is not the exact tail");
    // Bank notes equal the engine's own render (worker uses the selected engine).
    const auto analysis=pt::cycle::analyzeWithManualRoot(tail->sample,440.0*std::pow(2.0,(36-69.0)/12.0));
    const auto expected=pt::character::render(tail->sample,5.0,pt::character::prepare(tail->sample,analysis,1.0));
    check(bank->notes[41].left==expected.left&&bank->notes[41].right==expected.right,"Worker bank did not use Kick Character");
    for(int note=pt::kLowestPitchNote;note<=pt::kHighestPitchNote;++note)
        check(bank->notes[size_t(note)].left.size()==tail->sample.left.size(),"Pitch changed the tail duration");
    // Stretch changes rebuild with fixed duration.
    set(p,"stretch",125);
    auto stretched=waitForBank(p,4,1.25);
    check(stretched&&stretched!=bank&&stretched->fallback==0,"Stretch rebuild failed");
    for(int note:{20,30,41,51})check(stretched->notes[size_t(note)].left.size()==size_t(std::llround(tail->sample.left.size()*1.25)),"Stretch duration wrong");
    // Playback: retrigger across notes; output finite with no dropout at note switches.
    p.prepareToPlay(44100,128);
    juce::AudioBuffer<float> block(2,128);juce::MidiBuffer midi;
    int notes[]={30,41,24,51,36};
    for(int n:notes){
        midi.clear();midi.addEvent(juce::MidiMessage::noteOn(1,(n)+12,1.f),0);
        float peak=0;
        for(int b=0;b<40;++b){p.processBlock(block,midi);midi.clear();
            for(int c=0;c<2;++c)for(int i=0;i<128;++i){const float v=block.getSample(c,i);check(std::isfinite(v),"Non-finite playback");peak=(std::max)(peak,std::abs(v));}}
        check(peak>1e-3f,"A Kick Character note played silently");
    }
    // v0.4.49: Adaptive Stretch now renders with Highest Fidelity v4 (same per-note durations).
    set(p,"pitchAdaptive",1);set(p,"adaptiveAmount",100);
    auto adaptiveBank=waitForBank(p,3,1.25);
    check(adaptiveBank&&adaptiveBank->quality==3&&adaptiveBank->fallback==0&&std::abs(adaptiveBank->adaptiveAmount-100)<1e-9,"Adaptive bank failed");
    {const auto prep=pt::character::prepare(tail->sample,analysis,1.25);
     const auto want=pt::character::render(tail->sample,5.0,prep,{},nullptr,1.25*pt::adaptiveDurationRatio(5.0,100));
     check(adaptiveBank->notes[41].left==want.left&&adaptiveBank->notes[41].right==want.right,"Adaptive Stretch is not built on Highest Fidelity v4");
     check(std::abs(adaptiveBank->stretchForNote(41)-1.25*pt::adaptiveDurationRatio(5.0,100))<1./tail->sample.rate,"Adaptive per-note duration changed");}
    // v0.4.68 Extreme v5: appended switch, wins over Adaptive, bank quality 5 = the engine's own render,
    // exact root, fixed duration, Pitch Drop applied on top, composite (Root Render + Pitch) uses it too.
    set(p,"pitchExtremeV5",1);check(p.qualityValue()==5,"pitchExtremeV5 does not select Extreme v5");
    auto extremeBank=waitForBank(p,5,1.25);
    check(extremeBank&&extremeBank->quality==5&&extremeBank->fallback==0&&extremeBank->pitched==pt::kPitchNoteCount-1,"Extreme v5 bank failed");
    {const auto prep=pt::extreme5::prepare(tail->sample,analysis,1.25);
     const auto want=pt::extreme5::render(tail->sample,5.0,prep);
     check(extremeBank->notes[41].left==want.left&&extremeBank->notes[41].right==want.right,"Worker bank did not use Extreme v5");
     check(extremeBank->notes[36].left==pt::extreme5::render(tail->sample,0.0,prep).left,"Extreme v5 root at 125 % is not the engine's stretch render");
     for(int note:{20,30,41,51})check(extremeBank->notes[size_t(note)].left.size()==size_t(std::llround(tail->sample.left.size()*1.25)),"Extreme v5 duration wrong");}
    set(p,"stretch",100);extremeBank=waitForBank(p,5,1.0);
    check(extremeBank->notes[36].left==tail->sample.left,"Extreme v5 root note is not the exact tail");
    {   // v0.5.1: Pitch Drop works on Extreme v5 (tail bank = engine render + drop; punch lane too).
        set(p,"pitchDropOn",1);set(p,"pitchDropTarget",2);set(p,"pitchDropAmount",7);set(p,"pitchDropTime",80);
        const auto drop=p.pitchDropSettings(1);check(drop.active()&&p.pitchDropSettings(0).active(),"Pitch Drop is off in Extreme v5");
        std::shared_ptr<const PunchTailProcessor::Bank> dropped;
        for(int i=0;i<4800;++i){dropped=p.previewBank();if(dropped&&dropped->prepared&&dropped->quality==5&&dropped->drop==drop&&!p.rebuilding)break;dropped=nullptr;juce::Thread::sleep(25);}
        check(bool(dropped),"Extreme v5 bank with Pitch Drop never finished");
        const auto prep=pt::extreme5::prepare(tail->sample,analysis,1.0);
        const auto want=pt::drop::render(pt::extreme5::render(tail->sample,5.0,prep),drop,440.*std::pow(2.,(36+5-69.)/12.),0.);
        check(dropped->notes[41].left==want.left&&dropped->notes[41].right==want.right,"Extreme v5 bank does not apply Pitch Drop");
        check(dropped->notes[41].left!=pt::extreme5::render(tail->sample,5.0,prep).left,"Pitch Drop changed nothing on Extreme v5");
        set(p,"pitchDropOn",0);   // (the next wait, for Highest Fidelity v4, rebuilds without the drop)
    }
    {   // Composite (Root Render + Pitch) with Extreme v5 renders every note with the engine.
        pt::TailReverbSettings wet;wet.enabled=true;wet.tailSend=true;wet.mix=55;wet.spaceMs=120;
        const auto composite=pt::renderRootTailCompositeBank(tail->sample,36,0,wet,1.12,5,false);
        const auto expected=pt::extreme5::render(*composite.rootRender,5,pt::extreme5::prepare(*composite.rootRender,composite.phaseMap,1.12));
        check(composite.notes[41]->left==expected.left&&composite.notes[41]->right==expected.right,"Root Render + Pitch did not use Extreme v5");
    }
    set(p,"pitchExtremeV5",0);
    set(p,"pitchAdaptive",0);set(p,"pitchCharacter",1);
    // v0.4.61: Extreme was removed; the old switch is inert and the bank stays Highest Fidelity v4.
    set(p,"pitchCharacter",0);check(p.qualityValue()==4,"Inert engine switch changed the engine");
    auto stillV4=waitForBank(p,4,1.0);check(stillV4&&stillV4->quality==4&&stillV4->fallback==0&&!stillV4->drop.active()&&stillV4->notes[36].left==tail->sample.left,"Highest Fidelity v4 bank lost (or Pitch Drop stayed on)");   // stretch is 100 % since the Extreme v5 root check
    stop(p);
    // Saved state and legacy migration.
    set(p,"pitchCharacter",1);juce::MemoryBlock saved;p.getStateInformation(saved);
    PunchTailProcessor restored;stop(restored);restored.setStateInformation(saved.getData(),int(saved.getSize()));
    check(restored.qualityValue()==4,"Kick Character selection not saved");
    {set(p,"pitchExtremeV5",1);juce::MemoryBlock x5;p.getStateInformation(x5);PunchTailProcessor r5;stop(r5);r5.setStateInformation(x5.getData(),int(x5.getSize()));
     check(r5.qualityValue()==5,"Extreme v5 selection not saved");set(p,"pitchExtremeV5",0);}
    auto legacy=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
    for(int i=legacy.getNumChildren()-1;i>=0;--i)if(legacy.getChild(i).getProperty("id").toString()=="pitchCharacter")legacy.removeChild(i,nullptr);
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    restored.setStateInformation(old.getData(),int(old.getSize()));
    // v0.4.61: older engines were removed, so old projects open on Highest Fidelity v4.
    check(restored.qualityValue()==4&&restored.params.getRawParameterValue("pitchCharacter")->load()==1,"Old projects must open on Highest Fidelity v4");
    check(p.params.getParameter("pitchCharacter")->getParameterIndex()>p.params.getParameter("masterRelease")->getParameterIndex(),"pitchCharacter was not appended");
    PunchTailProcessor fresh;stop(fresh);check(fresh.qualityValue()==4,"Fresh instance must default to Highest Fidelity v4");
 }
};

int main(int argc,char** argv) try {
    check(argc>1,"Missing kick fixture");
    juce::ScopedJuceInitialiser_GUI init;
    TailtoneKickCharacterTest::run(argv[1]);
    std::cout<<"PASS Kick Character worker bank, fixed duration, stretch rebuild, note switching playback, inert removed-engine switch, state and legacy migration\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
