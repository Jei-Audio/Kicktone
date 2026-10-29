// v0.4.54: Root Render + Pitch reverb auto match and low phase lock.
#include "../Source/PluginProcessor.h"
#include "../Source/WetAlign.h"
#include "../Source/TailReverbPitchBank.h"
#include "WaveFixture.h"
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
using pt::Sample;

static Sample tone(double rate,size_t n,double hz,double amp,double phase=0,double decay=0){
    Sample s;s.rate=rate;s.left.resize(n);s.right.resize(n);
    for(size_t i=0;i<n;++i){const double t=double(i)/rate;s.left[i]=float(amp*std::exp(-decay*t)*std::sin(2*pt::pi*hz*t+phase));s.right[i]=s.left[i];}
    return s;
}
static Sample add(Sample a,const Sample& b,double g=1){for(size_t i=0;i<a.left.size()&&i<b.left.size();++i){a.left[i]+=float(g*b.left[i]);a.right[i]+=float(g*b.right[i]);}return a;}
// Worst 50 ms window: low-band (<300 Hz) stereo energy of mix relative to dry, dB.
static double worstLowWindowDb(const Sample& dry,const Sample& mix,size_t frames){
    const auto dl=pt::wetalign::lowPass(dry.left,300,dry.rate),dr=pt::wetalign::lowPass(dry.right,300,dry.rate);
    const auto ml=pt::wetalign::lowPass(mix.left,300,dry.rate),mr=pt::wetalign::lowPass(mix.right,300,dry.rate);
    const size_t w=size_t(dry.rate*.05);double worst=1e9,peak=0;
    for(size_t i=0;i+w<=frames;i+=w){double e=0;for(size_t k=i;k<i+w;++k)e+=dl[k]*dl[k]+dr[k]*dr[k];peak=(std::max)(peak,e);}
    for(size_t i=0;i+w<=frames;i+=w){double e0=0,e1=0;for(size_t k=i;k<i+w;++k){e0+=dl[k]*dl[k]+dr[k]*dr[k];e1+=ml[k]*ml[k]+mr[k]*mr[k];}
        if(e0>peak*1e-3)worst=(std::min)(worst,10*std::log10((e1+1e-20)/(e0+1e-20)));}
    return worst;
}

