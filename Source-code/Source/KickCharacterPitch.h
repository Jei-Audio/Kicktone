#pragma once
// Kick Character engine (v0.4.48) — a resample + cycle-splice tail engine
// written for distorted hardstyle/hardcore kick tails.
//
// Idea
// ----
// The best possible "pitched kick" for a distorted tail is the tail itself,
// resampled: every cycle keeps its exact shape, crunch, noise and harmonic
// phase relationships, just shorter or longer. The only problem with plain
// resampling is that the duration changes. Extreme and older engines fix the
// duration by continuously morphing between neighbouring cycles at every
// output sample. That morph averages two different cycles, and whatever is
// not identical between them (crunch, noise-like distortion, evolving
// harmonics) partly cancels — the waveform gets smoother and weaker.
//
// Kick Character never morphs. It plays the resampled tail as one continuous
// stream and corrects the duration only by occasionally repeating (pitch up)
// or skipping (pitch down) ONE whole cycle. Between those splices the output
// IS the resampled source, sample for sample. Each splice is:
//   * period-exact: the jump length is the local cycle period, refined by a
//     full-band, stereo-linked cross-correlation (not just the fundamental),
//   * placed where the two cycles agree best across the whole spectrum,
//   * crossfaded only as long as that kick needs: very similar cycles get a
//     short handover, rough/noisy cycles a longer one (adaptive per splice),
//   * level-compensated using the measured correlation, so uncorrelated
//     crunch does not dip in level during the handover.
// Left/right use identical splice decisions, so the stereo image is locked.
// The full tail is processed together — no noise/body split, no generated
// noise, no EQ, no automatic brightness correction. All work happens at 4x
// resolution for sub-sample splice alignment; the transposition itself is a
// single r8brain conversion, so there is no per-sample cubic interpolation.
//
// The duration is fixed by the Tail Stretch setting only (80–150%). Root note
// at 100% returns the exact source. Everything runs on the preparation
// worker, never in the audio callback.

#include "PitchShift.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pt::character {
using Cancel = std::function<bool()>;
inline void checkpoint(const Cancel& cancel) {
    if(cancel && cancel())throw std::runtime_error("Kick Character preparation cancelled");
}
constexpr double resolution = 4.0;

struct Prepared {
    std::vector<double> marks;   // source cycle starts, native samples
    double fundamentalHz = 0;
    double stretch = 1.0;
    size_t sourceFrames = 0;
    double sourceRate = 0;
};

// Diagnostics for tests and the status line.
struct Splice { double outputFrame, period, lag, fade, mismatch, correlation; };
struct RenderStats { int repeats = 0, skips = 0; double meanCrossfadeCycles = 0, worstMismatch = 0; std::vector<Splice> splices; };

inline Prepared prepare(const Sample& input,const cycle::Analysis& analysis,double stretch=1.0,const Cancel& cancel={}) {
    if(input.left.empty() || input.left.size()!=input.right.size() || !std::isfinite(input.rate) || input.rate<8000 ||
       !std::isfinite(stretch) || stretch<.8 || stretch>1.5)
        throw std::invalid_argument("Invalid Kick Character source/stretch");
    if(analysis.marks.size()<3 || !std::isfinite(analysis.fundamentalHz) || analysis.fundamentalHz<=0)
        throw std::invalid_argument("Kick Character needs a cycle map");
    checkpoint(cancel);
    Prepared p; p.marks=analysis.marks; p.fundamentalHz=analysis.fundamentalHz; p.stretch=stretch;
    p.sourceFrames=input.left.size(); p.sourceRate=input.rate;
    std::sort(p.marks.begin(),p.marks.end());
    return p;
}

