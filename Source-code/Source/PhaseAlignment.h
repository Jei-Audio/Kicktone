#pragma once
#include "Engine.h"
#include <array>
#include <limits>

namespace pt::phase {
// Pre-effects, overlap-only normalized correlation. This is a diagnostic,
// not a guarantee of phase coherence after EQ or distortion.
struct Result { double offsetMs=0, before=0, after=0; bool valid=false; };

inline double linear(const Sample& sample, int channel, double index) {
    const auto& x=channel==0?sample.left:sample.right;
    if(index < 0 || index >= double(x.size()) || x.empty())return 0;
    const size_t i=size_t(index);const double f=index-double(i);
    return x[i]*(1-f)+(i+1<x.size()?x[i+1]:0.0)*f;
}
struct Window {
    std::array<double,2048> times{};
    std::array<double,2048> punchLeft{},punchRight{};
    size_t count=0;
    double pp=0;
    std::array<double,2048> weights{};
};
inline Window prepareWindow(const Sample& p,const Voice& voice) {
    Window w;
    if(voice.overlap<0.0015 || p.left.empty())return w;
    const double begin=voice.tailStart, end=(std::min)(voice.punchEnd,voice.tailEnd);
    // Fixed analysis rate and bounded work, independently of source sample rate.
    // Both channels averaged in the correlation to avoid choosing an alignment
    // which reinforces only one stereo side.
    constexpr double rate=12000.0;
    for(double time=begin;time<end && w.count<w.times.size();time+=1/rate) {
        const double x=(time-begin)/(end-begin);
        const double fade=std::cos(x*pi/2.0)*std::sin(x*pi/2.0);
        if(fade<0.03)continue;
        const double l=linear(p,0,time*p.rate)*fade*(voice.settings.invertPunch?-1.0:1.0);
        const double r=linear(p,1,time*p.rate)*fade*(voice.settings.invertPunch?-1.0:1.0);
        w.times[w.count]=time;w.weights[w.count]=fade;w.punchLeft[w.count]=l;w.punchRight[w.count]=r;
        w.pp+=l*l+r*r;++w.count;
    }
    return w;
}
inline double correlation(const Sample& tail, const Voice& voice, const Window& w,
                          double totalShiftMs, bool invert, bool& valid) {
    double pt=0,tt=0;const double polarity=invert?-1.0:1.0;
    for(size_t i=0;i<w.count;++i) {
        const double time=w.times[i];
        const double x=(time-voice.tailStart)*voice.ratio + voice.tailTrim - totalShiftMs*0.001;
        const double l=linear(tail,0,x*tail.rate)*polarity*w.weights[i];
        const double r=linear(tail,1,x*tail.rate)*polarity*w.weights[i];
        pt+=w.punchLeft[i]*l+w.punchRight[i]*r;
        tt+=l*l+r*r;
    }
    valid = w.count>=12 && w.pp>1e-8 && tt>1e-8;
    return valid ? pt/std::sqrt(w.pp*tt) : 0.0;
}
// The caller must run this on the sample-preparation worker, never processBlock.
// MIDI-ready note audio is used here: no realtime pitch shifting is introduced.
inline Result analyse(const Sample* punch,const Sample* tail,Settings settings,int note,
                      bool automatic) {
    Result out;
    if(!punch || !tail)return out;
    settings.root=note;
    Voice v;v.start(note,1,1,punch,tail,settings);
    auto w=prepareWindow(*punch,v);
    bool baseline=false;
    out.before=correlation(*tail,v,w,settings.phaseShiftMs,settings.invertTail,baseline);
    if(!baseline)return out;
    out.valid=true;out.after=out.before;
    if(!automatic)return out;
    double bestFitness=out.before;
    // +/- 3.0 ms in 0.1 ms steps. The conservative travel penalty avoids
    // picking large offsets merely to chase a noisy, ambiguous correlation.
    for(int tenth=-30;tenth<=30;++tenth) {
        const double shift=tenth*0.1;
        bool valid=false;
        const double c=correlation(*tail,v,w,settings.phaseShiftMs+shift,settings.invertTail,valid);
        if(!valid)continue;
        const double fitness=c-0.04*std::abs(shift)/3.0;
        if(fitness>bestFitness+1e-6) {bestFitness=fitness;out.offsetMs=shift;out.after=c;}
    }
    // Require meaningful correlation improvement before changing any audio.
    if(out.after-out.before<0.06){out.offsetMs=0;out.after=out.before;}
    return out;
}
} // namespace pt::phase