struct TailtoneWetAlignTest {
 static void match() {
    const double rate=48000;const size_t n=48000;
    const auto dry=tone(rate,n,55,.5,0,2);
    // Reverb that is inverted and 2 ms late: cancels the bass badly.
    Sample wet=tone(rate,n,55,.45,0,2);for(auto* ch:{&wet.left,&wet.right})for(auto& v:*ch)v=-v;
    Sample late=wet;for(auto* ch:{&late.left,&late.right}){std::vector<float> o(ch->size(),0.f);for(size_t i=96;i<ch->size();++i)o[i]=(*ch)[i-96];*ch=o;}
    check(worstLowWindowDb(dry,add(dry,late),n)<-3,"Fixture does not cancel");
    const auto m=pt::wetalign::findMatch(dry,late,n);
    check(m.polarity==-1&&std::abs(m.shift+96)<=8,"Auto match did not find polarity -1 and -2 ms (got "+std::to_string(m.polarity)+", "+std::to_string(m.shift)+")");
    auto fixed=late;pt::wetalign::applyMatch(fixed,m);
    check(worstLowWindowDb(dry,add(dry,fixed),n-200)>2,"Auto match still loses bass");
    check(m.worst>m.baselineWorst,"Worst window did not improve");
    // Already-aligned reverb is left alone.
    const auto aligned=tone(rate,n,55,.3,0,2);
    const auto a=pt::wetalign::findMatch(dry,aligned,n);
    check(a.shift==0&&a.polarity==1,"Aligned reverb was moved");
    // Shift is limited to +-5 ms.
    auto farShifted=late;for(auto* ch:{&farShifted.left,&farShifted.right}){std::vector<float> o(ch->size(),0.f);for(size_t i=480;i<ch->size();++i)o[i]=(*ch)[i-480];*ch=o;}
    const auto f=pt::wetalign::findMatch(dry,farShifted,n);check(std::abs(f.shift)<=240,"Shift exceeded 5 ms");
 }
 static void lock() {
    const double rate=48000;const size_t n=48000,dryFrames=36000;
    auto dry=tone(rate,n,60,.5,0,1.5);for(size_t i=dryFrames;i<n;++i){dry.left[i]=0;dry.right[i]=0;}
    // Reverb low end drifting out of phase (61 Hz vs 60 Hz) + bright part.
    const auto wetLow=tone(rate,n,61,.3,pt::pi/2,1.0),wetHigh=tone(rate,n,3000,.1,0,1.0);
    auto wet=add(wetLow,wetHigh);const auto original=wet;
    pt::wetalign::phaseLock(wet,dry,dryFrames);
    const auto lowOut=pt::wetalign::lowPass(wet.left,200,rate),lowDry=pt::wetalign::lowPass(dry.left,200,rate),lowIn=pt::wetalign::lowPass(original.left,200,rate);
    double ab=0,aa=0,bb=0,eo=0,ei=0;
    for(size_t i=2400;i<dryFrames-2400;++i){ab+=lowOut[i]*lowDry[i];aa+=lowOut[i]*lowOut[i];bb+=lowDry[i]*lowDry[i];eo+=lowOut[i]*lowOut[i];ei+=lowIn[i]*lowIn[i];}
    check(ab/std::sqrt(aa*bb)>.97,"Locked low band does not follow the dry tail's phase");
    check(std::abs(10*std::log10(eo/ei))<1.0,"Phase lock changed the reverb's low-end loudness");
    // High part untouched.
    // What phase lock changed must lie in the low end only (nothing above ~1 kHz).
    {std::vector<float> diff(n);for(size_t i=0;i<n;++i)diff[i]=wet.left[i]-original.left[i];
     std::vector<float> hiOrig(original.left);
     const auto dl=pt::wetalign::lowPass(diff,1000,rate),ol=pt::wetalign::lowPass(hiOrig,1000,rate);
     double hd=0,hr=0;for(size_t i=0;i<n;++i){const double a1=diff[i]-dl[i],b1=hiOrig[i]-ol[i];hd+=a1*a1;hr+=b1*b1;}
     check(hd<hr*1e-3,"Phase lock changed the reverb above 200 Hz");}
    // After the dry tail ends the original reverb low end returns.
    double diff=0,ref=0;for(size_t i=dryFrames+2400;i<n-2400;++i){diff+=(lowOut[i]-lowIn[i])*(lowOut[i]-lowIn[i]);ref+=lowIn[i]*lowIn[i];}
    check(diff<ref*1e-3,"Reverb ring-out after the dry tail was altered");
    // With the lock, no window loses bass.
    check(worstLowWindowDb(dry,add(dry,wet),dryFrames)>-0.5,"Locked reverb still cancels the bass");
 }
 // v0.4.55: both stages stop promptly when the worker is cancelled (knob moved),
 // and a no-op checkpoint changes nothing.
 static void cancel() {
    const double rate=48000;const size_t n=48000;
    const auto dry=tone(rate,n,55,.5,0,2);
    Sample wet=tone(rate,n,55,.45,0,2);for(auto* ch:{&wet.left,&wet.right})for(auto& v:*ch)v=-v;
    const auto plain=pt::wetalign::findMatch(dry,wet,n);
    int calls=0;const auto counted=pt::wetalign::findMatch(dry,wet,n,[&]{++calls;});
    check(calls>100&&counted.shift==plain.shift&&counted.polarity==plain.polarity&&counted.worst==plain.worst,"Checkpoint changed the auto match result or was rarely called");
    calls=0;bool threw=false;
    try{pt::wetalign::findMatch(dry,wet,n,[&]{if(++calls>3)throw std::runtime_error("cancelled");});}catch(const std::runtime_error&){threw=true;}
    check(threw&&calls==4,"Auto match did not stop at the first checkpoint after cancel");
    auto a=wet,b=wet;pt::wetalign::phaseLock(a,dry,n-4800);
    calls=0;pt::wetalign::phaseLock(b,dry,n-4800,[&]{++calls;});
    check(calls>=6&&a.left==b.left&&a.right==b.right,"Checkpoint changed the phase lock result");
    auto c=wet;calls=0;threw=false;
    try{pt::wetalign::phaseLock(c,dry,n-4800,[&]{if(++calls>1)throw std::runtime_error("cancelled");});}catch(const std::runtime_error&){threw=true;}
    check(threw&&calls==2,"Phase lock did not stop at the first checkpoint after cancel");
 }
 static void composite(const char* path) {
    const auto tail=loadFloatWave(path);
    pt::TailReverbSettings s;s.enabled=true;s.tailSend=true;s.mix=60;s.spaceMs=600;s.distance=0;s.width=100;s.duckAmount=0;s.keepDry=true;
    const auto off=pt::renderRootTailCompositeBank(tail,36,0,s,1.0,4,false);
    auto sm=s;sm.wetAutoMatch=true;const auto matched=pt::renderRootTailCompositeBank(tail,36,0,sm,1.0,4,false);
    auto sl=sm;sl.wetPhaseLock=true;const auto locked=pt::renderRootTailCompositeBank(tail,36,0,sl,1.0,4,false);
    auto dryOnly=s;dryOnly.mix=.001f;const auto dry=pt::renderRootTailCompositeBank(tail,36,0,dryOnly,1.0,4,false);
    const size_t frames=tail.left.size();
    const double wOff=worstLowWindowDb(*dry.rootRender,*off.rootRender,frames),wMatch=worstLowWindowDb(*dry.rootRender,*matched.rootRender,frames),
                 wLock=worstLowWindowDb(*dry.rootRender,*locked.rootRender,frames);
    std::cout<<"  "<<path<<": worst 50 ms low-band change vs dry tail: off "<<wOff<<" dB, auto match "<<wMatch<<" dB ("<<matched.wetMatch.shift*1000.0/tail.rate<<" ms, pol "<<matched.wetMatch.polarity<<"), + phase lock "<<wLock<<" dB\n";
    check(matched.wetMatch.worst>=matched.wetMatch.baselineWorst*0.995,"Auto match made the worst window worse");
    check(wMatch>=wOff-0.05,"Auto match lost bass somewhere");
    check(wLock>=-0.5&&wLock>=wMatch-0.05,"Phase lock did not remove bass loss");
    check(!off.phaseLocked&&locked.phaseLocked&&off.wetMatch.shift==0&&off.wetMatch.polarity==1,"Flags/identity wrong");
    for(int n:{30,41})check(matched.notes[size_t(n)]->left.size()==off.notes[size_t(n)]->left.size(),"Auto match changed note length");
    check(matched.notes[36]==matched.rootRender,"Root note identity lost");
 }
 static void processor() {
    PunchTailProcessor fresh;fresh.signalThreadShouldExit();fresh.notify();fresh.stopThread(-1);
    check(fresh.params.getRawParameterValue("tailVerbAutoMatch")->load()>.5f&&fresh.params.getRawParameterValue("tailVerbPhaseLock")->load()<.5f,"Defaults: auto match ON, phase lock OFF");
    auto s=fresh.tailReverbSettings();check(s.wetAutoMatch&&!s.wetPhaseLock,"Settings not read");
    PunchTailProcessor::TailWetBank b;b.autoMatch=true;b.phaseLock=false;
    auto t=s;t.wetPhaseLock=true;check(!(b.autoMatch==t.wetAutoMatch&&b.phaseLock==t.wetPhaseLock),"Key unaffected");
    juce::MemoryBlock saved;fresh.getStateInformation(saved);
    auto legacy=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
    for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();if(id=="tailVerbAutoMatch"||id=="tailVerbPhaseLock")legacy.removeChild(i,nullptr);}
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    PunchTailProcessor r;r.signalThreadShouldExit();r.notify();r.stopThread(-1);r.setStateInformation(old.getData(),int(old.getSize()));
    check(r.params.getRawParameterValue("tailVerbAutoMatch")->load()<.5f&&r.params.getRawParameterValue("tailVerbPhaseLock")->load()<.5f,"Old projects must load with both OFF");
    check(fresh.params.getParameter("tailVerbPhaseLock")->getParameterIndex()>fresh.params.getParameter("crunchMix")->getParameterIndex(),"Not appended");
 }
};

int main(int argc,char** argv) try {
    if(argc<3)throw std::runtime_error("Usage: WetAlignTests tail.wav virus-kick-3.wav");
    juce::ScopedJuceInitialiser_GUI init;
    TailtoneWetAlignTest::match();
    TailtoneWetAlignTest::lock();
    TailtoneWetAlignTest::cancel();
    for(int i=1;i<argc;++i)TailtoneWetAlignTest::composite(argv[i]);
    TailtoneWetAlignTest::processor();
    std::cout<<"PASS reverb auto match (cancel checkpoints, polarity/shift found, +-5 ms limit, aligned untouched) and low phase lock (follows dry phase, loudness/highs/ring-out kept, no bass loss) on fixtures; defaults/state\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
