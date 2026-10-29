#include "../Source/Engine.h"
#include "../Source/DistortionCurve.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
static void verify(bool ok,const char* msg){if(!ok) throw std::runtime_error(msg);}
static pt::Sample sample(float x,double seconds=0.3) {
    pt::Sample s;s.rate=44100;
    s.left.assign(size_t(seconds*44100),x);s.right=s.left;return s;
}
int main() try {
    for (int mode=0;mode<3;++mode) {
        verify(std::abs(pt::distort(-0.6f,mode)+pt::distort(0.6f,mode))<1e-7f,
               "Waveshaper is not symmetric");
        verify(std::abs(pt::distort(1000.0f,mode))<=1.0f,
               "Waveshaper exceeded full scale");
        verify(std::isfinite(pt::distort(3.0f,mode)),"Waveshaper produced invalid audio");
    }
    verify(std::abs(pt::distort(2.0f,1)-1.0f)<1e-7f,"Hard clip not limiting input");
    pt::Settings s;
    verify(s.gate,"MIDI gate must default ON in fresh settings");
    s.punchGain=s.tailGain=s.reverbGain=s.output=1;
    s.crossfadeMs=0;s.joinMs=0;
    const auto punch=sample(0.8f,.1),tail=sample(.3f,.3),reverb=sample(.2f,.3);
    pt::Voice a,b;
    a.start(36,1,1,&punch,&tail,s,&reverb);
    b.start(36,1,1,&punch,&tail,s,&reverb);
    double punchEnergy=0,tailEnergy=0,changed=0;
    for(int i=0;i<14000;++i){
        const auto channels=a.nextParts(&punch,&tail,44100,&reverb);
        const auto full=b.next(&punch,&tail,44100,&reverb);
        verify(std::abs(channels.punch[0]+channels.tailReverb[0]-full[0])<1e-6f,
               "Separated buses changed original voice playback");
        if(i<4300){punchEnergy+=std::abs(channels.punch[0]);
                   verify(std::abs(channels.tailReverb[0])<1e-6f,"Tail/reverb leaked into punch-only time");}
        if(i>5000 && i<10000){tailEnergy+=std::abs(channels.tailReverb[0]);
                   verify(std::abs(channels.punch[0])<1e-6f,"Punch leaked into tail/reverb section");}
        changed+=std::abs(channels.tailReverb[0]);
    }
    verify(punchEnergy>100 && tailEnergy>100 && changed>100,"Missing isolated audio bus");
    std::cout<<"PASS separated punch and combined tail/reverb buses, unchanged summed voice, MIDI gate default\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
