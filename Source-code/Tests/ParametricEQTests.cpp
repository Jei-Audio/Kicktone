#include "../Source/ParametricEQ.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main() {
    using namespace tt;
    std::array<Band,kBands> b{};
    auto& low=b[0];low.enabled=true;low.type=LowShelf;low.hz=150;low.gain=6;
    check(responseDb(b,45,48000)>4.8,"Low shelf did not boost lows");
    check(std::abs(responseDb(b,10000,48000))<0.1,"Low shelf changed highs");
    low.enabled=false;
    auto& high=b[1];high.enabled=true;high.type=HighShelf;high.hz=4500;high.gain=-6;
    check(responseDb(b,18000,48000)<-5.0,"High shelf did not attenuate highs");
    high.enabled=false;
    auto& bell=b[2];bell.enabled=true;bell.type=Bell;bell.hz=900;bell.gain=-12;bell.q=1.4;
    check(responseDb(b,900,48000)<-11.8,"Bell centre frequency not correct");
    check(std::abs(responseDb(b,60,48000))<0.1,"Bell leaked to bass");
    bell.enabled=false;
    auto& hp=b[3];hp.enabled=true;hp.type=LowCut;hp.hz=220;hp.q=0.71;
    hp.slope=0;const double gentle=responseDb(b,55,48000);
    hp.slope=2;const double steep=responseDb(b,55,48000);
    check(gentle<-20&&steep<gentle-30,"Low cut 48 dB slope not steeper than 12 dB");
    hp.enabled=false;
    auto& lp=b[4];lp.enabled=true;lp.type=HighCut;lp.hz=1500;
    lp.slope=2;check(responseDb(b,7000,48000)<-40,"High cut slope ineffective");
    lp.enabled=false;
    check(std::abs(responseDb(b,1000,48000))<1.e-8,"Disabled bands not neutral");
    ParametricEQ eq;eq.reset();eq.update(b,true,48000);
    for(int i=0;i<2000;++i){float l=0.25f,r=-0.17f;eq.process(l,r);
        check(std::isfinite(l)&&std::isfinite(r),"EQ generated NaN/INF");
        check(std::abs(l-0.25f)<0.001f&&std::abs(r+0.17f)<0.001f,"Neutral EQ altered sample");}
    b[2].enabled=true;b[2].type=Bell;b[2].hz=1000;b[2].gain=12;
    eq.update(b,true,48000);
    for(int i=0;i<48000;++i){float l=std::sin(2*pi*1000*i/48000),r=-l;eq.process(l,r);
        check(std::isfinite(l)&&std::isfinite(r)&&std::abs(l)<8,"DSP became unstable");
        check(std::abs(l+r)<0.0001,"Stereo symmetry broken");}
    eq.update(b,false,48000);
    for(int i=0;i<1000;++i){float l=0.1f,r=-0.1f;eq.process(l,r);
        check(std::isfinite(l)&&std::isfinite(r),"Bypass ramp unstable");}
    for(double sr:{44100.0,48000.0,96000.0,192000.0}) {
        for(int type=0;type<5;++type)for(double hz:{20.0,100.0,20000.0}){
            Band x;x.enabled=true;x.type=type;x.hz=hz;x.gain=18;
            const auto c=makeCoeffs(x,sr);
            check(std::isfinite(c.b0)&&std::isfinite(c.a1)&&std::isfinite(c.a2),"Invalid filter coefficient");
        }
    }
    std::cout<<"Tailtone parametric EQ DSP tests passed\n";
}
