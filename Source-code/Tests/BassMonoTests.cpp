// v0.5 MONO BASS: DSP (Source/BassMono.h), processor integration, state and preview.
#include "LegacyTestProcessor.h"
#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <complex>
#include <random>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* param=p.params.getParameter(id);param->setValueNotifyingHost(param->convertTo0to1(v));}
static float value(PunchTailProcessor& p,const char* id){return p.params.getRawParameterValue(id)->load();}
static constexpr double kPi=3.14159265358979323846;

// Steady-state side (L-R)/2 and mid (L+R)/2 RMS of a stereo tone after the
// filter settled (first 300 ms skipped).
struct Levels { double side=0, mid=0; };
static Levels run(double rate,double hz,double sideAmp,double midAmp,bool on,double cutoff,float mix) {
    tt::BassMono m;m.prepare(rate);m.settle(on,mix);
    const int n=int(rate*1.0),skip=int(rate*.3);double s2=0,m2=0;
    for(int i=0;i<n;++i){
        const double ph=2*kPi*hz*i/rate;
        float l=float(midAmp*std::sin(ph)+sideAmp*std::sin(ph)),r=float(midAmp*std::sin(ph)-sideAmp*std::sin(ph));
        m.process(l,r,on,cutoff,mix);
        if(i>=skip){const double s=.5*(double(l)-double(r)),md=.5*(double(l)+double(r));s2+=s*s;m2+=md*md;}
    }
    return {std::sqrt(s2/(n-skip)),std::sqrt(m2/(n-skip))};
}
static double db(double x){return 20*std::log10((std::max)(x,1e-12));}

static void dsp() {
    // OFF and MIX 0 % pass every frame bit-identically.
    {std::mt19937 rng(7);std::uniform_real_distribution<float> u(-1,1);
     for(int mode=0;mode<2;++mode){tt::BassMono m;m.prepare(48000);
         for(int i=0;i<48000;++i){const float l=u(rng),r=u(rng);float a=l,b=r;
             m.process(a,b,mode==1,150,mode==1?0.f:1.f);check(a==l&&b==r,"OFF / MIX 0 % changed the audio");}}}
    for(double rate:{44100.0,48000.0,96000.0}) {
        // Pure stereo difference at 40 Hz with MONO BELOW 120 Hz, MIX 100 %: removed (LR4: -38 dB).
        const auto low=run(rate,40,.5,0,true,120,1.f);
        check(db(low.side/(.5/std::sqrt(2.)))<-35,"Side below the frequency was not made mono");
        // Side at 1 kHz keeps its level.
        const auto high=run(rate,1000,.5,0,true,120,1.f);
        check(std::abs(db(high.side/(.5/std::sqrt(2.))))<.05,"Side above the frequency lost level");
        // The mono sum is never touched, at any frequency.
        for(double hz:{30.0,120.0,2000.0}){const auto mid=run(rate,hz,0,.5,true,300,1.f);
            check(std::abs(mid.mid-.5/std::sqrt(2.))<1e-6&&mid.side<1e-9,"Mono sum changed");}
    }
    // FREQUENCY moves the split: a 200 Hz stereo difference survives at 90 Hz, is mostly gone at 300 Hz.
    for(double cutoff:{90.0,150.0,300.0}){const auto lv=run(48000,200,.5,0,true,cutoff,1.f);
        const double expected=db(tt::BassMono::sideGain(200,cutoff));
        check(std::abs(db(lv.side/(.5/std::sqrt(2.)))-expected)<.3,"Side response does not follow MONO BELOW");}
    check(db(tt::BassMono::sideGain(200,90))>-0.5&&db(tt::BassMono::sideGain(200,300))<-15,"MONO BELOW range has no audible effect at 200 Hz");
    // Range: below 90 acts as 90, above 300 as 300.
    check(std::abs(run(48000,200,.5,0,true,20,1.f).side-run(48000,200,.5,0,true,90,1.f).side)<1e-7,"Frequency not clamped to 90 Hz");
    check(std::abs(run(48000,200,.5,0,true,5000,1.f).side-run(48000,200,.5,0,true,300,1.f).side)<1e-7,"Frequency not clamped to 300 Hz");
    // MIX between minimal stereo and pure mono.
    for(float mix:{.25f,.5f,.75f}){const auto lv=run(48000,40,.5,0,true,120,mix);
        const double expected=(1-mix)+mix*tt::BassMono::sideGain(40,120);
        check(std::abs(lv.side/(.5/std::sqrt(2.))-expected)<.015,"MIX does not blend between stereo and mono");}
    // Switching ON, MIX and FREQUENCY changes are smooth: no output step larger than the input's.
    {tt::BassMono m;m.prepare(48000);double maxIn=0,maxOut=0;float prevL=0,prevIn=0;
     for(int i=0;i<48000;++i){const float x=.8f*float(std::sin(2*kPi*60*i/48000.));float l=x,r=-x;
         const bool on=i>=9000;const double cutoff=i<24000?90:300;const float mix=i<36000?1.f:.2f;
         m.process(l,r,on,cutoff,mix);
         if(i>0){maxIn=(std::max)(maxIn,double(std::abs(x-prevIn)));maxOut=(std::max)(maxOut,double(std::abs(l-prevL)));}
         prevIn=x;prevL=l;
         if(i==9000+48*20)check(std::abs(m.currentMix()-1.f)<1e-4f,"Switching ON did not settle in 20 ms");}
     check(maxOut<=maxIn*1.05,"Mono bass switching/automation steps the output");}
    // Stability under fast automation with full-scale noise.
    {tt::BassMono m;m.prepare(44100);std::mt19937 rng(3);std::uniform_real_distribution<float> u(-1,1);float peak=0;
     for(int i=0;i<44100*2;++i){float l=u(rng),r=u(rng);m.process(l,r,true,(i/512)%2?90:300,(i/700)%2?1.f:0.f);
         check(std::isfinite(l)&&std::isfinite(r),"Mono bass produced NaN/inf");peak=(std::max)({peak,std::abs(l),std::abs(r)});}
     check(peak<2.5f,"Mono bass unstable under automation");}
}

