#pragma once
#include "Engine.h"
#include <numeric>
namespace pt {
struct PunchEndSuggestion { double endMs=0, confidence=0; bool valid=false; };
// Cached at import, never called by the audio callback or a mouse drag.
// A heuristic: distinguish the broadband impact from a sustained bass tail
// using local energy and high-frequency energy, rather than requiring silence.
inline PunchEndSuggestion detectPunchEnd(const Sample& sample) {
    PunchEndSuggestion result;
    const auto n=sample.left.size();
    if(sample.rate<8000 || n<sample.rate*.008 || sample.right.size()!=n)return result;
    const size_t hop=(std::max)(size_t(1),size_t(sample.rate*.002));
    const size_t count=(n+hop-1)/hop;
    std::vector<double> energy(count),high(count);
    double low[2]={0,0};const double alpha=1-std::exp(-2*pi*900/sample.rate);
    for(size_t i=0;i<n;++i)for(int ch=0;ch<2;++ch) {
        const double x=ch?sample.right[i]:sample.left[i];
        low[ch]+=alpha*(x-low[ch]);const double h=x-low[ch];
        energy[i/hop]+=x*x;high[i/hop]+=h*h;
    }
    auto smooth=[](const std::vector<double>& in) {
        std::vector<double> out(in.size());
        for(size_t i=0;i<in.size();++i) {
            const auto lo=i>4?i-4:0,hi=(std::min)(in.size(),i+5);
            for(auto k=lo;k<hi;++k)out[i]+=in[k];
            out[i]/=double(hi-lo);
        }return out;
    };
    auto e=smooth(energy),h=smooth(high);
    const double frameMs=1000.0*hop/sample.rate;
    const size_t early=(std::min)(count,size_t(120/frameMs)+1);
    const double peak=*std::max_element(e.begin(),e.begin()+ptrdiff_t(early));
    if(peak/double(hop*2)<1e-10)return result;
    size_t onset=0;while(onset+1<early && e[onset]<peak*.08)++onset;
    const size_t begin=(std::min)(count-1,onset+size_t(20/frameMs));
    const size_t end=(std::min)(count-1,onset+size_t(300/frameMs));
    // A short punch that actually ends in silence should retain its decay.
    for(size_t i=begin;i+4<count && i<end;++i) {
        bool silent=true;for(size_t j=0;j<4;++j)silent &= e[i+j]<peak*.0003;
        if(silent){result={i*frameMs,.9,true};break;}
    }
    if(!result.valid) {
        const size_t tailBegin=(std::min)(count-1,(std::max)(begin+1,size_t(300/frameMs)));
        const size_t tailEnd=(std::min)(count,tailBegin+size_t(300/frameMs));
        if(tailEnd<=tailBegin+3)return result;
        auto median=[&](const std::vector<double>& v){
            std::vector<double> a(v.begin()+ptrdiff_t(tailBegin),v.begin()+ptrdiff_t(tailEnd));
            std::nth_element(a.begin(),a.begin()+ptrdiff_t(a.size()/2),a.end());return a[a.size()/2];
        };
        const double tailE=median(e),tailH=median(h);
        const double peakH=*std::max_element(h.begin(),h.begin()+ptrdiff_t(early));
        const bool spectralContrast=peakH>(std::max)(tailH*2.5,peak*.015);
        const bool energyContrast=peak>tailE*2.5;
        if(!spectralContrast && !energyContrast)return result;
        for(size_t i=begin;i+5<count && i<end;++i) {
            bool settled=true;
            for(size_t j=0;j<5;++j) {
                if(spectralContrast)settled &= h[i+j] <= (std::max)(tailH*1.55,peakH*.12);
                else settled &= e[i+j] <= (std::max)(tailE*1.5,peak*.12);
            }
            if(settled){result={i*frameMs,spectralContrast?.75:.55,true};break;}
        }
    }
    if(result.valid) {
        const auto centre=size_t(result.endMs*sample.rate/1000.0);
        const auto radius=size_t(sample.rate*.002);
        const auto lo=centre>radius?centre-radius:0, hi=(std::min)(n-1,centre+radius);
        size_t best=centre;double score=1e100;
        for(size_t i=lo;i<=hi;++i) {
            const double v=(std::max)(std::abs(sample.left[i]),std::abs(sample.right[i]));
            const double cost=v+std::sqrt(peak/double(hop*2))*.025*std::abs(double(i)-centre)/(std::max)(1.0,double(radius));
            if(cost<score){score=cost;best=i;}
        }
        result.endMs=1000.0*best/sample.rate;
    }
    return result;
}

// v0.5.13: pt::detectTailStart (the v0.4.66 AUTO-SPLIT boundary) was removed
// together with AUTO-SPLIT KICK (user request).
}
