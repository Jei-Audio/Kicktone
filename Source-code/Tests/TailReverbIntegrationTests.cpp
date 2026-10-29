#include "LegacyTestProcessor.h"
#include <stdexcept>
#include <iostream>
static void ck(bool v,const char* m){if(!v)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* par=p.params.getParameter(id);par->setValueNotifyingHost(par->convertTo0to1(v));}
struct TailtoneReverbRoutingTest {
// Fresh defaults intentionally bypass both generated networks and mute the Reverb
// layer. Once enabled/unmuted, existing nonzero Punch Mix and unity Level must
// immediately make wet sound. Changes while MIDI is held must take effect.
static void punchWetLiveAutomationTest(){
 LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
 ck(p.params.getRawParameterValue("tailVerbOn")->load()==0 &&
    p.params.getRawParameterValue("tailVerbPunchSend")->load()==0 &&
    p.params.getRawParameterValue("muteReverb")->load()==1,
    "Fresh generated reverb/Punch send must be OFF and Reverb layer muted");
 ck(p.params.getRawParameterValue("punchVerbMix")->load()>0 &&
    p.params.getRawParameterValue("punchVerbLevel")->load()==0,
    "Punch wet Mix and Level must have audible defaults when enabled");
 auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample.rate=48000;
 punch->sample.left.resize(48000);punch->sample.right.resize(48000);
 for(size_t i=0;i<punch->sample.left.size();++i)
    punch->sample.left[i]=punch->sample.right[i]=.35f*std::sin(float(2*pt::pi*930*i/48000));
 p.assets[0]=punch;
 set(p,"gate",0);set(p,"output",0);set(p,"mutePunch",1);set(p,"muteTail",1);
 set(p,"tailVerbTailSend",0);
 set(p,"punchVerbMix",100);set(p,"punchVerbLevel",0);set(p,"punchVerbDuck",0);
 set(p,"tailVerbMix",0); // Punch return must not depend on Tail Mix.
 p.prepareToPlay(48000,128);
 // Still muted: no transient leak at start of host playback.
 juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;
 midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
 for(int i=0;i<40;++i){p.processBlock(audio,midi);ck(audio.getMagnitude(0,128)<1.e-7f,"Default mute leaked wet audio");}
 // Enable on the held note after prepareToPlay, exercising bypass-to-live
 // automation as well as the initial mute.
 set(p,"tailVerbOn",1);set(p,"tailVerbPunchSend",1);set(p,"muteReverb",0);
 double on=0;for(int j=0;j<70;++j){p.processBlock(audio,midi);if(j>20)on+=audio.getRMSLevel(0,0,128);}
 ck(on>1.e-3,"Punch-only wet reverb must be audible after enabling/unmuting");
 set(p,"punchVerbMix",0);
 for(int j=0;j<15;++j)p.processBlock(audio,midi);
 ck(audio.getMagnitude(0,128)<1.e-6f,"Live Punch Mix 0%% did not silence wet return");
 set(p,"punchVerbMix",100);
 double resumed=0;for(int j=0;j<60;++j){p.processBlock(audio,midi);if(j>20)resumed+=audio.getRMSLevel(0,0,128);}
 ck(resumed>1.e-3,"Live Punch Mix 100%% did not restore wet return");
 set(p,"punchVerbLevel",-60);
 for(int j=0;j<15;++j)p.processBlock(audio,midi);
 ck(audio.getMagnitude(0,128)<1.e-6f,"Live Punch Level -60 dB did not mute wet return");
 set(p,"punchVerbLevel",0);
 double louder=0;for(int j=0;j<60;++j){p.processBlock(audio,midi);if(j>20)louder+=audio.getRMSLevel(0,0,128);}
 ck(louder>1.e-3,"Live Punch Level restoration did not return wet audio");
 // User save/restore overrides new defaults.
 set(p,"muteReverb",0);set(p,"tailVerbOn",1);set(p,"tailVerbPunchSend",1);
 juce::MemoryBlock saved;p.getStateInformation(saved);LegacyTestProcessor restored;
 restored.setStateInformation(saved.getData(),int(saved.getSize()));
 ck(restored.params.getRawParameterValue("muteReverb")->load()==0 &&
    restored.params.getRawParameterValue("tailVerbOn")->load()==1 &&
    restored.params.getRawParameterValue("tailVerbPunchSend")->load()==1,
    "Saved user choices were replaced by fresh-instance defaults");
 set(p,"muteReverb",1);set(p,"tailVerbOn",0);set(p,"tailVerbPunchSend",0);
 p.getStateInformation(saved);restored.setStateInformation(saved.getData(),int(saved.getSize()));
 ck(restored.params.getRawParameterValue("muteReverb")->load()==1 &&
    restored.params.getRawParameterValue("tailVerbOn")->load()==0 &&
    restored.params.getRawParameterValue("tailVerbPunchSend")->load()==0,
    "Saved disabled/muted user choices were not preserved");
}
static void modeSwitchTest(){
 LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
 p.bank=std::make_shared<PunchTailProcessor::Bank>();p.bank->prepared=true;
 auto& tail=p.bank->notes[36];tail.rate=48000;tail.left.resize(24000);tail.right.resize(24000);
 for(size_t i=0;i<24000;++i)tail.left[i]=tail.right[i]=float(.2*std::sin(2*pt::pi*110*i/48000));
 set(p,"gate",0);set(p,"output",0);set(p,"muteReverb",0);set(p,"muteTail",1);set(p,"tailVerbOn",1);set(p,"tailVerbMix",100);set(p,"tailVerbDuck",0);set(p,"tailVerbEqOn",0);
 p.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;
 midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
 for(int block=0;block<60;++block)p.processBlock(audio,midi);
 ck(audio.getMagnitude(0,128)>1.e-5f,"Mode switch regression needs an audible live reverb");
 set(p,"tailVerbRootPitch",1);p.processBlock(audio,midi);
 ck(audio.getMagnitude(0,128)==0,"Root mode retained an earlier live Tail reverb history");
}
static void workerBankTest(){
 LegacyTestProcessor p;set(p,"pitchAdaptive",0);set(p,"tailVerbSpace",80);set(p,"tailVerbRootPitch",1);set(p,"tailVerbOn",1);set(p,"tailVerbTailSend",1);set(p,"tailVerbEqOn",0);
 auto asset=std::make_shared<PunchTailProcessor::Asset>();asset->sample.rate=24000;
 asset->sample.left.resize(7200);asset->sample.right.resize(7200);
 const double hz=440.0*std::pow(2.0,(36-69.0)/12.0);
 for(size_t i=0;i<7200;++i)asset->sample.left[i]=asset->sample.right[i]=float(.15*std::sin(2*pt::pi*hz*i/24000));
 auto dry=std::make_shared<PunchTailProcessor::Bank>();dry->prepared=true;dry->root=36;dry->quality=4;
 pt::fillDryPitchBank(dry->notes,asset->sample);
 {const juce::ScopedLock lock(p.assetLock);p.assets[1]=asset;p.bank=dry;}p.notify();
 const auto deadline=juce::Time::getMillisecondCounter()+15000;
 std::shared_ptr<const pt::Sample> first;
 while(!(first=p.previewTailWet(36)) && juce::Time::getMillisecondCounter()<deadline)juce::Thread::sleep(1);
 ck(bool(first),"Preparation worker did not publish root-rendered wet bank");
 set(p,"tailVerbWidth",150);p.notify();
 const auto rebuildDeadline=juce::Time::getMillisecondCounter()+15000;
 auto second=p.previewTailWet(36);
 while((!second || second==first) && juce::Time::getMillisecondCounter()<rebuildDeadline){juce::Thread::sleep(1);second=p.previewTailWet(36);}
 ck(second&&second!=first,"Reverb settings did not replace the root wet bank");
 ck(p.previewBank()==dry,"Wet-only edits unnecessarily replaced the dry Tail pitch bank");
 p.unloadFile(1);ck(!p.previewTailWet(36),"Tail unload retained the root wet bank");
}

static std::vector<float> renderRootWetRoute(bool tailSend,bool reset=false,bool mismatch=false){
 LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
 set(p,"pitchAdaptive",0);set(p,"stretch",100);
 p.bank=std::make_shared<PunchTailProcessor::Bank>();p.bank->prepared=true;p.bank->root=36;p.bank->stretch=1;p.bank->quality=4;
 auto& tail=p.bank->notes[36];tail.rate=48000;tail.left.resize(24000);tail.right.resize(24000);
 for(size_t i=0;i<tail.left.size();++i)tail.left[i]=tail.right[i]=.12f*std::sin(float(2*pt::pi*110*i/48000));
 set(p,"gate",0);set(p,"output",0);set(p,"muteReverb",0);set(p,"tailVerbOn",1);set(p,"tailVerbMix",100);set(p,"tailVerbDuck",0);
 set(p,"tailVerbRootPitch",1);set(p,"tailVerbAutoMatch",0);set(p,"tailVerbOffset",25);set(p,"tailVerbTailSend",tailSend);set(p,"tailVerbEqOn",0);set(p,"tailVerbLinearPhase",0);set(p,"tailVerbSubMono",0);if(reset)set(p,"muteTail",1);
 p.tailWetBank=std::make_shared<PunchTailProcessor::TailWetBank>();auto& composite=*p.tailWetBank;
 composite.dryBank=p.bank;
 composite.prepared=true;composite.root=36;composite.stretch=1;composite.quality=4;composite.enabled=true;composite.tailSend=true;
 composite.mix=100;composite.spaceMs=p.tailReverbSettings().spaceMs;composite.width=p.tailReverbSettings().width;composite.distance=p.tailReverbSettings().distance;
 composite.keepDry=p.tailReverbSettings().keepDry;composite.duckAmount=p.tailReverbSettings().duckAmount;composite.duckHz=p.tailReverbSettings().duckHz;
 composite.duckRecovery=p.tailReverbSettings().duckRecovery;composite.levelDb=p.tailReverbSettings().levelDb;composite.tailOffsetMs=25;composite.wetEQ=false;
 composite.wetBands=p.eqSettings(3);composite.linearPhase=false;composite.subMono=false;composite.engine=p.tailReverbSettings().engine; // v0.4.59 key field
 auto rendered=std::make_shared<pt::Sample>(tail);rendered->left.resize(38400);rendered->right.resize(38400);
 for(size_t i=1200;i<rendered->left.size();++i){rendered->left[i]+=.25f*std::sin(float(2*pt::pi*330*i/48000));rendered->right[i]=rendered->left[i];}
 composite.notes[36]=rendered;composite.dryTailDurations[36]=.5;if(mismatch)composite.stretch=1.2;
 p.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;
 midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);std::vector<float> result(40960);
 for(size_t base=0;base<result.size();base+=128){if(reset&&base==3840)midi.addEvent(juce::MidiMessage::noteOn(1,49,1.f),17);p.processBlock(audio,midi);for(int i=0;i<128;++i)result[base+size_t(i)]=audio.getSample(0,i);}
 return result;
}
static std::vector<float> renderRoute(bool effect,bool punchSend,bool tailSend,float preDelay=0,float fade=0,float punchMix=100,float tailMix=100,float punchSpace=600,float tailSpace=600,float punchLevel=0,float tailLevel=0){
 LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
 auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample.rate=48000;punch->sample.left.resize(4800);punch->sample.right.resize(4800);
 for(size_t i=0;i<punch->sample.left.size();++i){const float env=std::exp(-float(i)/1300.f);punch->sample.left[i]=punch->sample.right[i]=.35f*env*std::sin(float(2*pt::pi*180*i/48000));}p.assets[0]=punch;
 p.bank=std::make_shared<PunchTailProcessor::Bank>();auto& tail=p.bank->notes[36];tail.rate=48000;tail.left.resize(24000);tail.right.resize(24000);
 for(size_t i=0;i<tail.left.size();++i){const float env=std::exp(-float(i)/7500.f);tail.left[i]=tail.right[i]=.12f*env*std::sin(float(2*pt::pi*110*i/48000));}
 set(p,"gate",0);set(p,"output",0);set(p,"muteReverb",0);set(p,"tailVerbKeepDry",1);set(p,"tailVerbOn",effect);set(p,"tailVerbMix",tailMix);set(p,"tailVerbSpace",tailSpace);set(p,"tailVerbLevel",tailLevel);set(p,"tailVerbDuck",0);set(p,"punchVerbMix",punchMix);set(p,"punchVerbSpace",punchSpace);set(p,"punchVerbLevel",punchLevel);set(p,"punchVerbDuck",0);set(p,"tailVerbPunchSend",punchSend);set(p,"tailVerbTailSend",tailSend);set(p,"tailVerbPunchPreDelay",preDelay);set(p,"tailVerbPunchFade",fade);
 p.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
 std::vector<float> result(48000);for(size_t base=0;base<result.size();base+=128){p.processBlock(audio,midi);for(int i=0;i<128&&base+size_t(i)<result.size();++i)result[base+size_t(i)]=audio.getSample(0,i);}
 return result;
}
static double routeDifferenceEnergy(const std::vector<float>& a,const std::vector<float>& dry,int begin,int end){double e=0;for(int i=begin;i<end;++i){const double d=a[size_t(i)]-dry[size_t(i)];e+=d*d;}return e;}
};
struct PunchTailPlaybackTest {
 static double levelRender(float db,float mix){
  LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
  p.bank=std::make_shared<PunchTailProcessor::Bank>();auto& source=p.bank->notes[36];source.rate=48000;source.left.resize(36000);source.right.resize(36000);
  for(size_t i=0;i<source.left.size();++i)source.left[i]=source.right[i]=float(.2*std::sin(2*pt::pi*110*i/48000));
  set(p,"muteReverb",0);set(p,"tailVerbKeepDry",0);set(p,"tailVerbOn",1);set(p,"tailVerbMix",mix);set(p,"tailVerbDuck",0);set(p,"tailVerbLevel",db);set(p,"gate",0);set(p,"output",0);
  p.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
  double energy=0;for(int j=0;j<400;++j){p.processBlock(audio,midi);if(j>30)for(int i=0;i<128;++i)energy+=double(audio.getSample(0,i))*audio.getSample(0,i);}
  return energy;
 }
 static void additiveTest(){
  // With the wet volume muted, any Mix must exactly preserve distortion input,
  // output and the old overlapping dry voice when Keep Dry is active.
  LegacyTestProcessor dry,wet;
  for(auto* p:{&dry,&wet}){
   p->signalThreadShouldExit();p->notify();p->stopThread(-1);
   p->bank=std::make_shared<PunchTailProcessor::Bank>();
   for(int note:{36,37}){auto& s=p->bank->notes[size_t(note)];s.rate=48000;s.left.resize(48000);s.right.resize(48000);
    for(size_t i=0;i<s.left.size();++i)s.left[i]=s.right[i]=float(.15*std::sin(2*pt::pi*110*i/48000));}
   set(*p,"gate",0);set(*p,"output",0);set(*p,"tailDistOn",1);set(*p,"tailDistDrive",14);set(*p,"tailDistGain",-12);
   set(*p,"muteReverb",0);set(*p,"tailVerbKeepDry",1);set(*p,"tailVerbLevel",-60);p->prepareToPlay(48000,128);
  }
  set(wet,"tailVerbOn",1);set(wet,"tailVerbMix",100);
  juce::AudioBuffer<float> a(2,128),b(2,128);juce::MidiBuffer ma,mb;
  ma.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);mb=ma;
  for(int j=0;j<160;++j){if(j==80){ma.addEvent(juce::MidiMessage::noteOn(1,49,1.f),17);mb=ma;}
   dry.processBlock(a,ma);wet.processBlock(b,mb);
   for(int ch=0;ch<2;++ch)for(int i=0;i<128;++i)ck(a.getSample(ch,i)==b.getSample(ch,i),"Keep Dry changed the distorted dry tail");}
  
