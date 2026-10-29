#pragma once
#include "ParametricEQ.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace tt::linear {
// Symmetric, windowed frequency-sampling FIR. Designs and allocations are
// confined to prepare/the worker. Uniform partitioning bounds callback work.
constexpr int block=128, fftSize=2*block;
template<class T> inline void fft(std::vector<std::complex<T>>& a,bool inverse) {
    const size_t n=a.size();
    for(size_t i=1,j=0;i<n;++i){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(size_t len=2;len<=n;len<<=1){const auto w=std::polar(T(1),T((inverse?2:-2)*pi/len));
        for(size_t i=0;i<n;i+=len){std::complex<T> x(1,0);for(size_t j=0;j<len/2;++j){const auto u=a[i+j],v=a[i+j+len/2]*x;a[i+j]=u+v;a[i+j+len/2]=u-v;x*=w;}}}
    if(inverse)for(auto& x:a)x/=T(n);
}
struct Kernel {
    double rate=48000;bool enabled=false,subMono=false;
    double subMonoHz=90; // v0.4.56: mono sub crossover (90..300 Hz)
    std::array<Band,kBands> bands{};
    int radius=2048;
    std::vector<float> taps,sideTaps;
    std::vector<std::vector<std::complex<float>>> spectra,sideSpectra;
    int latency() const {return radius+block-1;}
    bool matches(const std::array<Band,kBands>& b,bool on,double sr,bool mono=false,double monoHz=90) const {
        if(rate!=sr || enabled!=on || subMono!=mono)return false;
        if(subMono && std::abs(subMonoHz-monoHz)>.01)return false;
        for(int i=0;i<kBands;++i){const auto& x=b[size_t(i)];const auto& y=bands[size_t(i)];if(x.enabled!=y.enabled||x.type!=y.type||x.slope!=y.slope||x.hz!=y.hz||x.gain!=y.gain||x.q!=y.q)return false;}return true;
    }
};
constexpr double kSubMonoMinHz=90, kSubMonoMaxHz=300;
inline double clampSubMonoHz(double hz){return std::isfinite(hz)?std::clamp(hz,kSubMonoMinHz,kSubMonoMaxHz):kSubMonoMinHz;}
inline std::shared_ptr<const Kernel> design(const std::array<Band,kBands>& bands,bool enabled,double rate,bool subMono=false,double subMonoHz=90) {
    auto k=std::make_shared<Kernel>();k->rate=rate;k->bands=bands;k->enabled=enabled;k->subMono=subMono;
    subMonoHz=clampSubMonoHz(subMonoHz);k->subMonoHz=subMonoHz;
    k->radius=std::clamp(int(std::lround(2048*rate/48000.)),512,8192);
    const int length=2*k->radius+1;size_t n=1;while(n<size_t(length*4))n*=2;
    std::vector<std::complex<double>> spectrum(n);
    for(size_t i=0;i<=n/2;++i){const double magnitude=enabled?std::pow(10.,responseDb(bands,double(i)*rate/n,rate)/20.):1.;spectrum[i]=magnitude;if(i)spectrum[n-i==n?0:n-i]=magnitude;}
    auto side=spectrum;
    if(subMono)for(size_t i=0;i<n;++i){
        const double hz=double((std::min)(i,n-i))*rate/n;
        // Guard the mono stop band against finite-window leakage. A short
        // crossover transition restores side information above the sub band.
        // The 20 Hz guard and 40 Hz transition are absolute (FIR resolution is
        // fixed), so 90 Hz is sample-identical to v0.4.41..v0.4.55.
        const double position=std::clamp((hz-(subMonoHz+20.))/40.,0.,1.);
        side[i]*=.5-.5*std::cos(pi*position);
    }
    fft(spectrum,true);if(subMono)fft(side,true);k->taps.resize(size_t(length));
    if(subMono)k->sideTaps.resize(size_t(length));
    for(int i=0;i<length;++i){const int lag=i-k->radius;
        const double window=.42+.5*std::cos(pi*lag/k->radius)+.08*std::cos(2*pi*lag/k->radius);
        k->taps[size_t(i)]=float(spectrum[size_t((int(n)+lag)%int(n))].real()*window);
        if(subMono)k->sideTaps[size_t(i)]=float(side[size_t((int(n)+lag)%int(n))].real()*window);}
    // Explicit symmetry avoids round-off phase asymmetry from the large FFT.
    for(int i=0;i<k->radius;++i){const float v=.5f*(k->taps[size_t(i)]+k->taps[size_t(length-1-i)]);k->taps[size_t(i)]=k->taps[size_t(length-1-i)]=v;}
    if(subMono)for(int i=0;i<k->radius;++i){const float v=.5f*(k->sideTaps[size_t(i)]+k->sideTaps[size_t(length-1-i)]);k->sideTaps[size_t(i)]=k->sideTaps[size_t(length-1-i)]=v;}
    const size_t partitions=(size_t(length)+block-1)/block;
    k->spectra.assign(partitions,std::vector<std::complex<float>>(fftSize));
    for(size_t p=0;p<partitions;++p){auto& a=k->spectra[p];for(int i=0;i<block && p*block+size_t(i)<k->taps.size();++i)a[size_t(i)]=k->taps[p*block+size_t(i)];fft(a,false);}
    if(subMono){
        k->sideSpectra.assign(partitions,std::vector<std::complex<float>>(fftSize));
        for(size_t p=0;p<partitions;++p){auto& a=k->sideSpectra[p];for(int i=0;i<block && p*block+size_t(i)<k->sideTaps.size();++i)a[size_t(i)]=k->sideTaps[p*block+size_t(i)];fft(a,false);}
    }
    return k;
}
class Convolver {
    std::shared_ptr<const Kernel> current,previous,target;
    std::array<std::vector<std::vector<std::complex<float>>>,2> history;
    std::array<std::vector<std::complex<float>>,2> scratch,oldScratch;
    std::array<std::array<float,block>,2> before{},input{},output{},oldOutput{};
    size_t head=0;int fill=0,out=block,ramp=0,rampLength=1;
    // v0.5.10 CPU: the expensive part of a block (partition multiply-add and the
    // inverse FFTs) is computed LAZILY, the first time a sample of that block is
    // actually wanted, from exactly the state it had at the block boundary (the
    // history slots, the kernels and the ramp flag), so the numbers are the same
    // as before. The input FFT into the history still runs every block, so a
    // block that IS wanted later (mode switch) is exact without warm-up.
    // When every history slot holds a silent block the output is zero and the
    // multiply-add is skipped altogether (the generated reverb is often off or
    // silent while this FIR keeps running).
    size_t pendingSlot=0; bool pendingRamp=false, computed=true;
    // Raw pointers: current/previous own both kernels until the next boundary,
    // and swapKernel computes a pending block first, so no extra owner (and no
    // possible final release) lives on the audio thread.
    const Kernel* pendingCurrent=nullptr; const Kernel* pendingPrevious=nullptr;
    size_t silentSlots=0;   // consecutive history slots that hold an all-zero block
    void compute() {
        computed=true;
        const size_t slots=history[0].size();
        if(!pendingRamp && silentSlots>=slots) {
            for(size_t c=0;c<2;++c){output[c].fill(0.f);oldOutput[c].fill(0.f);}
            return;
        }
        const auto& cur=*pendingCurrent; const auto& prev=*pendingPrevious;
        for(size_t c=0;c<2;++c){auto& sum=scratch[c];auto& old=oldScratch[c];std::fill(sum.begin(),sum.end(),std::complex<float>{});if(pendingRamp)std::fill(old.begin(),old.end(),std::complex<float>{});
            for(size_t p=0;p<cur.spectra.size();++p){
                const size_t slot=(pendingSlot+slots-p)%slots;
                const auto& x=history[c][slot];const auto& other=history[1-c][slot];
                const auto& a=cur.spectra[p];const auto& b=prev.spectra[p];
                for(int i=0;i<fftSize;++i){const size_t f=size_t(i);
                    auto value=x[f]*a[f];
                    if(cur.subMono)value-=.5f*(x[f]-other[f])*(a[f]-cur.sideSpectra[p][f]);
                    sum[f]+=value;
                    if(pendingRamp){auto beforeValue=x[f]*b[f];if(prev.subMono)beforeValue-=.5f*(x[f]-other[f])*(b[f]-prev.sideSpectra[p][f]);old[f]+=beforeValue;}
                }
            }
            fft(sum,true);if(pendingRamp)fft(old,true);for(int i=0;i<block;++i){output[c][size_t(i)]=sum[size_t(i+block)].real();oldOutput[c][size_t(i)]=pendingRamp?old[size_t(i+block)].real():output[c][size_t(i)];}}
    }
public:
    void prepare(std::shared_ptr<const Kernel> k){current=std::move(k);previous=target=current;ramp=0;rampLength=(std::max)(1,int(current->rate*.01));
        for(auto& c:history)c.assign(current->spectra.size(),std::vector<std::complex<float>>(fftSize));for(auto& c:scratch)c.resize(fftSize);for(auto& c:oldScratch)c.resize(fftSize);clear();}
    void setKernel(const std::shared_ptr<const Kernel>& k){if(k && k->spectra.size()==current->spectra.size())target=k;}
    // v0.5.1: adopt immediately (no 10 ms ramp), history kept; used while the output is muted for a mode swap.
    void swapKernel(const std::shared_ptr<const Kernel>& k){if(k && k->spectra.size()==current->spectra.size()){if(!computed)compute();current=previous=target=k;ramp=0;}}
    void clear(){for(auto& c:history)for(auto& p:c)std::fill(p.begin(),p.end(),std::complex<float>{});before={};input={};output={};oldOutput={};head=0;fill=0;out=block;
        computed=true;silentSlots=history[0].size();}
    // v0.5.10: become what `from` would be after `calls` more zero-input samples and
    // a clear(): only the kernel/ramp bookkeeping survives a clear, and it does not
    // depend on the audio, so it is stepped without any filtering.
    void inheritAfterFlush(const Convolver& from,int64_t calls){
        current=from.current;previous=from.previous;target=from.target;ramp=from.ramp;rampLength=from.rampLength;
        int f=from.fill,o=from.out;
        for(int64_t k=0;k<calls;++k){
            if(++f==block){if(target!=current && ramp==0){previous=current;current=target;ramp=rampLength;}f=0;o=0;}
            if(o>=block)continue;
            if(ramp)--ramp;
            ++o;
        }
        clear();
    }
    // wanted=false: the caller discards this sample, so the block need not be computed (yet).
    std::array<float,2> process(float l,float r,bool wanted=true){input[0][size_t(fill)]=l;input[1][size_t(fill)]=r;
        if(++fill==block){if(target!=current && ramp==0){previous=current;current=target;ramp=rampLength;}
            // Both input FFTs must be ready before the optional stereo matrix.
            bool silent=true;
            for(size_t c=0;c<2;++c)for(int i=0;i<block&&silent;++i)if(before[c][size_t(i)]!=0.f||input[c][size_t(i)]!=0.f)silent=false;
            for(size_t c=0;c<2;++c){auto& h=history[c][head];
                if(silent)std::fill(h.begin(),h.end(),std::complex<float>{});
                else{for(int i=0;i<block;++i){h[size_t(i)]=before[c][size_t(i)];h[size_t(i+block)]=input[c][size_t(i)];}fft(h,false);}
                before[c]=input[c];}
            silentSlots=silent?silentSlots+1:0;
            pendingSlot=head;pendingRamp=ramp!=0;pendingCurrent=current.get();pendingPrevious=previous.get();computed=false;
            head=(head+1)%history[0].size();fill=0;out=0;
        }
        if(out>=block)return {};
        if(!wanted){if(ramp)--ramp;++out;return {};}
        if(!computed)compute();
        auto result=std::array<float,2>{output[0][size_t(out)],output[1][size_t(out)]};if(ramp){const float blend=float(rampLength-ramp+1)/rampLength;for(size_t c=0;c<2;++c)result[c]=oldOutput[c][size_t(out)]+blend*(result[c]-oldOutput[c][size_t(out)]);--ramp;}++out;return result;
    }
};
// Preserve the previous note's pending compensated output, but cut it exactly
// at the new note's compensated boundary. A fresh note cannot pre-ring before
// that boundary, and old feedback can never leak into the new note.
class NoteFilter {
    // v0.5.10 CPU: a note start no longer flushes the previous note's ring-out
    // (latency samples, ~45 ms of FFT work) in one burst on the audio thread.
    // The previous instance keeps ringing out IN REAL TIME, fed the same zeros in
    // the same order, while a cleared instance with the same kernel state plays the
    // new note. Every output sample is the one the burst would have stored.
    // Pool exhausted (several note starts within one latency): the oldest pending
    // ring-outs are completed into the pending ring exactly as before.
    static constexpr int kPool=4;
    std::array<Convolver,kPool> pool;
    int cur=0;
    struct Flush { int inst=-1; int64_t start=0,from=0,calls=0; };   // window [from,start+L)
    std::array<Flush,kPool> flushes{}; int flushCount=0;
    std::vector<std::array<float,2>> pending;
    int64_t time=0,validFrom=0;
    int64_t length() const {return int64_t(pending.size());}
    void materialize(Flush& f){
        const int64_t L=length();
        while(f.calls<L){const int64_t P=f.start+f.calls;const bool inWindow=P>=f.from;
            const auto x=pool[size_t(f.inst)].process(0,0,inWindow);if(inWindow)pending[size_t(P%L)]=x;++f.calls;}
    }
    bool used(int inst) const {if(inst==cur)return true;for(int i=0;i<flushCount;++i)if(flushes[size_t(i)].inst==inst)return true;return false;}
public:
    void prepare(std::shared_ptr<const Kernel> k){pending.assign(size_t(k->latency()),{});for(auto& c:pool)c.prepare(k);cur=0;flushCount=0;time=0;validFrom=0;}
    void setKernel(const std::shared_ptr<const Kernel>& k){pool[size_t(cur)].setKernel(k);}
    void swapKernel(const std::shared_ptr<const Kernel>& k){pool[size_t(cur)].swapKernel(k);}
    void startNote(){
        if(time==0){pool[size_t(cur)].clear();validFrom=length();return;}
        int free=-1;for(int i=0;i<kPool;++i)if(!used(i)){free=i;break;}
        if(free<0){   // complete every running ring-out into the pending ring (the old way)
            for(int i=0;i<flushCount;++i)materialize(flushes[size_t(i)]);
            flushCount=0;for(int i=0;i<kPool;++i)if(i!=cur){free=i;break;}
        }
        flushes[size_t(flushCount++)]={cur,time,validFrom,0};
        pool[size_t(free)].inheritAfterFlush(pool[size_t(cur)],length());
        cur=free;validFrom=time+length();
    }
    // wanted=false skips computing a sample the caller discards (state advances identically).
    std::array<float,2> process(float l,float r,bool wanted=true){
        const int64_t L=length();
        std::array<float,2> ring{};bool covered=false;
        for(int i=0;i<flushCount;){
            auto& f=flushes[size_t(i)];
            const int64_t P=f.start+f.calls;   // == time
            const bool inWindow=P>=f.from;
            const auto x=pool[size_t(f.inst)].process(0,0,inWindow&&wanted&&time<validFrom);
            if(inWindow){ring=x;covered=true;}
            if(++f.calls>=L){flushes[size_t(i)]=flushes[size_t(flushCount-1)];--flushCount;}   // finished
            else ++i;
        }
        auto x=pool[size_t(cur)].process(l,r,wanted&&time>=validFrom);
        auto& old=pending[size_t(time%L)];
        if(time<validFrom)x=covered?ring:old;
        old={};++time;return x;
    }
};
} // namespace tt::linear
