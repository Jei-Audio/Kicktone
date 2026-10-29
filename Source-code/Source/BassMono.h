#pragma once
#include "LinearPhaseEQ.h"   // tt::linear::fft, block size
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace tt {
// v0.5 MONO BASS (user request: "mono 90 Hz down, up to 300 Hz and down, with a
// mix between minimal stereo and pure mono").
//
// The output is split into mid (L+R)/2 and side (L-R)/2. The mid, which is
// everything a mono speaker plays, is never touched. Only the side is filtered:
// a 24 dB/oct Linkwitz-Riley high-pass at FREQUENCY (90-300 Hz, -6 dB at the
// frequency) removes the stereo difference below it, and MIX blends between
// the original side (0 %) and the filtered side (100 % = pure mono below the
// frequency). Above the frequency the side keeps its level.
//
// Zero latency, no allocation, per-frame processing. While the effective mix
// is exactly 0 (OFF, or MIX 0 %), the frame passes bit-identically; switching
// and MIX changes are smoothed over 20 ms and FREQUENCY over ~20 ms, so
// automation never clicks. The filter keeps running while bypassed so turning
// it on starts from settled state.
class BassMono {
public:
    static constexpr double kMinHz=90.0, kMaxHz=300.0, kDefaultHz=120.0;
    void prepare(double sampleRate) {
        rate=(std::max)(1.0,sampleRate);
        mixStep=float(1.0/(0.020*rate));
        hzCoefficient=1.0-std::exp(-1.0/(0.0047*rate));   // ~20 ms to settle
        reset();
    }
    void reset() {
        for(auto& s:stages)s=Stage{};
        mix=0.f;hz=-1.0;designedHz=-1.0;sinceDesign=0;
    }
    // enabled: the ON switch. frequency in Hz (clamped to 90-300). amount 0..1.
    void process(float& l,float& r,bool enabled,double frequency,float amount) {
        frequency=std::clamp(frequency,kMinHz,kMaxHz);
        if(hz<0)hz=frequency; else hz+=(frequency-hz)*hzCoefficient;
        if(designedHz<0 || (std::abs(hz-designedHz)>1e-3 && ++sinceDesign>=16)){design(hz);sinceDesign=0;}
        const float target=enabled?std::clamp(amount,0.f,1.f):0.f;
        if(mix<target)mix=(std::min)(target,mix+mixStep);
        else if(mix>target)mix=(std::max)(target,mix-mixStep);
        const double mid=0.5*(double(l)+double(r)), side=0.5*(double(l)-double(r));
        double high=side;
        for(auto& s:stages)high=s.run(high,c);
        if(mix<=0.f)return;                               // exact bypass
        const double out=side+double(mix)*(high-side);
        l=float(mid+out);r=float(mid-out);
    }
    // Start at the target mix without the 20 ms ramp (offline renders, prepareToPlay).
    void settle(bool enabled,float amount) { mix=enabled?std::clamp(amount,0.f,1.f):0.f; }
    float currentMix() const { return mix; }
    double currentHz() const { return hz; }
    // Magnitude of the side-channel response at hz (for tests / display): LR4 high-pass.
    static double sideGain(double hzAt,double cutoff) {
        const double x=std::pow(hzAt/cutoff,4.0);
        return x/(1.0+x);   // |H_LR4,hp| = |H_BW2,hp|^2 = (w/wc)^4 / (1+(w/wc)^4)
    }
private:
    struct Coeffs { double b0=1,b1=0,b2=0,a1=0,a2=0; } c;
    struct Stage {
        double z1=0,z2=0;
        double run(double x,const Coeffs& k) {   // transposed direct form II
            const double y=k.b0*x+z1;
            z1=k.b1*x-k.a1*y+z2;
            z2=k.b2*x-k.a2*y;
            return y;
        }
    };
    void design(double f) {
        // Second-order Butterworth high-pass (Q = 1/sqrt 2); two in series = LR4.
        const double w=2.0*3.14159265358979323846*f/rate, cw=std::cos(w), alpha=std::sin(w)/(2.0*0.70710678118654752);
        const double a0=1.0+alpha;
        c.b0=(1.0+cw)*0.5/a0;c.b1=-(1.0+cw)/a0;c.b2=c.b0;
        c.a1=-2.0*cw/a0;c.a2=(1.0-alpha)/a0;
        designedHz=f;
    }
    Stage stages[2];
    double rate=44100, hz=-1, designedHz=-1, hzCoefficient=.005;
    float mix=0.f, mixStep=.001f;
    int sinceDesign=0;
};

// v0.5.1 LINEAR-PHASE MONO BASS (user request: "is mono bass linear phase? I
// would want that"). Same idea, no phase shift: the side's low band is a
// symmetric FIR low-pass with the zero-phase magnitude 1/(1+(f/fc)^4), and the
// output is the DELAYED input minus MIX x that low band of the side:
//     L = L(n-D) - mix*lp,  R = R(n-D) + mix*lp      (mid = (L+R)/2 unchanged)
// so at MIX 100 % the side response is exactly the LR4 magnitude x/(1+x) with
// zero phase, and MIX 0 % is the delayed input sample for sample. The latency
// D = radius + block - 1 (1663 samples / 35 ms at 48 kHz) is reported to the
// host. Kernels are designed off the audio thread; a new FREQUENCY crossfades
// over 10 ms. process() allocates nothing.
namespace bassmono {
// 32 ms each side: the zero-phase LR4 low-pass has decayed below -100 dB by then
// at 90 Hz, so the response matches LR4 within 0.05 dB at every setting.
inline int radiusFor(double rate){return std::clamp(int(std::lround(1536*rate/48000.)),384,8192);}
// Symmetric low-pass taps (length 2*radius+1, centre at index radius), DC gain exactly 1.
inline std::vector<double> lowpassTaps(double hz,double rate,int radius) {
    hz=std::clamp(hz,BassMono::kMinHz,BassMono::kMaxHz);
    const int length=2*radius+1;size_t n=1;while(n<size_t(length*4))n*=2;
    std::vector<std::complex<double>> spectrum(n);
    for(size_t i=0;i<n;++i){const double f=double((std::min)(i,n-i))*rate/double(n);const double x=std::pow(f/hz,4.0);spectrum[i]=1.0/(1.0+x);}
    tt::linear::fft(spectrum,true);
    auto taps=std::vector<double>(static_cast<size_t>(length));
    for(int i=0;i<length;++i){const int lag=i-radius;
        // Tukey window (flat centre, cosine taper over the outer half): keeps the
        // impulse response intact (a Blackman window bent the response by ~2 dB).
        const double x=std::abs(double(lag))/radius;
        const double window=x<.5?1.0:.5+.5*std::cos(tt::pi*(x-.5)/.5);
        taps[size_t(i)]=spectrum[size_t((int(n)+lag)%int(n))].real()*window;}
    for(int i=0;i<radius;++i){const double v=.5*(taps[size_t(i)]+taps[size_t(length-1-i)]);taps[size_t(i)]=taps[size_t(length-1-i)]=v;}
    double sum=0;for(double t:taps)sum+=t;for(double& t:taps)t/=sum;
    return taps;
}
struct LinearKernel {
    double rate=48000,hz=BassMono::kDefaultHz;int radius=2048;
    std::vector<std::vector<std::complex<float>>> spectra;   // uniform partitions of the taps
    int latency() const {return radius+tt::linear::block-1;}
};
inline std::shared_ptr<const LinearKernel> design(double hz,double rate) {
    auto k=std::make_shared<LinearKernel>();k->rate=rate;k->hz=std::clamp(hz,BassMono::kMinHz,BassMono::kMaxHz);k->radius=radiusFor(rate);
    const auto taps=lowpassTaps(k->hz,rate,k->radius);constexpr int block=tt::linear::block,fftSize=tt::linear::fftSize;
    const size_t partitions=(taps.size()+block-1)/block;
    k->spectra.assign(partitions,std::vector<std::complex<float>>(fftSize));
    for(size_t p=0;p<partitions;++p){auto& a=k->spectra[p];for(int i=0;i<block && p*block+size_t(i)<taps.size();++i)a[size_t(i)]=float(taps[p*block+size_t(i)]);tt::linear::fft(a,false);}
    return k;
}
// Offline zero-phase version for the Final Kick waveform / Export WAV: returns
// the side low band aligned with the input (no delay, pre-ring kept in range).
inline std::vector<double> sideLowBand(const std::vector<float>& left,const std::vector<float>& right,double hz,double rate) {
    const int radius=radiusFor(rate);const auto taps=lowpassTaps(hz,rate,radius);const size_t frames=left.size();
    size_t n=1;while(n<frames+taps.size())n*=2;
    std::vector<std::complex<double>> x(n),h(n);
    for(size_t i=0;i<frames;++i)x[i]=.5*(double(left[i])-double(right[i]));
    for(size_t i=0;i<taps.size();++i)h[i]=taps[i];
    tt::linear::fft(x,false);tt::linear::fft(h,false);for(size_t i=0;i<n;++i)x[i]*=h[i];tt::linear::fft(x,true);
    std::vector<double> out(frames);for(size_t i=0;i<frames;++i)out[i]=x[i+size_t(radius)].real();
    return out;
}
class LinearBassMono {
    static constexpr int block=tt::linear::block,fftSize=tt::linear::fftSize;
    std::shared_ptr<const LinearKernel> current,previous,target;
    std::vector<std::vector<std::complex<float>>> history;
    std::vector<std::complex<float>> scratch,oldScratch;
    std::array<float,block> before{},input{},output{},oldOutput{};
    std::vector<std::array<float,2>> delay;
    size_t head=0,position=0;int fill=0,out=block,ramp=0,rampLength=1;
    float mix=0.f,mixStep=.001f;
    // v0.5.10 CPU: as tt::linear::Convolver - the input FFT runs every block
    // (warm history for a seamless switch), the multiply-add + inverse FFT only
    // when a sample of the block is actually used, from the boundary state.
    size_t pendingSlot=0; bool pendingRamp=false, computed=true;
    const LinearKernel* pendingCurrent=nullptr; const LinearKernel* pendingPrevious=nullptr;
    void compute() {
        computed=true;
        std::fill(scratch.begin(),scratch.end(),std::complex<float>{});if(pendingRamp)std::fill(oldScratch.begin(),oldScratch.end(),std::complex<float>{});
        for(size_t p=0;p<pendingCurrent->spectra.size();++p){
            const auto& xs=history[(pendingSlot+history.size()-p)%history.size()];const auto& a=pendingCurrent->spectra[p];
            for(int i=0;i<fftSize;++i)scratch[size_t(i)]+=xs[size_t(i)]*a[size_t(i)];
            if(pendingRamp){const auto& b=pendingPrevious->spectra[p];for(int i=0;i<fftSize;++i)oldScratch[size_t(i)]+=xs[size_t(i)]*b[size_t(i)];}
        }
        tt::linear::fft(scratch,true);if(pendingRamp)tt::linear::fft(oldScratch,true);
        for(int i=0;i<block;++i){output[size_t(i)]=scratch[size_t(i+block)].real();oldOutput[size_t(i)]=pendingRamp?oldScratch[size_t(i+block)].real():output[size_t(i)];}
    }
    float convolve(float x,bool wanted) {
        input[size_t(fill)]=x;
        if(++fill==block){
            if(target!=current && ramp==0){previous=current;current=target;ramp=rampLength;}
            auto& h=history[head];for(int i=0;i<block;++i){h[size_t(i)]=before[size_t(i)];h[size_t(i+block)]=input[size_t(i)];}tt::linear::fft(h,false);before=input;
            pendingSlot=head;pendingRamp=ramp!=0;pendingCurrent=current.get();pendingPrevious=previous.get();computed=false;
            head=(head+1)%history.size();fill=0;out=0;
        }
        if(out>=block)return 0.f;
        if(!wanted){if(ramp)--ramp;++out;return 0.f;}
        if(!computed)compute();
        float y=output[size_t(out)];
        if(ramp){const float blend=float(rampLength-ramp+1)/float(rampLength);y=oldOutput[size_t(out)]+blend*(y-oldOutput[size_t(out)]);--ramp;}
        ++out;return y;
    }
public:
    void prepare(std::shared_ptr<const LinearKernel> k) {
        current=previous=target=std::move(k);
        history.assign(current->spectra.size(),std::vector<std::complex<float>>(fftSize));
        scratch.assign(fftSize,{});oldScratch.assign(fftSize,{});
        delay.assign(size_t(current->latency()),{});
        rampLength=(std::max)(1,int(current->rate*.01));mixStep=float(1.0/(0.020*current->rate));
        clear();mix=0.f;
    }
    void clear() {
        for(auto& p:history)std::fill(p.begin(),p.end(),std::complex<float>{});
        before={};input={};output={};oldOutput={};for(auto& d:delay)d={};
        head=0;position=0;fill=0;out=block;ramp=0;computed=true;
    }
    // Realtime: only swaps a pointer (the kernel is owned elsewhere until retired).
    void setKernel(const std::shared_ptr<const LinearKernel>& k){if(k && current && k->spectra.size()==current->spectra.size() && k->rate==current->rate)target=k;}
    void settle(bool enabled,float amount){mix=enabled?std::clamp(amount,0.f,1.f):0.f;}
    int latency() const {return current?current->latency():0;}
    double targetHz() const {return target?target->hz:0.0;}
    float currentMix() const {return mix;}
    // wanted=false: the caller discards l/r this sample (linear path not heard).
    void process(float& l,float& r,bool enabled,float amount,bool wanted=true) {
        const float goal=enabled?std::clamp(amount,0.f,1.f):0.f;
        if(mix<goal)mix=(std::min)(goal,mix+mixStep);else if(mix>goal)mix=(std::max)(goal,mix-mixStep);
        const float low=convolve(.5f*(l-r),wanted&&mix>0.f);
        const auto delayed=delay[position];delay[position]={l,r};position=(position+1)%delay.size();
        l=delayed[0];r=delayed[1];
        if(mix>0.f){l-=mix*low;r+=mix*low;}
    }
};
} // namespace bassmono
}

