#pragma once
// v0.5.36 (user request): DETECT END on the Punch window and DETECT START on the
// Tail window find where the punch ends and the tail begins in the loaded
// sample. Run once on the preparation worker when a sample is loaded; never
// used by the audio callback or by note rendering (outside the render key).
#include "PunchDetection.h"
namespace pt {
// Where the stable pitched TAIL of a sample begins (the v0.4.66 AUTO-SPLIT
// boundary; removed in v0.5.13 with AUTO-SPLIT, back for the detect buttons).
// The punch is the attack AND its decay (the noisy, pitch-falling body); the
// tail starts at the first rising zero crossing of a run of repeating cycles
// at the tail's settled pitch. Steps:
//  1. Reference period: median YIN period of the voiced frames in 30-80 % of
//     the audible length (decimated to ~5.5 kHz; 25-400 Hz).
//  2. Rising zero crossings of a zero-phase low-pass (4 x f0) of the mono mix.
//  3. The first cycle k that starts a run of max(3, 40 ms) cycles whose periods
//     are within +-1.5 semitones of the reference and whose shapes repeat
//     (normalised cross-correlation of consecutive cycles >= 0.9). The first
//     cycle, the tail's first swing, may be up to 2.5 semitones LONGER (lower)
//     and less similar (>= 0.6).
//  4. Snap to the quietest frame within +-1 ms.
// v0.5.36: a punch must be PROVEN before the run: a cycle at least 3 semitones
// above the tail pitch (the punch's pitch drop), or more than 1.5 tail periods
// of sound before the first low-passed cycle. A run with no proof before it
// means the sample is a tail from its start: valid, punch=false, ms=0. (v0.4.66
// cut tail.wav 54 ms in: its first swings repeat less well but are already at
// the tail pitch.) Invalid when no run exists (noise, a punch without a tail).
struct TailStart { double ms=0, hz=0; bool valid=false, punch=false; };
inline TailStart detectTailStart(const Sample& sample) {
    TailStart result;
    const size_t n=sample.left.size();
    if(sample.rate<8000 || sample.right.size()!=n || n<size_t(sample.rate*.06))return result;
    std::vector<double> mono(n);
    for(size_t i=0;i<n;++i)mono[i]=.5*(double(sample.left[i])+double(sample.right[i]));
    // 1. Reference period.
    const int D=(std::max)(1,int(sample.rate/5500.0));
    const double rate=sample.rate/D;
    std::vector<double> y(n/size_t(D));
    for(size_t i=0;i<y.size();++i){double sum=0;for(int k=0;k<D;++k)sum+=mono[i*size_t(D)+size_t(k)];y[i]=sum/D;}
    double peak=0;for(double v:y)peak=(std::max)(peak,std::abs(v));
    if(peak<1e-6)return result;
    const size_t block=(std::max)(size_t(1),size_t(rate*.01));
    size_t audible=0;
    for(size_t b=0;b*block<y.size();++b) {
        double e=0;const size_t hi=(std::min)(y.size(),(b+1)*block);
        for(size_t i=b*block;i<hi;++i)e+=y[i]*y[i];
        if(std::sqrt(e/double(hi-b*block))>peak*.01)audible=hi;
    }
    const int minLag=(std::max)(2,int(rate/400.0)), maxLag=int(std::ceil(rate/25.0)), W=maxLag;
    // At most ~400 analysis frames, so a long file cannot stall the worker
    // (kicks under ~8 s keep the 10 ms hop).
    const size_t hop=(std::max)({size_t(1),size_t(rate*.01),size_t(audible/2/400)});
    std::vector<double> cm(size_t(maxLag)+1,1.0), periods;
    for(size_t t=size_t(audible*.3);t<size_t(audible*.8) && t+size_t(W+maxLag)<y.size();t+=hop) {
        double e=0;for(int j=0;j<W;++j)e+=y[t+size_t(j)]*y[t+size_t(j)];
        if(std::sqrt(e/W)<peak*.01)continue;
        double run=0;
        for(int tau=1;tau<=maxLag;++tau) {
            double sum=0;
            for(int j=0;j<W;++j){const double diff=y[t+size_t(j)]-y[t+size_t(j+tau)];sum+=diff*diff;}
            run+=sum;cm[size_t(tau)]=run>1e-20?sum*tau/run:1.0;
        }
        int best=-1;
        for(int tau=minLag;tau<=maxLag;++tau)if(cm[size_t(tau)]<.2) {
            while(tau<maxLag && cm[size_t(tau+1)]<cm[size_t(tau)])++tau;
            best=tau;break;
        }
        if(best>0 && cm[size_t(best)]<.25)periods.push_back(double(best)*D);
    }
    if(periods.size()<3)return result;
    std::nth_element(periods.begin(),periods.begin()+ptrdiff_t(periods.size()/2),periods.end());
    const double reference=periods[periods.size()/2];
    // 2. Zero-phase low-pass (two one-pole stages forward, two backward).
    std::vector<double> low(mono);
    const double cutoff=std::clamp(4.0*sample.rate/reference,60.0,sample.rate*.45);
    const double alpha=1-std::exp(-2*pi*cutoff/sample.rate);
    for(int pass=0;pass<2;++pass) {
        double s1=0,s2=0;
        if(pass==0)for(size_t i=0;i<n;++i){s1+=alpha*(low[i]-s1);s2+=alpha*(s1-s2);low[i]=s2;}
        else for(size_t i=n;i-->0;){s1+=alpha*(low[i]-s1);s2+=alpha*(s1-s2);low[i]=s2;}
    }
    double lowPeak=0;for(double v:low)lowPeak=(std::max)(lowPeak,std::abs(v));
    if(lowPeak<1e-6)return result;
    std::vector<size_t> z;bool armed=false;
    for(size_t i=1;i<n;++i) {
        if(low[i]<-.02*lowPeak)armed=true;
        if(armed && low[i-1]<0 && low[i]>=0){z.push_back(i);armed=false;}
    }
    if(z.empty())return result;
    // 3. First run of repeating cycles at the tail pitch.
    const size_t cycles=(std::max)(size_t(3),size_t(std::ceil(.04*sample.rate/reference)));
    // Period offset in semitones of length (positive = longer = lower pitch).
    const auto semitones=[&](size_t k){return 12.0*std::log2(double(z[k+1]-z[k])/reference);};
    const auto periodOk=[&](size_t k,double longer){const double st=semitones(k);return st>=-1.5 && st<=longer;};
    const auto repeats=[&](size_t k,double limit) {
        const size_t L=(std::min)(z[k+1]-z[k],z[k+2]-z[k+1]);
        double ab=0,aa=0,bb=0;
        for(size_t i=0;i<L;++i){const double a=mono[z[k]+i],b=mono[z[k+1]+i];ab+=a*b;aa+=a*a;bb+=b*b;}
        return ab>=limit*std::sqrt(aa*bb) && aa>0;
    };
    const auto tailCycle=[&](size_t j,bool first){return first?periodOk(j,2.5) && repeats(j,.6):periodOk(j,1.5) && repeats(j,.9);};
    // Proof of a punch: a long sound before the first low-passed cycle ...
    double monoPeak=0;for(double v:mono)monoPeak=(std::max)(monoPeak,std::abs(v));
    size_t onset=0;while(onset<n && std::abs(mono[onset])<monoPeak*.01)++onset;
    bool punchSeen=z.front()>onset+size_t(1.5*reference);
    size_t cut=0;bool fromStart=false;
    for(size_t k=0;k+cycles+1<z.size();++k) {
        bool run=true;
        for(size_t j=k;j<k+cycles && run;++j)run=tailCycle(j,j==k);
        if(run && !punchSeen){fromStart=true;break;}
        if(run && z[k]>=size_t(sample.rate*.003)){cut=z[k];break;}
        punchSeen|=semitones(k)<-3.0;   // ... or a cycle >= 3 semitones above the tail
    }
    if(fromStart){result.hz=sample.rate/reference;result.valid=true;return result;}
    if(!cut)return result;
    // 4. Quietest frame within +-1 ms (the low-pass crossing is zero-phase).
    const size_t radius=(std::max)(size_t(1),size_t(sample.rate*.001));
    const size_t lo=cut>radius?cut-radius:0, hi=(std::min)(n-1,cut+radius);
    size_t best=cut;double score=1e100;
    for(size_t i=lo;i<=hi;++i) {
        const double v=(std::max)(std::abs(sample.left[i]),std::abs(sample.right[i]));
        const double cost=v+lowPeak*.02*std::abs(double(i)-double(cut))/double(radius);
        if(cost<score){score=cost;best=i;}
    }
    result.ms=1000.0*double(best)/sample.rate;result.hz=sample.rate/reference;result.valid=result.punch=true;
    return result;
}
// DETECT END: in a sample with a punch AND a tail the punch (attack + decay)
// ends where the tail begins; anything else keeps the v0.4 impact detector
// (a punch-only file that ends in silence or settles).
inline PunchEndSuggestion suggestPunchEnd(const Sample& sample,const TailStart& tail) {
    if(tail.valid && tail.punch)return {tail.ms,.9,true};
    return detectPunchEnd(sample);
}
}