namespace detail {
// Local source period (native samples) around a source position.
inline double localPeriod(const Prepared& p,double sourcePos) {
    const auto& m=p.marks;
    auto it=std::upper_bound(m.begin(),m.end(),sourcePos);
    size_t k=it==m.begin()?0:size_t(it-m.begin()-1);
    if(k+1>=m.size())k=m.size()-2;
    const double fallback=p.sourceRate/p.fundamentalHz;
    const double span=m[k+1]-m[k];
    return std::isfinite(span) && span>fallback*.5 && span<fallback*2.0 ? span : fallback;
}
struct Stream {
    const std::vector<float>* l; const std::vector<float>* r;
    float L(long i) const { return i>=0 && size_t(i)<l->size() ? (*l)[size_t(i)] : 0.f; }
    float R(long i) const { return i>=0 && size_t(i)<r->size() ? (*r)[size_t(i)] : 0.f; }
};
// Full-band, stereo-linked normalized similarity of [a,a+w) and [b,b+w).
// v0.4.65: the three sums are accumulated over exactly the same terms, in the
// same ascending-k order, as the original bounds-checked loop (out-of-range
// stream samples contribute exact zeros, which are skipped instead of added),
// so the result is bit-identical while the hot loop has no per-sample branch.
inline double similarity(const Stream& s,long a,long b,long w,long step=1) {
    double ab=0,aa=0,bb=0;
    const long n=long(s.l->size());
    if(step<1)step=1;
    if(s.r->size()!=s.l->size()) {   // never the case for a rendered stream; keep the generic form
        for(long k=0;k<w;k+=step) {
            const double x0=s.L(a+k),y0=s.L(b+k),x1=s.R(a+k),y1=s.R(b+k);
            ab+=x0*y0+x1*y1; aa+=x0*x0+x1*x1; bb+=y0*y0+y1*y1;
        }
        if(aa<1e-12 || bb<1e-12)return aa<1e-12 && bb<1e-12 ? 1.0 : 0.0;
        return ab/std::sqrt(aa*bb);
    }
    // k in [0,w) on the step grid; a+k valid for k in [aLo,aHi), b+k for [bLo,bHi).
    const long aLo=(std::max)(0L,-a),aHi=(std::min)(w,n-a);
    const long bLo=(std::max)(0L,-b),bHi=(std::min)(w,n-b);
    const auto grid=[step](long lo){return lo<=0?0L:((lo+step-1)/step)*step;};
    const float* L=s.l->data(); const float* R=s.r->data();
    const long iLo=(std::max)(aLo,bLo),iHi=(std::min)(aHi,bHi);
    if(iLo<iHi) {
        for(long k=grid(aLo);k<iLo;k+=step){const double x0=L[a+k],x1=R[a+k];aa+=x0*x0+x1*x1;}
        for(long k=grid(bLo);k<iLo;k+=step){const double y0=L[b+k],y1=R[b+k];bb+=y0*y0+y1*y1;}
        for(long k=grid(iLo);k<iHi;k+=step) {
            const double x0=L[a+k],y0=L[b+k],x1=R[a+k],y1=R[b+k];
            ab+=x0*y0+x1*y1; aa+=x0*x0+x1*x1; bb+=y0*y0+y1*y1;
        }
        for(long k=grid(iHi);k<aHi;k+=step){const double x0=L[a+k],x1=R[a+k];aa+=x0*x0+x1*x1;}
        for(long k=grid(iHi);k<bHi;k+=step){const double y0=L[b+k],y1=R[b+k];bb+=y0*y0+y1*y1;}
    } else {
        for(long k=grid(aLo);k<aHi;k+=step){const double x0=L[a+k],x1=R[a+k];aa+=x0*x0+x1*x1;}
        for(long k=grid(bLo);k<bHi;k+=step){const double y0=L[b+k],y1=R[b+k];bb+=y0*y0+y1*y1;}
    }
    if(aa<1e-12 || bb<1e-12)return aa<1e-12 && bb<1e-12 ? 1.0 : 0.0;
    return ab/std::sqrt(aa*bb);
}
// Refine a splice lag around the expected cycle length: coarse (step 4) then
// exact search. Uses the whole band so crunch and harmonics line up too.
inline long refineLag(const Stream& s,long at,long expected,int direction) {
    const long w=(std::max)(32L,expected/2);
    const long radius=(std::max)(4L,long(expected*.06));
    const auto score=[&](long lag,long step){
        return direction<0 ? similarity(s,at,at-lag,w,step) : similarity(s,at,at+lag,w,step);
    };
    long best=expected; double bestScore=-2;
    for(long lag=expected-radius;lag<=expected+radius;lag+=4){
        if(lag<8)continue; const double v=score(lag,4); if(v>bestScore){bestScore=v;best=lag;}
    }
    const long centre=best; bestScore=-2;
    for(long lag=centre-4;lag<=centre+4;++lag){
        if(lag<8)continue; const double v=score(lag,1); if(v>bestScore){bestScore=v;best=lag;}
    }
    return best;
}
} // namespace detail

