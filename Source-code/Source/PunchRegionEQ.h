#pragma once
#include "ParametricEQ.h"

namespace tt {
struct PunchRegionSettings { bool enabled=false; double splitPercent=20,transitionMs=2,attackGainDb=0,decayGainDb=0; };
struct PunchRegionDesign {
    PunchRegionSettings settings;
    ParametricEQ attack,decay;
    void update(PunchRegionSettings next,const std::array<Band,kBands>& a,bool attackOn,
                const std::array<Band,kBands>& d,bool decayOn,double rate) {
        settings=next;attack.update(a,attackOn,rate);decay.update(d,decayOn,rate);
    }
};
// One pair of filter histories per voice. No buffers, allocations, locks or
// coefficient design at note-on/per-sample; the shared block design supplies targets.
class PunchRegionEQ {
    ParametricEQ attack,decay;
    PunchRegionSettings settings;
    double rate=48000,end=0,blend=0,split=20,width=2,follow=.001,weight=0;
    int weightRamp=0;
    double attackLevel=1,decayLevel=1,attackTarget=1,decayTarget=1;
    int gainRamp=0;
    static double gain(double db){return std::pow(10.,std::clamp(db,-24.,24.)/20.);}
public:
    void start(const PunchRegionDesign& d,double sr,double punchEndSeconds) {
        rate=sr;end=punchEndSeconds;settings=d.settings;follow=1-std::exp(-1/(rate*.003));
        attack=d.attack;decay=d.decay;attack.settleTargets();decay.settleTargets();
        attack.clearState();decay.clearState();blend=settings.enabled?1:0;
        attackLevel=attackTarget=gain(settings.attackGainDb);decayLevel=decayTarget=gain(settings.decayGainDb);gainRamp=0;
        split=std::clamp(settings.splitPercent,0.,100.);width=std::clamp(settings.transitionMs,0.,20.);weight=split<=0?1:0;weightRamp=0;
    }
    void update(const PunchRegionDesign& d) {
        if(settings.splitPercent!=d.settings.splitPercent || settings.transitionMs!=d.settings.transitionMs)
            weightRamp=int(rate*.01);
        const double a=gain(d.settings.attackGainDb),b=gain(d.settings.decayGainDb);
        if(a!=attackTarget || b!=decayTarget){attackTarget=a;decayTarget=b;gainRamp=(std::max)(1,int(rate*.01));}
        settings=d.settings;attack.followTargets(d.attack);decay.followTargets(d.decay);
    }
    void process(float& l,float& r,double time) {
        if(time>=end){l=r=0;return;}
        const double step=1./(rate*.01);
        blend=std::clamp(blend+(settings.enabled?step:-step),0.,1.);
        // Boundary/width automation follows over ~10ms without jumping the weights.
        split+=(std::clamp(settings.splitPercent,0.,100.)-split)*follow;
        width+=(std::clamp(settings.transitionMs,0.,20.)-width)*follow;
        if(gainRamp>0){attackLevel+=(attackTarget-attackLevel)/gainRamp;decayLevel+=(decayTarget-decayLevel)/gainRamp;--gainRamp;}
        if(blend==0)return; // Exact original Punch when disabled.
        float al=l,ar=r,dl=l,dr=r;attack.process(al,ar);decay.process(dl,dr);
        const double boundary=end*split*.01;
        double w=0;
        if(split>=100)w=0;
        else if(split<=0)w=1;
        else {
            const double half=(std::min)({width*.0005,boundary,end-boundary});
            w=half>0?std::clamp((time-boundary+half)/(2*half),0.,1.):(time>=boundary?1.:0.);
            w=w*w*(3-2*w); // Constant-amplitude smooth transition, no +3dB overlap.
        }
        if(weightRamp>0){weight+=(w-weight)/weightRamp;--weightRamp;}else weight=w;
        w=weight;
        l=float(l+(al*attackLevel*(1-w)+dl*decayLevel*w-l)*blend);r=float(r+(ar*attackLevel*(1-w)+dr*decayLevel*w-r)*blend);
    }
};
}
