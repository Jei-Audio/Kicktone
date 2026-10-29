// v0.4.62 ATTACK LAYER: pitch-swept click mixed into the Punch bus.
// DSP (via pt::Voice), processor parameters/state and the processed preview.
#include "LegacyTestProcessor.h"
#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* param=p.params.getParameter(id);param->setValueNotifyingHost(param->convertTo0to1(v));}
static float value(PunchTailProcessor& p,const char* id){return p.params.getRawParameterValue(id)->load();}

static pt::Sample tone(double hz,double seconds,double rate=48000){
    pt::Sample s;s.rate=rate;const size_t n=size_t(seconds*rate);s.left.resize(n);s.right.resize(n);
    for(size_t i=0;i<n;++i){s.left[i]=float(.3*std::sin(2*pt::pi*hz*double(i)/rate));s.right[i]=s.left[i];}
    return s;
}
// Render the punch bus of one voice (punch + attack layer) for n samples.
static std::vector<float> punchBus(const pt::Settings& s,int note,const pt::Sample* punch,const pt::Sample* tail,size_t n,double rate=48000){
    pt::Voice v;v.start(note,1,1,punch,tail,s);std::vector<float> out(n);
    for(size_t i=0;i<n;++i)out[i]=v.nextParts(punch,tail,rate).punch[0];
    return out;
}
static std::vector<double> crossings(const std::vector<float>& x,size_t from,size_t to){
    std::vector<double> c;for(size_t i=from+1;i<to;++i)if(x[i-1]<0&&x[i]>=0)c.push_back(double(i-1)-x[i-1]/double(x[i]-x[i-1]));return c;
}
static void dsp(){
    const auto punch=tone(110,.2),tail=tone(65.406,.6);
    pt::Settings off;off.root=36;
    pt::Settings on=off;on.attackOn=true;on.attackLengthMs=3;on.attackStartHz=4000;on.attackGain=1;
    // OFF: identical to a voice that never heard of the attack layer.
    {pt::Settings legacy;legacy.root=36;auto a=punchBus(off,36,&punch,&tail,9600),b=punchBus(legacy,36,&punch,&tail,9600);check(a==b,"Attack layer OFF changed the punch");}
    // ON with no punch loaded: the layer alone, exactly as long as LENGTH.
    for(double ms:{1.0,3.0,5.0,20.0,50.0}){auto s=on;s.attackLengthMs=ms;   // v0.5.1: up to 50 ms
        const auto x=punchBus(s,36,nullptr,&tail,9600);const size_t len=size_t(ms*48);
        double energy=0;for(size_t i=0;i<len;++i)energy+=x[i]*x[i];
        check(energy>1e-3,"Attack layer is silent");
        for(size_t i=len+1;i<x.size();++i)check(x[i]==0.f,"Attack layer is longer than LENGTH");
        check(std::abs(x[0])<1e-6f,"Attack layer must start at zero (no click at sample 0)");
        check(std::abs(x[len-1])<.02f,"Attack layer must fade out to zero");}
    // Pitch falls: the first half crosses zero faster than the second half.
    {auto s=on;s.attackLengthMs=5;const auto x=punchBus(s,36,nullptr,&tail,480);
     const auto a=crossings(x,0,120),b=crossings(x,120,240);check(a.size()>b.size()&&a.size()>=3,"Attack layer pitch does not fall");
     const double startHz=48000.0/(a[1]-a[0]);check(startHz>1500&&startHz<4500,"Attack layer does not start near START");}
    // v0.5.1: a long (40 ms) layer still sweeps all the way down to the played note.
    {auto s=on;s.attackLengthMs=40;const auto x=punchBus(s,36,nullptr,&tail,1920);
     const auto end=crossings(x,800,1900);check(end.size()>=2,"Long attack layer has no end cycles");
     const double endHz=48000.0/(end.back()-end[end.size()-2]);check(endHz<200,"Long attack layer does not reach the note pitch");
     check(punchBus(s,36,nullptr,&tail,2400)[1925]==0.f,"Long attack layer is longer than LENGTH");}
    // Higher START gives a brighter click.
    {auto lo=on,hi=on;lo.attackStartHz=1000;hi.attackStartHz=12000;
     check(crossings(punchBus(hi,36,nullptr,&tail,144),0,144).size()>crossings(punchBus(lo,36,nullptr,&tail,144),0,144).size()+3,"START does not set the click pitch");}
    // Follows the played note: a higher note ends higher (more cycles in the tail end of the click).
    {auto s=on;s.attackStartHz=500;s.attackLengthMs=5;
     const auto low=punchBus(s,24,nullptr,&tail,240),high=punchBus(s,48,nullptr,&tail,240);
     check(crossings(high,0,240).size()>crossings(low,0,240).size(),"Attack layer does not follow the played note");
     check(std::abs(pt::noteHz(69)-440)<1e-9&&std::abs(pt::noteHz(36)-65.406)<.01,"Note frequency mapping");}
    // LEVEL scales linearly; invert punch flips it; mixed on top of the punch.
    {auto half=on;half.attackGain=.5;const auto a=punchBus(on,36,nullptr,&tail,240),b=punchBus(half,36,nullptr,&tail,240);
     for(size_t i=0;i<240;++i)check(std::abs(b[i]-.5f*a[i])<1e-6f,"LEVEL does not scale the layer");
     auto inv=on;inv.invertPunch=true;const auto c=punchBus(inv,36,nullptr,&tail,240);
     for(size_t i=0;i<240;++i)check(std::abs(c[i]+a[i])<1e-6f,"Punch polarity does not flip the layer");
     const auto mixed=punchBus(on,36,&punch,&tail,480),dry=punchBus(off,36,&punch,&tail,480);
     for(size_t i=0;i<480;++i)check(std::abs(mixed[i]-dry[i]-(i<240?a[i]:0.f))<1e-5f,"Attack layer is not added on top of the Punch");}
    // Stereo: identical in both channels, and the tail bus is untouched.
    {pt::Voice v;v.start(36,1,1,&punch,&tail,on);pt::Voice w;w.start(36,1,1,&punch,&tail,off);
     for(int i=0;i<480;++i){auto p=v.nextParts(&punch,&tail,48000),q=w.nextParts(&punch,&tail,48000);
        check(p.punch[0]-q.punch[0]==p.punch[1]-q.punch[1],"Attack layer differs between channels");
        check(p.tail==q.tail&&p.reverb==q.reverb,"Attack layer leaked into the tail/reverb buses");}}
    // v0.5.13 END: KEY (below 20 Hz, the default) is bit-identical to the
    // played-note sweep of every earlier version; a fixed END is the same on
    // every key and sets where the sweep stops.
    {check(on.attackEndHz<pt::kAttackEndKeyHz,"END must default to KEY");
     for(double key:{0.0,19.0,19.99})for(int note:{24,36,48}){auto k=on;k.attackEndHz=key;
        check(pt::attackEndFor(key,note)==pt::noteHz(note),"KEY must end on the played note");
        check(punchBus(k,note,nullptr,&tail,2400)==punchBus(on,note,nullptr,&tail,2400),"END = KEY changed the attack layer");}
     auto fixed=on;fixed.attackLengthMs=40;fixed.attackStartHz=4000;fixed.attackEndHz=1000;
     check(pt::attackEndFor(1000,24)==1000&&pt::attackEndFor(20,60)==20,"A fixed END must ignore the key");
     const auto a=punchBus(fixed,24,nullptr,&tail,1920),b=punchBus(fixed,48,nullptr,&tail,1920);
     check(a==b,"A fixed END must sound the same on every key");
     const auto end=crossings(a,1500,1880);check(end.size()>=4,"Fixed END: no end cycles");
     const double endHz=48000.0/((end.back()-end.front())/double(end.size()-1));
     check(endHz>800&&endHz<1300,"The sweep does not stop near END (1000 Hz)");
     auto key=fixed;key.attackEndHz=0;const auto c=crossings(punchBus(key,24,nullptr,&tail,1920),1500,1880);
     check(c.size()<end.size(),"END 1000 Hz must end higher than the played note (KEY)");
     // END above START: the click rises.
     auto rise=on;rise.attackLengthMs=10;rise.attackStartHz=500;rise.attackEndHz=4000;const auto r=punchBus(rise,36,nullptr,&tail,480);
     check(crossings(r,240,480).size()>crossings(r,0,240).size()+3,"END above START must rise");}
    // Never exceeds the start/limit range, even at extreme rates/values.
    for(double rate:{22050.,44100.,96000.}){auto s=on;s.attackStartHz=20000;s.attackLengthMs=9;
        pt::Voice v;v.start(36,1,1,nullptr,&tail,s);for(int i=0;i<int(rate*.01);++i){auto p=v.nextParts(nullptr,&tail,rate);check(std::isfinite(p.punch[0])&&std::abs(p.punch[0])<=1.0001f,"Attack layer out of range");}}
}
struct TailtoneAttackLayerTest {
static void processor(){
    auto pOwner=std::make_unique<LegacyTestProcessor>();auto& p=*pOwner;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
    check(value(p,"attackLayerOn")==0&&std::abs(value(p,"attackLayerLength")-3)<1e-4f&&std::abs(value(p,"attackLayerStart")-4000)<.5f&&std::abs(value(p,"attackLayerLevel")+6)<1e-4f,"Fresh attack layer defaults");
    check(p.params.getParameter("attackLayerOn")->getParameterIndex()>p.params.getParameter("outputLimiter")->getParameterIndex(),"Attack layer parameters were not appended");
    // v0.5.13 END: appended after every earlier parameter; KEY by default; host text.
    {auto* end=dynamic_cast<juce::RangedAudioParameter*>(p.params.getParameter("attackLayerEnd"));check(end!=nullptr,"attackLayerEnd missing");
     // v0.5.20: the EQ position parameters are appended right after it.
     check(end->getParameterIndex()==p.params.getParameter("punchEqPost")->getParameterIndex()-1,"attackLayerEnd must be appended after every earlier parameter");
     check(value(p,"attackLayerEnd")==19&&p.settings().attackEndHz<pt::kAttackEndKeyHz,"END must start at KEY");
     check(end->getNormalisableRange().start==19&&end->getNormalisableRange().end==12000,"END range must be KEY, 20-12000 Hz");
     check(end->getText(end->getValue(),0)=="Key","KEY text");
     check(end->getText(end->convertTo0to1(440),0)=="440 Hz","END Hz text");
     check(std::abs(end->convertFrom0to1(end->getValueForText("1000"))-1000)<1&&end->convertFrom0to1(end->getValueForText("key"))==19,"END text entry");
     set(p,"attackLayerEnd",2500);check(std::abs(p.settings().attackEndHz-2500)<1,"settings() does not carry END");set(p,"attackLayerEnd",19);}
    for(auto* id:{"attackLayerLength","attackLayerStart","attackLayerLevel"})check(p.params.getParameter(id)->getParameterIndex()>p.params.getParameter("attackLayerOn")->getParameterIndex(),"Attack layer order");
    {auto* r=dynamic_cast<juce::RangedAudioParameter*>(p.params.getParameter("attackLayerLength"));check(r->getNormalisableRange().start==1&&r->getNormalisableRange().end==50,"LENGTH must be 1-50 ms (v0.5.1)");
     check(std::abs(r->convertFrom0to1(.5f)-5.f)<1.5f,"LENGTH 1-5 ms must keep about half the knob travel");}
    set(p,"attackLayerOn",1);set(p,"attackLayerLength",1.5f);set(p,"attackLayerStart",8000);set(p,"attackLayerLevel",0);set(p,"attackLayerEnd",700);
    const auto s=p.settings();
    check(s.attackOn&&std::abs(s.attackLengthMs-1.5)<.01&&std::abs(s.attackStartHz-8000)<1&&std::abs(s.attackGain-1)<1e-5,"settings() does not carry the attack layer");
    juce::MemoryBlock saved;p.getStateInformation(saved);
    auto qOwner=std::make_unique<LegacyTestProcessor>();auto& q=*qOwner;q.signalThreadShouldExit();q.notify();q.stopThread(-1);q.setStateInformation(saved.getData(),int(saved.getSize()));
    check(value(q,"attackLayerOn")==1&&std::abs(value(q,"attackLayerLength")-1.5f)<.01f&&std::abs(value(q,"attackLayerStart")-8000)<1&&std::abs(value(q,"attackLayerEnd")-700)<1,"Attack layer state did not restore");
    auto old=p.params.copyState();
    for(int i=old.getNumChildren()-1;i>=0;--i)if(old.getChild(i).getProperty("id").toString().startsWith("attackLayer"))old.removeChild(i,nullptr);
    juce::MemoryBlock bytes;{juce::MemoryOutputStream out(bytes,false);old.writeToStream(out);}
    q.setStateInformation(bytes.getData(),int(bytes.getSize()));
    check(value(q,"attackLayerOn")==0&&std::abs(value(q,"attackLayerLength")-3)<1e-4f&&std::abs(value(q,"attackLayerLevel")+6)<1e-4f&&value(q,"attackLayerEnd")==19,"Older projects must load with the attack layer OFF and END = KEY");
    // Live playback: a played note carries it on the Punch bus (same tail, ON vs OFF).
    auto render=[](bool on){
        auto owner=std::make_unique<LegacyTestProcessor>();auto& live=*owner;live.signalThreadShouldExit();live.notify();live.stopThread(-1);
        auto asset=std::make_shared<PunchTailProcessor::Asset>();asset->sample=tone(60,.5);
        live.assets[0]=asset;   // same fixture style as OutputLimiterTests
        set(live,"gate",0);set(live,"output",0);set(live,"eqOn",0);set(live,"punchEqOn",0);set(live,"reverbEqOn",0);set(live,"phaseAuto",0);set(live,"outputLimiter",0);set(live,"transientOn",0);set(live,"punchDistOn",0);set(live,"tailDistOn",0);set(live,"attackLayerOn",on?1.f:0.f);set(live,"attackLayerLevel",0);set(live,"attackLayerLength",5);
        live.prepareToPlay(48000,128);
        juce::AudioBuffer<float> a(2,256);juce::MidiBuffer m;m.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);live.processBlock(a,m);
        return a;};
    const auto with=render(true),without=render(false);
    float diff=0,late=0;for(int i=0;i<256;++i){const float d=std::abs(with.getSample(0,i)-without.getSample(0,i));if(i<240)diff=(std::max)(diff,d);else late=(std::max)(late,d);}
    check(without.getMagnitude(0,0,256)>1e-4f,"Test punch did not play");
    check(diff>1e-3f,"Played note has no attack layer");
    check(late<1e-6f,"Attack layer continued after LENGTH in playback");
}
};
int main()try{
    juce::ScopedJuceInitialiser_GUI init;
    dsp();TailtoneAttackLayerTest::processor();
    std::cout<<"PASS attack layer: OFF identity, LENGTH 1/3/5/20/50 ms (40 ms sweep), zero start/fade, falling pitch from START to the played note, END (KEY identity, fixed end, rise), LEVEL, polarity, stereo, bus isolation, range, params/state/legacy, live note\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