  // Unmute after settling, then a silent new tail: compare against the identical
  // dry renderer, so surviving older dry voices cannot hide a wet-cut failure.
  set(wet,"tailVerbLevel",0);set(wet,"tailVerbSpace",3000);
  bool audible=false;
  for(int j=0;j<100;++j){dry.processBlock(a,ma);wet.processBlock(b,mb);for(int i=0;i<128;++i)if(std::abs(a.getSample(0,i)-b.getSample(0,i))>1.e-6f)audible=true;}
  ck(audible,"Additive reverb did not become audible");
  for(auto* p:{&dry,&wet}){auto& s=p->bank->notes[38];s.rate=48000;s.left.assign(48000,0);s.right=s.left;}
  ma.addEvent(juce::MidiMessage::noteOn(1,50,1.f),53);mb=ma;
  dry.processBlock(a,ma);wet.processBlock(b,mb);
  for(int ch=0;ch<2;++ch)for(int i=53;i<128;++i)ck(a.getSample(ch,i)==b.getSample(ch,i),"Additive wet crossed next note boundary");
  for(int j=0;j<80;++j){dry.processBlock(a,ma);wet.processBlock(b,mb);for(int ch=0;ch<2;++ch)for(int i=0;i<128;++i)ck(a.getSample(ch,i)==b.getSample(ch,i),"Old dry tail re-fed additive reverb");}
  
 }
 static void volumeTest(){
  const double unity=levelRender(0,100),quiet=levelRender(-6,100);
  ck(unity>1.e-5 && std::abs(quiet/unity-std::pow(10.,-6./10.))<.0001,"Live generated reverb volume gain incorrect");
  ck(levelRender(-60,100)==0,"Live generated reverb mute failed");
  // v0.4.49: wet level reaches +12 dB.
  ck(std::abs(levelRender(12,100)/unity-std::pow(10.,12./10.))<.0005,"Live generated reverb +12 dB gain incorrect");
  ck(levelRender(0,0)==levelRender(-60,0),"Wet volume altered dry tail");
 }
 static void test(int offset,int block,int prepared=-1){
  LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
  auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=48000;a->sample.left.assign(4320,0);a->sample.right=a->sample.left;p.assets[0]=a;
  p.bank=std::make_shared<PunchTailProcessor::Bank>();auto& old=p.bank->notes[36];old.rate=48000;old.left.resize(96000);old.right.resize(96000);
  for(size_t i=0;i<old.left.size();++i)old.left[i]=old.right[i]=float(.3*std::sin(2*pt::pi*110*i/48000));
  auto& fresh=p.bank->notes[37];fresh.rate=48000;fresh.left.assign(96000,0);fresh.right=fresh.left;
  set(p,"tailVerbKeepDry",0);set(p,"tailVerbDistortWet",1);set(p,"tailVerbPunchSend",1);set(p,"tailVerbOn",1);set(p,"tailVerbMix",100);set(p,"tailVerbSpace",1200);set(p,"tailVerbDistance",40);set(p,"gate",0);set(p,"output",0);set(p,"muteReverb",0);set(p,"tailVerbEq1Gain",6);
  p.prepareToPlay(48000,prepared>0?prepared:block);juce::AudioBuffer<float> audio(2,block);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
  for(int i=0;i<20000/block+1;++i)p.processBlock(audio,midi);
  ck(audio.getMagnitude(0,block)>1.e-5f,"Old reverb was not audible before reset");
  midi.addEvent(juce::MidiMessage::noteOn(1,49,1.f),offset);p.processBlock(audio,midi);
  for(int i=offset;i<block;++i)ck(audio.getSample(0,i)==0 && audio.getSample(1,i)==0,"Old wet crossed exact MIDI note-on sample");
  for(int j=0;j<20;++j){p.processBlock(audio,midi);ck(audio.getMagnitude(0,block)==0,"Older held tail fed reverb again after next note");}
  ck(!p.voices[0].active,"Next note failed to choke the old dry voice");
 }
