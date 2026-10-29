#include "../Source/PluginProcessor.h"
#include "../Source/PitchDrop.h"
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}
struct TailtonePitchDropTest {
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static auto wait(PunchTailProcessor& p,bool enabled){
    std::shared_ptr<const PunchTailProcessor::Bank> b;
    for(int i=0;i<4800;++i){b=p.previewBank();if(b&&b->prepared&&b->quality==4&&b->drop.active()==enabled&&!p.rebuilding)return b;juce::Thread::sleep(25);}
    throw std::runtime_error("Pitch drop worker did not finish");
 }
 // Punch rendering is asynchronous just like the tail note bank. The
 // preparation worker must remain running: rebuildPunch() deliberately
 // discards cancelled renders when threadShouldExit() is set.
 static std::shared_ptr<const PunchTailProcessor::Asset> waitPunch(
     PunchTailProcessor& p, const pt::PitchDropSettings& expected) {
    for(int i=0;i<4800;++i){
        {
            const juce::ScopedLock l(p.assetLock);
            if(p.preparedPunch && p.preparedPunchSource==p.assets[0] &&
               p.preparedPunchDrop==expected)
                return p.preparedPunch;
        }
        juce::Thread::sleep(25);
    }
    throw std::runtime_error("Punch drop worker did not publish the requested state");
 }
 static void run(){
    auto owner=std::make_unique<PunchTailProcessor>();auto& p=*owner;stop(p);
    check(!p.pitchDropSettings(1).active(),"Fresh drop must be OFF");
    auto tail=std::make_shared<PunchTailProcessor::Asset>();tail->sample.rate=16000;tail->sample.left.resize(8000);tail->sample.right.resize(8000);
    for(size_t i=0;i<8000;++i)tail->sample.left[i]=tail->sample.right[i]=float(.3*std::sin(2*pt::pi*65.406*i/16000.));
    auto punch=std::make_shared<PunchTailProcessor::Asset>(*tail);punch->sample.left.resize(1600);punch->sample.right.resize(1600);
    {const juce::ScopedLock l(p.assetLock);p.assets[1]=tail;p.assets[0]=punch;}
    set(p,"root",36);set(p,"phaseAuto",0);set(p,"tailVerbLinearPhase",0);set(p,"lowKeyPunch",0);
    p.startThread();auto off=wait(p,false);check(off->notes[36].left==tail->sample.left,"OFF lost root identity");
    set(p,"pitchDropOn",1);auto on=wait(p,true);check(on!=off&&on->fallback==0,"Drop did not rebuild cleanly");
    check(on->notes[36].left!=off->notes[36].left,"Drop ignored root");
    for(int note=pt::kLowestPitchNote;note<=pt::kHighestPitchNote;++note)check(on->notes[note].left.size()==8000,"Drop changed note duration");
    check(p.previewPunch()->sample.left==punch->sample.left,"Tail target changed Punch");
    // Held voices pin the old prepared bank while automation rebuilds.
    p.prepareToPlay(16000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);p.processBlock(audio,midi);
    bool pinned=false;for(const auto& v:p.voices)if(v.active&&v.pitches==on)pinned=true;
    check(pinned,"Voice did not pin prepared drop bank");
    set(p,"pitchDropTime",100);auto changed=on;
    for(int i=0;i<4800;++i){changed=p.previewBank();if(changed&&changed!=on&&changed->prepared&&changed->drop.timeMs==100&&!p.rebuilding)break;juce::Thread::sleep(25);}
    check(changed&&changed!=on&&changed->drop.timeMs==100,"Time edit did not rebuild");
    check(on->notes[36].left!=changed->notes[36].left,"Time edit did not alter audio");
    pinned=false;for(const auto& v:p.voices)if(v.active&&v.pitches==on)pinned=true;check(pinned,"Rebuild replaced held bank");
    // Do not stop the worker before this step: it cancels any new Punch
    // render. Verify the prepared snapshot actually matches the requested
    // target, rather than accepting a stale, previously published Punch.
    set(p,"pitchDropTarget",0);
    auto punchDrop=p.pitchDropSettings(0);
    check(punchDrop.active(),"Punch target did not enable Punch drop");
    auto pitchedPunch=waitPunch(p,punchDrop);
    check(pitchedPunch->sample.left!=punch->sample.left,"Punch target ignored");
    check(pitchedPunch->sample.left.size()==punch->sample.left.size(),"Punch drop changed duration");
    check(!p.pitchDropSettings(1).active(),"Punch-only target affected tail settings");
    set(p,"pitchDropTarget",2);check(p.pitchDropSettings(0).active()&&p.pitchDropSettings(1).active(),"Both target wrong");
    // Engine gating is based on effective selection, including Adaptive priority.
    set(p,"pitchAdaptive",1);check(!p.pitchDropSettings(0).active()&&!p.pitchDropSettings(1).active(),"Adaptive accepted pitch drop");
    auto plainPunch=waitPunch(p,p.pitchDropSettings(0));
    check(plainPunch->sample.left==punch->sample.left,"Switching engine retained Punch drop");
    check(plainPunch!=pitchedPunch,"Engine change retained stale Punch snapshot");
    // v0.4.61: Legacy/High Fidelity/Extreme were removed. Their old switches are
    // inert placeholders: whatever they hold, the engine is Highest Fidelity v4.
    set(p,"pitchAdaptive",0);set(p,"pitchCharacter",0);set(p,"pitchExtreme",0);set(p,"pitchQuality",0);
    check(p.qualityValue()==4&&p.pitchDropSettings(1).active(),"Removed engine switches still change the engine");
    set(p,"pitchCharacter",1);set(p,"pitchDropCurve",2.5);set(p,"pitchDropAmount",18);
    juce::MemoryBlock saved;p.getStateInformation(saved);
    auto restored=std::make_unique<PunchTailProcessor>();stop(*restored);restored->setStateInformation(saved.getData(),int(saved.getSize()));
    const auto d=restored->pitchDropSettings(1);check(d.active()&&d.amount==18&&d.timeMs==100&&d.curve==2.5&&d.target==2,"Drop state did not restore");
    auto legacy=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
    for(int i=legacy.getNumChildren()-1;i>=0;--i)if(legacy.getChild(i).getProperty("id").toString().startsWith("pitchDrop"))legacy.removeChild(i,nullptr);
    juce::MemoryOutputStream stream;legacy.writeToStream(stream);restored->setStateInformation(stream.getData(),int(stream.getDataSize()));
    check(!restored->pitchDropSettings(1).active(),"Legacy state enabled drop");
    check(p.params.getParameter("pitchDropOn")->getParameterIndex()>p.params.getParameter("tailEndFade")->getParameterIndex(),"New params not appended");
 }
};
int main()try{juce::ScopedJuceInitialiser_GUI init;TailtonePitchDropTest::run();std::cout<<"Pitch drop integration passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
