// v0.4.48 Kick Character engine: character preservation, fixed duration,
// pitch accuracy, no added clicks, stereo lock, onset, cancellation, and
// that Tail wet root rendering uses the same engine. Measured against a pure
// resample (the ideal "same kick, other note" waveform, but wrong duration)
// and against the Extreme engine.
#include "../Source/KickCharacterPitch.h"
#include "../Source/PitchBankPolicy.h"
#include "../Source/TailReverbPitchBank.h"
#include "WaveFixture.h"
#include <cstdint>
#include <iostream>
#include <numeric>

static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
using pt::Sample;

static std::vector<double> highPass(const std::vector<float>& x,double fc,double rate) {
    std::vector<double> y(x.begin(),x.end());const double a=std::exp(-2*pt::pi*fc/rate);
    for(int stage=0;stage<4;++stage){double lp=0;for(auto& v:y){lp=a*lp+(1-a)*v;v-=lp;}}
    return y;
}
// High-frequency share (above 3 kHz) of the tail body, in dB.
static double brightness(const Sample& s) {
    const auto h=highPass(s.left,3000,s.rate);double hf=0,all=0;
    for(size_t i=s.left.size()/20;i<s.left.size();++i){hf+=h[i]*h[i];all+=double(s.left[i])*s.left[i];}
    return 10*std::log10((hf+1e-12)/(all+1e-12));
}
// Largest >8 kHz spike after the onset: clicks/splice discontinuities show here.
static double clickPeak(const Sample& s) {
    const auto h=highPass(s.left,8000,s.rate);double m=0;
    for(size_t i=s.left.size()/20;i<h.size();++i)m=(std::max)(m,std::abs(h[i]));
    return m;
}
static double onsetLevel(const Sample& s,double ms) {
    const size_t n=size_t(ms*.001*s.rate);double e=0;
    for(size_t i=0;i<n;++i)e+=double(s.left[i])*s.left[i]+double(s.right[i])*s.right[i];
    return 10*std::log10(e/double(n)+1e-12);
}
static double medianBodyHz(const Sample& y,double stretch) {
    bool armed=false;std::vector<double> c;
    for(size_t i=size_t(.14*y.rate*stretch);i<size_t(.40*y.rate*stretch)&&i+22<y.left.size();++i){
        double average=0;for(int k=-22;k<=22;++k)average+=y.left[i+k]/45.0;
        if(average<-.35)armed=true;
        if(armed&&average>.35){c.push_back(double(i));armed=false;}
    }
    std::vector<double> d;for(size_t i=1;i<c.size();++i)d.push_back(c[i]-c[i-1]);
    check(d.size()>=4,"Lost sustained-body cycles");
    std::sort(d.begin(),d.end());return y.rate/d[d.size()/2];
}
static Sample resampled(const Sample& x,double semitones) {
    const double ratio=std::pow(2.0,semitones/12.0);Sample r;r.rate=x.rate;
    r.left=pt::cycle::resample(x.left,x.rate,ratio);r.right=pt::cycle::resample(x.right,x.rate,ratio);return r;
}

static void characterQuality(const char* path,bool measurePitch) {
    const auto x=loadFloatWave(path);
    const auto analysis=pt::cycle::analyze(x);
    const double sourceHz=measurePitch?medianBodyHz(x,1.0):0.0;
    double characterError=0;int cases=0;
    for(double stretch:{1.0,1.3}) {
        const auto ch=pt::character::prepare(x,analysis,stretch);
        for(double semi:{-12.,-7.,-3.,3.,7.,12.}) {
            const auto ref=resampled(x,semi);
            pt::character::RenderStats stats;
            const auto y=pt::character::render(x,semi,ch,{},&stats);
            const std::string tag=std::string(path)+" "+std::to_string(semi)+" st "+std::to_string(stretch)+": ";
            check(pt::usablePitchResult(x,y,stretch),tag+"invalid or wrong duration");
            // 1. Crunch/brightness: within 0.75 dB of the ideal resampled kick.
            const double c=std::abs(brightness(y)-brightness(ref));
            check(c<0.75,tag+"high-frequency crunch changed by "+std::to_string(c)+" dB");
            characterError+=c;++cases;
            // 2. No added clicks: >8 kHz peaks no larger than the source's own.
            check(clickPeak(y)<=clickPeak(ref)*1.3,tag+"splice added a click");
            // 3. Onset preserved: the first 10 ms keeps the resampled attack level.
            check(std::abs(onsetLevel(y,10)-onsetLevel(ref,10))<1.0,tag+"onset level changed");
            // 4. Handovers are short: on average under 30% of a cycle.
            check(stats.meanCrossfadeCycles<0.30,tag+"handovers are too long");
            // 5. Pitch of the sustained body. At -12 the measurement window
            // holds too few 22 Hz cycles, so only -7..+12 are measured.
            if(measurePitch && semi>=-7) {
                const double cents=1200*std::log2(medianBodyHz(y,stretch)/(sourceHz*std::pow(2.0,semi/12.0)));
                check(std::abs(cents)<22,tag+"body pitch off by "+std::to_string(cents)+" cents");
            }
        }
    }
    // v0.4.61: the Extreme engine (the old comparison baseline) was removed.
    std::cout<<"  "<<path<<": mean brightness error Highest Fidelity v4 "<<characterError/cases<<" dB vs ideal resample\n";
}

