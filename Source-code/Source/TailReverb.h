#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace pt {
struct TailReverbSettings { bool enabled=false; float mix=25,spaceMs=600,width=100,distance=20,duckAmount=85,duckHz=120,duckRecovery=60,levelDb=0,punchPreDelayMs=0,punchFadeMs=0,tailOffsetMs=0,punchDuck=0,punchDuckReleaseMs=80,tailFadeMs=0; int engine=0; /* v0.4.59: 0 Legacy, 1 V2 */ bool wetAutoMatch=false,wetPhaseLock=false,keepDry=true,distortWet=false,punchSend=false,tailSend=true,tailRootPitch=false; };
// v0.4.59: linear fade-in of a generated wet return, measured from the moment
// that return can first be heard. Zero (or negative/NaN) fade is exactly 1.
inline float wetFadeIn(double elapsedMs,double onsetMs,float fadeMs){
    if(!(fadeMs>0.f))return 1.f;
    return float(std::clamp((elapsedMs-onsetMs)/double((std::min)(fadeMs,1000.f)),0.0,1.0));
}
// Live Tail wet: the send opens at Punch end, then the network adds its Distance
// pre-delay plus the (positive-only in live mode) Tail Reverb Offset.
inline double tailWetOnsetMs(double punchEndSeconds,const TailReverbSettings& s){
    return punchEndSeconds*1000.0+.35*std::clamp(double(s.distance),0.0,100.0)+std::clamp(double(s.tailOffsetMs),0.0,300.0);
}
// Original stereo room network: four input diffusers per side, eight longer
// decorrelated feedback paths, gentle fractional-delay modulation and two output
// diffusers per side. Space is RT60, independent of room path length and wet level.
// Every history uses epoch tags. Reset remains bounded, allocation-free and exact.
// v0.5.10 CPU: index arithmetic without integer division. wrap(x,n) returns
// exactly x%n for every x (one subtraction covers the normal range).
inline size_t wrap(size_t x,size_t n){if(x>=n){x-=n;if(x>=n)x%=n;}return x;}
class TailReverb {
    struct Delay {
        std::vector<float> data;std::vector<uint64_t> tags;size_t pos=0;uint64_t epoch=1;
        // v0.5.10 CPU: true once every cell was written since the last clear()
        // (pos wrapped); then every tag equals epoch and cell() can skip the check.
        bool filled=false;
        void prepare(size_t n){data.assign(n,0);tags.assign(n,0);pos=0;epoch=1;filled=false;}
        void clear(){++epoch;if(epoch==0)++epoch;pos=0;filled=false;}
        float cell(size_t i) const {return (filled||tags[i]==epoch)?data[i]:0;}
        float read() const {return cell(pos);}
        float tap(double delay) const {
            const size_t whole=size_t(delay);const float fraction=float(delay-double(whole));
            const size_t n=data.size();
            const size_t a=wrap(pos+n-whole,n),b=a==0?n-1:a-1;
            return cell(a)+(cell(b)-cell(a))*fraction;
        }
        void write(float x){data[pos]=x;tags[pos]=epoch;if(++pos==data.size()){pos=0;filled=true;}}
        float allpass(float x,float gain){const float y=read()-gain*x;write(x+gain*y);return y;}
    };
    std::array<Delay,8> lines;
    std::array<std::array<Delay,4>,2> inputDiffusers;
    std::array<std::array<Delay,2>,2> outputDiffusers;
    std::array<float,8> feedback{},low{};
    std::array<double,8> nominal{},oscSin{},oscCos{},stepSin{},stepCos{};
    float lastSpace=-1,lastWidth=-1,lastDistance=-1,lastPunchPreDelay=-1;int lastEngine=-1;
    // ---- v0.4.59 V2 engine (Legacy members above are untouched) ----
    // 16-line FDN, fast Walsh-Hadamard feedback, per-line two-band decay
    // (true RT60 low / shorter HF RT60), cubic modulated reads, six input
    // diffusers, stereo early reflections and orthogonal output taps.
    static constexpr size_t kV2Lines=16;
    bool v2=false;
    std::array<Delay,kV2Lines> v2Lines;
    std::array<std::array<Delay,6>,2> v2InputDiffusers;
    std::array<std::array<Delay,2>,2> v2OutputDiffusers;
    std::array<Delay,2> v2Early;
    std::array<double,kV2Lines> v2Nominal{},v2Sin{},v2Cos{},v2StepSin{},v2StepCos{};
    std::array<float,kV2Lines> v2Low{},v2LowGain{},v2HighGain{};
    std::array<float,2> v2Band{},v2DcIn{},v2DcOut{};
    std::array<std::array<size_t,8>,2> v2EarlyTaps{};
    double v2Depth=0;float v2Split=.3f,v2Bandwidth=1,v2DcCoeff=.997f,v2EarlyGain=.3f;
    static constexpr float kV2OutputGain=.89f; // energy-matched to Legacy at default Space/Distance (TailReverbTests)
    void resetV2Oscillators(){for(size_t i=0;i<kV2Lines;++i){const double phase=double(i)*1.37;v2Sin[i]=std::sin(phase);v2Cos[i]=std::cos(phase);}}
    static constexpr bool parity(unsigned v){bool p=false;while(v){p=!p;v&=v-1;}return p;}
    static float cubicTap(const Delay& d,double delay){
        // 4-point Hermite read; epoch-aware via cell().
        const size_t whole=size_t(delay);const float f=float(delay-double(whole));const size_t n=d.data.size();
        const size_t i0=wrap(d.pos+n-whole,n);const size_t im1=wrap(i0+1,n),i1=wrap(i0+n-1,n),i2=wrap(i0+n-2,n);
        const float ym1=d.cell(im1),y0=d.cell(i0),y1=d.cell(i1),y2=d.cell(i2);
        const float c1=.5f*(y1-ym1),c2=ym1-2.5f*y0+2.f*y1-.5f*y2,c3=.5f*(y2-ym1)+1.5f*(y0-y1);
        return ((c3*f+c2)*f+c1)*f+y0;
    }
    double rate=44100,depth=0;float damp=.5f,stereo=1,atten=1;size_t predelay=0;
    size_t prePos=0;uint64_t preEpoch=1;
    std::array<std::vector<float>,2> preData;
    std::vector<uint64_t> preTags;
    void resetOscillators(){for(size_t i=0;i<8;++i){const double phase=double(i)*.83;oscSin[i]=std::sin(phase);oscCos[i]=std::cos(phase);}}
public:
    void prepare(double sr){
        rate=sr;depth=sr*.00035;
        constexpr double times[]={.0371,.0437,.0533,.0617,.0719,.0823,.0937,.1073};
        constexpr double speeds[]={.23,.31,.41,.29,.37,.19,.43,.47};
        for(size_t i=0;i<8;++i){nominal[i]=std::round(sr*times[i]);lines[i].prepare(size_t(std::ceil(nominal[i]+depth))+3);
            const double angle=2*3.14159265358979323846*speeds[i]/sr;stepSin[i]=std::sin(angle);stepCos[i]=std::cos(angle);}
        constexpr double diffusion[]={.0043,.0061,.0097,.0127};
        for(size_t ch=0;ch<2;++ch){
            for(size_t i=0;i<4;++i)inputDiffusers[ch][i].prepare((std::max)(size_t(1),size_t(std::round(sr*(diffusion[i]+ch*.00073)))));
            for(size_t i=0;i<2;++i)outputDiffusers[ch][i].prepare((std::max)(size_t(1),size_t(std::round(sr*(.0031+i*.0048+ch*.00041)))));
        }
        // The same prepared ring serves the normal Distance pre-delay and the
        // independent Punch send pre-delay (up to 300 ms). No RT allocation.
        const size_t maxPre=size_t(std::ceil(sr*.335))+2;
        for(auto& d:preData)d.assign(maxPre,0);preTags.assign(maxPre,0);prePos=0;preEpoch=1;
        // V2 buffers are always prepared (outside the audio callback).
        constexpr double v2Times[kV2Lines]={.0297,.0331,.0371,.0411,.0437,.0479,.0533,.0571,.0617,.0661,.0719,.0767,.0823,.0877,.0937,.0991};
        constexpr double v2Speeds[kV2Lines]={.11,.23,.17,.31,.13,.37,.19,.29,.41,.15,.33,.21,.27,.39,.25,.35};
        v2Depth=sr*.00042;
        for(size_t i=0;i<kV2Lines;++i){v2Nominal[i]=std::round(sr*v2Times[i]);v2Lines[i].prepare(size_t(std::ceil(v2Nominal[i]+2*v2Depth))+5);
            const double angle=2*3.14159265358979323846*v2Speeds[i]/sr;v2StepSin[i]=std::sin(angle);v2StepCos[i]=std::cos(angle);}
        constexpr double v2Diffusion[6]={.0047,.0036,.0127,.0093,.0021,.0161};
        for(size_t ch=0;ch<2;++ch){
            for(size_t i=0;i<6;++i)v2InputDiffusers[ch][i].prepare((std::max)(size_t(1),size_t(std::round(sr*(v2Diffusion[i]+ch*.00053)))));
            for(size_t i=0;i<2;++i)v2OutputDiffusers[ch][i].prepare((std::max)(size_t(1),size_t(std::round(sr*(.0029+i*.0053+ch*.00047)))));
            v2Early[ch].prepare(size_t(std::ceil(sr*.08))+2);
        }
        constexpr double earlyMs[2][8]={{7.3,11.9,17.1,23.9,31.3,41.7,53.9,67.1},{8.9,13.1,19.7,26.3,34.1,44.3,57.7,71.9}};
        for(size_t ch=0;ch<2;++ch)for(size_t i=0;i<8;++i)v2EarlyTaps[ch][i]=size_t(std::round(sr*earlyMs[ch][i]*.001));
        v2DcCoeff=float(std::exp(-2*3.14159265358979323846*25.0/sr));
        lastSpace=lastWidth=lastDistance=lastPunchPreDelay=-1;lastEngine=-1;clear();setSettings({});
    }
    void clear(){
        for(auto& line:lines)line.clear();
        for(auto& side:inputDiffusers)for(auto& d:side)d.clear();
        for(auto& side:outputDiffusers)for(auto& d:side)d.clear();
        low.fill(0);resetOscillators();prePos=0;++preEpoch;if(preEpoch==0)++preEpoch;
        for(auto& line:v2Lines)line.clear();
        for(auto& side:v2InputDiffusers)for(auto& d:side)d.clear();
        for(auto& side:v2OutputDiffusers)for(auto& d:side)d.clear();
        for(auto& d:v2Early)d.clear();
        v2Low.fill(0);v2Band.fill(0);v2DcIn.fill(0);v2DcOut.fill(0);resetV2Oscillators();
    }
    void setSettings(TailReverbSettings s){
        if(s.spaceMs==lastSpace && s.width==lastWidth && s.distance==lastDistance && s.punchPreDelayMs==lastPunchPreDelay && s.engine==lastEngine)return;
        lastSpace=s.spaceMs;lastWidth=s.width;lastDistance=s.distance;lastPunchPreDelay=s.punchPreDelayMs;lastEngine=s.engine;
        v2=s.engine==1;
        const double seconds=std::clamp(double(s.spaceMs),20.0,3000.0)*.001;
        for(size_t i=0;i<8;++i)feedback[i]=float(std::pow(.001,nominal[i]/rate/seconds));
        const float distance=std::clamp(s.distance,0.f,100.f)*.01f;
        damp=float(1-std::exp(-2*3.14159265358979323846*(14000-12000*distance)/rate));
        predelay=size_t(std::round(rate*(.035*distance+std::clamp(s.punchPreDelayMs,0.f,300.f)*.001f)));
        stereo=std::clamp(s.width,0.f,200.f)*.01f;atten=1-.35f*distance;
        if(!v2Lines[0].data.empty()) { // V2 coefficients (cheap; computed only when settings change)
            const double hfRatio=.8-.45*distance;   // HF decays faster as the source gets further away
            for(size_t i=0;i<kV2Lines;++i){
                const double t=v2Nominal[i]/rate;
                v2LowGain[i]=float(std::pow(.001,t/seconds));
                v2HighGain[i]=float(std::pow(.001,t/(seconds*hfRatio)));
            }
            v2Split=float(1-std::exp(-2*3.14159265358979323846*(6500-4000*distance)/rate));
            v2Bandwidth=float(1-std::exp(-2*3.14159265358979323846*(18000-11000*distance)/rate));
            v2EarlyGain=.42f*(1-.55f*distance);
        }
    }
    std::array<float,2> process(float l,float r){
        if(preData[0].empty())return {};
        if(v2)return processV2(l,r);
        // Always advance the ring, including zero distance, so automation never
        // replays stale input when predelay changes from zero to a positive value.
        const size_t index=wrap(prePos+preData[0].size()-predelay,preData[0].size());
        const float pl=predelay==0?l:(preTags[index]==preEpoch?preData[0][index]:0);
        const float pr=predelay==0?r:(preTags[index]==preEpoch?preData[1][index]:0);
        preData[0][prePos]=l;preData[1][prePos]=r;preTags[prePos]=preEpoch;
        if(++prePos==preData[0].size())prePos=0;l=pl;r=pr;
        for(size_t i=0;i<4;++i){l=inputDiffusers[0][i].allpass(l,.64f);r=inputDiffusers[1][i].allpass(r,.64f);}
        std::array<float,8> d{};float sum=0;
        for(size_t i=0;i<8;++i){
            const float x=lines[i].tap(nominal[i]+depth*oscSin[i]);low[i]+=damp*(x-low[i]);d[i]=low[i];sum+=d[i];
            const double nextSin=oscSin[i]*stepCos[i]+oscCos[i]*stepSin[i];
            oscCos[i]=oscCos[i]*stepCos[i]-oscSin[i]*stepSin[i];oscSin[i]=nextSin;
        }
        // A permutation followed by a Householder reflection is orthogonal:
        // feedback gains below unity keep the network bounded.
        for(size_t i=0;i<8;++i){const float input=(i%2==0?l:r)*.28f;lines[i].write(input+feedback[i]*(d[(i+3)%8]-.25f*sum));}
        float wl=(d[0]+d[2]-d[4]+d[6])*.7f,wr=(d[1]+d[3]-d[5]+d[7])*.7f;
        for(size_t i=0;i<2;++i){wl=outputDiffusers[0][i].allpass(wl,.55f);wr=outputDiffusers[1][i].allpass(wr,.55f);}
        const float mid=(wl+wr)*.5f,side=(wl-wr)*.5f*stereo;
        return {(mid+side)*atten,(mid-side)*atten};
    }
    // v0.4.59 V2 room. Same pre-delay ring, Space (RT60), Width and Distance
    // semantics as Legacy; allocation-free and fully epoch-reset by clear().
    std::array<float,2> processV2(float l,float r){
        const size_t index=wrap(prePos+preData[0].size()-predelay,preData[0].size());
        const float pl=predelay==0?l:(preTags[index]==preEpoch?preData[0][index]:0);
        const float pr=predelay==0?r:(preTags[index]==preEpoch?preData[1][index]:0);
        preData[0][prePos]=l;preData[1][prePos]=r;preTags[prePos]=preEpoch;
        if(++prePos==preData[0].size())prePos=0;
        std::array<float,2> in{pl,pr};
        for(size_t ch=0;ch<2;++ch){
            // 25 Hz DC/rumble blocker, then input bandwidth (Distance darkens).
            const float x=in[ch];const float hp=x-v2DcIn[ch]+v2DcCoeff*v2DcOut[ch];v2DcIn[ch]=x;v2DcOut[ch]=hp;
            v2Band[ch]+=v2Bandwidth*(hp-v2Band[ch]);in[ch]=v2Band[ch];
        }
        // Early reflections from the pre-delayed (undiffused) input.
        std::array<float,2> early{};
        for(size_t ch=0;ch<2;++ch){
            constexpr float g[8]={.62f,-.48f,.41f,-.33f,.27f,-.21f,.16f,-.12f};
            v2Early[ch].write(in[ch]);
            for(size_t i=0;i<8;++i)early[ch]+=g[i]*v2Early[ch].tap(double(v2EarlyTaps[ch][i]));
        }
        constexpr float gains[6]={.75f,.75f,.625f,.625f,.7f,.6f};
        for(size_t ch=0;ch<2;++ch)for(size_t i=0;i<6;++i)in[ch]=v2InputDiffusers[ch][i].allpass(in[ch],gains[i]);
        std::array<float,kV2Lines> d{};
        for(size_t i=0;i<kV2Lines;++i){
            const float x=cubicTap(v2Lines[i],v2Nominal[i]+v2Depth*(1.0+v2Sin[i]));
            v2Low[i]+=v2Split*(x-v2Low[i]);
            d[i]=v2LowGain[i]*v2Low[i]+v2HighGain[i]*(x-v2Low[i]); // two-band decay, |gain|<=max(low,high)<1
            const double nextSin=v2Sin[i]*v2StepCos[i]+v2Cos[i]*v2StepSin[i];
            v2Cos[i]=v2Cos[i]*v2StepCos[i]-v2Sin[i]*v2StepSin[i];v2Sin[i]=nextSin;
        }
        // Orthogonal output taps (two distinct Hadamard rows) before mixing.
        float wl=0,wr=0;
        for(size_t i=0;i<kV2Lines;++i){
            const float sl=parity(unsigned(i)&0x5u)?-1.f:1.f;   // portable (MSVC has no __builtin_popcount)
            const float sr=parity(unsigned(i)&0xAu)?-1.f:1.f;
            wl+=sl*d[i];wr+=sr*d[i];
        }
        // Fast Walsh-Hadamard (orthonormal with the .25 scale): lossless mixing.
        std::array<float,kV2Lines> m=d;
        for(size_t h=1;h<kV2Lines;h<<=1)for(size_t i=0;i<kV2Lines;i+=h<<1)for(size_t j=i;j<i+h;++j){const float a=m[j],b=m[j+h];m[j]=a+b;m[j+h]=a-b;}
        for(size_t i=0;i<kV2Lines;++i)v2Lines[i].write((i%2==0?in[0]:in[1])*.25f+m[i]*.25f);
        wl*=.25f;wr*=.25f;
        for(size_t i=0;i<2;++i){wl=v2OutputDiffusers[0][i].allpass(wl,.5f);wr=v2OutputDiffusers[1][i].allpass(wr,.5f);}
        wl=(wl+early[0]*v2EarlyGain)*kV2OutputGain;wr=(wr+early[1]*v2EarlyGain)*kV2OutputGain;
        const float mid=(wl+wr)*.5f,side=(wl-wr)*.5f*stereo;
        return {(mid+side)*atten,(mid-side)*atten};
    }
};
}
