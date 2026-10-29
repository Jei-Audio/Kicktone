#pragma once
// v0.5.26 REFERENCE: Source/ExtremeV5.h exactly as shipped in v0.5.25 (only this
// comment, the include path and the namespace differ). ExtremeV5Tests requires the
// faster v0.5.26 search to give BIT-IDENTICAL notes. Never edit this file to make
// that test pass: it defines the sound of Extreme v5.
// Extreme v5 tail engine (v0.4.68) — cycle-resequencing resample engine for
// distorted hardstyle / hardcore kick tails.
//
// Same foundation as Highest Fidelity v4 (KickCharacterPitch.h), because it
// meets the sound rules: the transposition is ONE r8brain conversion of the
// whole tail, every output cycle is a verbatim copy of a real source cycle
// (its exact shape, crunch, noise and harmonic phase), the duration is kept
// by occasionally repeating (pitch up) or skipping (pitch down) whole cycles.
// No cycle morphing, no noise/body split, no generated noise, no EQ, no
// automatic brightness change. Root at 100 % returns the source untouched.
//
// What Extreme v5 does differently - all of it about making the NON-verbatim
// part of the output (the handovers between two different cycles) smaller
// and better placed:
//   * 8x resolution instead of 4x: joins are aligned to 1/8 sample.
//   * A time map (output progress -> source progress) decides WHICH cycle
//     is played next. A repeat is not a literal copy of the cycle just
//     played: the partner is chosen from the few source cycles around the
//     ideal position (previous, same, next), so the evolution of the tail
//     keeps moving through a repeat, and jumps are spread evenly (no new
//     rhythmic pattern).
//   * The partner cycle is the one of those candidates that joins best: its
//     lag is refined by a full-band, stereo-linked cross-correlation over a
//     whole cycle, then the join point inside the cycle is the place of least
//     full-band mismatch over half a cycle.
//   * The handover is as short as the pair allows: 4 % of a cycle (at least
//     1 ms) for near-identical cycles, up to 30 % for rough pairs (HF v4:
//     8-45 %), with
//     the same measured-correlation level compensation so uncorrelated
//     crunch does not dip. Rough pairs are deferred to a better cycle while
//     the timing allows it.
// Left/right use identical decisions, so the stereo image is locked. The
// duration is set by Tail Stretch (80-150 %) and Adaptive Stretch only.
// Everything runs on the preparation worker, never in the audio callback.

#include "../Source/PitchShift.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pt::ref0525::extreme5 {
using Cancel = std::function<bool()>;
inline void checkpoint(const Cancel& cancel) {
    if(cancel && cancel())throw std::runtime_error("Extreme v5 preparation cancelled");
}
constexpr double resolution = 8.0;

struct Prepared {
    std::vector<double> marks;   // source cycle starts, native samples (sorted)
    double fundamentalHz = 0;
    double stretch = 1.0;
    size_t sourceFrames = 0;
    double sourceRate = 0;
};

// Diagnostics for tests and the status line.
struct Join { double outputFrame, period, lag, fade, mismatch, correlation; int fromCycle, toCycle; };
struct RenderStats {
    int repeats = 0, skips = 0;
    double meanCrossfadeCycles = 0, worstMismatch = 0;
    double verbatimShare = 1.0;        // output samples that are exact copies of the resampled source
    std::vector<Join> joins;
};

inline Prepared prepare(const Sample& input,const cycle::Analysis& analysis,double stretch=1.0,const Cancel& cancel={}) {
    if(input.left.empty() || input.left.size()!=input.right.size() || !std::isfinite(input.rate) || input.rate<8000 ||
       !std::isfinite(stretch) || stretch<.8 || stretch>1.5)
        throw std::invalid_argument("Invalid Extreme v5 source/stretch");
    if(analysis.marks.size()<3 || !std::isfinite(analysis.fundamentalHz) || analysis.fundamentalHz<=0)
        throw std::invalid_argument("Extreme v5 needs a cycle map");
    checkpoint(cancel);
    Prepared p; p.fundamentalHz=analysis.fundamentalHz; p.stretch=stretch;
    p.sourceFrames=input.left.size(); p.sourceRate=input.rate;
    for(double m:analysis.marks)if(std::isfinite(m) && m>=0 && m<double(input.left.size()))p.marks.push_back(m);
    std::sort(p.marks.begin(),p.marks.end());
    // Reject marks closer than a quarter of the nominal period (not a cycle).
    const double nominal=input.rate/analysis.fundamentalHz;
    std::vector<double> clean;
    for(double m:p.marks)if(clean.empty() || m-clean.back()>nominal*.25)clean.push_back(m);
    p.marks=std::move(clean);
    if(p.marks.size()<3)throw std::invalid_argument("Extreme v5 needs a cycle map");
    return p;
}

