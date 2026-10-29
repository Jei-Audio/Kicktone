#pragma once
#include <algorithm>
#include <cmath>

namespace tt {
// v0.5.21 ATTACK / DECAY NORMALIZE (user request): two output trims for the
// processed Punch, one before and one after the Punch Attack/Decay SPLIT, applied
// after every Punch effect like punchNormalize (which still trims the whole Punch
// on top). The boundary and its constant-amplitude TRANSITION are the ones
// PunchRegionEQ uses; they apply whether or not the Attack/Decay EQ is enabled.
// Portable, no allocation: one instance follows the newest note.
class PunchSplitTrim {
    double split=20,width=2,follow=.001,weight=0;
public:
    // Weight of the DECAY trim (0 = attack, 1 = decay) at `time` of a Punch lasting `end` seconds.
    static double decayWeight(double time,double end,double splitPercent,double transitionMs) {
        const double s=std::clamp(splitPercent,0.,100.),wd=std::clamp(transitionMs,0.,20.);
        if(s>=100)return 0;
        if(s<=0)return 1;
        const double boundary=end*s*.01;
        const double half=(std::min)({wd*.0005,boundary,end-boundary});
        double w=half>0?std::clamp((time-boundary+half)/(2*half),0.,1.):(time>=boundary?1.:0.);
        return w*w*(3-2*w);
    }
    // Linear amplitude: exactly `attack` when both trims are equal (0 dB = exact bypass).
    static float gain(float attack,float decay,float w) { return attack==decay?attack:attack*(1.f-w)+decay*w; }
    void prepare(double rate,double splitPercent,double transitionMs) {
        follow=1-std::exp(-1/((std::max)(1.,rate)*.003));
        split=std::clamp(splitPercent,0.,100.);width=std::clamp(transitionMs,0.,20.);weight=0;
    }
    // SPLIT / TRANSITION automation follows over ~3 ms, as in PunchRegionEQ.
    float next(double time,double end,double splitPercent,double transitionMs) {
        split+=(std::clamp(splitPercent,0.,100.)-split)*follow;
        width+=(std::clamp(transitionMs,0.,20.)-width)*follow;
        weight=decayWeight(time,end,split,width);
        return float(weight);
    }
    // No note sounding: keep the last weight.
    float hold() const { return float(weight); }
};
}
