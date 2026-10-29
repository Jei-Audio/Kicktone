#pragma once
#include "Engine.h"
#include <CDSPResampler.h>  // Official MIT-licensed r8brain-free-src
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pt {
namespace cycle {

// Shared building blocks for the Highest Fidelity v4 engine family
// (KickCharacterPitch.h): r8brain resampling and cycle (phase-mark) analysis.
// v0.4.61: the Legacy v1 / High Fidelity cycle renderer that used to live here
// was removed together with the Extreme engine.
// r8brain only converts sample rates: this makes a *genuinely pitch-resampled*,
// anti-aliased stream.
inline std::vector<float> resample(const std::vector<float>& in,
                                   double rate, double ratio) {
    if (in.empty()) return {};
    constexpr int block = 2048;
    const size_t wanted = std::max<size_t>(1, size_t(std::llround(double(in.size()) / ratio)));
    r8b::CDSPResampler24 src(rate * ratio, rate, block);
    std::vector<double> input(block, 0.0);
    std::vector<float> result;
    result.reserve(wanted + 64);
    double* output = nullptr;
    auto send = [&](size_t at, size_t count) {
        std::fill(input.begin(), input.end(), 0.0);
        for (size_t k=0; k<count; ++k) input[k] = in[at+k];
        int got = src.process(input.data(), block, output);
        if (got > 0 && output != nullptr) {
            for (int k=0;k<got && result.size()<wanted;++k)
                result.push_back(std::isfinite(output[k]) ? float(output[k]) : 0.0f);
        }
    };
    // Full blocks, zero-pad last block once. r8brain compensates processing
    // latency; padding is subsequently flushed rather than left in the tail.
    for (size_t at=0;at<in.size();at+=block)
        send(at,std::min<size_t>(block,in.size()-at));
    for (int flush=0;flush<128 && result.size()<wanted;++flush) send(in.size(),0);
    if (result.size()<wanted)
        throw std::runtime_error("r8brain resampler did not produce enough samples");
    result.resize(wanted);
    return result;
}

inline std::vector<double> findPhaseMarks(const Sample& s, double period) {
    const size_t n=s.left.size();
    const auto& x=[&]() -> const std::vector<float>& {
        double l=0,r=0;for(size_t i=0;i<n;++i){ l+=s.left[i]*s.left[i]; r+=s.right[i]*s.right[i]; }
        return (l>=r)?s.left:s.right;
    }();
    // Symmetric low-pass removes misleading zero-crossings caused by the
    // distorted upper harmonics, without filtering the final audio itself.
    const int radius=std::clamp(int(std::llround(period*0.11)),3,180);
    const int dcRadius=std::clamp(int(std::llround(period*.75)),radius+1,4000);
    std::vector<double> prefix(n+1,0.0), sm(n);
    for(size_t i=0;i<n;++i)prefix[i+1]=prefix[i]+x[i];
    auto mean=[&](int i,int r) {
        const size_t a=size_t((std::max)(0,i-r));
        const size_t b=(std::min)(n,size_t(i+r+1));
        return (prefix[b]-prefix[a])/double(b-a);
    };
    for(size_t i=0;i<n;++i)sm[i]=mean(int(i),radius)-mean(int(i),dcRadius);
    std::vector<double> candidates;
    for(size_t i=1;i<n;++i) if(sm[i-1]<0 && sm[i]>=0) {
        const double denom=sm[i]-sm[i-1];
        candidates.push_back(double(i-1)+(denom>1e-15?-sm[i-1]/denom:0));
    }
    if(candidates.size()<2)throw std::runtime_error("No stable rising-edge cycles found in tail");
    // The attack can contain many unrelated crossings. Lock to the longest
    // plausible run in the body, then track in both directions. Starting at
    // the first attack crossing could leave the entire body on invented marks.
    size_t bestStart=0, bestLength=0, runStart=0;
    for(size_t j=1;j<candidates.size();++j) {
        const double d=candidates[j]-candidates[j-1];
        if(d<period*.65 || d>period*1.4)runStart=j;
        const size_t length=j-runStart;
        if(length>bestLength){bestLength=length;bestStart=runStart;}
    }
    const size_t anchor=bestStart+bestLength/2;
    const double initialPeriod=bestLength>0
        ? candidates[anchor+1]-candidates[anchor] : period;
    std::vector<double> marks{candidates[anchor]};
    auto track=[&](int direction) {
        double current=candidates[anchor], runningPeriod=initialPeriod;
        for(int guard=0;guard<100000;++guard) {
            const double expected=current+direction*runningPeriod;
            auto low=std::lower_bound(candidates.begin(),candidates.end(),expected-period*.45);
            double found=std::numeric_limits<double>::quiet_NaN(),best=period*.46;
            for(auto it=low;it!=candidates.end() && *it<=expected+period*.45;++it) {
                const double distance=direction*(*it-current),err=std::abs(*it-expected);
                if(distance>period*.5 && err<best){best=err;found=*it;}
            }
            const double next=std::isfinite(found)?found:expected;
            if(std::isfinite(found))runningPeriod=std::clamp(
                .65*runningPeriod+.35*std::abs(next-current),period*.65,period*1.4);
            marks.push_back(next);
            current=next;
            if(direction<0 ? current<=0 : current>=double(n)+period)break;
        }
    };
    track(-1);track(1);
    std::sort(marks.begin(),marks.end());
    return marks;
}


struct Analysis {
    double fundamentalHz = 0.0;
    std::vector<double> marks;
};

inline Analysis analyze(const Sample& input, double fallbackHz = 0.0) {
    const auto f = detect(input);
    const double hz = f.valid ? f.hz : fallbackHz;
    // A manually supplied root is not permission to infer arbitrary cycles
    // in broadband noise: demand at least some periodicity evidence.
    if(!f.valid && f.confidence < 0.35)
        throw std::runtime_error("Tail is too noisy or non-periodic for cycle resynthesis");
    if(!std::isfinite(hz) || hz<18 || hz>1400)
        throw std::runtime_error("Tail has no reliable periodic fundamental; set a plausible root note or use R3");
    Analysis a; a.fundamentalHz=hz;
    a.marks=findPhaseMarks(input,input.rate/hz);
    std::vector<double> periods;
    for(size_t j=0;j+1<a.marks.size();++j) {
        if(a.marks[j]<0 || a.marks[j+1]>=double(input.left.size()))continue;
        const double d=a.marks[j+1]-a.marks[j];
        if(d>input.rate/hz*.72 && d<input.rate/hz*1.28)periods.push_back(d);
    }
    // Raw frequency estimates can be biased by high harmonics in heavily
    // distorted hardstyle tails. Phase-mark periods are more reliable here.
    if(periods.size()>=4) {
        std::sort(periods.begin(),periods.end());
        // Prefer the densest stable group: attack and release pitch sweeps
        // must not drag the root away from the sustained body.
        size_t bestBegin=0,bestCount=0;
        for(size_t begin=0,end=0;begin<periods.size();++begin) {
            while(end<periods.size() && periods[end]<=periods[begin]*1.04)++end;
            if(end-begin>bestCount){bestBegin=begin;bestCount=end-begin;}
        }
        const double median=bestCount>=4 ? periods[bestBegin+bestCount/2]
                                         : periods[periods.size()/2];
        const double refined=input.rate/median;
        if(std::abs(12.0*std::log2(refined/hz))<1.5)a.fundamentalHz=refined;
    }
    return a;
}

// v0.4.47 manual-root fallback. When automatic detection cannot find a stable
// note, the user's TAIL ROOT is trusted as the fundamental so the tail can still
// be pitched. Prefer real rising-edge marks tracked at that period; when the
// material has none (very noisy/non-periodic), use an evenly spaced cycle grid
// at the manual root period. Detected tails take the unchanged analyze() path.
inline bool plausibleMarks(const std::vector<double>& marks,size_t frames,double period) {
    if(marks.size()<3 || marks.front()>0.0 || marks.back()<double(frames))return false;
    for(size_t j=1;j<marks.size();++j) {
        const double d=marks[j]-marks[j-1];
        if(!std::isfinite(d) || d<period*.5 || d>period*2.0)return false;
    }
    return true;
}
inline std::vector<double> uniformMarks(size_t frames,double period) {
    std::vector<double> marks;
    for(double t=0.0;;t+=period){marks.push_back(t);if(t>=double(frames)+period)break;}
    return marks;
}
inline Analysis analyzeWithManualRoot(const Sample& input,double rootHz,bool* usedManualRoot=nullptr) {
    if(usedManualRoot)*usedManualRoot=false;
    try { return analyze(input,rootHz); }
    catch(const std::exception&) {
        if(!std::isfinite(rootHz) || rootHz<18 || rootHz>1400 || input.left.empty() ||
           !std::isfinite(input.rate) || input.rate<8000)throw;
    }
    Analysis a; a.fundamentalHz=rootHz;
    const double period=input.rate/rootHz;
    try { a.marks=findPhaseMarks(input,period); } catch(const std::exception&) { a.marks.clear(); }
    if(!plausibleMarks(a.marks,input.left.size(),period))a.marks=uniformMarks(input.left.size(),period);
    if(usedManualRoot)*usedManualRoot=true;
    return a;
}
} // namespace cycle
inline Pitch refinedTailPitch(const Sample& input) {
    auto p=detect(input);
    if(!p.valid)return p;
    try {
        auto a=cycle::analyze(input);
        p.hz=a.fundamentalHz;
        p.midi=69.0+12.0*std::log2(p.hz/440.0);
    } catch(const std::exception&) {}
    return p;
}
} // namespace pt
