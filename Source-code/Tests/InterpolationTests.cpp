#include "../Source/Engine.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
static float original(const std::vector<float>& a,double pos) {
    const auto centre=int64_t(std::floor(pos));double sum=0,weights=0;
    for(int k=-7;k<=8;++k){
        const auto i=centre+k;const double d=pos-centre-k,x=pt::pi*d;
        const double w=(std::abs(x)<1e-10?1:std::sin(x)/x)*(.5+.5*std::cos(pt::pi*d/8));
        if(i>=0 && i<int64_t(a.size()))sum+=a[size_t(i)]*w;
        weights+=w;
    }
    return float(sum/(std::max)(.001,weights));
}
int main(){
    pt::Sample s;s.left.resize(44100);s.right.resize(44100);
    uint32_t seed=12345;for(auto& x:s.left){seed=seed*1664525u+1013904223u;x=float(double(seed)/4294967295.*2-1);}s.right=s.left;
    (void)pt::sincTable();double peak=0;
    for(int i=0;i<50000;++i){double pos=std::fmod(i*17.131719,44100.);peak=(std::max)(peak,double(std::abs(s.at(0,pos)-original(s.left,pos))));}
    if(peak>2e-6)throw std::runtime_error("Sinc table differs from original by more than 2e-6");
    volatile double sink=0;
    auto bench=[&](bool table){auto t=std::chrono::steady_clock::now();for(int i=0;i<500000;++i){double pos=std::fmod(i*17.131719,44100.);sink+=table?s.at(0,pos):original(s.left,pos);}return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
    const auto oldMs=bench(false),newMs=bench(true);
    std::cout<<"PASS interpolation max error "<<peak<<"; old "<<oldMs<<" ms; table "<<newMs<<" ms; speedup "<<oldMs/newMs<<"x (kernel only)\n";
}