// Render one note. semitones: transposition; stretchOverride>0 replaces the
// prepared stretch (kept for API symmetry with Extreme; 80–150% only).
// v0.4.63: periodAt (optional) supplies the local source period in native
// samples at a source position. The Punch Highest Fidelity mode uses it for
// punches whose pitch sweeps far beyond one cycle map. Empty = the prepared
// marks, exactly as before (the tail engine never passes it).
inline Sample render(const Sample& original,double semitones,const Prepared& p,const Cancel& cancel={},
                     RenderStats* stats=nullptr,double stretchOverride=0.0,
                     const std::function<double(double)>& periodAt={}) {
    checkpoint(cancel);
    if(!std::isfinite(semitones) || original.rate!=p.sourceRate || original.left.size()!=p.sourceFrames ||
       original.right.size()!=p.sourceFrames)
        throw std::invalid_argument("Invalid Kick Character pitch/source");
    const double stretch=stretchOverride>0?stretchOverride:p.stretch;
    // Adaptive Stretch (v0.4.49) passes its per-note duration as an override,
    // which may shorten down to 20% (25% of the 80% base stretch).
    if(!std::isfinite(stretch) || stretch<(stretchOverride>0?.2:.8) || stretch>1.5)throw std::invalid_argument("Tail stretch must be 80-150 percent");
    if(std::abs(semitones)<1.e-5 && std::abs(stretch-1.)<1.e-9)return original; // exact root
    const double ratio=std::pow(2.0,semitones/12.0);
    if(ratio<1.0/16.0 || ratio>16.0)throw std::invalid_argument("Pitch exceeds Kick Character range");

    // One high-quality conversion: 4x resolution AND transposition together.
    // Stream index q corresponds to source time q*ratio/resolution.
    std::vector<float> rl=cycle::resample(original.left,original.rate,ratio/resolution);
    checkpoint(cancel);
    std::vector<float> rr=cycle::resample(original.right,original.rate,ratio/resolution);
    checkpoint(cancel);
    const detail::Stream s{&rl,&rr};
    const long streamLength=long(rl.size());
    const double toStream=resolution/ratio;              // source samples -> stream samples
    const size_t outFrames=size_t(std::llround(double(p.sourceFrames)*stretch));
    const long out4=long(std::llround(double(outFrames)*resolution))+8;
    std::vector<float> ol(size_t(out4),0.f),orr(size_t(out4),0.f);

    // Read pointer q advances one stream sample per output sample. The ideal
    // (duration-correct) position is d(o)=o/(stretch*ratio). Splices keep q
    // within about half a cycle of d(o).
    long q=0; long o=0; int repeats=0,skips=0; double fadeCycles=0, worst=0;
    const double drift=1.0/(stretch*ratio); // ideal stream samples per output sample
    while(o<out4) {
        if((o&16383)==0)checkpoint(cancel);
        const double ideal=double(o)*drift;
        const double sourcePos=double(q)/toStream;
        const double localP=periodAt ? periodAt(sourcePos) : detail::localPeriod(p,sourcePos);
        const long period=(std::max)(16L,long(std::llround(localP*toStream)));
        const double error=double(q)-ideal;
        // Look one cycle ahead: by the end of the coming cycle, will we be more
        // than half a cycle away from the ideal position?
        const double errorAhead=error+double(period)*(1.0-drift);
        // Whole cycles to repeat (negative) or skip (positive). Large
        // transpositions can need more than one cycle per splice.
        long cycles=long(std::llround(-errorAhead/double(period)));
        // Never run out of source before the fixed output length is filled:
        // near the end, repeat the final cycle instead of reading silence.
        if(cycles>=0 && q+period>=streamLength-period && out4-o>streamLength-q)cycles=-1;
        const int direction=cycles<0?-1:cycles>0?1:0;
        const long jump=std::labs(cycles)*period;
        if(direction==0 || (direction<0 && q-jump-period<0) || (direction>0 && q+jump+2*period>=streamLength)) {
            // No splice needed in this cycle: copy the resampled source verbatim.
            const long n=(std::min)(long(period),out4-o);
            for(long k=0;k<n;++k){ol[size_t(o+k)]=s.L(q+k);orr[size_t(o+k)]=s.R(q+k);}
            o+=n;q+=n;continue;
        }
        const long lag=detail::refineLag(s,q,jump,direction);
        const long partner=direction<0?-lag:lag;
        // Choose where inside this cycle to splice: minimum full-band mismatch
        // between the current stream and the partner stream over a probe window.
        const long probe=(std::max)(16L,period/4);
        long bestAt=0; double bestMismatch=(std::numeric_limits<double>::max)();
        {
            // Prefix sums of mismatch and energy across [q, q+period+probe).
            const long span=period+probe;
            std::vector<double> mis(size_t(span)+1,0.0),en(size_t(span)+1,0.0);
            for(long k=0;k<span;++k){
                const double a0=s.L(q+k),b0=s.L(q+k+partner),a1=s.R(q+k),b1=s.R(q+k+partner);
                mis[size_t(k)+1]=mis[size_t(k)]+(a0-b0)*(a0-b0)+(a1-b1)*(a1-b1);
                en[size_t(k)+1]=en[size_t(k)]+a0*a0+b0*b0+a1*a1+b1*b1;
            }
            for(long k=0;k<period;++k){
                const double m=mis[size_t(k+probe)]-mis[size_t(k)],e=en[size_t(k+probe)]-en[size_t(k)];
                const double v=e>1e-12?m/e:0.0;
                if(v<bestMismatch){bestMismatch=v;bestAt=k;}
            }
        }
        // Adaptive deferral: if this cycle pair is rough (cycles disagree) and
        // timing still allows it, wait for a cycle that matches better.
        if(bestMismatch>0.22 && std::abs(errorAhead)<double(period)*0.75 && std::labs(cycles)==1 &&
           q+3*period<streamLength) {
            const long n=(std::min)(long(period),out4-o);
            for(long k=0;k<n;++k){ol[size_t(o+k)]=s.L(q+k);orr[size_t(o+k)]=s.R(q+k);}
            o+=n;q+=n;continue;
        }
        // Copy verbatim up to the splice.
        const long pre=(std::min)(bestAt,out4-o);
        for(long k=0;k<pre;++k){ol[size_t(o+k)]=s.L(q+k);orr[size_t(o+k)]=s.R(q+k);}
        o+=pre;q+=pre;
        if(o>=out4)break;
        // Adaptive handover length: 8% of a cycle for near-identical cycles,
        // up to 45% for rough or evolving material. 0 mismatch = identical.
        const double roughness=std::clamp(bestMismatch/0.35,0.0,1.0);
        const long fade=(std::max)(24L,long(double(period)*(0.08+0.37*roughness)));
        // Measured correlation over the handover controls level compensation.
        const double rho=std::clamp(detail::similarity(s,q,q+partner,fade),-1.0,1.0);
        const long n=(std::min)(fade,out4-o);
        for(long k=0;k<n;++k){
            const double u=(double(k)+.5)/double(fade);
            const double b=.5-.5*std::cos(pt::pi*u), a=1.0-b;   // raised cosine, a+b=1
            const double power=a*a+b*b+2.0*a*b*(std::max)(0.0,rho);
            const double g=1.0/std::sqrt((std::max)(0.5,power)); // 1 for identical cycles
            ol[size_t(o+k)]=float(g*(a*s.L(q+k)+b*s.L(q+k+partner)));
            orr[size_t(o+k)]=float(g*(a*s.R(q+k)+b*s.R(q+k+partner)));
        }
        if(stats)stats->splices.push_back({double(o)/resolution,double(period)/resolution,double(partner)/resolution,double(fade)/resolution,bestMismatch,rho});
        o+=n;q+=n+partner;
        if(direction<0)repeats+=int(std::labs(cycles)); else skips+=int(cycles);
        fadeCycles+=double(fade)/double(period);worst=(std::max)(worst,bestMismatch);
    }
    checkpoint(cancel);
    Sample out;out.rate=original.rate;
    out.left=cycle::resample(ol,original.rate*resolution,resolution);
    checkpoint(cancel);
    out.right=cycle::resample(orr,original.rate*resolution,resolution);
    out.left.resize(outFrames,0.f);out.right.resize(outFrames,0.f);
    if(stats){stats->repeats=repeats;stats->skips=skips;const int n=repeats+skips;
        stats->meanCrossfadeCycles=n?fadeCycles/n:0;stats->worstMismatch=worst;}
    checkpoint(cancel);
    return out;
}
} // namespace pt::character
