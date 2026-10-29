#include "../Source/PluginProcessor.h"
#include <stdexcept>
#include <iostream>
static void ck(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void waitFor(PunchTailProcessor& p,int slot,int count){
 for(int i=0;i<20000;++i){auto a=p.asset(slot);if(a && int(a->sample.left.size())==count)return;juce::Thread::sleep(1);}
 throw std::runtime_error("Worker trim/restore did not complete");
}
int main(){
 juce::ScopedJuceInitialiser_GUI init;
 const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("tailtone-silence-test",".wav");
 {
  juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file),48000,2,24,{},0));
  ck(bool(writer),"WAV writer");juce::AudioBuffer<float> audio(2,4800);audio.clear();
  for(int i=1000;i<3000;++i){audio.setSample(0,i,.1f);audio.setSample(1,i,.2f);}
  ck(writer->writeFromAudioSampleBuffer(audio,0,4800),"WAV fixture");
 }
 {
  PunchTailProcessor p;p.loadFile(0,file);waitFor(p,0,4800);
  auto original=p.asset(0);p.trimSilence(0);waitFor(p,0,2048);
  auto a=p.asset(0);ck(a->trimFirst==976 && a->trimLast==3024,"Saved bounds");
  ck(a->bytes==original->bytes,"Original embedded bytes changed");
  ck(a->sample.left[24]==original->sample.left[1000],"Audio modified during trim");
  ck(!p.asset(1),"Punch trim changed tail lane");
  for(auto pair:std::initializer_list<std::pair<const char*,float>>{{"punchNormalize",-3},{"tailNormalize",6}}) {auto* par=p.params.getParameter(pair.first);par->setValueNotifyingHost(par->convertTo0to1(pair.second));}
  juce::MemoryBlock saved;p.getStateInformation(saved);
  PunchTailProcessor restored;restored.setStateInformation(saved.getData(),int(saved.getSize()));waitFor(restored,0,2048);
  ck(restored.params.getRawParameterValue("punchNormalize")->load()==-3 && restored.params.getRawParameterValue("tailNormalize")->load()==6,"Normalization gain did not round trip");
  auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
  for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();if(id=="punchNormalize"||id=="tailNormalize")legacy.removeChild(i,nullptr);}
  juce::MemoryBlock oldState;{juce::MemoryOutputStream out(oldState,false);legacy.writeToStream(out);}
  restored.setStateInformation(oldState.getData(),int(oldState.getSize()));waitFor(restored,0,2048);
  ck(restored.params.getRawParameterValue("punchNormalize")->load()==0 && restored.params.getRawParameterValue("tailNormalize")->load()==0,"Legacy normalization must restore neutral gain");
  ck(restored.asset(0)->sample.left==a->sample.left && restored.asset(0)->sample.right==a->sample.right,"Trimmed project audio did not round trip");
  p.loadFile(0,file);waitFor(p,0,4800);ck(p.asset(0)->trimFirst==0,"Reload must restore original");
  p.loadFile(1,file);waitFor(p,1,4800);p.trimSilence(1);waitFor(p,1,2048);
  ck(p.asset(0)->sample.left.size()==4800,"Tail trim changed punch");
  p.trimSilence(1);juce::Thread::sleep(20);ck(p.asset(1)->sample.left.size()==2048,"Repeated trim changed audio");
 }
 file.deleteFile();std::cout<<"PASS trim worker, stereo, lane isolation, original bytes, project reload and original-file reload\n";
}