// v0.4.57: the Tail END cut shortens only the dry Tail. The live Tail reverb is
// fed the uncut Tail, so with the dry Tail muted the output is bit-identical.
static std::vector<float> renderTailEnd(float endTrim,bool muteDry,bool* audibleAfterCut=nullptr){
 LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
 p.bank=std::make_shared<PunchTailProcessor::Bank>();auto& tail=p.bank->notes[36];tail.rate=48000;tail.left.resize(24000);tail.right.resize(24000);
 for(size_t i=0;i<tail.left.size();++i){tail.left[i]=.12f*std::sin(float(2*pt::pi*110*i/48000));tail.right[i]=-.5f*tail.left[i];}
 set(p,"gate",0);set(p,"output",0);set(p,"mutePunch",1);set(p,"muteTail",muteDry?1.f:0.f);set(p,"muteReverb",0);set(p,"tailVerbOn",1);set(p,"tailVerbMix",60);set(p,"tailVerbKeepDry",1);
 set(p,"tailVerbDuck",70);set(p,"tailVerbTailSend",1);set(p,"tailVerbPunchSend",0);set(p,"tailVerbRootPitch",0);set(p,"tailVerbLinearPhase",0);set(p,"tailVerbSubMono",0);set(p,"crossfade",0);set(p,"phaseAuto",0);
 set(p,"tailEndTrim",endTrim);set(p,"tailEndFade",20);
 ck(std::abs(p.settings().tailEndTrimMs-endTrim)<.01,"Tail end parameter not read into settings");
 p.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
 std::vector<float> result(60000);
 for(size_t base=0;base<result.size();base+=128){p.processBlock(audio,midi);midi.clear();for(int i=0;i<128&&base+size_t(i)<result.size();++i)result[base+size_t(i)]=audio.getSample(0,i);}
 if(audibleAfterCut){float peak=0;for(size_t i=size_t(48000*.3)+50;i<size_t(48000*.5);++i)peak=(std::max)(peak,std::abs(result[i]));*audibleAfterCut=peak>1.e-4f;}
 return result;
}
static void tailEndCutTest(){
 LegacyTestProcessor fresh;fresh.signalThreadShouldExit();fresh.notify();fresh.stopThread(-1);
 ck(fresh.params.getRawParameterValue("tailEndTrim")->load()==0 && fresh.params.getRawParameterValue("tailEndFade")->load()==10,"Tail end defaults changed");
 ck(fresh.params.getParameter("tailEndTrim")->getParameterIndex()>fresh.params.getParameter("tailVerbSubMonoHz")->getParameterIndex(),"Tail end parameters were not appended");
 // Wet only (dry muted): identical whether or not the dry Tail is cut.
 ck(renderTailEnd(0,true)==renderTailEnd(200,true),"Tail end cut changed the Tail reverb");
 // Dry audible: identical before the fade, different after the cut (0.5 s file - 0.2 s = 0.3 s).
 const auto full=renderTailEnd(0,false),cut=renderTailEnd(200,false);
 for(size_t i=0;i<size_t(48000*.27);++i)ck(full[i]==cut[i],"Tail end cut changed audio before its fade");
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(full,cut,int(48000*.3),int(48000*.5))>1.e-4,"Tail end cut did not shorten the dry Tail");
 // State: saved, restored; legacy projects open uncut with a 10 ms fade.
 set(fresh,"tailEndTrim",321.5f);set(fresh,"tailEndFade",75);juce::MemoryBlock saved;fresh.getStateInformation(saved);
 LegacyTestProcessor restored;restored.signalThreadShouldExit();restored.notify();restored.stopThread(-1);restored.setStateInformation(saved.getData(),int(saved.getSize()));
 ck(std::abs(restored.params.getRawParameterValue("tailEndTrim")->load()-321.5f)<.06f && std::abs(restored.params.getRawParameterValue("tailEndFade")->load()-75)<.06f,"Tail end state was lost");
 auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
 for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();if(id.startsWith("tailEnd"))legacy.removeChild(i,nullptr);}
 juce::MemoryBlock oldState;{juce::MemoryOutputStream out(oldState,false);legacy.writeToStream(out);}
 restored.setStateInformation(oldState.getData(),int(oldState.getSize()));
 ck(restored.params.getRawParameterValue("tailEndTrim")->load()==0 && restored.params.getRawParameterValue("tailEndFade")->load()==10,"Legacy project did not open with an uncut Tail");
}
};
static void stateTest(){
 LegacyTestProcessor p;set(p,"punchInvert",1);set(p,"tailVerbKeepDry",1);set(p,"tailVerbDistortWet",1);set(p,"tailVerbPunchSend",1);set(p,"tailVerbTailSend",0);set(p,"tailVerbPunchPreDelay",145);set(p,"tailVerbPunchFade",280);set(p,"tailVerbRootPitch",1);set(p,"tailVerbOffset",137);set(p,"tailVerbOn",1);set(p,"tailVerbMix",60);set(p,"tailVerbSpace",2500);set(p,"tailVerbDuck",72);set(p,"tailVerbSubHz",170);set(p,"tailVerbRecovery",45);set(p,"tailVerbLevel",-4);set(p,"tailVerbWidth",170);set(p,"tailVerbDistance",65);set(p,"tailVerbEq1Gain",-8);set(p,"punchVerbMix",37);set(p,"punchVerbSpace",1400);set(p,"punchVerbLevel",-9);set(p,"punchVerbEq1Gain",5);set(p,"eqGeneratedView",1);set(p,"eqPunchGeneratedView",1);
 ck(p.params.getRawParameterValue("tailVerbEngine")->load()==1 && p.params.getRawParameterValue("tailVerbTailFade")->load()==0,"Fresh reverb engine must be V2 with immediate Tail fade");
 set(p,"tailVerbEngine",0);set(p,"tailVerbTailFade",260);
 juce::MemoryBlock saved;p.getStateInformation(saved);LegacyTestProcessor restored;restored.setStateInformation(saved.getData(),int(saved.getSize()));
 ck(restored.params.getRawParameterValue("punchInvert")->load()==1 && restored.tailReverbSettings().keepDry && restored.tailReverbSettings().distortWet && restored.tailReverbSettings().punchSend && !restored.tailReverbSettings().tailSend && restored.tailReverbSettings().tailRootPitch && restored.tailReverbSettings().tailOffsetMs==137 && restored.tailReverbSettings().punchPreDelayMs==145 && restored.tailReverbSettings().punchFadeMs==280 && restored.tailReverbSettings().enabled && restored.tailReverbSettings().spaceMs==2500 && restored.tailReverbSettings().duckAmount==72 && restored.tailReverbSettings().duckHz==170 && restored.tailReverbSettings().duckRecovery==45 && restored.tailReverbSettings().levelDb==-4 && restored.eqSettings(3)[1].gain==-8 && restored.punchReverbSettings().mix==37 && restored.punchReverbSettings().spaceMs==1400 && restored.punchReverbSettings().levelDb==-9 && restored.eqSettings(4)[1].gain==5 && restored.params.getRawParameterValue("eqPunchGeneratedView")->load()==1 && restored.tailReverbSettings().engine==0 && restored.punchReverbSettings().engine==0 && restored.tailReverbSettings().tailFadeMs==260,"Independent Punch/Tail generated reverb settings did not survive state reload");
 set(p,"tailVerbOffset",-137);juce::MemoryBlock negative;p.getStateInformation(negative);
 restored.setStateInformation(negative.getData(),int(negative.getSize()));
 ck(restored.tailReverbSettings().tailOffsetMs==-137 && restored.tailReverbSettings().tailRootPitch,
    "Negative Tail wet offset or root route did not survive project reload");
 auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
 for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();if(id.startsWith("tailVerb") || id.startsWith("punchVerb") || id=="eqGeneratedView" || id=="eqPunchGeneratedView" || id=="punchInvert")legacy.removeChild(i,nullptr);}
 juce::MemoryBlock oldState;{juce::MemoryOutputStream out(oldState,false);legacy.writeToStream(out);}
 restored.setStateInformation(oldState.getData(),int(oldState.getSize()));
 ck(restored.params.getRawParameterValue("punchInvert")->load()==0 && !restored.tailReverbSettings().keepDry && !restored.tailReverbSettings().distortWet && !restored.tailReverbSettings().punchSend && !restored.tailReverbSettings().enabled && !restored.tailReverbSettings().tailRootPitch && restored.tailReverbSettings().tailOffsetMs==0 && restored.params.getRawParameterValue("eqGeneratedView")->load()==0 && restored.eqSettings(3)[1].gain==0 && restored.tailReverbSettings().duckAmount==0 && restored.tailReverbSettings().levelDb==0 && restored.punchReverbSettings().mix==25 && restored.punchReverbSettings().spaceMs==200 && restored.eqSettings(4)[1].gain==0 && restored.tailReverbSettings().engine==0 && restored.tailReverbSettings().tailFadeMs==0,"Old projects must restore bypass and neutral new EQ with migrated Punch controls");
}
// v0.4.59: Tail fade-in (live), Punch +24 dB level, and Legacy/V2 engine routing.
struct TailtoneReverbV059Test {
static std::vector<float> renderWetOnly(bool tailSend,bool punchSend,float tailFade,int engine,float punchLevel=0,bool reset=false){
 LegacyTestProcessor p;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
 auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample.rate=48000;punch->sample.left.resize(4800);punch->sample.right.resize(4800);
 for(size_t i=0;i<punch->sample.left.size();++i){const float env=std::exp(-float(i)/1300.f);punch->sample.left[i]=punch->sample.right[i]=.35f*env*std::sin(float(2*pt::pi*180*i/48000));}p.assets[0]=punch;
 p.bank=std::make_shared<PunchTailProcessor::Bank>();auto& tail=p.bank->notes[36];tail.rate=48000;tail.left.resize(24000);tail.right.resize(24000);
 for(size_t i=0;i<tail.left.size();++i){const float env=std::exp(-float(i)/7500.f);tail.left[i]=tail.right[i]=.12f*env*std::sin(float(2*pt::pi*110*i/48000));}
 set(p,"gate",0);set(p,"output",0);set(p,"muteReverb",0);set(p,"mutePunch",1);set(p,"muteTail",1);set(p,"tailVerbKeepDry",1);set(p,"tailVerbOn",1);
 set(p,"tailVerbMix",100);set(p,"tailVerbDuck",0);set(p,"tailVerbDistance",0);set(p,"punchVerbMix",100);set(p,"punchVerbDuck",0);set(p,"punchVerbLevel",punchLevel);
 set(p,"tailVerbTailSend",tailSend);set(p,"tailVerbPunchSend",punchSend);set(p,"tailVerbTailFade",tailFade);set(p,"tailVerbEngine",float(engine));
 p.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
 std::vector<float> result(48000);
 for(size_t base=0;base<result.size();base+=128){if(reset&&base==9600)midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),33);p.processBlock(audio,midi);midi.clear();for(int i=0;i<128&&base+size_t(i)<result.size();++i)result[base+size_t(i)]=audio.getSample(0,i);}
 return result;
}
static double windowEnergy(const std::vector<float>& x,size_t a,size_t b){double e=0;for(size_t i=a;i<b&&i<x.size();++i)e+=double(x[i])*x[i];return e;}
static void tailFadeAndEngineTest(){
 for(int engine:{0,1}) {
  const auto immediate=renderWetOnly(true,false,0,engine),faded=renderWetOnly(true,false,400,engine);
  // Punch ends at 100 ms (4800 samples): the Tail send and its wet start there.
  const double earlyImmediate=windowEnergy(immediate,4800,4800+4800),earlyFaded=windowEnergy(faded,4800,4800+4800);
  ck(earlyImmediate>1e-6,"Tail wet missing in fade reference");
  ck(earlyFaded<.08*earlyImmediate,"Tail fade-in did not attenuate the first 100 ms of Tail wet");
  const double midImmediate=windowEnergy(immediate,4800+9600,4800+14400),midFaded=windowEnergy(faded,4800+9600,4800+14400);
  ck(midFaded>.2*midImmediate && midFaded<midImmediate,"Tail fade-in is not a monotonic ramp toward full level");
  const double lateImmediate=windowEnergy(immediate,4800+21600,4800+28800),lateFaded=windowEnergy(faded,4800+21600,4800+28800);
  ck(lateFaded>.4*lateImmediate,"Tail wet did not recover after the fade time");
  for(size_t i=0;i<4800;++i)ck(immediate[i]==faded[i],"Tail fade changed audio before the Tail send opened");
  // Tail fade must never touch the Punch reverb.
  const auto punchA=renderWetOnly(false,true,0,engine),punchB=renderWetOnly(false,true,400,engine);
  ck(punchA==punchB,"Tail fade-in leaked into the Punch reverb");
  // Exact reset at the next note-on with the fade active.
  const auto retrig=renderWetOnly(true,false,400,engine,0,true);
  for(size_t i=9600+33;i<9600+33+4800;++i)ck(std::abs(retrig[i])<1e-4f,"Retriggered note leaked old wet before its Tail send opened");
  ck(windowEnergy(retrig,9600+33+4800,9600+33+9600)<.08*earlyImmediate,"Retriggered note did not restart the Tail fade");
 }
 // Engine selection: both engines are audible, different, and Legacy is the stored default for old projects.
 const auto legacy=renderWetOnly(true,true,0,0),v2=renderWetOnly(true,true,0,1);
 ck(legacy!=v2,"Reverb engine switch did not change the generated wet");
 const double eL=windowEnergy(legacy,0,legacy.size()),eV=windowEnergy(v2,0,v2.size());
 ck(eV>.4*eL && eV<2.5*eL,"V2 wet loudness is not within a few dB of Legacy");
 // Punch reverb level now reaches +24 dB.
 LegacyTestProcessor fresh;fresh.signalThreadShouldExit();fresh.notify();fresh.stopThread(-1);
 auto* level=dynamic_cast<juce::RangedAudioParameter*>(fresh.params.getParameter("punchVerbLevel"));
 ck(level && level->getNormalisableRange().end==24.f && level->getNormalisableRange().start==-60.f,"Punch reverb level range must be -60..+24 dB");
 const auto p12=renderWetOnly(false,true,0,1,12),p24=renderWetOnly(false,true,0,1,24);
 const double r=windowEnergy(p24,0,p24.size())/(windowEnergy(p12,0,p12.size())+1e-30);
 ck(r>10 && r<20,"Punch reverb +24 dB is not ~12 dB louder than +12 dB");
 ck(fresh.params.getParameter("tailVerbEngine")->getParameterIndex()>fresh.params.getParameter("pitchDropTarget")->getParameterIndex() &&
    fresh.params.getParameter("tailVerbTailFade")->getParameterIndex()>fresh.params.getParameter("pitchDropTarget")->getParameterIndex(),"v0.4.59 parameters were not appended");
}
};
static void punchReverbHighPassTest(){
 auto response=[](double hz){pt::ReverbHPFState filter;filter.reset(pt::makeReverbHPFAt(48000,12,250));double energy=0;constexpr int n=48000;for(int i=0;i<n;++i){const float x=float(std::sin(2*pt::pi*hz*i/48000));const float y=filter.process(0,x);if(i>24000)energy+=double(y)*y;}return std::sqrt(energy/24000.0);};
 const double low=response(50),cut=response(250),high=response(2000);
 ck(low<.08 && cut>.44 && cut<.56 && high>.67,"Punch generated reverb must retain its fixed 250 Hz high-pass");
}
static void sourceRoutingTest(){
 const auto dry=TailtoneReverbRoutingTest::renderRoute(false,false,true),none=TailtoneReverbRoutingTest::renderRoute(true,false,false),punchOnly=TailtoneReverbRoutingTest::renderRoute(true,true,false),tailOnly=TailtoneReverbRoutingTest::renderRoute(true,false,true),both=TailtoneReverbRoutingTest::renderRoute(true,true,true);
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(none,dry,0,48000)<1.e-12,"No selected source should leave the generated reverb silent");
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(punchOnly,dry,0,48000)>1.e-5,"Punch-only source did not feed generated reverb");
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(tailOnly,dry,0,48000)>1.e-5,"Tail-only source did not feed generated reverb");
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(both,dry,0,48000)>1.e-5,"Both selected sources did not feed generated reverb");
 const auto punchBaseline=TailtoneReverbRoutingTest::renderRoute(true,true,false,0,0,100,100,600,600,0,0);
 const auto punchWithOtherChanged=TailtoneReverbRoutingTest::renderRoute(true,true,false,0,0,100,0,600,3000,0,-12);
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(punchBaseline,punchWithOtherChanged,0,48000)<1.e-12,"Tail reverb controls changed a Punch-only reverb");
 const auto tailBaseline=TailtoneReverbRoutingTest::renderRoute(true,false,true,0,0,100,100,600,600,0,0);
 const auto tailWithOtherChanged=TailtoneReverbRoutingTest::renderRoute(true,false,true,0,0,0,100,3000,600,-18,0);
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(tailBaseline,tailWithOtherChanged,0,48000)<1.e-12,"Punch reverb controls changed a Tail-only reverb");
 const auto delayed=TailtoneReverbRoutingTest::renderRoute(true,true,false,100,200);
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(delayed,dry,0,4300)<1.e-12,"Punch pre-delay produced wet output before its selected offset");
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(delayed,dry,4800,24000)>1.e-5,"Punch pre-delay/fade removed the delayed wet return");
 const auto immediate=TailtoneReverbRoutingTest::renderRoute(true,true,false,0,0),faded=TailtoneReverbRoutingTest::renderRoute(true,true,false,0,300);
 const double immediateEarly=TailtoneReverbRoutingTest::routeDifferenceEnergy(immediate,dry,2400,9600),fadedEarly=TailtoneReverbRoutingTest::routeDifferenceEnergy(faded,dry,2400,9600);
 const double immediateLate=TailtoneReverbRoutingTest::routeDifferenceEnergy(immediate,dry,24000,47000),fadedLate=TailtoneReverbRoutingTest::routeDifferenceEnergy(faded,dry,24000,47000);
 ck(immediateEarly>1.e-6 && fadedEarly<immediateEarly*.8,"Punch wet fade-in did not soften its onset");
 ck(immediateLate>1.e-6 && std::abs(fadedLate/immediateLate-1.0)<.01,"Punch fade-in changed the reverb after it reached full level");
}
static void rootRenderedTailRouteTest(){
 const auto dry=TailtoneReverbRoutingTest::renderRootWetRoute(false),wet=TailtoneReverbRoutingTest::renderRootWetRoute(true);
 for(int i=0;i<1200;++i)ck(dry[size_t(i)]==wet[size_t(i)],"Tail Reverb Offset moved before its selected time");
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(wet,dry,1500,12000)>1.e-3,
    "Root-rendered Tail wet sample did not play at its independent offset");
 ck(TailtoneReverbRoutingTest::routeDifferenceEnergy(wet,dry,24500,38000)>1.e-3,
    "Composite generated reverb was inaudible after the dry Tail ended");
 const auto mismatched=TailtoneReverbRoutingTest::renderRootWetRoute(true,false,true);
 ck(mismatched==dry,"A mismatched wet pitch/stretch bank played with the Tail bank");
}
int main()try{juce::ScopedJuceInitialiser_GUI init;stateTest();TailtoneReverbV059Test::tailFadeAndEngineTest();TailtoneReverbRoutingTest::punchWetLiveAutomationTest();punchReverbHighPassTest();sourceRoutingTest();rootRenderedTailRouteTest();PunchTailPlaybackTest::tailEndCutTest();TailtoneReverbRoutingTest::workerBankTest();TailtoneReverbRoutingTest::modeSwitchTest();PunchTailPlaybackTest::volumeTest();PunchTailPlaybackTest::additiveTest();for(int offset:{0,17,127})PunchTailPlaybackTest::test(offset,128);PunchTailPlaybackTest::test(530,1024);PunchTailPlaybackTest::test(530,1024,128);PunchTailPlaybackTest::test(128,1024,128);std::cout<<"PASS independent Punch/Tail sends, root-rendered Tail reverb and offset, Punch pre-delay/fade, exact note boundary, dry-only Tail end cut, wet EQ history clear, overlap ownership and chunk/block boundaries\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
