#pragma once
#include "PitchShift.h"
#include "KickCharacterPitch.h"

namespace pt {
enum class PunchPitchMode { StretchAligned=0, Resample=1, HighestFidelity=2 };
// Pure band-limited resampling: speed and pitch change together. No cycle
// reconstruction, grain alignment or duration compensation.
inline Sample resamplePunchPitch(const Sample& input,double semitones) {
    if(!std::isfinite(semitones) || semitones < -144 || semitones > 144 ||
       !std::isfinite(input.rate) || input.rate<8000 || input.left.empty() ||
       input.left.size()!=input.right.size())throw std::invalid_argument("Invalid punch pitch/source");
    if(std::abs(semitones)<1.e-6)return input;
    const double ratio=std::pow(2.,semitones/12.);
    if(input.left.size()/ratio > 5000000.0)throw std::invalid_argument("Punch shift exceeds safe preparation size");
    Sample out;out.rate=input.rate;
    out.left=cycle::resample(input.left,input.rate,ratio);
    out.right=cycle::resample(input.right,input.rate,ratio);
    return out;
}

// Offline stereo-locked waveform-similarity overlap/add for short, swept or
// noisy punches. Unlike the tail renderer, no stable root note is required.
// Resample for pitch first; reconstruct the original exact frame count.
inline Sample renderHighestFidelityPunch(const Sample& input,double semitones);
inline Sample renderPunchPitch(const Sample& input,double semitones,
                               PunchPitchMode mode=PunchPitchMode::StretchAligned) {
    if(mode==PunchPitchMode::Resample)return resamplePunchPitch(input,semitones);
    if(mode==PunchPitchMode::HighestFidelity)return renderHighestFidelityPunch(input,semitones);
    if(!std::isfinite(semitones) || semitones < -144 || semitones > 144 ||
       !std::isfinite(input.rate) || input.rate<8000 || input.left.empty() ||
       input.left.size()!=input.right.size())throw std::invalid_argument("Invalid punch pitch/source");
    if(std::abs(semitones)<1.e-6)return input;
    const double ratio=std::pow(2.,semitones/12.);
    if(input.left.size()/ratio > 5000000.0)throw std::invalid_argument("Punch shift exceeds safe preparation size");
    const auto l=cycle::resample(input.left,input.rate,ratio);
    const auto r=cycle::resample(input.right,input.rate,ratio);
    Sample out;out.rate=input.rate;
    const size_t count=input.left.size();out.left.assign(count,0);out.right.assign(count,0);
    std::vector<double> weights(count,0);
    const int window=(std::max)(32,int(input.rate*.024));
    const int hop=window/4, search=(std::max)(1,int(input.rate*.004));
    const int last=(std::max)(0,int(l.size())-window);
    int previous=0;
    for(size_t dest=0;dest<count;dest+=size_t(hop)) {
        const int nominal=std::clamp(int(std::llround(double(dest)/ratio)),0,last);
        int source=nominal;
        if(dest>0 && last>0) {
            const int lo=(std::max)(previous,(std::max)(0,nominal-search));
            const int hi=(std::min)(last,nominal+search);
            double best=-2;
            auto score=[&](int at) {
                double dot=0,aa=0,bb=0;
                for(int k=0;k<window-hop && dest+size_t(k)<count;k+=4) {
                    const size_t o=dest+size_t(k),s=size_t(at+k);
                    if(weights[o]<.05 || s>=l.size())continue;
                    const double a=out.left[o]/weights[o],b=l[s];
                    const double c=out.right[o]/weights[o],d=r[s];
                    dot+=a*b+c*d;aa+=a*a+c*c;bb+=b*b+d*d;
                }
                if(aa<1.e-12 || bb<1.e-12)return -2.;
                return dot/std::sqrt(aa*bb)-.02*std::abs(at-nominal)/search;
            };
            // Coarse search followed by sample-accurate local refinement.
            for(int at=lo;at<=hi;at+=4) {const double v=score(at);if(v>best){best=v;source=at;}}
            const int centre=source;
            for(int at=(std::max)(lo,centre-3);at<=(std::min)(hi,centre+3);++at) {
                const double v=score(at);if(v>best){best=v;source=at;}
            }
        }
        previous=source;
        for(int k=0;k<window && dest+size_t(k)<count;++k) {
            // First grain is anchored at sample zero, with no added onset latency.
            const double w=dest==0 && k<window/2 ? 1. : .5-.5*std::cos(2*pi*(k+.5)/window);
            const size_t i=dest+size_t(k),s=size_t(source+k);
            if(s<l.size()){out.left[i]+=float(w*l[s]);out.right[i]+=float(w*r[s]);weights[i]+=w;}
        }
    }
    for(size_t i=0;i<count;++i)if(weights[i]>1.e-12) {
        out.left[i]=float(out.left[i]/weights[i]);out.right[i]=float(out.right[i]/weights[i]);
    }
    return out;
}
// v0.4.63 Punch HIGHEST FIDELITY: the Highest Fidelity v4 (Kick Character)
// principle applied to the punch. The punch is resampled once (r8brain, 4x
// resolution) so every cycle keeps its exact shape, click, crunch and noise;
// the original duration is restored only by repeating (pitch up) or skipping
// (pitch down) whole cycles at the best-matching points with adaptive,
// level-compensated handovers. No grain morphing or overlap/add averaging.
// A punch sweeps from a high pitch down to the body, so instead of one tail
// cycle map it uses a time-varying period track (YIN on a decimated mono
// analysis copy; the audio itself is never filtered). Frame count, timing and
// stereo are identical to Stretch Aligned; 0 st returns the exact source.
// Punches without measurable periodicity (pure noise/clicks) fall back to the
// Stretch Aligned renderer.
namespace punchfidelity {
struct PeriodTrack {
    std::vector<double> position;   // native sample of each frame centre
    std::vector<double> period;     // native samples
    double at(double pos) const {
        if(period.empty())return 0;
        if(pos<=position.front())return period.front();
        if(pos>=position.back())return period.back();
        const auto it=std::upper_bound(position.begin(),position.end(),pos);
        const size_t k=size_t(it-position.begin());
        const double u=(pos-position[k-1])/(position[k]-position[k-1]);
        return period[k-1]+(period[k]-period[k-1])*u;
    }
};
// Returns an empty track when the punch has fewer than 3 periodic frames.
// v0.5.5: voicedOut (optional) gets one flag per returned frame: measured
// (true) or filled from a neighbour (false). The track itself is unchanged.
inline PeriodTrack trackPeriods(const Sample& in,std::vector<char>* voicedOut=nullptr) {
    PeriodTrack track;
    const size_t n=in.left.size();
    const int D=(std::max)(1,int(in.rate/12000.0));
    const double rate=in.rate/D;
    std::vector<double> x(n/size_t(D),0.0);
    for(size_t i=0;i<x.size();++i){
        double sum=0;for(int k=0;k<D;++k){const size_t j=i*size_t(D)+size_t(k);sum+=.5*(double(in.left[j])+double(in.right[j]));}
        x[i]=sum/D;
    }
    const int minLag=(std::max)(2,int(rate/1200.0));
    const int maxLag=int(std::ceil(rate/25.0));
    const int W=(std::max)(16,int(rate*.012));
    const int hop=(std::max)(1,int(rate*.001));
    double peak=0;for(double v:x)peak=(std::max)(peak,std::abs(v));
    if(peak<1e-6)return track;
    std::vector<double> d(size_t(maxLag)+2,0.0),c(size_t(maxLag)+2,0.0);
    std::vector<double> raw; std::vector<char> voiced; std::vector<double> centre;
    for(size_t t=0;t+size_t(W)+size_t(2*minLag)<x.size();t+=size_t(hop)) {
        const int lagMax=(std::min)(maxLag,int(x.size()-t-size_t(W))-1);
        double e=0;for(int j=0;j<W;++j)e+=x[t+size_t(j)]*x[t+size_t(j)];
        const bool loud=std::sqrt(e/W)>peak*.003;
        double best=0,bestV=1e9;
        if(loud && lagMax>2*minLag) {
            double run=0;c[0]=1;
            for(int tau=1;tau<=lagMax;++tau){
                double sum=0;
                for(int j=0;j<W;++j){const double diff=x[t+size_t(j)]-x[t+size_t(j+tau)];sum+=diff*diff;}
                d[size_t(tau)]=sum;run+=sum;c[size_t(tau)]=run>1e-20?sum*tau/run:1.0;
            }
            // First dip below the absolute threshold (YIN); without one, the
            // first dip close to the global minimum. Taking the global minimum
            // itself would favour long sub-harmonic lags in noisy punches.
            double global=1e9;for(int tau=minLag;tau<=lagMax;++tau)global=(std::min)(global,c[size_t(tau)]);
            const double threshold=(std::max)(.2,global+.08);
            int pick=-1;
            for(int tau=minLag;tau<=lagMax;++tau) if(c[size_t(tau)]<threshold) {
                while(tau+1<=lagMax && c[size_t(tau+1)]<c[size_t(tau)])++tau;
                pick=tau;break;
            }
            if(pick<0)pick=minLag;
            bestV=c[size_t(pick)];
            double refined=pick;
            if(pick>minLag && pick<lagMax){
                const double a=c[size_t(pick-1)],b=c[size_t(pick)],g=c[size_t(pick+1)],den=a-2*b+g;
                if(std::abs(den)>1e-12)refined=pick+std::clamp(.5*(a-g)/den,-.5,.5);
            }
            best=refined;
        }
        // Near the end the lag search is cut short; a pick close to that
        // limit cannot be trusted.
        centre.push_back(double(t));
        raw.push_back(best*D);
        voiced.push_back(loud && best>0 && bestV<.45 && best<lagMax-1 && (lagMax==maxLag || best*1.25<=lagMax));
    }
    size_t count=0;for(char v:voiced)count+=size_t(v);
    if(count<3)return track;
    // Unvoiced frames hold the nearest voiced period.
    std::vector<double> filled(raw.size());
    for(size_t i=0;i<raw.size();++i){
        if(voiced[i]){filled[i]=raw[i];continue;}
        size_t lo=i,hi=i;bool fl=false,fh=false;
        while(lo>0){--lo;if(voiced[lo]){fl=true;break;}}
        while(hi+1<raw.size()){++hi;if(voiced[hi]){fh=true;break;}}
        filled[i]=fl&&(!fh||i-lo<=hi-i)?raw[lo]:raw[hi];
    }
    // Octave flips (half or double the neighbouring period) are folded back
    // to the local median over +-8 frames before the final median of 5.
    {
        std::vector<double> folded(filled);
        for(size_t i=0;i<filled.size();++i){
            std::vector<double> w;
            for(long k=-8;k<=8;++k){const long j=long(i)+k;if(j>=0&&size_t(j)<filled.size())w.push_back(filled[size_t(j)]);}
            std::nth_element(w.begin(),w.begin()+long(w.size()/2),w.end());
            const double med=w[w.size()/2],r=filled[i]/med;
            if(r>1.7&&r<2.35)folded[i]=filled[i]*.5;
            else if(r>.42&&r<.59)folded[i]=filled[i]*2;
        }
        filled.swap(folded);
    }
    // Median of 5 removes isolated outliers.
    track.position=centre;track.period.resize(filled.size());
    for(size_t i=0;i<filled.size();++i){
        double w[5];int m=0;
        for(int k=-2;k<=2;++k){const long j=long(i)+k;if(j>=0&&size_t(j)<filled.size())w[m++]=filled[size_t(j)];}
        std::sort(w,w+m);track.period[i]=w[m/2];
        // The difference window spans [t, t+W+lag): that is where the period
        // was measured (native samples).
        track.position[i]=(centre[i]+.5*(W+track.period[i]/D))*D;
    }
    // Frame centres depend on the measured lag: keep them strictly ordered.
    std::vector<size_t> order(track.position.size());
    for(size_t i=0;i<order.size();++i)order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return track.position[a]<track.position[b];});
    PeriodTrack sorted;if(voicedOut)voicedOut->clear();
    for(size_t i:order) if(sorted.position.empty() || track.position[i]>sorted.position.back()+1e-9) {
        sorted.position.push_back(track.position[i]);sorted.period.push_back(track.period[i]);
        if(voicedOut)voicedOut->push_back(voiced[i]);
    }
    return sorted;
}
} // namespace punchfidelity

