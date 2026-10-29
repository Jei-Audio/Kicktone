// v0.5.10 CPU OPTIMIZATIONS: every optimized realtime DSP class must give
// BIT-IDENTICAL output to its v0.5.9 implementation, which is embedded below
// verbatim (namespace ref) as the reference. Never "fix" this test by changing
// the reference code: the references define the sound.
//   tt::linear::Convolver / NoteFilter  lazy blocks, silent-block skip, real-time ring-out pool
//   tt::bassmono::LinearBassMono        lazy blocks
//   pt::TailReverb                      division-free indexing, filled delay lines
//   tt::ParametricEQ                    exact-identity stage skip, engaged-stage list
#include "../Source/LinearPhaseEQ.h"
#include "../Source/BassMono.h"
#include "../Source/TailReverb.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static bool same(float a,float b){uint32_t x,y;std::memcpy(&x,&a,4);std::memcpy(&y,&b,4);return x==y || (a==0.f&&b==0.f);}   // +-0 only
namespace ref { using namespace tt; using namespace tt::linear;
class RefConvolver {
    std::shared_ptr<const Kernel> current,previous,target;
    std::array<std::vector<std::vector<std::complex<float>>>,2> history;
    std::array<std::vector<std::complex<float>>,2> scratch,oldScratch;
    std::array<std::array<float,block>,2> before{},input{},output{},oldOutput{};
    size_t head=0;int fill=0,out=block,ramp=0,rampLength=1;
public:
    void prepare(std::shared_ptr<const Kernel> k){current=std::move(k);previous=target=current;ramp=0;rampLength=(std::max)(1,int(current->rate*.01));
        for(auto& c:history)c.assign(current->spectra.size(),std::vector<std::complex<float>>(fftSize));for(auto& c:scratch)c.resize(fftSize);for(auto& c:oldScratch)c.resize(fftSize);clear();}
    void setKernel(const std::shared_ptr<const Kernel>& k){if(k && k->spectra.size()==current->spectra.size())target=k;}
    // v0.5.1: adopt immediately (no 10 ms ramp), history kept; used while the output is muted for a mode swap.
    void swapKernel(const std::shared_ptr<const Kernel>& k){if(k && k->spectra.size()==current->spectra.size()){current=previous=target=k;ramp=0;}}
    void clear(){for(auto& c:history)for(auto& p:c)std::fill(p.begin(),p.end(),std::complex<float>{});before={};input={};output={};oldOutput={};head=0;fill=0;out=block;}
    std::array<float,2> process(float l,float r){input[0][size_t(fill)]=l;input[1][size_t(fill)]=r;
        if(++fill==block){if(target!=current && ramp==0){previous=current;current=target;ramp=rampLength;}
            // Both input FFTs must be ready before the optional stereo matrix.
            for(size_t c=0;c<2;++c){auto& h=history[c][head];for(int i=0;i<block;++i){h[size_t(i)]=before[c][size_t(i)];h[size_t(i+block)]=input[c][size_t(i)];}fft(h,false);before[c]=input[c];}
            for(size_t c=0;c<2;++c){auto& sum=scratch[c];auto& old=oldScratch[c];std::fill(sum.begin(),sum.end(),std::complex<float>{});if(ramp)std::fill(old.begin(),old.end(),std::complex<float>{});
                for(size_t p=0;p<current->spectra.size();++p){
                    const size_t slot=(head+history[c].size()-p)%history[c].size();
                    const auto& x=history[c][slot];const auto& other=history[1-c][slot];
                    const auto& a=current->spectra[p];const auto& b=previous->spectra[p];
                    for(int i=0;i<fftSize;++i){const size_t f=size_t(i);
                        auto value=x[f]*a[f];
                        if(current->subMono)value-=.5f*(x[f]-other[f])*(a[f]-current->sideSpectra[p][f]);
                        sum[f]+=value;
                        if(ramp){auto beforeValue=x[f]*b[f];if(previous->subMono)beforeValue-=.5f*(x[f]-other[f])*(b[f]-previous->sideSpectra[p][f]);old[f]+=beforeValue;}
                    }
                }
                fft(sum,true);if(ramp)fft(old,true);for(int i=0;i<block;++i){output[c][size_t(i)]=sum[size_t(i+block)].real();oldOutput[c][size_t(i)]=ramp?old[size_t(i+block)].real():output[c][size_t(i)];}}
            head=(head+1)%history[0].size();fill=0;out=0;
        }
        if(out>=block)return {};auto result=std::array<float,2>{output[0][size_t(out)],output[1][size_t(out)]};if(ramp){const float blend=float(rampLength-ramp+1)/rampLength;for(size_t c=0;c<2;++c)result[c]=oldOutput[c][size_t(out)]+blend*(result[c]-oldOutput[c][size_t(out)]);--ramp;}++out;return result;
    }
};
// Preserve the previous note's pending compensated output, but cut it exactly
// at the new note's compensated boundary. A fresh note cannot pre-ring before
// that boundary, and old feedback can never leak into the new note.
class RefNoteFilter {
    RefConvolver filter;
    std::vector<std::array<float,2>> pending;
    int64_t time=0,validFrom=0;
public:
    void prepare(std::shared_ptr<const Kernel> k){pending.assign(size_t(k->latency()),{});filter.prepare(std::move(k));time=0;validFrom=0;}
    void setKernel(const std::shared_ptr<const Kernel>& k){filter.setKernel(k);}
    void swapKernel(const std::shared_ptr<const Kernel>& k){filter.swapKernel(k);}
    void startNote(){if(time==0){filter.clear();validFrom=int64_t(pending.size());return;}for(size_t i=0;i<pending.size();++i){const auto x=filter.process(0,0);if(time+int64_t(i)>=validFrom)pending[size_t((time+int64_t(i))%pending.size())]=x;}filter.clear();validFrom=time+int64_t(pending.size());}
    std::array<float,2> process(float l,float r){auto x=filter.process(l,r);auto& old=pending[size_t(time%pending.size())];if(time<validFrom)x=old;old={};++time;return x;}
};

}
namespace ref { using namespace tt; using namespace tt::bassmono; using tt::bassmono::LinearKernel;
class RefLinearBassMono {
    static constexpr int block=tt::linear::block,fftSize=tt::linear::fftSize;
    std::shared_ptr<const LinearKernel> current,previous,target;
    std::vector<std::vector<std::complex<float>>> history;
    std::vector<std::complex<float>> scratch,oldScratch;
    std::array<float,block> before{},input{},output{},oldOutput{};
    std::vector<std::array<float,2>> delay;
    size_t head=0,position=0;int fill=0,out=block,ramp=0,rampLength=1;
    float mix=0.f,mixStep=.001f;
    float convolve(float x) {
        input[size_t(fill)]=x;
        if(++fill==block){
            if(target!=current && ramp==0){previous=current;current=target;ramp=rampLength;}
            auto& h=history[head];for(int i=0;i<block;++i){h[size_t(i)]=before[size_t(i)];h[size_t(i+block)]=input[size_t(i)];}tt::linear::fft(h,false);before=input;
            std::fill(scratch.begin(),scratch.end(),std::complex<float>{});if(ramp)std::fill(oldScratch.begin(),oldScratch.end(),std::complex<float>{});
            for(size_t p=0;p<current->spectra.size();++p){
                const auto& xs=history[(head+history.size()-p)%history.size()];const auto& a=current->spectra[p];
                for(int i=0;i<fftSize;++i)scratch[size_t(i)]+=xs[size_t(i)]*a[size_t(i)];
                if(ramp){const auto& b=previous->spectra[p];for(int i=0;i<fftSize;++i)oldScratch[size_t(i)]+=xs[size_t(i)]*b[size_t(i)];}
            }
            tt::linear::fft(scratch,true);if(ramp)tt::linear::fft(oldScratch,true);
            for(int i=0;i<block;++i){output[size_t(i)]=scratch[size_t(i+block)].real();oldOutput[size_t(i)]=ramp?oldScratch[size_t(i+block)].real():output[size_t(i)];}
            head=(head+1)%history.size();fill=0;out=0;
        }
        if(out>=block)return 0.f;
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
        head=0;position=0;fill=0;out=block;ramp=0;
    }
    // Realtime: only swaps a pointer (the kernel is owned elsewhere until retired).
    void setKernel(const std::shared_ptr<const LinearKernel>& k){if(k && current && k->spectra.size()==current->spectra.size() && k->rate==current->rate)target=k;}
    void settle(bool enabled,float amount){mix=enabled?std::clamp(amount,0.f,1.f):0.f;}
    int latency() const {return current?current->latency():0;}
    double targetHz() const {return target?target->hz:0.0;}
    float currentMix() const {return mix;}
    void process(float& l,float& r,bool enabled,float amount) {
        const float goal=enabled?std::clamp(amount,0.f,1.f):0.f;
        if(mix<goal)mix=(std::min)(goal,mix+mixStep);else if(mix>goal)mix=(std::max)(goal,mix-mixStep);
        const float low=convolve(.5f*(l-r));
        const auto delayed=delay[position];delay[position]={l,r};position=(position+1)%delay.size();
        l=delayed[0];r=delayed[1];
        if(mix>0.f){l-=mix*low;r+=mix*low;}
    }
};

}
namespace ref { using namespace pt;
class RefTailReverb {
    struct Delay {
        std::vector<float> data;std::vector<uint64_t> tags;size_t pos=0;uint64_t epoch=1;
        void prepare(size_t n){data.assign(n,0);tags.assign(n,0);pos=0;epoch=1;}
        void clear(){++epoch;if(epoch==0)++epoch;pos=0;}
        float cell(size_t i) const {return tags[i]==epoch?data[i]:0;}
        float read() const {return cell(pos);}
        float tap(double delay) const {
            const size_t whole=size_t(delay);const float fraction=float(delay-double(whole));
            const size_t a=(pos+data.size()-whole)%data.size(),b=(a+data.size()-1)%data.size();
            return cell(a)+(cell(b)-cell(a))*fraction;
        }
        void write(float x){data[pos]=x;tags[pos]=epoch;if(++pos==data.size())pos=0;}
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
        const size_t i0=(d.pos+n-whole)%n;const size_t im1=(i0+1)%n,i1=(i0+n-1)%n,i2=(i0+n-2)%n;
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
        const size_t index=(prePos+preData[0].size()-predelay)%preData[0].size();
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
        const size_t index=(prePos+preData[0].size()-predelay)%preData[0].size();
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
namespace ref { using tt::Band; using tt::Coeff; using tt::kBands; using tt::makeCoeffs; using tt::sections; using tt::sectionQ;
class RefParametricEQ {
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
    bool on=false,configured=false,lastEnabled=false;
    double blend=0, blendTarget=0, lastRate=0;
    std::array<Band,kBands> lastBands{};
public:
    void reset() { for(auto& band:stages)for(auto& stage:band)stage.reset();on=false;blend=blendTarget=0;configured=false; }
    void clearState() {for(auto& band:stages)for(auto& stage:band){stage.z1={};stage.z2={};}}
    // Fixed-size copies of coefficient targets preserve voice-local histories.
    void followTargets(const RefParametricEQ& design) {
        blendTarget=design.blendTarget;on=blendTarget>0 || blend>0;
        for(size_t b=0;b<stages.size();++b)for(size_t i=0;i<stages[b].size();++i)
            stages[b][i].setTarget(design.stages[b][i].target,design.stages[b][i].wanted);
    }
    void settleTargets() {
        for(auto& band:stages)for(auto& s:band){s.current=s.start=s.target;s.ramp=0;s.engaged=s.wanted;}
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
    }
    void process(float& left,float& right) {
        if(!on && blend<=0)return;
        const double dryL=left,dryR=right;
        double l=left,r=right;
        for(auto& band:stages)for(auto& stage:band) {
            if(!stage.engaged)continue;
            stage.advance();l=stage.process(0,l);r=stage.process(1,r);
        }
        if(blend<blendTarget)blend=(std::min)(blendTarget,blend+1.0/128.0);
        else if(blend>blendTarget)blend=(std::max)(blendTarget,blend-1.0/128.0);
        left=static_cast<float>(dryL+(l-dryL)*blend);right=static_cast<float>(dryR+(r-dryR)*blend);
        if(blend==0 && blendTarget==0)on=false;
    }
};

}

static std::array<tt::Band,tt::kBands> bandsA(){std::array<tt::Band,tt::kBands> b{};
    b[0]={true,tt::LowShelf,0,120,4,.7};b[1]={true,tt::Bell,0,900,-6,1.4};b[2]={true,tt::HighShelf,0,7000,3,.71};b[3]={true,tt::LowCut,1,40,0,.71};return b;}
static std::array<tt::Band,tt::kBands> bandsB(){auto b=bandsA();b[1].gain=5;b[2].hz=4000;return b;}
// Kick-like test signal with exact-zero stretches (silence) and bursts.
static std::vector<std::array<float,2>> signal(size_t n,unsigned seed){
    std::mt19937 rng(seed);std::uniform_real_distribution<float> u(-1,1);std::vector<std::array<float,2>> x(n);
    for(size_t i=0;i<n;++i){const size_t seg=(i/7000)%4;   // segment 3 is digital silence
        if(seg==3)continue;const double t=double(i%7000)/48000.0;
        const float s=float(std::sin(2*3.14159265*55*t)*std::exp(-t*3))*.7f+.05f*u(rng);x[i]={s,s*.6f+.1f*u(rng)};}
    return x;
}

static void convolver(){
    const double rate=48000;
    auto k1=tt::linear::design(bandsA(),true,rate,false),k2=tt::linear::design(bandsB(),true,rate,false),k3=tt::linear::design(bandsA(),true,rate,true,150);
    auto k4=tt::linear::design(bandsB(),true,rate,true,150);
    const auto x=signal(60000,3);
    for(int pass=0;pass<2;++pass){
        const auto a=pass==0?k1:k3,b=pass==0?k2:k4;
        ref::RefConvolver r;tt::linear::Convolver c;r.prepare(a);c.prepare(a);
        std::mt19937 rng(11+pass);size_t compared=0;
        for(size_t i=0;i<x.size();++i){
            if(i==9000||i==33000){r.setKernel(b);c.setKernel(b);}         // 10 ms kernel ramps
            if(i==21000||i==45000){r.setKernel(a);c.setKernel(a);}
            if(i==27011){r.swapKernel(b);c.swapKernel(b);}                 // mid-block swap
            if(i==40000){r.clear();c.clear();}
            const bool wanted=((i/3000)%3)!=1 || (rng()%97==0);           // long unwanted stretches + stray single samples
            const auto y=r.process(x[i][0],x[i][1]);const auto z=c.process(x[i][0],x[i][1],wanted);
            if(wanted){check(same(y[0],z[0])&&same(y[1],z[1]),"Convolver differs from v0.5.9 at sample "+std::to_string(i)+(pass?" (sub mono)":""));++compared;}
        }
        check(compared>30000,"too few compared samples");
    }
}
static void noteFilter(){
    const double rate=48000;auto k=tt::linear::design(bandsA(),true,rate,false),k2=tt::linear::design(bandsB(),true,rate,false);
    const auto x=signal(90000,5);
    ref::RefNoteFilter r;tt::linear::NoteFilter c;r.prepare(k);c.prepare(k);
    std::mt19937 rng(7);size_t next=1500;int starts=0;
    for(size_t i=0;i<x.size();++i){
        if(i==next){r.startNote();c.startNote();++starts;
            // mostly normal spacing; bursts of retriggers far inside one latency (~2175 samples) exhaust the pool
            next=i+((i>30000&&i<42000)?(150+rng()%400):(i>60000&&i<64000)?(40+rng()%60):(900+rng()%4000));}
        if(i==20000){r.setKernel(k2);c.setKernel(k2);}
        if(i==50000){r.swapKernel(k);c.swapKernel(k);}
        const bool wanted=((i/5000)%4)!=2;
        const auto y=r.process(x[i][0],x[i][1]);const auto z=c.process(x[i][0],x[i][1],wanted);
        if(wanted)check(same(y[0],z[0])&&same(y[1],z[1]),"NoteFilter differs from v0.5.9 at sample "+std::to_string(i));
    }
    check(starts>60,"too few note starts");
}
static void bassMono(){
    for(double rate:{44100.0,48000.0}){
        ref::RefLinearBassMono r;tt::bassmono::LinearBassMono c;auto k=tt::bassmono::design(120,rate),k2=tt::bassmono::design(200,rate);r.prepare(k);c.prepare(k);
        const auto x=signal(50000,9);
        for(size_t i=0;i<x.size();++i){
            if(i==15000){r.setKernel(k2);c.setKernel(k2);}
            const bool enabled=i>2000,wanted=((i/4000)%3)!=0;
            float rl=x[i][0],rr=x[i][1],cl=rl,cr=rr;r.process(rl,rr,enabled,.8f);c.process(cl,cr,enabled,.8f,wanted);
            if(wanted)check(same(rl,cl)&&same(rr,cr),"LinearBassMono differs from v0.5.9 at sample "+std::to_string(i));
        }
    }
}
static void reverb(){
    for(int engine:{0,1}){
        ref::RefTailReverb r;pt::TailReverb c;r.prepare(48000);c.prepare(48000);
        pt::TailReverbSettings s;s.engine=engine;s.spaceMs=900;s.distance=40;s.width=120;s.punchPreDelayMs=12;r.setSettings(s);c.setSettings(s);
        const auto x=signal(120000,13);
        for(size_t i=0;i<x.size();++i){
            if(i==50000){r.clear();c.clear();}                              // epoch reset: stale cells must read 0 again
            if(i==70000){s.spaceMs=300;s.distance=5;s.punchPreDelayMs=0;r.setSettings(s);c.setSettings(s);}
            const auto y=r.process(x[i][0],x[i][1]);const auto z=c.process(x[i][0],x[i][1]);
            check(same(y[0],z[0])&&same(y[1],z[1]),"TailReverb differs from v0.5.9 (engine "+std::to_string(engine)+") at sample "+std::to_string(i));
        }
    }
    for(size_t n:{size_t(1),size_t(2),size_t(7),size_t(4096)})for(size_t x:{size_t(0),size_t(1),n-1,n,n+1,2*n-1,2*n,5*n+3,size_t(0)-1,size_t(0)-n})
        check(pt::wrap(x,n)==x%n,"wrap() is not x % n");
}
static void eq(){
    const double rate=48000;
    for(int variant=0;variant<3;++variant){
        ref::RefParametricEQ r;tt::ParametricEQ c;auto b=bandsA();
        if(variant==1){for(auto& x:b)x.gain=0;}                              // default-like: 0 dB bands (exact identities)
        if(variant==2){b[1].gain=0;b[4]={true,tt::Bell,0,2500,0,.71};}      // identities between real bands
        r.update(b,true,rate);c.update(b,true,rate);
        const auto x=signal(40000,17);
        for(size_t i=0;i<x.size();++i){
            if(i==10000){b[0].gain=b[0].gain==0?3:0;r.update(b,true,rate);c.update(b,true,rate);}   // ramps
            if(i==20000){r.update(b,false,rate);c.update(b,false,rate);}                             // blend out, stages release
            if(i==26000){r.update(b,true,rate);c.update(b,true,rate);}
            if(i==31000){r.clearState();c.clearState();}
            float rl=x[i][0],rr=x[i][1],cl=rl,cr=rr;r.process(rl,rr);c.process(cl,cr);
            check(same(rl,cl)&&same(rr,cr),"ParametricEQ differs from v0.5.9 (variant "+std::to_string(variant)+") at sample "+std::to_string(i));
        }
        // Voice-local copies (followTargets / settleTargets) behave the same too.
        tt::ParametricEQ cv;ref::RefParametricEQ rv;cv.followTargets(c);rv.followTargets(r);cv.settleTargets();rv.settleTargets();
        tt::ParametricEQ copy=cv;
        for(size_t i=0;i<4000;++i){float rl=x[i][0],rr=x[i][1],cl=rl,cr=rr,ql=rl,qr=rr;rv.process(rl,rr);cv.process(cl,cr);
            check(same(rl,cl)&&same(rr,cr),"followTargets/settleTargets path differs");
            if(i<2000){copy.process(ql,qr);}}
    }
}
int main(){
    try{convolver();noteFilter();bassMono();reverb();eq();}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
    std::cout<<"PASS v0.5.10 CPU optimizations are bit-identical to v0.5.9: linear-phase convolver (lazy, silent skip, kernel ramps/swaps, sub mono), "
               "note filter (real-time ring-out pool incl. exhausted pool), linear bass mono, generated reverb (Legacy and V2), parametric EQ\n";
    return 0;
}