namespace detail {
struct Stream {
    const std::vector<float>* l; const std::vector<float>* r;
    long n;
    float L(long i) const { return i>=0 && i<n ? (*l)[size_t(i)] : 0.f; }
    float R(long i) const { return i>=0 && i<n ? (*r)[size_t(i)] : 0.f; }
};
// Full-band, stereo-linked normalized similarity of [a,a+w) and [b,b+w).
inline double similarity(const Stream& s,long a,long b,long w,long stride=1) {
    double ab=0,aa=0,bb=0;
    for(long k=0;k<w;k+=stride){
        const double l0=s.L(a+k),l1=s.L(b+k),r0=s.R(a+k),r1=s.R(b+k);
        ab+=l0*l1+r0*r1;aa+=l0*l0+r0*r0;bb+=l1*l1+r1*r1;
    }
    return aa>1e-12 && bb>1e-12 ? ab/std::sqrt(aa*bb) : 0.0;
}
// Refine the lag of partner position b against a over a window w, within
// +-range stream samples: coarse pass every 2 native samples, then every
// 2/8 sample around the best coarse lag, then the 1/8 neighbours.
inline long refine(const Stream& s,long a,long b,long w,long range,long step) {
    // Coarse pass: every 2 native samples, window sampled every 4th stream
    // sample (the 8x stream is heavily oversampled, so the correlation
    // surface is smooth at that stride). Then every 2/8 sample at stride 2,
    // then the 1/8 neighbours at full resolution.
    long best=b;double bestSim=-2;
    const long coarseStep=2*step;
    for(long d=-range;d<=range;d+=coarseStep){const double v=similarity(s,a,b+d,w,4);if(v>bestSim){bestSim=v;best=b+d;}}
    long centre=best;bestSim=-2;
    for(long d=-coarseStep;d<=coarseStep;d+=2){const double v=similarity(s,a,centre+d,w,2);if(v>bestSim){bestSim=v;best=centre+d;}}
    centre=best;bestSim=-2;
    for(long d=-2;d<=2;++d){const double v=similarity(s,a,centre+d,w);if(v>bestSim){bestSim=v;best=centre+d;}}
    return best;
}
} // namespace detail

