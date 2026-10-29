// v0.4.49: legato Glide and the optional Punch layer (Punch 2).
#include "../Source/PluginProcessor.h"
#include "../Source/PunchPitch.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}
static double toneHz(int note){return 220.0*std::pow(2.0,(note-36)/12.0);}

struct TailtoneGlideLayerTest {
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static std::shared_ptr<PunchTailProcessor::Bank> toneBank() {
    auto bank=std::make_shared<PunchTailProcessor::Bank>();bank->prepared=true;bank->root=36;
    for(int n=0;n<128;++n){auto& s=bank->notes[size_t(n)];s.rate=48000;s.left.resize(96000);
        for(size_t i=0;i<s.left.size();++i)s.left[i]=float(.5*std::sin(2*pt::pi*toneHz(n)*double(i)/48000.0));
        s.right=s.left;}
    return bank;
 }
 // v0.5.10: a kick-like bank. Low fundamental (F1 = 43.65 Hz at note 36),
 // harmonics, soft clipping and an arbitrary phase per note, as a stretched
 // render has. This is what made the old 4 ms alignment pick a wrong phase.
 static std::shared_ptr<PunchTailProcessor::Bank> kickBank() {
    auto bank=std::make_shared<PunchTailProcessor::Bank>();bank->prepared=true;bank->root=36;
    std::mt19937 rng(7);std::uniform_real_distribution<double> ph(0,2*pt::pi);
    for(int n=0;n<128;++n){auto& s=bank->notes[size_t(n)];s.rate=48000;s.left.resize(72000);
        const double hz=43.65*std::pow(2.0,(n-36)/12.0),phase=ph(rng);
        for(size_t i=0;i<s.left.size();++i){const double t=double(i)/48000.0,w=2*pt::pi*hz*t+phase;
            const double v=std::tanh(3.0*(std::sin(w)+.35*std::sin(2*w)+.2*std::sin(3*w)))/std::tanh(3.0);
            s.left[i]=float(.6*v*std::exp(-t*1.2));}
        s.right=s.left;}
    return bank;
 }
 struct Rig {
    std::unique_ptr<PunchTailProcessor> p=std::make_unique<PunchTailProcessor>();
    std::vector<float> out;
    Rig(bool glideOn,float glideMs,std::shared_ptr<PunchTailProcessor::Bank> bank=nullptr) {
        stop(*p);
        set(*p,"phaseAuto",0);set(*p,"gate",1);set(*p,"output",0);set(*p,"crossfade",0);set(*p,"join",0);
        set(*p,"glide",glideOn?1.f:0.f);set(*p,"glideTime",glideMs);set(*p,"masterRelease",1);
        p->bank=bank?bank:toneBank();
        p->prepareToPlay(48000,128);
    }
    void event(const juce::MidiMessage& m){juce::MidiBuffer midi;midi.addEvent(m,0);run(1,&midi);}
    void run(int blocks,juce::MidiBuffer* midi=nullptr){
        juce::AudioBuffer<float> block(2,128);juce::MidiBuffer none;
        for(int b=0;b<blocks;++b){p->processBlock(block,(b==0&&midi)?*midi:none);
            for(int i=0;i<128;++i)out.push_back(block.getSample(0,i));}
    }
    PunchTailProcessor::PlayingVoice* playing(){for(auto& v:p->voices)if(v.active)return &v;return nullptr;}
 };
 // Frequency from rising zero crossings in [from,to) samples.
 static double frequency(const std::vector<float>& x,size_t from,size_t to) {
    std::vector<double> c;
    for(size_t i=from+1;i<to&&i<x.size();++i)if(x[i-1]<0&&x[i]>=0)c.push_back(double(i-1)+x[i-1]/(x[i-1]-x[i]));
    check(c.size()>=2,"Too few cycles to measure");
    return 48000.0*double(c.size()-1)/(c.back()-c.front());
 }
 static void glide() {
    // Defaults: both options unticked.
    {PunchTailProcessor fresh;stop(fresh);
     check(fresh.params.getRawParameterValue("glide")->load()<.5f,"Glide must be unticked by default");
     check(fresh.params.getRawParameterValue("punchLayer")->load()<.5f,"Punch layer must be unticked by default");
     check(std::abs(fresh.params.getRawParameterValue("glideTime")->load()-80.f)<.01f,"Glide time default");}
    // Glide OFF: a legato key retriggers like before.
    {Rig r(false,100);r.event(juce::MidiMessage::noteOn(1,48,1.f));r.run(110);
     r.event(juce::MidiMessage::noteOn(1,55,1.f));
     auto* v=r.playing();check(v&&v->note==43&&v->time<.01,"Glide OFF must retrigger on a legato key");}
    // Glide ON, legato: same voice continues and slides 220 Hz -> 329.6 Hz.
    {Rig r(true,100);r.event(juce::MidiMessage::noteOn(1,48,1.f));r.run(110);
     auto* before=r.playing();check(before&&before->note==36,"First key did not play");
     const double timeBefore=before->time;
     const size_t start=r.out.size()+size_t(r.p->getLatencySamples()); // output is delayed by reported latency
     r.event(juce::MidiMessage::noteOn(1,55,1.f));r.run(140);
     auto* v=r.playing();
     check(v==before&&v->time>timeBefore,"Legato glide restarted the kick");
     check(v->note==43&&v->key==55,"Glide did not move the voice to the new note");
     const double first=frequency(r.out,start,start+720),last=frequency(r.out,start+9600,start+14400);
     const double middle=frequency(r.out,start+1920,start+2880);
     check(std::abs(first/toneHz(36)-1)<.08,"Glide did not start at the old pitch: "+std::to_string(first));
     check(std::abs(last/toneHz(43)-1)<.01,"Glide did not arrive at the new pitch: "+std::to_string(last));
     check(middle>first*1.05&&middle<last*.97,"Glide did not slide through intermediate pitches");
     // No click: the steepest step during the switch/slide stays near the steady tone's own.
     double steady=0,during=0;
     // v0.5.26: the steady window ends at the rendered output. With the linear-phase
     // Tail wet latency (2175) start+16000 lay 127 samples past it: Release read
     // unrendered memory there, the Windows Debug build stopped on the bad index.
     const size_t steadyEnd=(std::min)(start+16000,r.out.size());
     check(steadyEnd>=start+15000,"Glide click check has too little steady output");
     for(size_t i=start+12001;i<steadyEnd;++i)steady=(std::max)(steady,double(std::abs(r.out[i]-r.out[i-1])));
     for(size_t i=start-200;i<start+4800;++i)during=(std::max)(during,double(std::abs(r.out[i]-r.out[i-1])));
     check(during<=steady*1.25,"Glide switch produced a click");
     // The old key's note-off must not end the new note; the new key's does.
     r.event(juce::MidiMessage::noteOff(1,48));check(!r.p->masterEnvelope.isReleased(),"Old key released the glided note");
     r.event(juce::MidiMessage::noteOff(1,55));check(r.p->masterEnvelope.isReleased(),"New key note-off did not release");}
    // Glide ON but not legato (previous key released first): retrigger.
    {Rig r(true,100);r.event(juce::MidiMessage::noteOn(1,48,1.f));r.run(50);
     r.event(juce::MidiMessage::noteOff(1,48));r.run(2);r.event(juce::MidiMessage::noteOn(1,55,1.f));
     auto* v=r.playing();check(v&&v->note==43&&v->time<.01,"Separate key presses must retrigger with Glide on");}
    // v0.5.10 OFF-PHASE CLICK: on a low, clipped tail the switch must continue the
    // held note (the new tail starts at the old pitch, phase-aligned). Compare the
    // first 1.5 ms after the switch with the same key simply held on, at several
    // switch times and intervals (up and down, more than an octave).
    {const auto bank=kickBank();
     const int pairs[6][2]={{48,55},{48,43},{50,38},{45,60},{56,44},{48,51}};
     int tried=0;
     for(int blocks:{37,61,89,113,151,197})for(const auto& pr:pairs) {
        Rig held(true,80,bank),glided(true,80,bank);
        held.event(juce::MidiMessage::noteOn(1,pr[0],1.f));glided.event(juce::MidiMessage::noteOn(1,pr[0],1.f));
        held.run(blocks);glided.run(blocks);
        const size_t start=glided.out.size()+size_t(glided.p->getLatencySamples());
        glided.event(juce::MidiMessage::noteOn(1,pr[1],1.f));held.run(40);glided.run(39);   // output is delayed by the reported latency
        double peak=0,resid=0,steady=0,during=0;
        for(size_t i=start-2880;i<start;++i)peak=(std::max)(peak,double(std::abs(held.out[i])));
        for(size_t i=start;i<start+72;++i)resid=(std::max)(resid,double(std::abs(held.out[i]-glided.out[i])));
        for(size_t i=start-2880;i<start;++i)steady=(std::max)(steady,double(std::abs(held.out[i]-held.out[i-1])));
        for(size_t i=start-5;i<start+600;++i)during=(std::max)(during,double(std::abs(glided.out[i]-glided.out[i-1])));
        check(peak>.01,"Kick bank is silent");
        check(resid<=.06*peak,"Glide switch is off phase (key "+std::to_string(pr[0])+" -> "+std::to_string(pr[1])+" after "+std::to_string(blocks)+" blocks: residual "+std::to_string(resid/peak)+" of peak)");
        check(during<=steady*1.15,"Glide switch clicks on a clipped tail ("+std::to_string(during/steady)+"x the steady slope)");
        ++tried;}
     check(tried==36,"Off-phase regression did not run");}
    // v0.5.10: a key pressed while a slide is still running continues from the
    // pitch heard at that moment (no jump back to the previous target) and
    // still arrives at the newest key.
    {Rig r(true,200);r.event(juce::MidiMessage::noteOn(1,48,1.f));r.run(110);
     r.event(juce::MidiMessage::noteOn(1,60,1.f));r.run(30);   // 80 ms into a 200 ms slide 220 -> 440 Hz
     const size_t start=r.out.size()+size_t(r.p->getLatencySamples());
     r.event(juce::MidiMessage::noteOn(1,52,1.f));r.run(140);
     const double before=frequency(r.out,start-720,start),after=frequency(r.out,start+200,start+920);
     check(before>toneHz(36)*1.12&&before<toneHz(48)*.9,"Mid-slide pitch not intermediate: "+std::to_string(before));
     check(std::abs(after/before-1)<.06,"Second legato key jumped the pitch: "+std::to_string(before)+" -> "+std::to_string(after));
     auto* v=r.playing();check(v&&v->note==40&&v->key==52,"Second legato key did not retarget the voice");
     const double last=frequency(r.out,start+12000,start+16800);
     check(std::abs(last/toneHz(40)-1)<.01,"Second glide did not arrive at the newest key: "+std::to_string(last));}
    // Glide time controls the slide length.
    {Rig r(true,20);r.event(juce::MidiMessage::noteOn(1,48,1.f));r.run(60);const size_t start=r.out.size()+size_t(r.p->getLatencySamples());
     r.event(juce::MidiMessage::noteOn(1,55,1.f));r.run(40);
     const double f=frequency(r.out,start+1440,start+3840);check(std::abs(f/toneHz(43)-1)<.01,"Short glide time did not arrive quickly: "+std::to_string(f));}
 }
 // v0.5.23 STRETCHED GLIDE (always on since v0.5.25, no switch): a tone whose amplitude marks the place
 // in the tail (a ramp over the prepared length). Every note has the same timeline,
 // like a fixed-length bank; `adaptive` gives each note its own length instead.
 static std::shared_ptr<PunchTailProcessor::Bank> rampBank(bool adaptive) {
    auto bank=std::make_shared<PunchTailProcessor::Bank>();bank->prepared=true;bank->root=36;
    for(int n=0;n<128;++n){auto& s=bank->notes[size_t(n)];s.rate=48000;
        const double scale=adaptive&&n>=15&&n<=51?1.0+.04*(n-36):1.0;
        if(adaptive&&n>=15&&n<=51)bank->noteStretches[size_t(n)]=scale;
        s.left.resize(size_t(96000*scale));
        for(size_t i=0;i<s.left.size();++i)s.left[i]=float((.1+.4*double(i)/double(s.left.size()))*std::sin(2*pt::pi*toneHz(n)*double(i)/48000.0));
        s.right=s.left;}
    return bank;
 }
 static double peakAround(const std::vector<float>& x,size_t centre,size_t half) {
    double peak=0;for(size_t i=centre-half;i<centre+half&&i<x.size();++i)peak=(std::max)(peak,double(std::abs(x[i])));return peak;
 }
 static double maxSlope(const std::vector<float>& x,size_t from,size_t to) {
    double m=0;for(size_t i=from;i<to&&i<x.size();++i)m=(std::max)(m,double(std::abs(x[i]-x[i-1])));return m;
 }
 static void stretched() {
    {PunchTailProcessor fresh;stop(fresh);
     // v0.5.25: the old switch is an inert placeholder (ON, not automatable, still at its index).
     check(fresh.params.getRawParameterValue("glideStretched")->load()>.5f,"Inert glideStretched placeholder must read ON");
     check(!fresh.params.getParameter("glideStretched")->isAutomatable(),"glideStretched placeholder must not be automatable");
     check(fresh.params.getParameter("glideStretched")->getParameterIndex()==fresh.params.getParameter("punchDecayNormalize")->getParameterIndex()+1,"glideStretched index moved");}
    // 1. Timeline: after a 600 ms octave slide the tail is where the prepared target
    // note is at the same time (the removed tape-style slide lagged ~170 ms behind).
    // The placeholder is set OFF here: it must not bring the tape-style slide back.
    {const auto bank=rampBank(false);
     Rig held(true,600,bank),str(true,600,bank);set(*str.p,"glideStretched",0);
     held.event(juce::MidiMessage::noteOn(1,60,1.f));str.event(juce::MidiMessage::noteOn(1,48,1.f));
     held.run(49);str.run(49);
     str.event(juce::MidiMessage::noteOn(1,60,1.f));held.run(1);
     const int rest=int(1.3*48000/128)-50;held.run(rest);str.run(rest);
     const size_t at=size_t(1.2*48000)+size_t(str.p->getLatencySamples());
     const double h=peakAround(held.out,at,480),st=peakAround(str.out,at,480);
     check(h>.01,"Ramp bank is silent");
     check(std::abs(st/h-1)<.02,"Stretched glide did not keep the prepared timeline: "+std::to_string(st/h));
     auto* v=str.playing();check(v&&v->note==48&&v->key==60&&!v->gliding,"Stretched glide did not end on the target note");
     check(std::abs(v->tailOffset)<.004,"Stretched glide left the timeline: offset "+std::to_string(v->tailOffset)+" s");}
    // 2. Pitch follows the classic curve (linear in semitones) while the voice steps
    // through the prepared notes in between, and arrives exactly.
    {Rig r(true,300);r.event(juce::MidiMessage::noteOn(1,48,1.f));r.run(49);
     const size_t start=r.out.size()+size_t(r.p->getLatencySamples());
     r.event(juce::MidiMessage::noteOn(1,60,1.f));r.run(55);   // ~150 ms: heard pitch 42
     auto* v=r.playing();check(v&&v->gliding&&v->stretchedGlide&&std::abs(v->note-42)<=1,"Stretched glide does not read the notes in between (note "+std::to_string(v?v->note:-1)+")");
     r.run(200);
     for(double tc:{.03,.09,.15,.21,.27}) {
         const double f=frequency(r.out,start+size_t((tc-.005)*48000),start+size_t((tc+.005)*48000)),want=toneHz(36)*std::pow(2.0,tc/.3);
         check(std::abs(f/want-1)<.025,"Stretched glide pitch off the curve at "+std::to_string(tc)+" s: "+std::to_string(f)+" Hz, want "+std::to_string(want));}
     const double last=frequency(r.out,start+size_t(.32*48000),start+size_t(.42*48000));
     check(std::abs(last/toneHz(48)-1)<.005,"Stretched glide did not arrive at the new pitch: "+std::to_string(last));
     check(r.playing()->note==48&&!r.playing()->gliding,"Stretched glide still running after GLIDE TIME");}
    // 3. Kick-like bank (arbitrary phase per note): the slide starts as the held note
    // (no switch), and every step is phase-aligned: no click, no level dip.
    {const auto bank=kickBank();
     const int pairs[4][2]={{48,55},{48,43},{50,38},{45,60}};int tried=0;
     for(int blocks:{61,113})for(const auto& pr:pairs) {
        Rig held(true,200,bank),target(true,200,bank),g(true,200,bank);
        held.event(juce::MidiMessage::noteOn(1,pr[0],1.f));target.event(juce::MidiMessage::noteOn(1,pr[1],1.f));g.event(juce::MidiMessage::noteOn(1,pr[0],1.f));
        held.run(blocks);target.run(blocks);g.run(blocks);
        const size_t L=size_t(g.p->getLatencySamples()),start=g.out.size()+L;
        g.event(juce::MidiMessage::noteOn(1,pr[1],1.f));held.run(1);target.run(1);
        const int after=int(.3*48000/128);g.run(after);held.run(after);target.run(after);
        double peak=0,resid=0;
        for(size_t i=start-2880;i<start;++i)peak=(std::max)(peak,double(std::abs(held.out[i])));
        for(size_t i=start;i<start+72;++i)resid=(std::max)(resid,double(std::abs(held.out[i]-g.out[i])));
        const std::string what=" (key "+std::to_string(pr[0])+" -> "+std::to_string(pr[1])+" after "+std::to_string(blocks)+" blocks)";
        check(peak>.01,"Kick bank is silent");
        check(resid<=.02*peak,"Stretched glide does not start as the held note"+what+": "+std::to_string(resid/peak));
        const double steady=(std::max)(maxSlope(held.out,start-2880,start),maxSlope(target.out,start,start+2880));
        const double during=maxSlope(g.out,start-5,start+size_t(.25*48000));
        check(during<=steady*1.2,"Stretched glide step clicks"+what+": "+std::to_string(during/steady)+"x");
        const auto level=[&](const std::vector<float>& x,size_t from){return peakAround(x,from+1200,1200)/std::exp(-1.2*double(from+1200-L)/48000.0);};
        const double ref=level(held.out,start-2400);
        for(size_t w=start;w+2400<start+size_t(.22*48000);w+=960){const double lv=level(g.out,w)/ref;
            check(lv>.8&&lv<1.2,"Stretched glide level dip/bump"+what+" at "+std::to_string(double(w-start)/48.0)+" ms: "+std::to_string(lv));}
        auto* v=g.playing();check(v&&v->note==pt::tailNoteForKey(pr[1],true)&&!v->gliding,"Stretched glide did not arrive"+what);
        ++tried;}
     check(tried==8,"Stretched kick regression did not run");}
    // 4. A key pressed mid-slide continues from the pitch heard right now.
    {Rig r(true,200);r.event(juce::MidiMessage::noteOn(1,48,1.f));r.run(110);
     r.event(juce::MidiMessage::noteOn(1,60,1.f));r.run(30);
     const size_t start=r.out.size()+size_t(r.p->getLatencySamples());
     r.event(juce::MidiMessage::noteOn(1,52,1.f));r.run(140);
     const double before=frequency(r.out,start-720,start),after=frequency(r.out,start+200,start+920);
     check(before>toneHz(36)*1.12&&before<toneHz(48)*.9,"Mid-slide pitch not intermediate: "+std::to_string(before));
     check(std::abs(after/before-1)<.06,"Second legato key jumped the pitch: "+std::to_string(before)+" -> "+std::to_string(after));
     auto* v=r.playing();check(v&&v->note==40&&v->key==52&&!v->gliding,"Second stretched slide did not reach the newest key");
     const double last=frequency(r.out,start+12000,start+16800);
     check(std::abs(last/toneHz(40)-1)<.01,"Second stretched slide did not arrive: "+std::to_string(last));}
    // 5. Adaptive Stretch (notes of different lengths): each step keeps the relative
    // place in the tail, then the note runs at its own length.
    {const auto bank=rampBank(true);
     Rig held(true,300,bank),g(true,300,bank);
     held.event(juce::MidiMessage::noteOn(1,60,1.f));g.event(juce::MidiMessage::noteOn(1,48,1.f));
     held.run(49);g.run(49);g.event(juce::MidiMessage::noteOn(1,60,1.f));held.run(1);
     const int rest=int(1.0*48000/128)-50;held.run(rest);g.run(rest);
     // Expected relative position: integrate 1/length of the note being read (it
     // steps at each half semitone), from the onset note's position at the key.
     const double t0=50*128/48000.0,glideT=.3;double r=t0/2.0;
     for(double t=t0;t<.9;t+=1e-5){const double heard=t<t0+glideT?36+12*(t-t0)/glideT:48.0;
         const int n=int(std::lround(heard));r+=1e-5/(2.0*(1.0+.04*(n-36)));}
     const size_t at=size_t(.9*48000)+size_t(g.p->getLatencySamples());
     const double gain=peakAround(held.out,at,480)/(.1+.4*.9/(2.0*1.48));
     const double want=gain*(.1+.4*r),got=peakAround(g.out,at,480);
     check(std::abs(got/want-1)<.03,"Adaptive stretched glide lost the relative place in the tail: "+std::to_string(got/want));}
 }
 static std::shared_ptr<PunchTailProcessor::Asset> asset(double rate,size_t n,double hz,float amp) {
    auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=rate;a->sample.left.resize(n);
    for(size_t i=0;i<n;++i)a->sample.left[i]=float(amp*std::sin(2*pt::pi*hz*double(i)/rate));
    a->sample.right=a->sample.left;a->name="synthetic";return a;
 }
 static void layer() {
    PunchTailProcessor p;
    set(p,"punchPitch",0);set(p,"punchPitchMode",0);set(p,"phaseAuto",0);
    auto punch=asset(44100,4410,90,.4f);
    const auto wait=[&](std::shared_ptr<const PunchTailProcessor::Asset> layer,const char* why){
        p.notify();
        for(int i=0;i<10000;++i){{const juce::ScopedLock lock(p.assetLock);
            if(p.preparedPunch&&p.preparedPunchSource==punch&&p.preparedPunchLayer==layer)return p.preparedPunch;}
            juce::Thread::sleep(1);}
        throw std::runtime_error(why);
    };
    {const juce::ScopedLock lock(p.assetLock);p.assets[0]=punch;}
    auto prepared=wait(nullptr,"Punch not prepared");
    check(prepared->sample.left==punch->sample.left,"Single punch changed");
    // Punch 2 loaded but PUNCH LAYER unticked: Punch 2 is silent.
    auto longer=asset(44100,8820,1500,.2f);
    {const juce::ScopedLock lock(p.assetLock);p.assets[3]=longer;}
    p.notify();juce::Thread::sleep(300);
    {const juce::ScopedLock lock(p.assetLock);check(p.preparedPunchLayer==nullptr&&p.preparedPunch->sample.left==punch->sample.left,"Unticked layer must not change Punch");}
    // v0.4.50 Ticked: Punch 2 is the DECAY. Punch 1 up to the Attack/Decay split,
    // Punch 2 (same timeline, trimmed) after it, crossfaded over the Transition.
    // Defaults: split 20%, transition 2 ms, offset 0 -> split at 882 frames, fade 88.2.
    set(p,"punchLayer",1);
    prepared=wait(longer,"Layered punch not prepared");
    check(prepared->sample.left.size()==4410&&prepared->sample.right.size()==4410,"Layer changed Punch length (join timing)");
    const auto& y=prepared->sample.left;
    for(size_t i:{size_t(0),size_t(400),size_t(830)})check(std::abs(y[i]-punch->sample.left[i])<1e-6f,"Attack before the split is not Punch 1 only");
    for(size_t i:{size_t(940),size_t(2000),size_t(4000)})check(std::abs(y[i]-longer->sample.left[i])<1e-6f,"Decay after the split is not Punch 2 only (trimmed, same timeline)");
    {const float mid=y[882],want=.5f*(punch->sample.left[882]+longer->sample.left[882]);check(std::abs(mid-want)<.02f,"Split crossfade is not halfway at the split point");}
    // Attack End (split) moves the meeting point.
    set(p,"punchRegionSplit",50);
    auto wait2=[&](const char* why){p.notify();for(int i=0;i<10000;++i){{const juce::ScopedLock lock(p.assetLock);
        if(std::abs(p.preparedLayerSplit-double(p.params.getRawParameterValue("punchRegionSplit")->load())*.01)<1e-9&&
           std::abs(p.preparedLayerOffset-double(p.params.getRawParameterValue("punch2Offset")->load())*.001)<1e-9&&p.preparedPunchLayer)return p.preparedPunch;}
        juce::Thread::sleep(1);}throw std::runtime_error(why);};
    prepared=wait2("Split change did not rebuild");
    check(std::abs(prepared->sample.left[2000]-punch->sample.left[2000])<1e-6f&&std::abs(prepared->sample.left[2300]-longer->sample.left[2300])<1e-6f,"Attack End did not move the meeting point");
    // v0.5.31 direction: Punch 2 Offset +10 ms moves Punch 2 RIGHT (441 frames later
    // in time, so the output plays Punch 2 audio from 441 frames earlier).
    set(p,"punch2Offset",10);
    prepared=wait2("Offset change did not rebuild");
    check(std::abs(prepared->sample.left[3000]-longer->sample.left[3000-441])<1e-6f,"Positive offset must move Punch 2 later (right)");
    set(p,"punch2Offset",-25);
    prepared=wait2("Negative offset did not rebuild");
    check(std::abs(prepared->sample.left[3000]-longer->sample.at(0,3000.0+1102.5))<1e-5f,"Negative offset must move Punch 2 earlier (left)");
    check(p.params.getParameter("punch2Offset")->getNormalisableRange().start==-25&&p.params.getParameter("punch2Offset")->getNormalisableRange().end==25,"Offset range must be -25..+25 ms");
    set(p,"punch2Offset",0);set(p,"punchRegionSplit",20);
    prepared=wait2("Reset did not rebuild");
    // v0.4.67 PUNCH 2 TUNE: Punch 2 alone is pitched (Punch Pitch mode) before the
    // mix; the attack (Punch 1) and the punch length are untouched; 0 st = exact.
    check(p.params.getParameter("punch2Pitch")->getNormalisableRange().start==-12&&p.params.getParameter("punch2Pitch")->getNormalisableRange().end==12&&
          p.params.getRawParameterValue("punch2Pitch")->load()==0,"Punch 2 tune must be -12..+12 st, default 0");
    for(double st:{7.0,-12.0,0.35}) {
        set(p,"punch2Pitch",float(st));p.notify();
        for(int i=0;i<10000;++i){{const juce::ScopedLock lock(p.assetLock);if(std::abs(p.preparedPunch2Pitch-st)<1e-6&&p.preparedPunchLayer==longer)break;}juce::Thread::sleep(1);}
        {const juce::ScopedLock lock(p.assetLock);check(std::abs(p.preparedPunch2Pitch-st)<1e-6,"Punch 2 tune did not rebuild");prepared=p.preparedPunch;}
        // The host stores the value as float (0.35 st -> 0.3499999940 st).
        const auto tuned=pt::renderPunchPitch(longer->sample,double(p.params.getRawParameterValue("punch2Pitch")->load()),pt::PunchPitchMode::StretchAligned);
        check(prepared->sample.left.size()==4410,"Punch 2 tune changed the punch length");
        for(size_t i:{size_t(0),size_t(400),size_t(830)})check(std::abs(prepared->sample.left[i]-punch->sample.left[i])<1e-6f,"Punch 2 tune changed the attack");
        for(size_t i:{size_t(940),size_t(2000),size_t(4000)})check(std::abs(prepared->sample.left[i]-tuned.left[i])<1e-6f,"decay is not the tuned Punch 2");
    }
    set(p,"punch2Pitch",0);p.notify();
    for(int i=0;i<10000;++i){{const juce::ScopedLock lock(p.assetLock);if(std::abs(p.preparedPunch2Pitch)<1e-9)break;}juce::Thread::sleep(1);}
    prepared=wait2("Punch 2 tune reset did not rebuild");
    for(size_t i:{size_t(940),size_t(2000),size_t(4000)})check(std::abs(prepared->sample.left[i]-longer->sample.left[i])<1e-6f,"Punch 2 tune 0 st must leave Punch 2 exact");
    // Pitch lanes are built from the layered punch.
    check(prepared->forNote(45).sample.left.size()==4410&&prepared->forNote(128).sample.left.size()>4410,"Key lanes do not use the layered punch");
    // A shorter Punch 2 at another sample rate is resampled; when it runs out the
    // decay fades to silence (no click) instead of returning to Punch 1.
    auto shorter=asset(22050,1000,700,.3f);
    {const juce::ScopedLock lock(p.assetLock);p.assets[3]=shorter;}
    prepared=wait(shorter,"Resampled layer not prepared");
    check(prepared->sample.left.size()==4410,"Shorter layer changed length");
    check(std::abs(prepared->sample.left[1500])>1e-4f,"Resampled Punch 2 (2000 frames at 44.1 kHz) missing");
    check(std::abs(prepared->sample.left[3000])<1e-6f,"Decay after Punch 2 ends must be silent");
    {float step=0;for(size_t i=1950;i<2050;++i)step=(std::max)(step,std::abs(prepared->sample.left[i]-prepared->sample.left[i-1]));
     check(step<.05f,"Punch 2 end clicks");}
    // Untick again: back to the single Punch exactly.
    set(p,"punchLayer",0);
    prepared=wait(nullptr,"Unticking did not rebuild");
    check(prepared->sample.left==punch->sample.left,"Unticked layer still audible");
    // v0.5.4 ONLY PUNCH 2 FOLLOWS (fresh ON): with the layer and PUNCH FOLLOWS TAIL PITCH the
    // keys pitch only Punch 2 (the decay); Punch 1 (the attack) keeps PUNCH PITCH.
    {check(p.params.getRawParameterValue("punchFollowDecayOnly")->load()==1,"ONLY PUNCH 2 FOLLOWS must be ON for new instances");
     auto a1=asset(44100,4410,2000,.4f),a2=asset(44100,8820,300,.3f);
     {const juce::ScopedLock lock(p.assetLock);p.assets[0]=a1;p.assets[3]=a2;}
     const auto build=[&](int mode,bool decayOnly,float manual,const char* why){
         set(p,"punchPitchMode",float(mode));set(p,"punchPitchFidelity",0);set(p,"punchPitch",manual);
         set(p,"punchLayer",1);set(p,"punchFollowTail",1);set(p,"punchFollowDecayOnly",decayOnly?1.f:0.f);p.notify();
         for(int i=0;i<20000;++i){{const juce::ScopedLock lock(p.assetLock);
             if(p.preparedPunch&&p.preparedPunchSource==a1&&p.preparedPunchLayer==a2&&p.preparedPunchFollow&&p.preparedPunchMode==mode&&
                p.preparedPunchDecayOnly==decayOnly&&std::abs(p.preparedPunchPitch-manual)<1e-6)return p.preparedPunch;}juce::Thread::sleep(1);}
         throw std::runtime_error(why);};
     auto hz=[](const std::vector<float>& x,size_t a,size_t b){std::vector<double> c;   // interpolated rising zero crossings
         for(size_t i=a+1;i<b&&i<x.size();++i)if(x[i-1]<0&&x[i]>=0)c.push_back(double(i-1)+double(-x[i-1])/double(x[i]-x[i-1]));
         return c.size()<2?0.0:double(c.size()-1)*44100.0/(c.back()-c.front());};
     auto closeTo=[](double a,double b){return std::abs(a/b-1)<.04;};
     {auto pr=build(0,true,0,"Decay-only punch not prepared");
      for(int note:{24,30,43,51}){const auto& x=pr->forNote(note).sample.left;const double ratio=std::pow(2.0,(note-36)/12.0);
          check(x.size()==4410,"Decay-only changed the punch length");
          check(closeTo(hz(x,0,600),2000),"Attack (Punch 1) followed the key: "+std::to_string(hz(x,0,600))+" Hz at note "+std::to_string(note));
          check(closeTo(hz(x,1500,3500),300*ratio),"Decay (Punch 2) did not follow the key: "+std::to_string(hz(x,1500,3500))+" Hz at note "+std::to_string(note));
          for(size_t i:{size_t(0),size_t(400),size_t(830)})check(x[i]==a1->sample.left[i],"Attack before the split is not Punch 1 unchanged");
          const auto decay=pt::renderPunchPitch(a2->sample,note-36.0,pt::PunchPitchMode::StretchAligned);
          for(size_t i:{size_t(940),size_t(2000),size_t(4000)})check(std::abs(x[i]-decay.left[i])<1e-6f,"Decay is not Punch 2 pitched by the key");}
      check(pr->forNote(36).sample.left==pr->sample.left,"Root key differs from the main punch");
      {const auto& x=pr->sample.left;for(size_t i:{size_t(400),size_t(2000)})check(x[i]==(i<838?a1->sample.left[i]:a2->sample.left[i]),"Main punch is not the plain Punch 1 + Punch 2 mix");}}
     // OFF: the whole punch follows (as in v0.4.67).
     {auto pr=build(0,false,0,"Whole-punch follow not prepared");const auto& x=pr->forNote(43).sample.left;
      check(closeTo(hz(x,0,600),2000*std::pow(2.0,7/12.0))&&closeTo(hz(x,1500,3500),300*std::pow(2.0,7/12.0)),"OFF must pitch the whole punch");}
     // PUNCH PITCH +2 still moves the attack; the decay gets PUNCH PITCH + key.
     {auto pr=build(0,true,2,"Decay-only with PUNCH PITCH not prepared");const auto& x=pr->forNote(43).sample.left;
      check(closeTo(hz(x,0,600),2000*std::pow(2.0,2/12.0))&&closeTo(hz(x,1500,3500),300*std::pow(2.0,9/12.0)),"PUNCH PITCH + key not applied as attack / decay");}
     // Resample: the attack (and so the punch length) keeps PUNCH PITCH; Punch 2 enters where Punch 1 is at the split.
     {auto pr=build(1,true,0,"Decay-only Resample not prepared");
      for(int note:{30,43}){const auto& x=pr->forNote(note).sample.left;
          check(x.size()==4410&&pr->forNote(note).punchTimeScale==1.0,"Resample decay-only changed the punch length");
          check(closeTo(hz(x,0,600),2000)&&closeTo(hz(x,1500,3500),300*std::pow(2.0,(note-36)/12.0)),"Resample decay-only pitches the wrong part");}}
     set(p,"punchPitch",0);set(p,"punchPitchMode",0);set(p,"punchFollowTail",0);set(p,"punchFollowDecayOnly",1);
     // Projects saved before v0.5.4 keep the whole punch following.
     {juce::MemoryBlock saved;p.getStateInformation(saved);auto old=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
      for(int i=old.getNumChildren()-1;i>=0;--i)if(old.getChild(i).getProperty("id").toString()=="punchFollowDecayOnly")old.removeChild(i,nullptr);
      juce::MemoryBlock bytes;{juce::MemoryOutputStream o(bytes,false);old.writeToStream(o);}
      auto qOwner=std::make_unique<PunchTailProcessor>();stop(*qOwner);qOwner->setStateInformation(bytes.getData(),int(bytes.getSize()));
      check(qOwner->params.getRawParameterValue("punchFollowDecayOnly")->load()==0,"Older projects must keep the whole punch following");}}
    // v0.5.5 PUNCH DECAY NOTE: measured on the worker from the prepared punch (after ATTACK END);
    // USE DECAY NOTE moves PUNCH PITCH (no layer) or PUNCH 2 TUNE (layer) so the mid tone = TAIL ROOT.
    {set(p,"punchLayer",0);set(p,"punchFollowTail",0);set(p,"punchPitch",0);set(p,"punch2Pitch",0);set(p,"root",36);set(p,"punchPitchMode",1);   // Resample (the default mode)
     auto a1=asset(44100,8820,2000,.4f);   // 2 kHz attack ...
     for(size_t i=1764;i<a1->sample.left.size();++i){const double v=.4*std::sin(2*pt::pi*110.0*double(i)/44100.0);a1->sample.left[i]=a1->sample.right[i]=float(v);}   // ... 110 Hz (A2) decay from 40 ms
     {const juce::ScopedLock lock(p.assetLock);p.assets[0]=a1;p.assets[3]=nullptr;}
     const auto waitNote=[&](double pitch,bool layer,const char* why){p.notify();
         for(int i=0;i<20000;++i){{const juce::ScopedLock lock(p.assetLock);
             if(p.preparedPunch&&p.preparedPunchSource==a1&&std::abs(p.preparedPunchPitch-pitch)<1e-6&&bool(p.preparedPunchLayer)==layer&&std::abs(p.preparedLayerSplit-.2)<1e-9&&p.preparedPunch->decayNote.valid)return p.preparedPunch->decayNote;}
             juce::Thread::sleep(1);}throw std::runtime_error(why);};
     auto note=waitNote(0,false,"Decay note not measured");
     check(std::abs(note.midi()-45)<.1,"Decay note of a 110 Hz decay is not A2: "+std::to_string(note.midi()));
     check(p.punchDecayNote()==note,"punchDecayNote() does not return the prepared value");
     // USE DECAY NOTE: PUNCH PITCH becomes root - detected = 36 - 45 = -9 st; the re-measured decay is then C3.
     check(p.useDecayNote(),"useDecayNote refused a valid note");
     const float pitch=p.params.getRawParameterValue("punchPitch")->load();check(std::abs(pitch+9)<.1f,"PUNCH PITCH not set to root - decay: "+std::to_string(pitch));
     note=waitNote(pitch,false,"Decay note not re-measured after USE DECAY NOTE");
     check(std::abs(note.midi()-36)<.15,"After USE DECAY NOTE the decay is not on the root: "+std::to_string(note.midi()));
     // With the layer: PUNCH 2 TUNE moves, PUNCH PITCH does not.
     set(p,"punchPitch",0);auto a2=asset(44100,8820,123.47,.3f);   // B2 decay (midi 47): TUNE -11 st stays inside -12..+12
     {const juce::ScopedLock lock(p.assetLock);p.assets[3]=a2;}set(p,"punchLayer",1);
     note=waitNote(0,true,"Layered decay note not measured");
     check(std::abs(note.midi()-47)<.15,"Layered decay note is not B2: "+std::to_string(note.midi()));
     check(p.useDecayNote(),"useDecayNote refused with the layer");
     check(p.params.getRawParameterValue("punchPitch")->load()==0&&std::abs(p.params.getRawParameterValue("punch2Pitch")->load()+11)<.15f,"USE DECAY NOTE did not move PUNCH 2 TUNE by root - decay (-11 st)");
     // No stable pitch: a noise decay is refused and the button does nothing.
     set(p,"punchLayer",0);set(p,"punch2Pitch",0);
     auto noisy=asset(44100,8820,2000,.4f);{std::mt19937 rng(5);std::uniform_real_distribution<float> u(-.4f,.4f);for(size_t i=1764;i<noisy->sample.left.size();++i)noisy->sample.left[i]=noisy->sample.right[i]=u(rng);}
     {const juce::ScopedLock lock(p.assetLock);p.assets[0]=noisy;}p.notify();
     for(int i=0;i<20000;++i){{const juce::ScopedLock lock(p.assetLock);if(p.preparedPunch&&p.preparedPunchSource==noisy&&!p.preparedPunchLayer)break;}juce::Thread::sleep(1);}
     check(!p.punchDecayNote().valid&&!p.useDecayNote()&&p.params.getRawParameterValue("punchPitch")->load()==0,"Noise decay must give no note and no retune");}
    stop(p);
 }
 static void state() {
    // Real file load into slot 3, saved and restored with the project.
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tailtone-punch2-test.wav");
    {juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream=std::make_unique<juce::FileOutputStream>(file);
     static_cast<juce::FileOutputStream*>(stream.get())->setPosition(0);static_cast<juce::FileOutputStream*>(stream.get())->truncate();
     std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(stream.release(),44100,2,24,{},0));
     check(bool(w),"Cannot write test WAV");juce::AudioBuffer<float> b(2,2205);
     for(int i=0;i<2205;++i){b.setSample(0,i,.3f*std::sin(i*.1f));b.setSample(1,i,.3f*std::sin(i*.1f));}
     w->writeFromAudioSampleBuffer(b,0,2205);}
    PunchTailProcessor p;set(p,"punchLayer",1);p.loadFile(3,file);
    std::shared_ptr<const PunchTailProcessor::Asset> loaded;
    for(int i=0;i<5000&&!loaded;++i){loaded=p.asset(3);juce::Thread::sleep(1);}
    check(loaded&&loaded->sample.left.size()==2205,"Punch 2 file did not load");
    juce::MemoryBlock saved;p.getStateInformation(saved);
    PunchTailProcessor q;q.setStateInformation(saved.getData(),int(saved.getSize()));
    std::shared_ptr<const PunchTailProcessor::Asset> restored;
    for(int i=0;i<5000&&!restored;++i){restored=q.asset(3);juce::Thread::sleep(1);}
    check(restored&&restored->sample.left==loaded->sample.left&&restored->name==loaded->name,"Punch 2 audio not saved with the project");
    check(q.params.getRawParameterValue("punchLayer")->load()>.5f,"Punch layer switch not saved");
    p.unloadFile(3);check(!p.asset(3),"Punch 2 unload failed");
    // Older projects: options stay unticked.
    auto legacy=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
    for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();
        if(id=="punchLayer"||id=="glide"||id=="glideTime"||id=="glideStretched")legacy.removeChild(i,nullptr);}
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    PunchTailProcessor r;set(r,"glide",1);set(r,"glideStretched",0);r.setStateInformation(old.getData(),int(old.getSize()));
    check(r.params.getRawParameterValue("glideStretched")->load()>.5f,"Loaded projects must reset the inert glideStretched placeholder to ON");
    check(r.params.getRawParameterValue("punchLayer")->load()<.5f&&r.params.getRawParameterValue("glide")->load()<.5f,"Old projects must load with Glide and Punch layer unticked");
    check(p.params.getParameter("glideTime")->getParameterIndex()>p.params.getParameter("lowKeyPunch")->getParameterIndex(),"New parameters not appended");
    // v0.5.31: PUNCH 2 OFFSET was reversed. Projects saved before it flip the stored
    // sign (same sound); projects saved now keep their value.
    {auto saveOffset=[&](const juce::var& version,float ms){auto t=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
        if(version.isVoid())t.removeProperty("savedWithVersion",nullptr);else t.setProperty("savedWithVersion",version,nullptr);
        for(int i=0;i<t.getNumChildren();++i)if(t.getChild(i).getProperty("id").toString()=="punch2Offset")t.getChild(i).setProperty("value",ms,nullptr);
        juce::MemoryBlock b;{juce::MemoryOutputStream stream(b,false);t.writeToStream(stream);}
        auto s=std::make_unique<PunchTailProcessor>();stop(*s);s->setStateInformation(b.getData(),int(b.getSize()));
        return s->params.getRawParameterValue("punch2Offset")->load();};
     check(std::abs(saveOffset("0.5.30",7.5f)+7.5f)<1e-4f,"A v0.5.30 project must load its Punch 2 offset with the sign flipped");
     check(std::abs(saveOffset("0.4.69",-12.f)-12.f)<1e-4f,"A v0.4.69 project must load its Punch 2 offset with the sign flipped");
     check(std::abs(saveOffset(juce::var(),3.f)+3.f)<1e-4f,"A project without savedWithVersion must flip the Punch 2 offset");
     check(std::abs(saveOffset("0.5.31",7.5f)-7.5f)<1e-4f,"A v0.5.31 project must keep its Punch 2 offset");
     check(std::abs(saveOffset("0.6.0",-4.f)+4.f)<1e-4f,"A newer project must keep its Punch 2 offset");
     check(saveOffset("0.5.30",0.f)==0.f,"A zero Punch 2 offset must stay zero");
     juce::MemoryBlock now;auto again=std::make_unique<PunchTailProcessor>();stop(*again);
     set(r,"punch2Offset",9);r.getStateInformation(now);again->setStateInformation(now.getData(),int(now.getSize()));
     check(std::abs(again->params.getRawParameterValue("punch2Offset")->load()-9.f)<.02f,"Punch 2 offset must round-trip unchanged in a new project");}
    stop(p);stop(q);stop(r);file.deleteFile();
 }
};

int main() try {
    juce::ScopedJuceInitialiser_GUI init;
    TailtoneGlideLayerTest::glide();
    TailtoneGlideLayerTest::stretched();
    TailtoneGlideLayerTest::layer();
    TailtoneGlideLayerTest::state();
    std::cout<<"PASS legato glide (slide, no punch/retrigger, no click, key-owned note-off, time), Punch layer (unticked silent, summed within Punch length, resampled, key lanes, untick), Punch 2 file/state, offset direction + pre-v0.5.31 migration, defaults/migration, stretched glide always on (timeline, pitch curve, aligned steps, retrigger, adaptive, inert switch)\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
