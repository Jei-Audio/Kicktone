#include "../Source/PitchShift.h"
#include "V4Render.h"
#include "../Source/PitchBankPolicy.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "WaveFixture.h"
int main(int argc,char** argv)try {
    if(argc<2)throw std::runtime_error("Missing WAV path");
    auto x=loadFloatWave(argv[1]);
    auto a=pt::cycle::analyze(x);
    if(x.left.size()!=17456 || a.marks.size()<8 || a.fundamentalHz<45 || a.fundamentalHz>52)
        throw std::runtime_error("Tail fixture analysis failed");
    for(double semi:{-12.,-7.,-3.,0.,3.,7.,12.}){
        auto y=v4::pitchShift(x,semi,a);
        if(y.left.size()!=x.left.size() || y.right.size()!=x.right.size())
            throw std::runtime_error("A pitched tail changed length");
        for(float v:y.left)if(!std::isfinite(v))throw std::runtime_error("Nonfinite left output");
        for(float v:y.right)if(!std::isfinite(v))throw std::runtime_error("Nonfinite right output");
        std::cout<<"PASS real-tail shift "<<semi<<" st "<<y.left.size()<<" frames\n";
    }
    const double root=69+12*std::log2(a.fundamentalHz/440.0);
    for(double stretch:{.8,1.,1.5}) for(int note=15;note<=51;++note) {
        auto y=v4::render(x,note-root,a,stretch);
        if(!pt::usablePitchResult(x,y,stretch))throw std::runtime_error("Real tail stretch output failed");
    }
    std::cout<<"PASS real tail 80/100/150% across all 37 supported MIDI notes\n";
    std::cout<<"PASS real tail fixed duration across note shifts\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