inline Sample render(const Sample& original,double semitones,const Prepared& p,const Cancel& cancel={},
                     RenderStats* stats=nullptr,double stretchOverride=0.0) {
    checkpoint(cancel);
    if(!std::isfinite(semitones) || original.rate!=p.sourceRate || original.left.size()!=p.sourceFrames ||
       original.right.size()!=p.sourceFrames)
        throw std::invalid_argument("Invalid Extreme v5 pitch/source");
    const double stretch=stretchOverride>0?stretchOverride:p.stretch;
    if(!std::isfinite(stretch) || stretch<(stretchOverride>0?.2:.8) || stretch>1.5)throw std::invalid_argument("Tail stretch must be 80-150 percent");
    if(std::abs(semitones)<1.e-5 && std::abs(stretch-1.)<1.e-9)return original; // exact root
    const double ratio=std::pow(2.0,semitones/12.0);
    if(ratio<1.0/16.0 || ratio>16.0)throw std::invalid_argument("Pitch exceeds Extreme v5 range");

    // One conversion: 8x resolution AND transposition together. Stream index q
    // corresponds to source sample q*ratio/resolution.
    std::vector<float> rl=cycle::resample(original.left,original.rate,ratio/resolution);
    checkpoint(cancel);
    std::vector<float> rr=cycle::resample(original.right,original.rate,ratio/resolution);
    checkpoint(cancel);
    const detail::Stream s{&rl,&rr,long(rl.size())};
    const long streamLength=s.n;
    const double toStream=resolution/ratio;
    // Cycle starts in stream samples; the pre-mark region is cycle 0 and the
    // stream end closes the last cycle.
    // A mark within a quarter cycle of the stream end would leave a stub cycle
    // that is not a cycle (v0.4.68 fix: "vector too long" on a kick whose last
    // mark sat 28 samples before the end); it is folded into the last cycle.
    const long nominal=(std::max)(long(resolution*2),long(std::llround(p.sourceRate/p.fundamentalHz*toStream)));
    std::vector<long> M;M.reserve(p.marks.size()+2);
    if(p.marks.front()>0.5)M.push_back(0);
    for(double m:p.marks){const long v=long(std::llround(m*toStream));if(v<streamLength-nominal/4 && (M.empty() || v>M.back()))M.push_back(v);}
    if(M.empty() || M.back()<streamLength-1)M.push_back(streamLength);
    const long cycles=long(M.size())-1;
    if(cycles<2)throw std::invalid_argument("Extreme v5 needs a cycle map");
    const auto cycleAt=[&](long q){   // index of the cycle containing stream position q
        auto it=std::upper_bound(M.begin(),M.end(),q);
        long k=long(it-M.begin())-1;return std::clamp(k,0L,cycles-1);
    };
    const size_t outFrames=size_t(std::llround(double(p.sourceFrames)*stretch));
    const long outTotal=long(std::llround(double(outFrames)*resolution))+long(resolution);
    std::vector<float> ol(size_t(outTotal),0.f),orr(size_t(outTotal),0.f);
    // Time map: output stream sample o should come from source stream position
    // d(o) = o/(stretch*ratio).
    const double drift=1.0/(stretch*ratio);
    const auto ideal=[&](long o){return double(o)*drift;};

    long q=0,o=0,i=0;int repeats=0,skips=0;double fadeCycles=0,worst=0;long blended=0;
    long sinceJoin=(std::numeric_limits<long>::max)()/2;   // stream samples played since the last join
    while(o<outTotal) {
        if((o&16383)==0)checkpoint(cancel);
        if(q>=streamLength)break;   // nothing left to copy (never happens after a valid jump)
        i=cycleAt(q);
        const long cycleEnd=M[i+1];
        const long remaining=cycleEnd-q;
        const long period=(std::max)(long(resolution*2),M[i+1]-M[i]);
        // The last mapped cycle: jump back only if more output is still needed.
        const bool last=i+1>=cycles && outTotal-o>remaining;
        // Where will the source be, relative to the ideal, at the end of this cycle?
        const double errorAhead=double(cycleEnd)-ideal(o+remaining);
        // Hysteresis: at least one whole cycle plays verbatim between joins
        // unless the error has grown past a full cycle (no back-and-forth).
        const bool needJump=last || (std::abs(errorAhead)>0.5*double(period) &&
                                     (sinceJoin>=period || std::abs(errorAhead)>double(period)));
        if(!needJump) {
            const long n=(std::min)(remaining,outTotal-o);
            for(long k=0;k<n;++k){ol[size_t(o+k)]=s.L(q+k);orr[size_t(o+k)]=s.R(q+k);}
            o+=n;q+=n;sinceJoin+=n;continue;
        }
        // Candidate partner cycles around the ideal source position at this
        // output time. The partner replaces the cycle that would follow (i+1).
        const double target=ideal(o+remaining);
        const long jc=cycleAt(long(std::llround(std::clamp(target,0.0,double(streamLength-1)))));
        long bestJ=-1,bestStart=0;double bestScore=(std::numeric_limits<double>::max)(),bestSim=0;
        for(long j=jc-1;j<=jc+1;++j) {
            if(j<0 || j>=cycles || j==i+1)continue;
            if(M[j+1]-M[j]<period/2 || M[j+1]-M[j]>period*2)continue;     // not a comparable cycle
            // Align the partner to this cycle using the cycle BEFORE the join
            // (always real content, also at the end of the stream): full-band
            // correlation over the cycle, refined to 1/8 sample within +-1/4 cycle.
            const long w=period;
            const long guess=M[j]-period;                                   // partner's preceding cycle
            if(guess<0)continue;
            const long aligned=detail::refine(s,M[i],guess,w,(std::max)(long(resolution),period/4),long(resolution))+period;
            if(aligned+period>streamLength)continue;                        // the landing cycle must be complete
            const double sim=detail::similarity(s,M[i],aligned-period,w);
            // The jump must clearly reduce the timing error (else it is not a
            // correction but a wobble); among alike joins prefer the cycle
            // nearest the ideal position.
            const double errorAfter=double(aligned)-target;
            if(!last && std::abs(errorAfter)>std::abs(errorAhead)-0.25*double(period))continue;
            const double distance=std::abs(errorAfter)/double(period);
            const double score=(1.0-sim)+0.1*distance;
            if(score<bestScore){bestScore=score;bestJ=j;bestStart=aligned;bestSim=sim;}
        }
        if(bestJ<0) {
            // Nothing to jump to (only at the very end): finish with silence-free
            // verbatim copy of what is left.
            const long n=(std::min)(remaining,outTotal-o);
            for(long k=0;k<n;++k){ol[size_t(o+k)]=s.L(q+k);orr[size_t(o+k)]=s.R(q+k);}
            o+=n;q+=n;sinceJoin+=n;if(q>=streamLength)break;continue;
        }
        // Partner offset: stream index x of this cycle corresponds to x+D in the partner.
        const long D=bestStart-cycleEnd;
        // Join point inside this cycle: least full-band mismatch over half a cycle.
        const long probe=(std::max)(long(resolution*2),period/3);
        long bestAt=0;double bestMismatch=(std::numeric_limits<double>::max)();
        {
            const long span=remaining+probe;
            std::vector<double> mis(size_t(span)+1,0.0),en(size_t(span)+1,0.0);
            for(long k=0;k<span;++k){
                const double a0=s.L(q+k),b0=s.L(q+k+D),a1=s.R(q+k),b1=s.R(q+k+D);
                mis[size_t(k)+1]=mis[size_t(k)]+(a0-b0)*(a0-b0)+(a1-b1)*(a1-b1);
                en[size_t(k)+1]=en[size_t(k)]+a0*a0+b0*b0+a1*a1+b1*b1;
            }
            for(long k=0;k<remaining;++k){
                const double m=mis[size_t(k+probe)]-mis[size_t(k)],e=en[size_t(k+probe)]-en[size_t(k)];
                const double v=e>1e-12?m/e:0.0;
                if(v<bestMismatch){bestMismatch=v;bestAt=k;}
            }
        }
        // Rough pair: wait for a better cycle while the timing still allows it.
        if(bestMismatch>0.2 && std::abs(errorAhead)<0.85*double(period) && !last) {
            const long n=(std::min)(remaining,outTotal-o);
            for(long k=0;k<n;++k){ol[size_t(o+k)]=s.L(q+k);orr[size_t(o+k)]=s.R(q+k);}
            o+=n;q+=n;sinceJoin+=n;continue;
        }
        // Verbatim up to the join.
        const long pre=(std::min)(bestAt,outTotal-o);
        for(long k=0;k<pre;++k){ol[size_t(o+k)]=s.L(q+k);orr[size_t(o+k)]=s.R(q+k);}
        o+=pre;q+=pre;
        if(o>=outTotal)break;
        // Handover: 4 % of a cycle (never under 1 ms) for near-identical cycles,
        // up to 30 % for rough ones.
        const double roughness=std::clamp(bestMismatch/0.3,0.0,1.0);
        const long minFade=long(std::llround(original.rate*.001*resolution));
        const long fade=(std::max)(minFade,long(double(period)*(0.04+0.26*roughness)));
        const double rho=std::clamp(detail::similarity(s,q,q+D,fade),-1.0,1.0);
        const long n=(std::min)(fade,outTotal-o);
        for(long k=0;k<n;++k){
            const double u=(double(k)+.5)/double(fade);
            const double b=.5-.5*std::cos(pt::pi*u),a=1.0-b;
            const double power=a*a+b*b+2.0*a*b*(std::max)(0.0,rho);
            const double g=1.0/std::sqrt((std::max)(0.5,power));
            ol[size_t(o+k)]=float(g*(a*s.L(q+k)+b*s.L(q+k+D)));
            orr[size_t(o+k)]=float(g*(a*s.R(q+k)+b*s.R(q+k+D)));
        }
        blended+=n;
        if(stats)stats->joins.push_back({double(o)/resolution,double(period)/resolution,double(D)/resolution,double(fade)/resolution,bestMismatch,rho,int(i),int(bestJ)});
        if(D<0)++repeats;else ++skips;
        fadeCycles+=double(fade)/double(period);worst=(std::max)(worst,bestMismatch);
        o+=n;q+=n+D;sinceJoin=0;
        (void)bestSim;
    }
    checkpoint(cancel);
    Sample out;out.rate=original.rate;
    out.left=cycle::resample(ol,original.rate*resolution,resolution);
    checkpoint(cancel);
    out.right=cycle::resample(orr,original.rate*resolution,resolution);
    out.left.resize(outFrames,0.f);out.right.resize(outFrames,0.f);
    if(stats){stats->repeats=repeats;stats->skips=skips;const int n=repeats+skips;
        stats->meanCrossfadeCycles=n?fadeCycles/n:0;stats->worstMismatch=worst;
        stats->verbatimShare=1.0-double(blended)/double((std::max)(1L,outTotal));}
    checkpoint(cancel);
    return out;
}
} // namespace pt::ref0525::extreme5
