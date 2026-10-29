// v0.5.5 PUNCH DECAY NOTE (Source/PunchDecayNote.h): mid tone of the decay sweep,
// start/end, region selection, refusal on noise/silence/short decays, correction.
#include "../Source/PunchDecayNote.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static constexpr double kPi=3.14159265358979323846;
// A punch: 20 ms click/attack noise, then a decay sweeping exponentially from
// `fromHz` to `toHz` over `decayMs` with a slow amplitude decay.
static pt::Sample punch(double rate,double attackMs,double decayMs,double fromHz,double toHz,double noise=0.0){
    pt::Sample s;s.rate=rate;const size_t n=size_t((attackMs+decayMs)*.001*rate);s.left.resize(n);s.right.resize(n);
    std::mt19937 rng(1);std::uniform_real_distribution<double> u(-1,1);double phase=0;
    for(size_t i=0;i<n;++i){const double t=i/rate*1000.0;double v;
        if(t<attackMs)v=.6*u(rng)*std::exp(-t/5.0);
        else{const double x=(t-attackMs)/decayMs;const double hz=fromHz*std::pow(toHz/fromHz,x);phase+=2*kPi*hz/rate;v=.5*std::sin(phase)*std::exp(-2.0*x)+noise*u(rng);}
        s.left[i]=float(v);s.right[i]=float(v);}
    return s;
}
static double midi(double hz){return 69+12*std::log2(hz/440.0);}
int main()try{
    for(double rate:{44100.0,48000.0,96000.0}){
        // Steady decay at 110 Hz: mid = start = end = A2 within 10 cents.
        {const auto s=punch(rate,20,120,110,110);const auto r=pt::decaynote::analyse(s,.020,0);
         check(r.valid,"Steady decay not detected");
         check(std::abs(r.midi()-midi(110))<.10&&std::abs(r.startMidi()-midi(110))<.10&&std::abs(r.endMidi()-midi(110))<.10,"Steady decay note wrong: "+std::to_string(r.midHz));}
        // Sweep 180 -> 60 Hz: mid tone is the median (near the geometric middle ~104 Hz),
        // start near 180 and end near 60, in order.
        {const auto s=punch(rate,20,150,180,60);const auto r=pt::decaynote::analyse(s,.020,0);
         check(r.valid,"Sweep not detected");
         check(r.startHz>r.midHz&&r.midHz>r.endHz,"Sweep order wrong");
         check(r.startHz>140&&r.startHz<=185&&r.endHz>=55&&r.endHz<80,"Sweep ends wrong: "+std::to_string(r.startHz)+" "+std::to_string(r.endHz));
         check(r.midHz>80&&r.midHz<130,"Sweep mid tone outside the middle: "+std::to_string(r.midHz));
         // correction puts the mid tone on a target note
         check(std::abs(pt::decaynote::correction(r,36)-(36-r.midi()))<1e-12,"Correction arithmetic");}
    }
    // The attack is ignored: a loud 2 kHz attack before the split does not change the result.
    {auto s=punch(48000,20,120,100,100);for(size_t i=0;i<960;++i){s.left[i]=s.right[i]=float(.9*std::sin(2*kPi*2000*i/48000.));}
     const auto r=pt::decaynote::analyse(s,.020,0);check(r.valid&&std::abs(r.midi()-midi(100))<.10,"Attack leaked into the decay note");}
    // The Punch End trim limits the region: cutting the end of a sweep raises the end pitch.
    {const auto s=punch(48000,20,150,180,60);const auto full=pt::decaynote::analyse(s,.020,0),cut=pt::decaynote::analyse(s,.020,.095);
     check(full.valid&&cut.valid&&cut.endHz>full.endHz+20,"Punch End trim not respected");}
    // Refusals: noise, silence, too short, split past the end.
    {pt::Sample n;n.rate=48000;n.left.resize(9600);n.right.resize(9600);std::mt19937 rng(3);std::uniform_real_distribution<float> u(-1,1);
     for(size_t i=0;i<n.left.size();++i)n.left[i]=n.right[i]=u(rng);
     check(!pt::decaynote::analyse(n,.020,0).valid,"Noise must not get a note");
     pt::Sample z;z.rate=48000;z.left.assign(9600,0.f);z.right.assign(9600,0.f);check(!pt::decaynote::analyse(z,.020,0).valid,"Silence must not get a note");
     check(!pt::decaynote::analyse(punch(48000,20,12,100,100),.020,0).valid,"A 12 ms decay must not get a note");
     check(!pt::decaynote::analyse(punch(48000,20,120,100,100),.5,0).valid,"Split past the end must not get a note");
     check(!pt::decaynote::analyse(pt::Sample{},0,0).valid,"Empty sample");}
    // Mild noise on the decay is fine.
    {const auto r=pt::decaynote::analyse(punch(48000,20,120,90,90,.02),.020,0);check(r.valid&&std::abs(r.midi()-midi(90))<.15,"Noisy decay note wrong");}
    std::cout<<"PASS decay note: steady, sweep mid/start/end, attack ignored, Punch End trim, refusals, correction\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