// v0.5.1 linear-phase engine.
static void linearDsp() {
    for(double rate:{44100.0,48000.0,96000.0}) {
        const int radius=tt::bassmono::radiusFor(rate);
        for(double cutoff:{90.0,150.0,300.0}) {
            const auto taps=tt::bassmono::lowpassTaps(cutoff,rate,radius);
            check(int(taps.size())==2*radius+1,"Linear kernel length");
            double sum=0;for(size_t i=0;i<taps.size();++i){sum+=taps[i];check(taps[i]==taps[taps.size()-1-i],"Linear kernel not symmetric (not linear phase)");}
            check(std::abs(sum-1)<1e-12,"Linear kernel DC gain is not 1 (DC side would leak)");
            // Side response at MIX 100 % = 1 - LP (zero phase): LR4 magnitude.
            for(double ratio:{.25,.5,.75,1.0,1.5,2.0,4.0,10.0}){const double f=cutoff*ratio;
                std::complex<double> h;for(size_t i=0;i<taps.size();++i)h+=taps[i]*std::polar(1.0,-2*kPi*f*(double(i)-radius)/rate);
                check(std::abs(h.imag())<1e-9,"Linear kernel has a phase response");
                const double side=1-h.real(),target=tt::BassMono::sideGain(f,cutoff);
                if(target>.01)check(std::abs(db(std::abs(side))-db(target))<.2,"Linear side response does not match LR4 magnitude");
                else check(std::abs(std::abs(side)-target)<.002,"Linear side stop band does not match LR4");}
        }
        const auto kernel=tt::bassmono::design(120,rate);const int d=kernel->latency();
        check(d==radius+tt::linear::block-1,"Linear latency");
        // MIX 0 % and the mono sum: the input, delayed by exactly the latency, bit for bit.
        {tt::bassmono::LinearBassMono m;m.prepare(kernel);std::mt19937 rng(5);std::uniform_real_distribution<float> u(-1,1);
         std::vector<std::array<float,2>> in;for(int i=0;i<int(rate*.3);++i){const float l=u(rng),r=u(rng);in.push_back({l,r});}
         for(size_t i=0;i<in.size();++i){float l=in[i][0],r=in[i][1];m.process(l,r,false,1.f);
             if(int(i)>=d)check(l==in[i-size_t(d)][0]&&r==in[i-size_t(d)][1],"Linear MIX 0 is not the delayed input");else check(l==0&&r==0,"Linear output before the latency");}}
        {tt::bassmono::LinearBassMono m;m.prepare(kernel);m.settle(true,1.f);
         for(int i=0;i<int(rate*.3);++i){const float x=.7f*float(std::sin(2*kPi*55*i/rate));float l=x,r=x;m.process(l,r,true,1.f);
             const float e=i>=d?.7f*float(std::sin(2*kPi*55*(i-d)/rate)):0.f;check(l==e&&r==e,"Linear mono bass touched a mono signal");}}
        // Impulse in the side: output side = delta(n-D) - lp(n-D) -> symmetric around D (linear phase).
        {tt::bassmono::LinearBassMono m;m.prepare(kernel);m.settle(true,1.f);std::vector<double> side;
         for(int i=0;i<2*d+16;++i){float l=i==0?1.f:0.f,r=i==0?-1.f:0.f;m.process(l,r,true,1.f);side.push_back(.5*(double(l)-double(r)));}
         check(std::abs(side[size_t(d)]-(1-tt::bassmono::lowpassTaps(120,rate,radius)[size_t(radius)]))<1e-4,"Linear impulse centre");
         for(int k=1;k<radius;++k)check(std::abs(side[size_t(d+k)]-side[size_t(d-k)])<1e-5,"Linear impulse response not symmetric");}
        // Steady tones: low side removed, high side unchanged and only delayed.
        for(double hz:{40.0,1000.0}){tt::bassmono::LinearBassMono m;m.prepare(kernel);m.settle(true,1.f);double e2=0,r2=0;const int n=int(rate);
            for(int i=0;i<n;++i){const float x=.5f*float(std::sin(2*kPi*hz*i/rate));float l=x,r=-x;m.process(l,r,true,1.f);
                if(i>d+int(rate*.2)){const double ref=.5*std::sin(2*kPi*hz*(i-d)/rate),out=.5*(double(l)-double(r));
                    if(hz<100)e2+=out*out;else e2+=(out-ref)*(out-ref);r2+=ref*ref;}}
            if(hz<100)check(db(std::sqrt(e2/r2))<-35,"Linear mono bass left low stereo");
            else check(db(std::sqrt(e2/r2))<-50,"Linear mono bass altered the high stereo (level or phase)");}
        // Offline zero-phase render = realtime render advanced by the latency.
        {std::mt19937 rng(9);std::uniform_real_distribution<float> u(-1,1);std::vector<float> L,R;for(int i=0;i<int(rate*.4);++i){L.push_back(u(rng));R.push_back(u(rng));}
         const auto low=tt::bassmono::sideLowBand(L,R,120,rate);tt::bassmono::LinearBassMono m;m.prepare(kernel);m.settle(true,1.f);
         double worst=0;for(size_t i=0;i<L.size()+size_t(d);++i){float l=i<L.size()?L[i]:0.f,r=i<L.size()?R[i]:0.f;m.process(l,r,true,1.f);
             if(i>=size_t(d)){const size_t j=i-size_t(d);if(j<L.size())worst=(std::max)(worst,std::abs(double(l)-(L[j]-low[j])));}}
         check(worst<2e-4,"Offline linear mono bass differs from playback");}
    }
    // FREQUENCY change while running: kernel crossfade, no step larger than the signal's own.
    {const auto a=tt::bassmono::design(90,48000),b=tt::bassmono::design(300,48000);tt::bassmono::LinearBassMono m;m.prepare(a);m.settle(true,1.f);
     double maxIn=0,maxOut=0;float prevIn=0,prevOut=0;
     for(int i=0;i<48000;++i){const float x=.8f*float(std::sin(2*kPi*200*i/48000.));if(i==20000)m.setKernel(b);float l=x,r=-x;m.process(l,r,true,1.f);
         if(i>0){maxIn=(std::max)(maxIn,double(std::abs(x-prevIn)));if(i>5000)maxOut=(std::max)(maxOut,double(std::abs(l-prevOut)));}prevIn=x;prevOut=l;}
     check(maxOut<=maxIn*1.05,"Linear kernel change clicks");
     check(m.targetHz()==300,"Linear kernel not adopted");}
}

