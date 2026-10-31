// v0.5.36 Detect End / Detect Start: pt::detectTailStart finds where the stable
// pitched TAIL of a sample begins, so the Punch keeps the attack AND its decay
// (the v0.4.66 AUTO-SPLIT boundary), and pt::suggestPunchEnd ends the Punch
// there. New in v0.5.36: a sample that is a tail from its start (no punch before
// the steady cycles) reports the tail at 0 ms instead of cutting its first swings.
#include "../Source/SplitDetection.h"
#include "WaveFixture.h"
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
static void ck(bool b,const char* m){if(!b)throw std::runtime_error(m);}
// 808-style kick: 5 ms noise click, 200 -> 50 Hz exponential pitch drop (tau),
// slow amplitude decay.
static pt::Sample sweepKick(double rate,double tau,double tailHz=50) {
    pt::Sample s;s.rate=rate;const int n=int(rate*.8);s.left.resize(size_t(n));s.right.resize(size_t(n));
    std::mt19937 g(7);std::normal_distribution<double> noise;double phase=0;
    for(int i=0;i<n;++i) {
        const double t=i/rate,f=tailHz+150*std::exp(-t/tau);phase+=2*pt::pi*f/rate;
        double x=.8*std::sin(phase)*std::exp(-t/.5);if(t<.005)x+=.4*noise(g);
        s.left[size_t(i)]=float(x);s.right[size_t(i)]=float(x*.98);
    }
    return s;
}
// Time at which the sweep is within `st` semitones of the tail pitch.
static double settleMs(double tau,double st,double tailHz=50){return 1000*tau*std::log(150/(tailHz*(std::pow(2.0,st/12)-1)));}
// A tail on its own: the pitch settles from `startSt` semitones above to the
// tail pitch, the first swing is softer and rounder than the rest.
static pt::Sample tailOnly(double rate,double startSt,double tailHz=48) {
    pt::Sample s;s.rate=rate;const int n=int(rate*.5);double phase=0;
    for(int i=0;i<n;++i) {
        const double t=i/rate,f=tailHz*std::pow(2.0,startSt*std::exp(-t/.02)/12);phase+=2*pt::pi*f/rate;
        const double drive=1+5*(1-std::exp(-t/.03));
        const float x=float(.7*std::tanh(drive*std::sin(phase))/std::tanh(drive)*std::exp(-t/.8));
        s.left.push_back(x);s.right.push_back(x);
    }
    return s;
}
int main(int argc,char** argv) try {
    for(double rate:{44100.0,48000.0,96000.0})for(double tau:{.010,.015,.025}) {
        const auto s=sweepKick(rate,tau);
        const auto t=pt::detectTailStart(s);
        ck(t.valid && t.punch,"no punch / tail start in a synthetic 808 kick");
        ck(std::abs(t.hz-50)<1.5,"wrong tail reference pitch");
        // Not inside the pitch drop (more than 3 semitones above the tail) and
        // no later than two tail cycles after it has settled to 1 semitone.
        ck(t.ms>=settleMs(tau,3.0),"cut inside the punch's pitch drop");
        ck(t.ms<=settleMs(tau,1.0)+40.0,"cut far after the tail has settled");
        ck(t.ms>pt::detectPunchEnd(s).endMs,"cut before the attack end");
        // Starts the tail at a rising zero crossing (quiet frame).
        const size_t at=size_t(std::llround(t.ms*rate/1000));
        ck(std::abs(s.left[at])<.08,"cut is not at a quiet frame");
        // Detect End ends the Punch exactly where the tail begins.
        const auto end=pt::suggestPunchEnd(s,t);
        ck(end.valid && end.endMs==t.ms,"Detect End must end the punch where the tail begins");
    }
    {   // A tail without a punch begins at 0 ms, even when its first swings repeat
        // less well or its pitch is still settling (by less than 3 semitones).
        for(double st:{0.0,1.0,2.5})for(double rate:{44100.0,48000.0}) {
            const auto t=pt::detectTailStart(tailOnly(rate,st));
            ck(t.valid && !t.punch && t.ms==0,"a tail without a punch must start at 0 ms");
            ck(std::abs(t.hz-48)<1.5,"tail without a punch: wrong pitch");
        }
        // A steady tone is a tail from its first sample, too (no punch).
        pt::Sample s;s.rate=48000;for(int i=0;i<48000;++i){const float v=.5f*float(std::sin(2*pt::pi*60*i/48000.));s.left.push_back(v);s.right.push_back(v);}
        {const auto t=pt::detectTailStart(s);ck(t.valid && !t.punch && t.ms==0,"a steady tone is a tail without a punch");
         ck(!pt::suggestPunchEnd(s,t).valid,"a steady tone has no punch end");}
        // A long sound before the first low cycle is a punch, even without a
        // pitch drop: 60 ms of high noise, then a 50 Hz tail.
        pt::Sample h;h.rate=48000;std::mt19937 g(5);std::uniform_real_distribution<float> u(-.6f,.6f);float hp=0;
        for(int i=0;i<24000;++i){const double t=i/48000.;float v;
            if(t<.06){const float x=u(g);v=x-hp;hp=x;}else v=.7f*float(std::sin(2*pt::pi*50*(t-.06)));
            h.left.push_back(v);h.right.push_back(v);}
        {const auto t=pt::detectTailStart(h);ck(t.valid && t.punch && t.ms>55 && t.ms<85,"a noise punch before the tail was not found");}
        // Noise has no pitched tail.
        std::uniform_real_distribution<float> w(-.5f,.5f);
        for(auto& v:s.left)v=w(g);s.right=s.left;
        ck(!pt::detectTailStart(s).valid,"noise has no tail");
        // Silence, too short, mismatched channels.
        pt::Sample quiet;quiet.rate=48000;quiet.left.assign(48000,0.f);quiet.right=quiet.left;ck(!pt::detectTailStart(quiet).valid,"silence has no tail");
        pt::Sample tiny=sweepKick(48000,.015);tiny.left.resize(1000);tiny.right.resize(1000);ck(!pt::detectTailStart(tiny).valid,"too short");
        pt::Sample odd=sweepKick(48000,.015);odd.right.pop_back();ck(!pt::detectTailStart(odd).valid,"mismatched channels");
        // A punch on its own (40 ms, fading out while its pitch still falls):
        // no tail; Detect End keeps the v0.4 impact detector.
        pt::Sample punch=sweepKick(48000,.010);punch.left.resize(1920);punch.right.resize(1920);
        for(size_t i=960;i<1920;++i){const float f=float(1920-i)/960.f;punch.left[i]*=f;punch.right[i]*=f;}
        punch.left.resize(14400,0.f);punch.right.resize(14400,0.f);
        {const auto t=pt::detectTailStart(punch);const auto end=pt::suggestPunchEnd(punch,t),old=pt::detectPunchEnd(punch);
         ck(!t.valid && end.valid==old.valid && end.endMs==old.endMs,"a punch without a tail must keep the impact detector");}
    }
    // Long files stay quick (at most ~400 pitch frames).
    {pt::Sample s=sweepKick(48000,.015);const auto first=pt::detectTailStart(s);
     s.left.resize(size_t(48000*60));s.right.resize(s.left.size());
     for(size_t i=38400;i<s.left.size();++i){const float v=.3f*float(std::sin(2*pt::pi*50*double(i)/48000));s.left[i]=v;s.right[i]=v*.98f;}
     const auto start=std::chrono::steady_clock::now();const auto t=pt::detectTailStart(s);
     const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
     ck(t.valid && t.punch && std::abs(t.ms-first.ms)<.5,"a long file moved the detected tail start");
     std::cout<<"60 s file: "<<seconds*1000<<" ms\n";ck(seconds<3,"tail start detection is too slow on a long file");}
    // The real fixtures. Complete kicks: the cut at the first clean tail cycle,
    // inspected by eye; the punch keeps attack + decay (same cuts as v0.4.66).
    // tail.wav is a tail on its own; punch.wav a punch on its own.
    ck(argc>5,"usage: TailStartTests virus-kick-3.wav kick-nc-lol-2.0.wav mpl-kick-merged.wav tail.wav punch.wav");
    struct F{const char* file;double lo,hi,hz;} kicks[]={{argv[1],85,100,43.6},{argv[2],125,145,51.3},{argv[3],195,215,52.0}};
    for(const auto& f:kicks) {
        const auto s=loadFloatWave(f.file);const auto t=pt::detectTailStart(s);
        ck(t.valid && t.punch && t.ms>f.lo && t.ms<f.hi,"real kick: cut is not at the start of the stable tail");
        ck(std::abs(t.hz-f.hz)<1.5,"real kick: wrong tail pitch");
        ck(t.ms>pt::detectPunchEnd(s).endMs+40,"real kick: the decay was not kept in the punch");
        ck(pt::suggestPunchEnd(s,t).endMs==t.ms,"real kick: Detect End is not the tail start");
        std::cout<<f.file<<": tail starts at "<<t.ms<<" ms ("<<t.hz<<" Hz); attack end "<<pt::detectPunchEnd(s).endMs<<" ms\n";
    }
    {const auto s=loadFloatWave(argv[4]);const auto t=pt::detectTailStart(s);
     ck(t.valid && !t.punch && t.ms==0 && std::abs(t.hz-48.8)<1.5,"tail.wav is a tail from its start (v0.4.66 cut 54 ms of it)");}
    {const auto s=loadFloatWave(argv[5]);const auto t=pt::detectTailStart(s);
     ck(!t.valid,"punch.wav has no tail");
     ck(pt::suggestPunchEnd(s,t).valid==pt::detectPunchEnd(s).valid,"punch.wav: Detect End must keep the impact detector");}
    std::cout<<"TailStart OK\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
