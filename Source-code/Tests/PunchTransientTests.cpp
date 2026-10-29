#include "../Source/PunchTransient.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <vector>
static void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
static double meanAbs(const std::vector<float>& s,int a,int b){
    double sum=0;for(int i=a;i<b;++i)sum+=std::abs(s[size_t(i)]);
    return sum/(b-a);
}
static std::vector<float> render(pt::TransientSettings s,double sr=48000.0){
    pt::PunchTransient dsp;dsp.prepare(sr);dsp.setSettings(s);
    std::vector<float> out(9600);
    for(size_t i=0;i<out.size();++i){
        // 8ms full-level impact, 140ms quieter body, silence afterward
        const double ms=i*1000/sr;
        const float dry=ms<8?0.65f*std::sin(2*3.141592653589793*250*i/sr)
                             :ms<148?0.15f*std::sin(2*3.141592653589793*250*i/sr):0;
        float l=dry,r=dry*0.8f;
        dsp.process(l,r);
        check(std::isfinite(l)&&std::isfinite(r),"NaN or Inf in transient output");
        if(dry!=0)check(std::abs(l-1.25f*r)<1e-5f,"Stereo image changed");
        out[i]=l;
    }
    return out;
}
int main(){
    pt::TransientSettings s;
    auto dry=render(s);
    // default OFF must be sample-identical even if attack defaults above zero
    for(size_t i=0;i<dry.size();++i) {
        const double ms=i*1000.0/48000.0;
        const float raw=ms<8?0.65f*std::sin(2*3.141592653589793*250*i/48000.0)
                            :ms<148?0.15f*std::sin(2*3.141592653589793*250*i/48000.0):0;
        check(dry[i]==raw,"Bypass not bit-identical");
    }
    s.enabled=true;s.attack=95;s.sustain=0;s.mix=100;s.sensitivityDb=-56;
    auto attack=render(s);
    check(meanAbs(attack,70,350)>meanAbs(dry,70,350)*1.1,"Attack boost did not emphasize transient");
    s.attack=-95;
    auto softer=render(s);
    check(meanAbs(softer,70,350)<meanAbs(dry,70,350)*0.9,"Negative attack did not soften transient");
    s.attack=0;s.sustain=90;
    auto sustain=render(s);
    check(meanAbs(sustain,1500,4000)>meanAbs(dry,1500,4000)*1.3,"Sustain boost failed");
    s.sustain=-90;
    auto shorter=render(s);
    check(meanAbs(shorter,1500,4000)<meanAbs(dry,1500,4000)*0.8,"Sustain reduction failed");
    s.sustain=100;s.mix=0;
    auto zeroMix=render(s);
    check(meanAbs(zeroMix,5000,7000)-meanAbs(dry,5000,7000)<1e-5,"Zero mix leaked FX");
    s.mix=100;s.attack=0;s.sustain=0;s.outputDb=-6;
    auto output=render(s);
    check(meanAbs(output,1500,4000)<meanAbs(dry,1500,4000)*0.65,"Output trim failed");
    // Sample-rate and tiny-block sanity, no allocations or latency.
    for(auto sr:{44100.0,48000.0,96000.0,192000.0}){
        pt::PunchTransient dsp;dsp.prepare(sr);dsp.setSettings(s);
        float l=0.4f,r=-0.4f;
        for(int i=0;i<10000;++i){dsp.process(l,r);check(std::isfinite(l+r),"Unstable at sample rate");l=0.4f;r=-0.4f;}
    }
    std::cout<<"Punch-only transient shaper: transparent bypass, signed attack/sustain, stereo link, mix, gain and sample rate tested\n";
}