struct TailtoneBassMonoTest {
// Punch fixture: a 60 Hz tone in antiphase (pure stereo difference), as in the limiter test.
static std::unique_ptr<LegacyTestProcessor> fixture() {
    auto p=std::make_unique<LegacyTestProcessor>();p->signalThreadShouldExit();p->notify();p->stopThread(-1);
    set(*p,"gate",0);set(*p,"output",0);set(*p,"eqOn",0);set(*p,"punchEqOn",0);set(*p,"reverbEqOn",0);set(*p,"phaseAuto",0);
    auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=48000;
    for(int i=0;i<24000;++i){const float v=.5f*float(std::sin(2*kPi*60*i/48000.)+.5*std::sin(2*kPi*2000*i/48000.));a->sample.left.push_back(v);a->sample.right.push_back(-v);}
    p->assets[0]=a;return p;
}
static std::vector<float> render(PunchTailProcessor& p,int blocks=60) {
    p.prepareToPlay(48000,256);juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;std::vector<float> out;
    midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
    for(int b=0;b<blocks;++b){p.processBlock(audio,midi);midi.clear();
        for(int i=0;i<256;++i)for(int ch=0;ch<2;++ch)out.push_back(audio.getSample(ch,i));}
    return out;
}
// Side-signal (L-R)/2 amplitude at 60 Hz and 2 kHz of an interleaved render, over
// 9600 frames (whole cycles of both) starting at `from`. Returned as energies.
static double sideAmplitude(const std::vector<float>& x,double hz,size_t from) {
    double re=0,im=0;const size_t n=9600;
    for(size_t k=0;k<n;++k){const size_t i=2*(from+k);if(i+1>=x.size())return 0;const double s=.5*(double(x[i])-double(x[i+1]));
        re+=s*std::cos(2*kPi*hz*double(k)/48000.);im+=s*std::sin(2*kPi*hz*double(k)/48000.);}
    return 2*std::sqrt(re*re+im*im)/double(n);
}
static std::complex<double> sideDft(const std::vector<float>& x,double hz,size_t from) {
    std::complex<double> sum;const size_t n=9600;
    for(size_t k=0;k<n;++k){const size_t i=2*(from+k);if(i+1>=x.size())break;const double s=.5*(double(x[i])-double(x[i+1]));
        sum+=s*std::polar(1.0,-2*kPi*hz*double(k)/48000.);}
    return sum;
}
static std::pair<double,double> sideBands(const std::vector<float>& x,size_t from=4800) {
    const double a=sideAmplitude(x,60,from),b=sideAmplitude(x,2000,from);return {a*a,b*b};
}
static void processor() {
    // Parameters: appended after every earlier one; fresh OFF, 120 Hz, 100 %; ranges 90-300 Hz / 0-100 %.
    {auto fresh=std::make_unique<PunchTailProcessor>();
     check(value(*fresh,"bassMono")==0&&value(*fresh,"bassMonoHz")==120&&value(*fresh,"bassMonoMix")==100,"Mono bass defaults");
     auto range=[&](const char* id){return fresh->params.getParameterRange(id);};
     check(range("bassMonoHz").start==90&&range("bassMonoHz").end==300&&range("bassMonoMix").start==0&&range("bassMonoMix").end==100,"Mono bass ranges");
     const int index=fresh->params.getParameter("bassMono")->getParameterIndex();
     check(index==fresh->params.getParameter("pitchExtremeV5")->getParameterIndex()+1&&
           fresh->params.getParameter("bassMonoHz")->getParameterIndex()==index+1&&fresh->params.getParameter("bassMonoMix")->getParameterIndex()==index+2,
           "Mono bass parameters were not appended after every earlier parameter");
     // v0.5.1 LINEAR PHASE: appended after bassMonoMix, fresh ON.
     check(fresh->params.getParameter("bassMonoLinear")->getParameterIndex()==index+3&&value(*fresh,"bassMonoLinear")==1,"bassMonoLinear not appended / not ON for new instances");}
    // OFF: bit-identical to a processor that never had the feature touched; MIX 0 % ON as well.
    {auto a=fixture(),b=fixture(),c=fixture();set(*c,"bassMono",1);set(*c,"bassMonoMix",0);set(*c,"bassMonoLinear",0);
     set(*b,"bassMonoHz",250);   // frequency alone does nothing while OFF
     const auto ra=render(*a),rb=render(*b),rc=render(*c);
     check(ra==rb&&ra==rc,"Mono bass OFF / MIX 0 % changed the output");}
    // ON, 120 Hz, 100 %: the 60 Hz stereo difference goes, the 2 kHz one stays; no latency.
    {auto off=fixture(),on=fixture();set(*on,"bassMono",1);set(*on,"bassMonoHz",150);set(*on,"bassMonoMix",100);set(*on,"bassMonoLinear",0);
     const auto ro=render(*off),rn=render(*on);const auto bo=sideBands(ro),bn=sideBands(rn);
     check(bo.first>1e-3,"Fixture has no low stereo content");
     check(10*std::log10(bn.first/bo.first)<-20,"Mono bass ON did not make the low end mono");
     check(std::abs(10*std::log10(bn.second/bo.second))<1,"Mono bass ON changed the stereo above the frequency");
     check(on->getLatencySamples()==off->getLatencySamples(),"Mono bass changed host latency");
     // MIX 50 % keeps about half the low stereo difference.
     auto half=fixture();set(*half,"bassMono",1);set(*half,"bassMonoHz",150);set(*half,"bassMonoMix",50);set(*half,"bassMonoLinear",0);
     const auto bh=sideBands(render(*half));const double ratio=std::sqrt(bh.first/bo.first);
     check(ratio>.4&&ratio<.62,"Mono bass MIX 50 % is not in between");}
    // v0.5.1 LINEAR PHASE: latency only while MONO BASS + LINEAR were on at audio start;
    // MIX 0 = the OFF render delayed by exactly the reported latency; the low side goes,
    // the 2 kHz side keeps level AND phase (only delayed).
    {auto off=fixture(),lin=fixture(),zero=fixture(),idle=fixture();
     for(auto* p:{lin.get(),zero.get()}){set(*p,"bassMono",1);set(*p,"bassMonoHz",150);set(*p,"bassMonoLinear",1);}
     set(*lin,"bassMonoMix",100);set(*zero,"bassMonoMix",0);
     set(*idle,"bassMono",0);set(*idle,"bassMonoLinear",1);
     const auto ro=render(*off,80),rl=render(*lin,80),rz=render(*zero,80),ri=render(*idle,80);
     const int d=lin->getLatencySamples();
     check(off->getLatencySamples()==0&&idle->getLatencySamples()==0,"Linear phase added latency while MONO BASS is off");
     check(d==tt::bassmono::radiusFor(48000)+tt::linear::block-1&&zero->getLatencySamples()==d,"Linear-phase mono bass latency not reported");
     check(lin->bassMonoUsesLinearPhase()&&!idle->bassMonoUsesLinearPhase(),"Linear-phase engine not latched as requested");
     check(ri==ro,"LINEAR switch alone changed the output");
     bool delayed=true;for(size_t i=0;i<ro.size();++i){const size_t j=i+2*size_t(d);if(j<rz.size()&&rz[j]!=ro[i]){delayed=false;break;}}
     for(size_t i=0;i<2*size_t(d)&&i<rz.size();++i)if(rz[i]!=0.f)delayed=false;
     check(delayed,"Linear-phase MIX 0 % is not the input delayed by the reported latency");
     const auto bo=sideBands(ro,4800),bl=sideBands(rl,4800+size_t(d));
     check(10*std::log10(bl.first/bo.first)<-25,"Linear-phase mono bass did not make the low end mono");
     check(std::abs(10*std::log10(bl.second/bo.second))<.1,"Linear-phase mono bass changed the stereo above the frequency");
     // Phase above the band: the 2 kHz side component keeps level and phase (only delayed).
     {const auto c0=sideDft(ro,2000,4800),c1=sideDft(rl,2000,4800+size_t(d));
      check(std::abs(c1/c0-std::complex<double>(1,0))<.01,"Linear-phase mono bass shifted the phase above the frequency");}
     // The mono sum is the OFF render delayed, sample for sample.
     for(size_t k=0;k+size_t(d)<ro.size()/2;++k){const size_t a=2*k,b=2*(k+size_t(d));
         check(std::abs((double(ro[a])+ro[a+1])-(double(rl[b])+rl[b+1]))<1e-5,"Linear-phase mono bass changed the mono sum");}}
    // v0.5.1 LIVE SWITCHING: LINEAR PHASE and ENABLE change the engine while audio runs
    // (10 ms crossfade, no restart); the reported latency follows; after the fade the
    // output is exactly what a processor started in that mode produces.
    {auto sw=fixture(),lin=fixture(),iir=fixture();
     for(auto* p:{sw.get(),lin.get(),iir.get()}){set(*p,"bassMono",1);set(*p,"bassMonoHz",150);set(*p,"bassMonoMix",100);}
     set(*sw,"bassMonoLinear",1);set(*lin,"bassMonoLinear",1);set(*iir,"bassMonoLinear",0);
     for(auto* p:{sw.get(),lin.get(),iir.get()})p->prepareToPlay(48000,256);
     const int d=lin->getLatencySamples();check(d>0&&sw->getLatencySamples()==d&&iir->getLatencySamples()==0,"Live switch fixture latency");
     juce::MidiBuffer midi;std::array<juce::AudioBuffer<float>,3> buffers{juce::AudioBuffer<float>(2,256),juce::AudioBuffer<float>(2,256),juce::AudioBuffer<float>(2,256)};
     std::array<std::vector<float>,3> out;
     auto block=[&](bool noteOn){std::array<PunchTailProcessor*,3> ps{sw.get(),lin.get(),iir.get()};
         for(size_t k=0;k<3;++k){midi.clear();if(noteOn)midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);ps[k]->processBlock(buffers[k],midi);
             for(int i=0;i<256;++i)for(int ch=0;ch<2;++ch)out[k].push_back(buffers[k].getSample(ch,i));}};
     block(true);for(int b=1;b<20;++b)block(false);
     check(out[0]==out[1],"Live-switch fixture differs before the switch");
     set(*sw,"bassMonoLinear",0);const size_t switchAt=out[0].size();
     for(int b=0;b<20;++b)block(false);
     check(!sw->bassMonoUsesLinearPhase(),"LINEAR PHASE off did not switch the running engine");
     sw->refreshReportedLatency();check(sw->getLatencySamples()==0,"Latency not reported as 0 after LINEAR PHASE off");
     const size_t settled=switchAt+2*size_t(256+480);
     for(size_t i=settled;i<out[0].size();++i)check(out[0][i]==out[2][i],"After switching off, output is not the zero-latency mono bass");
     double maxSwitch=0,maxRef=0;for(size_t i=switchAt+2;i<settled;i+=2){maxSwitch=(std::max)(maxSwitch,double(std::abs(out[0][i]-out[0][i-2])));maxRef=(std::max)(maxRef,double(std::abs(out[2][i]-out[2][i-2])));}
     check(maxSwitch<=maxRef*1.5+1e-6,"Switching LINEAR PHASE off clicks");
     // And back on.
     set(*sw,"bassMonoLinear",1);const size_t backAt=out[0].size();
     for(int b=0;b<20;++b)block(false);
     check(sw->bassMonoUsesLinearPhase(),"LINEAR PHASE on did not switch the running engine");
     sw->refreshReportedLatency();check(sw->getLatencySamples()==d,"Latency not reported after LINEAR PHASE on");
     for(size_t i=backAt+2*size_t(256+480);i<out[0].size();++i)check(out[0][i]==out[1][i],"After switching on, output is not the linear-phase mono bass");
     // ENABLE off also drops the latency live (MONO BASS off = no delay).
     set(*sw,"bassMono",0);for(int b=0;b<4;++b)block(false);sw->refreshReportedLatency();
     check(!sw->bassMonoUsesLinearPhase()&&sw->getLatencySamples()==0,"MONO BASS off did not drop the latency");
     // Reversing in the middle of a fade continues from where it was (no jump).
     set(*sw,"bassMono",1);{juce::AudioBuffer<float> one(2,240);midi.clear();sw->processBlock(one,midi);}
     set(*sw,"bassMono",0);{juce::AudioBuffer<float> one(2,100);midi.clear();sw->processBlock(one,midi);}
     check(sw->monoSwitchRemaining>0&&sw->monoSwitchRemaining<sw->monoSwitchLength,"Reversal restarted the fade instead of continuing it");}
    // State: values restore; projects saved before v0.5 load with mono bass OFF at the defaults.
    {auto p=fixture();set(*p,"bassMono",1);set(*p,"bassMonoHz",210);set(*p,"bassMonoMix",35);juce::MemoryBlock saved;p->getStateInformation(saved);
     auto q=fixture();q->setStateInformation(saved.getData(),int(saved.getSize()));
     check(value(*q,"bassMono")==1&&value(*q,"bassMonoHz")==210&&std::abs(value(*q,"bassMonoMix")-35)<.01f,"Mono bass state did not restore");
     auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
     for(int i=legacy.getNumChildren()-1;i>=0;--i)if(legacy.getChild(i).getProperty("id").toString().startsWith("bassMono"))legacy.removeChild(i,nullptr);
     juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
     q->setStateInformation(old.getData(),int(old.getSize()));
     check(value(*q,"bassMono")==0&&value(*q,"bassMonoHz")==120&&value(*q,"bassMonoMix")==100&&value(*q,"bassMonoLinear")==0,"Pre-v0.5 project must load with mono bass OFF");
     // A v0.5.0 project (mono bass ON, no bassMonoLinear) keeps its zero-latency mono bass.
     auto v050=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
     for(int i=v050.getNumChildren()-1;i>=0;--i)if(v050.getChild(i).getProperty("id").toString()=="bassMonoLinear")v050.removeChild(i,nullptr);
     juce::MemoryBlock mid;{juce::MemoryOutputStream stream(mid,false);v050.writeToStream(stream);}
     q->setStateInformation(mid.getData(),int(mid.getSize()));
     check(value(*q,"bassMono")==1&&value(*q,"bassMonoLinear")==0,"v0.5.0 project must keep zero-latency mono bass");
     q->prepareToPlay(48000,256);check(q->getLatencySamples()==0&&!q->bassMonoUsesLinearPhase(),"v0.5.0 project gained latency");}
    // Final Kick waveform / Export WAV audio mirrors it (from the first sample: no ramp offline).
    for(int mode=0;mode<3;++mode) {const bool on=mode>0;
        auto p=fixture();PreviewRequest request;request.bassMonoLinear=mode==2;request.samples[0]=std::make_shared<pt::Sample>(p->assets[0]->sample);request.rate=48000;
        request.bassMono=on;request.bassMonoHz=150;request.bassMonoMix=1.f;
        ProcessedPreview preview;preview.submit(request);std::optional<PreviewResult> result;auto deadline=juce::Time::getMillisecondCounter()+10000;
        while(!result&&juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);result=preview.take();}
        check(result&&result->audio&&!result->audio->left.empty(),"Preview timeout");
        std::vector<float> x;for(size_t i=0;i<result->audio->left.size();++i){x.push_back(result->audio->left[i]);x.push_back(result->audio->right[i]);}
        const auto bands=sideBands(x,2400);
        static double offLow=0,offHigh=0;static std::vector<float> offAudio;if(!on){offLow=bands.first;offHigh=bands.second;offAudio=x;}
        else check(offLow>0&&10*std::log10(bands.first/offLow)<-20,"Final waveform / Export WAV ignores mono bass");
        if(mode==2){   // zero-phase offline: no delay, mid identical, 2 kHz side level unchanged
            check(x.size()==offAudio.size(),"Linear-phase preview changed the length");
            double worst=0;for(size_t i=0;i+1<x.size();i+=2)worst=(std::max)(worst,std::abs((double(x[i])+x[i+1])-(double(offAudio[i])+offAudio[i+1])));
            check(worst<1e-5,"Linear-phase preview changed the mono sum");
            check(std::abs(10*std::log10(bands.second/offHigh))<.1,"Linear-phase preview changed the stereo above the frequency");}
    }
}
};
int main()try{juce::ScopedJuceInitialiser_GUI init;dsp();linearDsp();TailtoneBassMonoTest::processor();
    std::cout<<"PASS mono bass: linear phase (symmetric FIR = LR4 magnitude, exact delayed bypass/mid, latency reported, offline = playback, smooth kernel change); OFF/MIX 0 bit-identical, side removed below 90-300 Hz (LR4), mid untouched, MIX blend, smooth, stable, appended params, state/legacy, preview\n";return 0;}
catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