inline Sample renderHighestFidelityPunch(const Sample& input,double semitones) {
    if(!std::isfinite(semitones) || semitones < -144 || semitones > 144 ||
       !std::isfinite(input.rate) || input.rate<8000 || input.left.empty() ||
       input.left.size()!=input.right.size())throw std::invalid_argument("Invalid punch pitch/source");
    if(std::abs(semitones)<1.e-6)return input;
    const double ratio=std::pow(2.,semitones/12.);
    if(input.left.size()/ratio > 5000000.0)throw std::invalid_argument("Punch shift exceeds safe preparation size");
    const auto track=punchfidelity::trackPeriods(input);
    if(track.period.empty())return renderPunchPitch(input,semitones,PunchPitchMode::StretchAligned);
    character::Prepared p;
    p.stretch=1.0;p.sourceFrames=input.left.size();p.sourceRate=input.rate;
    double mean=0;for(double v:track.period)mean+=v;mean/=double(track.period.size());
    p.fundamentalHz=input.rate/mean;
    const double minPeriod=input.rate/1400.0,maxPeriod=input.rate/18.0;
    return character::render(input,semitones,p,{},nullptr,0.0,
        [&](double pos){return std::clamp(track.at(pos),minPeriod,maxPeriod);});
}
} // namespace pt
