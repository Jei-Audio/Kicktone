#include "../Source/PunchPitch.h"
#include "../Source/PitchBankPolicy.h"
#include "WaveFixture.h"
#include "../Source/KickOverview.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <vector>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static double magnitude(const pt::Sample& s,double hz) {
    double a=0,b=0;
    for(size_t i=size_t(.03*s.rate);i<size_t(.18*s.rate);++i) {
        a+=s.left[i]*std::cos(2*pt::pi*hz*i/s.rate);
        b+=s.left[i]*std::sin(2*pt::pi*hz*i/s.rate);
    }
    return std::sqrt(a*a+b*b);
}
int main(int argc,char** argv)try {
    for(double rate:{44100.,48000.,96000.}) {
        pt::Sample x;x.rate=rate;x.left.resize(size_t(rate*.22));x.right.resize(x.left.size());
        for(size_t i=0;i<x.left.size();++i){x.left[i]=float(.4*std::sin(2*pt::pi*220*i/rate));x.right[i]=-.5f*x.left[i];}
        auto root=pt::renderPunchPitch(x,0);
        check(root.left==x.left && root.right==x.right,"Punch zero is not exact identity");
        for(double semi:{-12.,-7.,3.,7.,12.}) {
            const auto y=pt::renderPunchPitch(x,semi);
            check(y.left.size()==x.left.size() && y.right.size()==x.right.size() && y.rate==x.rate,"Punch duration changed");
            const double target=220*std::pow(2.,semi/12.);
            double peak=0,bestHz=0;
            for(int cents=-80;cents<=80;cents+=2) {
                const double hz=target*std::pow(2.,cents/1200.);
                const double amp=magnitude(y,hz);if(amp>peak){peak=amp;bestHz=hz;}
            }
            check(std::abs(1200*std::log2(bestHz/target))<=16,"Punch pitch inaccurate");
            check(peak>100,"Punch lost tone energy");
            for(size_t i=0;i<y.left.size();++i)check(std::isfinite(y.left[i]) && std::abs(y.right[i]+.5*y.left[i])<1.e-6,"Punch stereo phase changed");
            pt::Settings s;s.crossfadeMs=8;
            pt::Voice dry,shifted;dry.start(27,1,1,&x,&x,s,&x);shifted.start(27,1,1,&y,&x,s,&x);
            check(dry.punchEnd==shifted.punchEnd && dry.tailStart==shifted.tailStart && dry.reverbStart==shifted.reverbStart,"Punch pitch moved layer timing");
        }
    }
    // Resample mode changes pitch and time together, including existing trim.
    for(double rate:{44100.,48000.,96000.}) {
        pt::Sample x;x.rate=rate;x.left.resize(size_t(rate*.4));x.right.resize(x.left.size());
        for(size_t i=0;i<x.left.size();++i){x.left[i]=float(.4*std::sin(2*pt::pi*220*i/rate));x.right[i]=-.5f*x.left[i];}
        for(double semi:{-12.,-7.,0.,7.,12.}) {
            const auto y=pt::renderPunchPitch(x,semi,pt::PunchPitchMode::Resample);
            const double ratio=std::pow(2.,semi/12.);
            check(y.left.size()==size_t(std::llround(x.left.size()/ratio)),"Resampled duration incorrect");
            if(semi==0)check(y.left==x.left && y.right==x.right,"Resample root changed");
            const double hz=220*ratio;
            check(magnitude(y,hz)>100,"Resampled pitch lost fundamental");
            for(size_t i=0;i<y.left.size();++i)check(std::abs(y.right[i]+.5*y.left[i])<1.e-6,"Resample changed stereo phase");
            pt::Settings settings;settings.punchEndOffsetMs=100;settings.punchTimeScale=y.duration()/x.duration();
            settings.crossfadeMs=8;settings.joinMs=3;settings.reverbDelayMs=40;
            pt::Voice voice;voice.start(36,1,1,&y,&x,settings,&x);
            const double expectedEnd=.3*settings.punchTimeScale;
            check(std::abs(voice.punchEnd-expectedEnd)<1.e-9,"Source trim point moved in resample mode");
            check(std::abs(voice.tailStart-(expectedEnd-.005))<1.e-9,"Tail did not follow resampled punch");
            check(std::abs(voice.reverbStart-(expectedEnd+.040))<1.e-9,"Reverb delay did not follow resampled punch");
            auto preview=pt::buildKickOverview(&y,&x,settings,36,100,&x);
            check(std::abs(preview.punchEndMs-voice.punchEnd*1000)<1.e-9 &&
                  std::abs(preview.tailStartMs-voice.tailStart*1000)<1.e-9,"Resample preview timing differs from voice");
            settings.punchEndOffsetMs=400;voice.start(36,1,1,&y,&x,settings,&x);
            check(voice.punchEnd<1.e-9,"Full source trim left a resampled punch");
        }
        // Two isolated source events must move with speed, without repetitions.
        x.left.assign(size_t(rate*.2),0);x.right=x.left;
        x.left[size_t(rate*.04)]=x.right[size_t(rate*.04)]=.8f;
        x.left[size_t(rate*.14)]=x.right[size_t(rate*.14)]=.5f;
        for(double semi:{-12.,12.}) {
            const auto y=pt::renderPunchPitch(x,semi,pt::PunchPitchMode::Resample);
            const double ratio=std::pow(2.,semi/12.);
            const size_t a=size_t(rate*.04)/ratio,b=size_t(rate*.14)/ratio;
            double ea=0,eb=0,elsewhere=0;
            for(size_t i=0;i<y.left.size();++i) {
                const double energy=y.left[i]*y.left[i];
                if(std::abs(double(i)-a)<128)ea+=energy;
                else if(std::abs(double(i)-b)<128)eb+=energy;
                else elsewhere+=energy;
            }
            check(ea>.01 && eb>.01 && elsewhere<(ea+eb)*.001,"Resample repeated or misplaced source events");
        }
    }
    // New FL Studio E3..D#4 lane: every key, both modes, manual offset,
    // boundary isolation, pitch and resampled join timing.
    check(!pt::followsPunchMidi(39) && pt::followsPunchMidi(40) &&
          pt::followsPunchMidi(51) && !pt::followsPunchMidi(52),"Punch lane boundaries");
    pt::Sample tone;tone.rate=44100;tone.left.resize(17640);
    for(size_t i=0;i<tone.left.size();++i)tone.left[i]=float(.4*std::sin(2*pt::pi*220*i/tone.rate));
    tone.right=tone.left;
    for(auto mode:{pt::PunchPitchMode::StretchAligned,pt::PunchPitchMode::Resample,pt::PunchPitchMode::HighestFidelity})
      for(int note=39;note<=52;++note) {
        const double shift=pt::punchPitchForNote(note,27,3);
        check(std::abs(shift-((note>=40 && note<=51)?note-24:3))<1.e-9,"Punch offset mapping");
        const auto y=pt::renderPunchPitch(tone,shift,mode);
        const double scale=mode==pt::PunchPitchMode::Resample?1/std::pow(2.,shift/12.):1.;
        check(y.left.size()==size_t(std::llround(tone.left.size()*scale)),"Upper lane duration");
        double energy=0,expected=0,wrong=0;
        const double hz=220*std::pow(2.,shift/12.);
        for(size_t i=32;i+32<y.left.size();++i){energy+=y.left[i]*y.left[i];
          expected+=y.left[i]*std::sin(2*pt::pi*hz*i/y.rate);
          wrong+=y.left[i]*std::sin(2*pt::pi*220*i/y.rate);}
        check(energy>1 && std::abs(expected)>std::abs(wrong)*3,"Upper lane pitch did not move");
        pt::Settings settings;settings.punchTimeScale=y.duration()/tone.duration();settings.punchEndOffsetMs=100;
        pt::Voice voice;voice.start(note,1,1,&y,&tone,settings,&tone);
        check(std::abs(voice.punchEnd-.3*settings.punchTimeScale)<1.e-9,"Upper lane trim/join mismatch");
      }
    // v0.4.63 Punch HIGHEST FIDELITY: fixed duration, exact root, accurate
    // pitch, locked stereo, follows a swept punch, keeps crunch level.
    for(double rate:{44100.,48000.,96000.}) {
        pt::Sample x;x.rate=rate;x.left.resize(size_t(rate*.22));x.right.resize(x.left.size());
        for(size_t i=0;i<x.left.size();++i){x.left[i]=float(.4*std::sin(2*pt::pi*220*i/rate));x.right[i]=-.5f*x.left[i];}
        const auto root=pt::renderPunchPitch(x,0,pt::PunchPitchMode::HighestFidelity);
        check(root.left==x.left && root.right==x.right,"Highest Fidelity punch zero is not exact identity");
        for(double semi:{-12.,-7.,3.,7.,12.}) {
            const auto y=pt::renderPunchPitch(x,semi,pt::PunchPitchMode::HighestFidelity);
            check(y.left.size()==x.left.size() && y.right.size()==x.right.size() && y.rate==x.rate,"Highest Fidelity punch duration changed");
            const double target=220*std::pow(2.,semi/12.);
            double peak=0,bestHz=0;
            for(int cents=-80;cents<=80;cents+=2) {
                const double hz=target*std::pow(2.,cents/1200.);
                const double amp=magnitude(y,hz);if(amp>peak){peak=amp;bestHz=hz;}
            }
            check(std::abs(1200*std::log2(bestHz/target))<=10,"Highest Fidelity punch pitch inaccurate");
            check(peak>100,"Highest Fidelity punch lost tone energy");
            for(size_t i=0;i<y.left.size();++i)check(std::isfinite(y.left[i]) && std::abs(y.right[i]+.5*y.left[i])<1.e-5,"Highest Fidelity punch stereo phase changed");
            double ex=0,ey=0;for(size_t i=size_t(.02*rate);i<size_t(.2*rate);++i){ex+=x.left[i]*x.left[i];ey+=y.left[i]*y.left[i];}
            check(std::abs(10*std::log10(ey/ex))<1.0,"Highest Fidelity punch level changed");
        }
    }
    {   // Swept, distorted punch: 420 Hz -> 55 Hz in 120 ms, then body.
        const double rate=48000;pt::Sample x;x.rate=rate;x.left.resize(size_t(rate*.25));
        double phase=0;
        const auto hzAt=[](double t){return 55+365*std::exp(-t/.03);};
        for(size_t i=0;i<x.left.size();++i){const double t=i/rate;phase+=2*pt::pi*hzAt(t)/rate;
            x.left[i]=float(.8*std::tanh(3*std::sin(phase))*std::exp(-t/.4));}
        x.right=x.left;
        const auto track=pt::punchfidelity::trackPeriods(x);
        check(track.period.size()>20,"Punch period track empty");
        for(double t:{.01,.03,.06,.1,.18}) {
            const double truth=rate/hzAt(t),got=track.at(t*rate);
            check(std::abs(got/truth-1)<.06,"Punch period track does not follow the sweep");
        }
        for(double semi:{-12.,-5.,5.,12.}) {
            const auto y=pt::renderPunchPitch(x,semi,pt::PunchPitchMode::HighestFidelity);
            check(y.left.size()==x.left.size() && y.left==y.right,"Swept punch length/stereo");
            const auto ty=pt::punchfidelity::trackPeriods(y);
            const double ratio=std::pow(2.,semi/12.);
            // Sweep: every output cycle is one whole resampled source cycle, so
            // the output follows the source sweep transposed. Measured on
            // rising zero crossings against the analytic sweep, and compared
            // with Stretch Aligned (whose grains average across the sweep).
            const auto sweepError=[&](const pt::Sample& s){
                double last=-1,sum=0;int n=0;
                for(size_t i=1;i<s.left.size();++i)if(s.left[i-1]<0&&s.left[i]>=0){
                    const double fi=double(i-1)+(-s.left[i-1])/(s.left[i]-s.left[i-1]);
                    if(last>=0){const double mid=(fi+last)*.5/rate;
                        if(mid>.004&&mid<.12){sum+=std::abs(1200*std::log2((fi-last)/(rate/hzAt(mid)/ratio)));++n;}}
                    last=fi;}
                return n?sum/n:1e9;};
            pt::Sample sine=x;phase=0;
            for(size_t i=0;i<sine.left.size();++i){const double t=i/rate;phase+=2*pt::pi*hzAt(t)/rate;sine.left[i]=float(.8*std::sin(phase)*std::exp(-t/.4));}
            sine.right=sine.left;
            const double hfError=sweepError(pt::renderPunchPitch(sine,semi,pt::PunchPitchMode::HighestFidelity));
            const double alignedError=sweepError(pt::renderPunchPitch(sine,semi,pt::PunchPitchMode::StretchAligned));
            std::cout<<"  sweep "<<semi<<" st: Highest Fidelity "<<hfError<<" cents, Stretch Aligned "<<alignedError<<" cents\n";
            check(hfError<(semi<-6?150:60) && hfError<alignedError*.5,"Swept punch pitch does not follow the source sweep");
            for(double t:{.15,.18,.21}) {
                const double want=track.at(t*rate)/ratio,got=ty.at(t*rate);
                check(std::abs(1200*std::log2(got/want))<25,"Punch body pitch inaccurate");
            }
            double ex=0,ey=0;for(size_t i=0;i<x.left.size();++i){ex+=x.left[i]*x.left[i];ey+=y.left[i]*y.left[i];}
            check(std::abs(10*std::log10(ey/ex))<1.5,"Swept punch level changed");
            const auto aligned=pt::renderPunchPitch(x,semi,pt::PunchPitchMode::StretchAligned);
            check(aligned.left!=y.left,"Highest Fidelity rendered like Stretch Aligned");
            // Onset: the very first samples are the (resampled) source, not a splice.
            float p0=0,p1=0;for(size_t i=0;i<size_t(.001*rate);++i){p0=(std::max)(p0,std::abs(x.left[i]));p1=(std::max)(p1,std::abs(y.left[i]));}
            check(std::abs(20*std::log10(p1/p0))<1.5,"Highest Fidelity punch softened the onset");
        }
    }
    {   // Pure noise has no cycles: falls back to Stretch Aligned exactly.
        pt::Sample n;n.rate=44100;n.left.resize(8820);uint32_t seed=12345;
        for(auto& v:n.left){seed=seed*1664525u+1013904223u;v=float((seed>>8)/16777216.0*2-1)*.3f;}
        n.right=n.left;
        const auto a=pt::renderPunchPitch(n,7,pt::PunchPitchMode::StretchAligned);
        const auto h=pt::renderPunchPitch(n,7,pt::PunchPitchMode::HighestFidelity);
        check(h.left.size()==n.left.size(),"Noise punch length");
        for(float v:h.left)check(std::isfinite(v),"Noise punch invalid");
        if(pt::punchfidelity::trackPeriods(n).period.empty())check(h.left==a.left && h.right==a.right,"Non-periodic punch did not fall back to Stretch Aligned");
    }
    check(argc>1,"Missing punch fixture");const auto real=loadFloatWave(argv[1]);
    for(auto mode:{pt::PunchPitchMode::StretchAligned,pt::PunchPitchMode::HighestFidelity})
    for(double semi:{-12.,-3.,3.,12.}) {
        const auto y=pt::renderPunchPitch(real,semi,mode);double energy=0;
        check(y.left.size()==real.left.size(),"Real punch duration changed");
        for(float v:y.left){check(std::isfinite(v),"Invalid real punch audio");energy+=v*v;}
        check(energy>.01,"Real punch silent");
    }
    std::cout<<"PASS punch modes: aligned duration, Highest Fidelity (duration, root, pitch, sweep, level, onset, stereo), resample speed/duration/events, trim, joins, preview, stereo, root and fixtures\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
