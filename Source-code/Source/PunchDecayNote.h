#pragma once
// v0.5.5 PUNCH DECAY NOTE (user request: "detect what note the punch decay is
// in and display it ... the mid tone of the decay", plus USE DECAY NOTE).
//
// The decay is the part of the prepared punch after ATTACK END (the same split
// Punch 2 uses). Its pitch is tracked with the Highest Fidelity punch tracker
// (punchfidelity::trackPeriods: YIN, 1 ms hop) on that region alone, 3 ms after
// the split to skip the handover. The decay of a hardstyle punch is usually a
// falling sweep, so the result is the MEDIAN pitch of the voiced frames (the
// "mid tone") plus the pitch at the start and the end of the sweep, so the
// user can see how far it moves. Nothing is stored in the project; it is
// re-measured on the worker whenever the prepared punch changes.
#include "PunchPitch.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace pt::decaynote {
struct Result {
    bool valid=false;
    double midHz=0, startHz=0, endHz=0;   // median, first 20 % and last 20 % of the voiced frames
    double midi() const { return midHz>0?69.0+12.0*std::log2(midHz/440.0):0.0; }
    double startMidi() const { return startHz>0?69.0+12.0*std::log2(startHz/440.0):0.0; }
    double endMidi() const { return endHz>0?69.0+12.0*std::log2(endHz/440.0):0.0; }
    bool operator==(const Result& o) const { return valid==o.valid&&midHz==o.midHz&&startHz==o.startHz&&endHz==o.endHz; }
    bool operator!=(const Result& o) const { return !(*this==o); }
};
// splitSeconds: where the decay starts (ATTACK END). endSeconds: audible end of
// the punch (Punch End trim); <= 0 = the whole sample.
inline Result analyse(const Sample& punch,double splitSeconds,double endSeconds=0) {
    Result r;
    if(punch.left.empty() || punch.left.size()!=punch.right.size() || !std::isfinite(punch.rate) || punch.rate<8000)return r;
    const double rate=punch.rate;
    const size_t total=punch.left.size();
    const size_t last=endSeconds>0?(std::min)(total,size_t(std::llround(endSeconds*rate))):total;
    const size_t first=(std::min)(last,size_t(std::llround((std::max)(0.0,splitSeconds)*rate+.003*rate)));
    if(last<=first || double(last-first)/rate<.020)return r;   // need at least 20 ms of decay
    Sample region;region.rate=rate;
    region.left.assign(punch.left.begin()+long(first),punch.left.begin()+long(last));
    region.right.assign(punch.right.begin()+long(first),punch.right.begin()+long(last));
    // Ignore the quiet end: frames below -50 dB of the region's own peak carry no usable pitch.
    double peak=0;for(size_t i=0;i<region.left.size();++i)peak=(std::max)(peak,std::abs(.5*(double(region.left[i])+double(region.right[i]))));
    if(peak<1e-5)return r;
    std::vector<char> voiced;
    const auto track=punchfidelity::trackPeriods(region,&voiced);
    if(track.period.size()<3 || voiced.size()!=track.period.size())return r;
    std::vector<double> hz;std::vector<size_t> index;
    for(size_t i=0;i<track.period.size();++i){
        if(!voiced[i] || track.period[i]<=0)continue;
        const size_t centre=(std::min)(region.left.size()-1,size_t((std::max)(0.0,track.position[i])));
        double e=0;int n=0;const size_t w=size_t(rate*.006);
        for(size_t j=centre>w?centre-w:0;j<(std::min)(region.left.size(),centre+w);++j){const double v=.5*(double(region.left[j])+double(region.right[j]));e+=v*v;++n;}
        if(n==0 || std::sqrt(e/n)<peak*.00316)continue;   // -50 dB
        const double f=rate/track.period[i];
        if(f<25 || f>1200)continue;
        hz.push_back(f);index.push_back(i);
    }
    // At least 10 ms of voiced decay (frames are 1 ms apart) and most of the
    // measured frames must be voiced, otherwise the decay has no stable pitch.
    if(hz.size()<10 || hz.size()*2<voiced.size())return r;
    auto median=[](std::vector<double> v){std::sort(v.begin(),v.end());const size_t n=v.size();return n%2?v[n/2]:.5*(v[n/2-1]+v[n/2]);};
    std::vector<double> logs;for(double f:hz)logs.push_back(std::log2(f));
    r.midHz=std::pow(2.0,median(logs));
    const size_t part=(std::max)(size_t(1),hz.size()/5);
    r.startHz=std::pow(2.0,median(std::vector<double>(logs.begin(),logs.begin()+long(part))));
    r.endHz=std::pow(2.0,median(std::vector<double>(logs.end()-long(part),logs.end())));
    r.valid=true;
    return r;
}
// Semitones to add to a tuning knob so a decay measured at `measured` lands on `targetMidi`.
inline double correction(const Result& measured,double targetMidi) {
    return measured.valid?targetMidi-measured.midi():0.0;
}
} // namespace pt::decaynote
