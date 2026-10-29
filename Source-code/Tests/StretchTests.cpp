#include "../Source/PitchShift.h"
#include "V4Render.h"
#include "../Source/PitchBankPolicy.h"
#include "../Source/KickOverview.h"
#include <iostream>
#include <stdexcept>
static void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
int main() try {
    pt::Sample x; x.rate=44100;
    for(int i=0;i<44100;++i) {
        const double phase=2*pt::pi*110*i/x.rate;
        x.left.push_back(float(.5*std::sin(phase)+.12*std::sin(3*phase)));
        x.right.push_back(float(.4*std::sin(phase+.3)+.1*std::sin(3*phase+.9)));
    }
    const auto a=pt::cycle::analyze(x);
    for(double stretch:{.8,1.,1.234,1.5}) for(double shift:{-12.,-7.,0.,7.,12.}) {
        const auto y=v4::render(x,shift,a,stretch);
        check(pt::usablePitchResult(x,y,stretch),"Stretch duration/finite/audibility failure");
        // Measure rising crossings away from the synthetic edge cycles.
        std::vector<double> crossings;
        for(size_t i=size_t(.2*y.rate);i<size_t(y.left.size()-.2*y.rate);++i)
            if(y.left[i-1]<0 && y.left[i]>=0)
                crossings.push_back(i-1-y.left[i-1]/double(y.left[i]-y.left[i-1]));
        check(crossings.size()>4,"Too few periodic cycles");
        const double hz=(crossings.size()-1)*y.rate/(crossings.back()-crossings.front());
        check(std::abs(1200*std::log2(hz/(110*std::pow(2.,shift/12))))<3.0,"Stretch changed pitch");
        pt::Settings s;s.trimMs=40*stretch;
        const auto overview=pt::buildKickOverview(nullptr,&y,s,36);
        check(std::abs(overview.totalMs-(y.duration()*1000-40*stretch))<1.e-6,"Stretched trim / preview mismatch");
        if(stretch==1 && shift==0)check(y.left==x.left && y.right==x.right,"100% root must be bit identical");
        std::cout<<"PASS stretch "<<stretch*100<<"% shift "<<shift<<" st pitch "<<hz<<" Hz\n";
    }
    std::cout<<"PASS stretch duration, pitch, stereo, trim and overview\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