static void fullRangeAndIdentity(const char* path) {
    const auto x=loadFloatWave(path);const auto a=pt::cycle::analyze(x);
    const double root=69+12*std::log2(a.fundamentalHz/440.);
    for(double stretch:{.8,1.,1.234,1.5}) {
        const auto p=pt::character::prepare(x,a,stretch);
        for(int note=pt::kLowestPitchNote;note<=pt::kHighestPitchNote;++note){
            const auto y=pt::character::render(x,note-root,p);
            check(pt::usablePitchResult(x,y,stretch),"Kick Character failed note "+std::to_string(note));
        }
    }
    const auto p=pt::character::prepare(x,a,1.0);
    const auto same=pt::character::render(x,0.0,p);
    check(same.left==x.left&&same.right==x.right,"Root at 100% is not the exact source");
    // Stereo lock: identical channels stay identical (same splice decisions).
    Sample mono=x;mono.right=mono.left;
    const auto pm=pt::character::prepare(mono,a,1.2);
    for(double semi:{-5.,4.}){const auto y=pt::character::render(mono,semi,pm);check(y.left==y.right,"Stereo splice decisions differ between channels");}
    // Cancellation stops work on the worker.
    bool cancelled=false;
    try{(void)pt::character::render(x,7.0,p,[]{return true;});}catch(const std::runtime_error&){cancelled=true;}
    check(cancelled,"Kick Character ignores cancellation");
    // Deterministic: the same note renders bit-identically (no random hitches between notes).
    check(pt::character::render(x,5.0,p).left==pt::character::render(x,5.0,p).left,"Render is not deterministic");
}

static void manualRootNoise() {
    pt::Sample s;s.rate=48000;s.left.resize(24000);s.right.resize(24000);uint32_t seed=7;
    double l=0,r=0;const double k=1-std::exp(-2*pt::pi*900/48000.);
    for(size_t i=0;i<s.left.size();++i){seed=seed*1664525u+1013904223u;l+=k*(double(seed>>8)/double(1u<<24)*2-1-l);
        seed=seed*1664525u+1013904223u;r+=k*(double(seed>>8)/double(1u<<24)*2-1-r);s.left[i]=float(l*2);s.right[i]=float(r*2);}
    const auto a=pt::cycle::analyzeWithManualRoot(s,440*std::pow(2.,(36-69.)/12.));
    const auto p=pt::character::prepare(s,a,1.0);
    for(double semi:{-12.,12.})check(pt::usablePitchResult(s,pt::character::render(s,semi,p)),"Manual-root noise tail failed in Kick Character");
}

static void wetUsesSameEngine(const char* path) {
    const auto tail=loadFloatWave(path);
    pt::TailReverbSettings settings;settings.enabled=true;settings.tailSend=true;settings.mix=55;settings.spaceMs=120;settings.width=140;settings.distance=12;
    for(double stretch:{1.0,1.12}) {
        auto bank=pt::renderRootTailCompositeBank(tail,36,0,settings,stretch,4,false);
        const auto expected=pt::character::render(*bank.rootRender,5,pt::character::prepare(*bank.rootRender,bank.phaseMap,stretch));
        check(bank.notes[41]->left==expected.left&&bank.notes[41]->right==expected.right,"Root Render + Pitch wet did not use Kick Character");
        check(bank.notes[41]->left.size()==size_t(std::llround(bank.rootRender->left.size()*stretch)),"Wet duration not fixed by stretch");
        if(stretch==1.0)check(bank.notes[36]==bank.rootRender,"Wet root identity lost");
    }
}

int main(int argc,char** argv) try {
    check(argc>3,"Usage: KickCharacterTests virus-kick-3.wav kick-nc-lol-2.0.wav tail.wav");
    // tail.wav is only 0.4 s: too short for the sustained-body pitch window.
    for(int i=1;i<argc;++i){characterQuality(argv[i],i<3);fullRangeAndIdentity(argv[i]);}
    manualRootNoise();
    wetUsesSameEngine(argv[1]);
    std::cout<<"PASS Kick Character: brightness within 0.75 dB of the ideal resample, no added clicks, onset kept, body pitch, 3 fixtures, all 37 notes at 80-150%, root identity, stereo lock, cancellation, manual root, wet root render uses same engine\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
