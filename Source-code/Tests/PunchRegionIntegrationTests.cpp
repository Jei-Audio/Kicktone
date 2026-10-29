#include "LegacyTestProcessor.h"
#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <stdexcept>
static void ck(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float value){auto* v=p.params.getParameter(id);v->setValueNotifyingHost(v->convertTo0to1(value));}
struct TailtonePunchRegionTest {
 static std::vector<float> render(bool enabled,bool onlyTail=false,bool onlyWet=false,float attackDb=0,float decayDb=0){
  auto processor=std::make_unique<LegacyTestProcessor>();auto& p=*processor;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
  auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample.rate=48000;punch->sample.left.resize(9600);punch->sample.right.resize(9600);
  for(int i=0;i<9600;++i)punch->sample.left[size_t(i)]=punch->sample.right[size_t(i)]=float(.05*std::sin(2*pt::pi*85*i/48000));
  p.assets[0]=punch;
  if(onlyTail){auto tail=std::make_shared<PunchTailProcessor::Asset>();tail->sample.rate=48000;tail->sample.left.assign(4800,.1f);tail->sample.right=tail->sample.left;p.assets[1]=tail;auto b=std::make_shared<PunchTailProcessor::Bank>();b->notes[36]=tail->sample;b->root=36;b->prepared=true;p.bank=b;}
  set(p,"punchAttackGain",attackDb);set(p,"punchDecayGain",decayDb);set(p,"punchRegionOn",enabled);set(p,"punchRegionSplit",50);set(p,"punchRegionTransition",2);
  set(p,"punchAttackEq0Gain",12);set(p,"punchAttackEq0Freq",400);set(p,"punchDecayEq0Gain",0);
  set(p,"gate",0);set(p,"phaseAuto",0);set(p,"output",0);set(p,"eqOn",0);set(p,"punchEqOn",0);set(p,"crossfade",0);
  if(onlyTail||onlyWet)set(p,"mutePunch",1);
  if(onlyWet){set(p,"tailVerbOn",1);set(p,"tailVerbPunchSend",1);set(p,"tailVerbTailSend",0);set(p,"muteTail",1);set(p,"muteReverb",0);set(p,"punchVerbMix",100);set(p,"punchVerbSpace",80);}
  p.prepareToPlay(48000,128);std::vector<float> out;out.reserve(20000);
  juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
  for(int b=0;b<150;++b){p.processBlock(audio,midi);for(int i=0;i<128;++i)out.push_back(audio.getSample(0,i));}
  ck(std::abs(p.voices[0].punchEnd-.2)<1e-8,"Region EQ changed selected Punch duration");return out;
 }
 static void run(){
  const auto dry=render(false),processed=render(true);double early=0,late=0;
  for(size_t i=0;i<dry.size();++i){if(i>1000&&i<4000)early+=std::abs(dry[i]-processed[i]);if(i>6000)late+=std::abs(dry[i]-processed[i]);}
  ck(early>1,"Attack EQ did not affect actual MIDI Punch playback");ck(late==0,"Attack EQ changed the neutral Decay or extended Punch output");
  ck(render(false,true)==render(true,true,false,24,-24),"Punch region EQ leaked into Tail playback");
  ck(render(false,false,true)==render(true,false,true,24,-24),"Punch region EQ changed the separate Punch reverb send");
  const auto gains=render(true,false,false,6,-6);
  for(size_t i=1000;i<4000;++i)ck(std::abs(gains[i]-processed[i]*std::pow(10.,6./20))<2e-6,"Attack gain missing from MIDI playback");
  for(size_t i=6500;i<9000;++i)ck(std::abs(gains[i]-processed[i]*std::pow(10.,-6./20))<2e-6,"Decay gain missing from MIDI playback");
  ck(render(false,false,false,24,-24)==dry,"Gain changed disabled MIDI Punch");
  auto processor=std::make_unique<LegacyTestProcessor>();auto& p=*processor;ck(p.punchRegionSettings().attackGainDb==0 && p.punchRegionSettings().decayGainDb==0,"Region gain defaults must be unity");ck(!p.punchRegionSettings().enabled,"New region feature must default OFF");
  ck(p.params.getParameter("punchAttackGain")->getParameterIndex()>p.params.getParameter("punchDecayEq7Slope")->getParameterIndex() && p.params.getParameter("punchDecayGain")->getParameterIndex()>p.params.getParameter("punchAttackGain")->getParameterIndex(),"Region gain parameters must be appended after v0.4.44 IDs");
  ck(p.params.getParameter("punchRegionOn")->getParameterIndex()>p.params.getParameter("tailVerbSubMono")->getParameterIndex(),"Region parameters reordered old host IDs");
  set(p,"punchAttackGain",6);set(p,"punchDecayGain",-9);set(p,"punchRegionOn",1);set(p,"punchRegionSplit",37);set(p,"punchRegionTransition",6);set(p,"punchRegionView",2);
  set(p,"punchAttackEq1Gain",6);set(p,"punchDecayEq1Gain",-9);set(p,"punchAttackEqOn",0);
  juce::MemoryBlock saved;p.getStateInformation(saved);auto restoredOwner=std::make_unique<LegacyTestProcessor>();auto& restored=*restoredOwner;restored.setStateInformation(saved.getData(),int(saved.getSize()));
  ck(restored.punchRegionSettings().attackGainDb==6 && restored.punchRegionSettings().decayGainDb==-9 && restored.punchRegionSettings().enabled && restored.punchRegionSettings().splitPercent==37 && restored.punchRegionSettings().transitionMs==6 &&
     restored.eqSettings(5)[1].gain==6 && restored.eqSettings(6)[1].gain==-9 && restored.params.getRawParameterValue("punchRegionView")->load()==2 && restored.params.getRawParameterValue("punchAttackEqOn")->load()==0,
     "Independent Punch region settings did not survive project reload");
  auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
  for(int i=legacy.getNumChildren()-1;i>=0;--i){auto id=legacy.getChild(i).getProperty("id").toString();if(id=="punchAttackGain"||id=="punchDecayGain"||id.startsWith("punchRegion")||id.startsWith("punchAttackEq")||id.startsWith("punchDecayEq"))legacy.removeChild(i,nullptr);}
  juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
  restored.setStateInformation(old.getData(),int(old.getSize()));ck(restored.punchRegionSettings().attackGainDb==0 && restored.punchRegionSettings().decayGainDb==0 && !restored.punchRegionSettings().enabled && restored.punchRegionSettings().splitPercent==20 && restored.eqSettings(5)[1].gain==0 && restored.eqSettings(6)[1].gain==0,
     "Legacy project retained stale region enable/EQ settings");
  // The processed waveform must include the new Punch treatment.
  PreviewRequest request;auto sample=std::make_shared<pt::Sample>();sample->rate=48000;sample->left.resize(9600);sample->right.resize(9600);
  for(int i=0;i<9600;++i)sample->left[size_t(i)]=sample->right[size_t(i)]=float(.15*std::sin(2*pt::pi*85*i/48000));
  request.samples[0]=sample;request.rate=48000;request.settings.gate=false;request.punchRegion={true,50,2,3,-3};request.attackBands[0]={true,tt::LowShelf,0,400,12,.71};
  ProcessedPreview preview;preview.submit(request);std::optional<PreviewResult> first;auto deadline=juce::Time::getMillisecondCounter()+15000;
  while(!first && juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);first=preview.take();}ck(bool(first),"Region processed preview timed out");
  request.punchRegion.enabled=false;preview.submit(request);std::optional<PreviewResult> second;deadline=juce::Time::getMillisecondCounter()+15000;
  while(!second && juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);second=preview.take();}ck(bool(second),"Bypassed region preview timed out");
  ck(first->processedPeaks[0]>second->processedPeaks[0]*1.8f,"Final waveform/normalization omitted regional Punch EQ");
 }
};
int main()try{juce::ScopedJuceInitialiser_GUI init;TailtonePunchRegionTest::run();std::cout<<"PASS actual Punch region playback, Tail/Punch-reverb isolation, preview, append-only state and legacy defaults\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
