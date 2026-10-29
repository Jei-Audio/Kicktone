#include "LegacyTestProcessor.h"
#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* param=p.params.getParameter(id);param->setValueNotifyingHost(param->convertTo0to1(v));}
struct TailtoneMasterEnvelopeTest {
 static std::unique_ptr<LegacyTestProcessor> fixture(bool linear=false) {
    auto p=std::make_unique<LegacyTestProcessor>();p->signalThreadShouldExit();p->notify();p->stopThread(-1);
    set(*p,"tailVerbLinearPhase",linear);set(*p,"gate",1);set(*p,"output",0);set(*p,"eqOn",0);set(*p,"punchEqOn",0);set(*p,"reverbEqOn",0);set(*p,"phaseAuto",0);
    auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=48000;a->sample.left.assign(48000,.2f);a->sample.right.assign(48000,-.1f);p->assets[0]=a;
    p->prepareToPlay(48000,128);return p;
 }
 static void run() {
    for(bool linear:{false,true}) {
      auto p=fixture(linear);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;
      midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
      for(int i=0;i<20;++i)p->processBlock(audio,midi);
      check(std::abs(audio.getSample(0,100)-.2f)<1e-6,"Default ADSR changed kick body");
      midi.addEvent(juce::MidiMessage::noteOn(1,49,.5f),127);p->processBlock(audio,midi);
      int active=0;for(auto& v:p->voices)active+=v.active;check(active==1&&!p->voices[0].active,"Previous kick survived next MIDI note");
      for(int i=0;i<20;++i)p->processBlock(audio,midi);
      const float newLevel=.2f*juce::MidiMessage::noteOn(1,49,.5f).getFloatVelocity();
      check(std::abs(audio.getSample(0,100)-newLevel)<1e-6,"Choked kick leaked into new kick");
      midi.addEvent(juce::MidiMessage::noteOff(1,48),0);p->processBlock(audio,midi);
      check(p->masterEnvelope.isActive(),"Old note-off released current note");
      midi.addEvent(juce::MidiMessage::noteOff(1,49),17);
      const int end=17+48+p->getLatencySamples();
      for(int b=0;b<20;++b){p->processBlock(audio,midi);
          for(int i=0;i<128;++i)if(b*128+i>=end)check(audio.getSample(0,i)==0&&audio.getSample(1,i)==0,"Release cutoff shifted at block/PDC boundary");}

      check(audio.getMagnitude(0,128)==0,"Complete output continued beyond MIDI release");
    }
    auto p=fixture();set(*p,"masterAttack",20);set(*p,"masterDecay",40);set(*p,"masterSustain",25);set(*p,"masterRelease",10);
    juce::MemoryBlock saved;p->getStateInformation(saved);auto q=fixture();q->setStateInformation(saved.getData(),int(saved.getSize()));
    auto settings=q->masterEnvelopeSettings();check(settings.attackMs==20&&settings.decayMs==40&&settings.sustainPercent==25&&settings.releaseMs==10,"ADSR state was not saved");
    auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
    for(int i=legacy.getNumChildren()-1;i>=0;--i)if(legacy.getChild(i).getProperty("id").toString().startsWith("master"))legacy.removeChild(i,nullptr);
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    q->setStateInformation(old.getData(),int(old.getSize()));settings=q->masterEnvelopeSettings();
    check(settings.attackMs==0&&settings.decayMs==0&&settings.sustainPercent==100&&settings.releaseMs==1,"Legacy ADSR defaults incorrect");
    check(p->params.getParameter("masterAttack")->getParameterIndex()>p->params.getParameter("punchDecayGain")->getParameterIndex(),"ADSR reordered legacy parameters");
    // Render only recorded reverb to verify the envelope is after every bus.
    auto wet=fixture();wet->assets[2]=wet->assets[0];set(*wet,"mutePunch",1);set(*wet,"muteReverb",0);set(*wet,"masterAttack",20);set(*wet,"masterSustain",25);
    set(*wet,"punchExtra",990);wet->prepareToPlay(48000,128);
    juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
    for(int i=0;i<20;++i)wet->processBlock(audio,midi);
    check(audio.getSample(0,100)>.01f&&audio.getSample(0,100)<.06f,"Master sustain omitted recorded reverb");
    PreviewRequest request;request.samples[0]=std::make_shared<pt::Sample>(p->assets[0]->sample);request.rate=48000;request.envelope={20,0,25,10};
    ProcessedPreview preview;preview.submit(request);std::optional<PreviewResult> result;auto deadline=juce::Time::getMillisecondCounter()+10000;
    while(!result&&juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);result=preview.take();}
    check(bool(result),"ADSR preview timeout");
    const auto& bins=result->kick.envelope.bins;check(bins.back().maximum<.01f,"Complete waveform omitted final release");
    check(bins[bins.size()/2].maximum<.06f,"Complete waveform omitted sustain");
 }
};
int main()try{juce::ScopedJuceInitialiser_GUI init;TailtoneMasterEnvelopeTest::run();std::cout<<"PASS full output ADSR, mono choke, block-edge/PDC timing, note-off identity, saved/migrated state and preview\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
