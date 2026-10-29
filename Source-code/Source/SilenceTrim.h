#pragma once
#include "Engine.h"
namespace pt {
struct SilenceBounds { size_t begin=0,end=0; bool valid=false; };
// -90 dBFS absolute threshold, both stereo channels, 0.5 ms safety padding.
// Do not gate gaps inside the sound, normalize, fade, or touch its sample values.
inline SilenceBounds silenceBounds(const Sample& s) {
    SilenceBounds b;const size_t n=(std::min)(s.left.size(),s.right.size());
    constexpr float threshold=0.0000316227766f;
    auto audible=[&](size_t i){return std::abs(s.left[i])>threshold || std::abs(s.right[i])>threshold;};
    size_t first=0;while(first<n && !audible(first))++first;
    if(first==n)return b; // silent files stay intact
    size_t end=n;while(end>first && !audible(end-1))--end;
    const size_t pad=size_t(std::ceil(s.rate*0.0005));
    b.begin=first>pad?first-pad:0;b.end=(std::min)(n,end+pad);b.valid=true;return b;
}
inline void cropSample(Sample& s,size_t first,size_t last) {
    const size_t n=(std::min)(s.left.size(),s.right.size());
    first=(std::min)(first,n);last=std::clamp(last,first,n);
    if(first==0 && last==n)return;
    for(auto* channel:{&s.left,&s.right}) {
        std::vector<float> cropped(channel->begin()+first,channel->begin()+last);
        *channel=std::move(cropped);
    }
}
}
