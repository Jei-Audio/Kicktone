#include "../Source/Engine.h"
#include "../Source/KickOverview.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
static void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
static double response(int slope, double hz, double sampleRate){
    pt::ReverbHPFState state;
    state.reset(pt::makeReverbHPF(sampleRate,slope));
    // Give the resonant high-order sections time to settle; measure RMS.
    const int length=int(sampleRate*2);
    double before=0,after=0;
    for(int i=0;i<length;++i){
        const float input=std::sin(2*pt::pi*hz*i/sampleRate);
        const float output=state.process(0,input);
        if(i>=length/2){before+=input*input;after+=output*output;}
    }
    return std::sqrt(after/before);
}
static pt::Sample tone(double hz,double sr=48000){
    pt::Sample s;s.rate=sr;s.left.resize(size_t(sr*0.6));s.right.resize(s.left.size());
    for(size_t i=0;i<s.left.size();++i){s.left[i]=float(std::sin(2*pt::pi*hz*i/sr));s.right[i]=0;}
    return s;
}
int main() try {
    for(double sr:{44100.,48000.,96000.}){
        check(pt::makeReverbHPF(sr,0).count==0,"OFF must be bypass");
        for(int slope:{12,24,48}){
            const auto c=pt::makeReverbHPF(sr,slope);
            check(c.count==slope/12,"Wrong number of Butterworth 2-pole sections");
            check(std::abs(response(slope,200,sr)-std::sqrt(0.5))<0.012,"200 Hz cutoff is not -3dB");
            check(response(slope,5000,sr)>0.995,"Filter attenuated treble");
        }
        const auto low12=response(12,50,sr),low24=response(24,50,sr),low48=response(48,50,sr);
        check(low12<0.07 && low12>0.05,"12 dB/oct response incorrect");
        check(low24<0.005 && low24>0.003,"24 dB/oct response incorrect");
        check(low48<0.00005,"48 dB/oct response incorrect");
        check(std::abs(response(0,50,sr)-1.0)<0.00001,"OFF changed unfiltered audio");
    }
    // Source is intentionally independent from the pitched tail. Changing
    // reverb slope cannot change punch, tail, or recorded reverb length.
    auto punch=tone(1000),tail=tone(80),reverb=tone(50);
    pt::Settings settings;settings.output=1;settings.reverbGain=1;
    settings.punchEndOffsetMs=500; settings.reverbDelayMs=40;
    pt::Voice dry, filtered;
    dry.start(36,1,1.0f,&punch,&tail,settings,&reverb);
    filtered.start(36,1,1.0f,&punch,&tail,settings,&reverb);
    filtered.setReverbFilter(pt::makeReverbHPF(48000,48),48000,true);
    check(std::abs(dry.audibleEnd()-filtered.audibleEnd())<1e-12,"Filter changed kick duration");
    double beforeOnset=0, postDiff=0;
    for(int i=0;i<24000;++i){
        const auto a=dry.next(&punch,&tail,48000,&reverb),b=filtered.next(&punch,&tail,48000,&reverb);
        if(i<6720) beforeOnset=std::max(beforeOnset,double(std::abs(a[0]-b[0])));
        if(i>12000) postDiff=std::max(postDiff,double(std::abs(a[0]-b[0])));
        check(std::abs(a[1]-b[1])<1e-6,"Filter leaked into silent right reverb channel");
    }
    check(beforeOnset<1e-6,"Filter changed punch or tail before reverb began");
    check(postDiff>0.02,"Filter did not alter low-frequency reverb");
    // The control changes on active notes without allocating or triggering
    // the pitch bank. A short crossfade prevents a full-level discontinuity.
    pt::Voice active;active.start(36,1,1,&punch,&tail,settings,&reverb);
    for(int i=0;i<15000;++i)active.next(&punch,&tail,48000,&reverb);
    active.setReverbFilter(pt::makeReverbHPF(48000,24),48000);
    check(active.filterSlope()==24,"Active-note slope was not selected");
    for(int i=0;i<1000;++i){
        const auto out=active.next(&punch,&tail,48000,&reverb);
        check(std::isfinite(out[0]) && std::isfinite(out[1]),"Automation produced non-finite audio");
    }
    check(active.reverbFilterTransition==0 && active.reverbFilter.design.slope==24,"Slope crossfade did not finish");
    active.setReverbFilter(pt::makeReverbHPF(48000,0),48000);
    for(int i=0;i<480;++i) active.next(&punch,&tail,48000,&reverb);
    check(active.reverbFilter.design.count==0,"OFF automation did not return to bypass");
    settings.reverbLowCutSlope=48;
    auto preview=pt::buildKickOverview(&punch,&tail,settings,36,900,&reverb);
    check(std::abs(preview.totalMs-dry.audibleEnd()*1000)<1,"Preview did not preserve natural reverb duration");
    std::cout<<"PASS 200 Hz Butterworth 12/24/48 dB/oct (44.1/48/96k), OFF identity, reverb-only routing, live changes and preview\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
