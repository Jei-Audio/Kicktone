#pragma once
// Tailtone's allocation-free, stereo, eight-band parametric EQ.
// A 2nd-order RBJ biquad is used for bells/shelves and cascaded HP/LPs.
// Parameters are clamped before designing coefficients; coefficient targets
// ramp per sample to avoid zipper noise without rebuilding the DSP graph.
#include <array>
#include <cmath>
#include <algorithm>
#include <complex>
#include <cstddef>

namespace tt {
constexpr int kBands=8;
constexpr double pi=3.14159265358979323846;
enum Type { LowCut=0, HighCut=1, Bell=2, LowShelf=3, HighShelf=4 };
struct Band {
    bool enabled=false;
    int type=Bell, slope=0; // slope index 0/1/2 -> 12/24/48 dB/oct on cuts
    double hz=1000.0, gain=0.0, q=0.70710678118;
};
struct Coeff {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    double magnitude(double hz,double sampleRate) const {
        const auto z=std::polar(1.0,-2.0*pi*hz/sampleRate);
        const auto z2=z*z;
        return std::abs((b0+b1*z+b2*z2)/(1.0+a1*z+a2*z2));
    }
};
inline Coeff makeCoeffs(const Band& band,double sampleRate,double overrideQ=-1.0) {
    const double fs=(std::max)(8000.0,sampleRate);
    const double freq=std::clamp(band.hz,20.0,(std::min)(20000.0,fs*0.47));
    const double omega=2.0*pi*freq/fs;
    const double cs=std::cos(omega), sn=std::sin(omega);
    const double q=std::clamp(overrideQ>0.0?overrideQ:band.q,0.1,12.0);
    const double alpha=sn/(2.0*q);
    const double A=std::pow(10.0,std::clamp(band.gain,-18.0,18.0)/40.0);
    double b0=1,b1=0,b2=0,a0=1,a1=0,a2=0;
    switch(std::clamp(band.type,0,4)) {
    case LowCut: // high pass
        b0=(1+cs)*0.5; b1=-(1+cs);b2=b0;
        a0=1+alpha;a1=-2*cs;a2=1-alpha;break;
    case HighCut: // low pass
        b0=(1-cs)*0.5;b1=1-cs;b2=b0;
        a0=1+alpha;a1=-2*cs;a2=1-alpha;break;
    case Bell:
        b0=1+alpha*A;b1=-2*cs;b2=1-alpha*A;
        a0=1+alpha/A;a1=-2*cs;a2=1-alpha/A;break;
    case LowShelf: {
        // RBJ shelf; Q controls transition width through shelf slope S.
        const double S=std::clamp(q/0.70710678118,0.2,1.0);
        const double root=2.0*std::sqrt(A)*std::sqrt((A+1/A)*(1/S-1)+2)*sn/2;
        b0=A*((A+1)-(A-1)*cs+root);
        b1=2*A*((A-1)-(A+1)*cs);
        b2=A*((A+1)-(A-1)*cs-root);
        a0=(A+1)+(A-1)*cs+root;
        a1=-2*((A-1)+(A+1)*cs);
        a2=(A+1)+(A-1)*cs-root;break;
    }
    case HighShelf: {
        const double S=std::clamp(q/0.70710678118,0.2,1.0);
        const double root=2.0*std::sqrt(A)*std::sqrt((A+1/A)*(1/S-1)+2)*sn/2;
        b0=A*((A+1)+(A-1)*cs+root);
        b1=-2*A*((A-1)+(A+1)*cs);
        b2=A*((A+1)+(A-1)*cs-root);
        a0=(A+1)-(A-1)*cs+root;
        a1=2*((A-1)-(A+1)*cs);
        a2=(A+1)-(A-1)*cs-root;break;
    }
    }
    const double inv=1.0/(std::max)(1.e-12,a0);
    return {b0*inv,b1*inv,b2*inv,a1*inv,a2*inv};
}
inline int sections(const Band& b) { return b.enabled ? ((b.type==LowCut||b.type==HighCut) ? (1<<std::clamp(b.slope,0,2)) : 1) : 0; }
inline double sectionQ(const Band& b,int index) {
    const int count=sections(b);
    if(count<=1 || (b.type!=LowCut && b.type!=HighCut)) return b.q;
    // Butterworth Q sections give the requested steepness without a peak.
    // Q is a resonance multiplier relative to the default 0.707 reference.
    return std::clamp((1.0/(2.0*std::sin((2.0*index+1.0)*pi/(4.0*count)))) * (b.q/0.70710678118),0.1,12.0);
}
inline double responseDb(const std::array<Band,kBands>& bands,double hz,double sampleRate) {
    double db=0;
    for(const auto& b:bands) for(int s=0;s<sections(b);++s)
        db+=20.0*std::log10((std::max)(1.e-15,makeCoeffs(b,sampleRate,sectionQ(b,s)).magnitude(hz,sampleRate)));
    return std::clamp(db,-96.0,48.0);
}
class ParametricEQ {
    static constexpr int maxSections=4;
    static constexpr int channels=2;
    struct Stage {
        Coeff current, start, target;
        std::array<double,channels> z1{},z2{};
        bool engaged=false, wanted=false;
        int ramp=0;
        void reset() { z1={};z2={};current=target=start={};ramp=0;engaged=false;wanted=false; }
        void setTarget(const Coeff& next,bool on) {
            if(!on && !engaged) return;
            if(on==engaged && std::abs(next.b0-target.b0)<1.e-9 &&
                std::abs(next.b1-target.b1)<1.e-9 && std::abs(next.b2-target.b2)<1.e-9 &&
                std::abs(next.a1-target.a1)<1.e-9 && std::abs(next.a2-target.a2)<1.e-9) return;
            // Fade a bypassed filter via separate master stage crossfade not possible
            // without extra buffers. Only change coefficients gradually when active.
            // Neutral band bypass is handled by bypassing the entire stage.
            if(!engaged && on) { current=start=Coeff{};target=next;ramp=128;z1={};z2={}; }
            else { start=current;target=next;ramp=128; }
            wanted=on;engaged=true;
        }
        void advance() {
            if(ramp<=0)return;
            const double t=(129.0-ramp)/128.0;
            const auto mix=[t](double a,double b){return a+(b-a)*t;};
            current={mix(start.b0,target.b0),mix(start.b1,target.b1),mix(start.b2,target.b2),
                     mix(start.a1,target.a1),mix(start.a2,target.a2)};
            --ramp;
            if(ramp==0 && !wanted) { engaged=false;z1={};z2={}; }
        }
        // v0.5.10 CPU: a settled stage with b0 == 1, b1 == a1, b2 == a2 (a band at
        // 0 dB, e.g. the three default nodes) and empty state returns its input
        // exactly (y = x + 0, and both states stay 0), so it can be skipped.
        bool passThrough() const {
            return ramp==0 && current.b0==1.0 && current.b1==current.a1 && current.b2==current.a2 &&
                   z1[0]==0 && z1[1]==0 && z2[0]==0 && z2[1]==0;
        }
        double process(int ch,double x) {
            const double y=current.b0*x+z1[static_cast<size_t>(ch)];
            z1[static_cast<size_t>(ch)]=current.b1*x-current.a1*y+z2[static_cast<size_t>(ch)];
            z2[static_cast<size_t>(ch)]=current.b2*x-current.a2*y;
            if(!std::isfinite(y) || !std::isfinite(z1[static_cast<size_t>(ch)]) || !std::isfinite(z2[static_cast<size_t>(ch)])) {
                z1[static_cast<size_t>(ch)]=z2[static_cast<size_t>(ch)]=0;return x;
            }
            return y;
        }
    };
    std::array<std::array<Stage,maxSections>,kBands> stages{};
    // v0.5.10 CPU: the engaged stages in band order (usually 3 of 32), rebuilt
    // after anything that can engage or release a stage. Same stages, same order.
    std::array<Stage*,kBands*maxSections> active{};int activeCount=0;bool activeDirty=true;
    void rebuildActive(){activeCount=0;for(auto& band:stages)for(auto& stage:band)if(stage.engaged)active[size_t(activeCount++)]=&stage;activeDirty=false;}
    bool on=false,configured=false,lastEnabled=false;
    double blend=0, blendTarget=0, lastRate=0;
    std::array<Band,kBands> lastBands{};
public:
    void reset() { for(auto& band:stages)for(auto& stage:band)stage.reset();on=false;blend=blendTarget=0;configured=false;activeDirty=true; }
    ParametricEQ()=default;
    ParametricEQ(const ParametricEQ& o):stages(o.stages),activeDirty(true),on(o.on),configured(o.configured),lastEnabled(o.lastEnabled),
        blend(o.blend),blendTarget(o.blendTarget),lastRate(o.lastRate),lastBands(o.lastBands){}
    ParametricEQ& operator=(const ParametricEQ& o){stages=o.stages;activeDirty=true;on=o.on;configured=o.configured;lastEnabled=o.lastEnabled;
        blend=o.blend;blendTarget=o.blendTarget;lastRate=o.lastRate;lastBands=o.lastBands;return *this;}
    void clearState() {for(auto& band:stages)for(auto& stage:band){stage.z1={};stage.z2={};}}
    // Fixed-size copies of coefficient targets preserve voice-local histories.
    void followTargets(const ParametricEQ& design) {
        blendTarget=design.blendTarget;on=blendTarget>0 || blend>0;
        for(size_t b=0;b<stages.size();++b)for(size_t i=0;i<stages[b].size();++i)
            stages[b][i].setTarget(design.stages[b][i].target,design.stages[b][i].wanted);
        activeDirty=true;
    }
    void settleTargets() {
        for(auto& band:stages)for(auto& s:band){s.current=s.start=s.target;s.ramp=0;s.engaged=s.wanted;}
        activeDirty=true;
        blend=blendTarget;on=blend>0;
    }
    void update(const std::array<Band,kBands>& bands,bool enabled,double sampleRate) {
        bool same=configured && enabled==lastEnabled && sampleRate==lastRate;
        if(same)for(int i=0;i<kBands;++i) {
            const auto& a=bands[size_t(i)];const auto& b=lastBands[size_t(i)];
            if(a.enabled!=b.enabled||a.type!=b.type||a.slope!=b.slope||
               a.hz!=b.hz||a.gain!=b.gain||a.q!=b.q){same=false;break;}
        }
        if(same)return;
        lastBands=bands;lastEnabled=enabled;lastRate=sampleRate;configured=true;
        blendTarget=enabled?1.0:0.0;
        on=enabled||blend>0.00001;
        if(!enabled) return;
        for(int b=0;b<kBands;++b) for(int s=0;s<maxSections;++s) {
            const int count=sections(bands[static_cast<size_t>(b)]);
            stages[static_cast<size_t>(b)][static_cast<size_t>(s)].setTarget(
                s<count?makeCoeffs(bands[static_cast<size_t>(b)],sampleRate,sectionQ(bands[static_cast<size_t>(b)],s)):Coeff{},s<count);
        }
        activeDirty=true;
    }
    void process(float& left,float& right) {
        if(!on && blend<=0)return;
        const double dryL=left,dryR=right;
        double l=left,r=right;
        if(activeDirty)rebuildActive();
        bool released=false;
        for(int k=0;k<activeCount;++k) {
            Stage& stage=*active[size_t(k)];
            if(!stage.engaged)continue;   // released earlier in this very loop cannot happen, kept for safety
            stage.advance();
            released|=!stage.engaged;
            if(stage.passThrough())continue;   // v0.5.10: exact identity
            l=stage.process(0,l);r=stage.process(1,r);
        }
        if(released)activeDirty=true;
        if(blend<blendTarget)blend=(std::min)(blendTarget,blend+1.0/128.0);
        else if(blend>blendTarget)blend=(std::max)(blendTarget,blend-1.0/128.0);
        left=static_cast<float>(dryL+(l-dryL)*blend);right=static_cast<float>(dryR+(r-dryR)*blend);
        if(blend==0 && blendTarget==0)on=false;
    }
};
} // namespace tt
