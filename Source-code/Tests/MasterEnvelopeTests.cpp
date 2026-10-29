#include "../Source/MasterEnvelope.h"
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
int main()try {
    for(double sr:{8000.,44100.,48000.,96000.,192000.}) {
        tt::MasterEnvelope e;e.start(sr,.1,{});
        const int frames=int(sr*.1);
        for(int i=0;i<frames;++i){const float g=e.next();
            if(i<int(sr*.099)-1)check(g==1,"Default changes kick body");
            check(g>=0&&g<=1,"Envelope outside gain range");
            if(i==frames-1)check(g<1e-5,"Natural end did not reach zero");}
        check(e.next()==0,"Natural release extends duration");
        e.start(sr,1,{10,20,40,5});
        check(e.next()==0,"Attack begins above zero");
        for(int i=1;i<int(sr*.05);++i)e.next();
        check(std::abs(e.next()-.4f)<1e-5,"Decay failed to reach sustain");
        e.noteOff();float prev=1;
        for(int i=0;i<int(sr*.005)+2;++i){float g=e.next();check(g<=prev,"Release grew louder");prev=g;}
        check(e.next()==0&&!e.isActive(),"MIDI release failed to finish");
        e.start(sr,1,{0,0,100,0});e.next();e.noteOff();check(e.next()==0,"Zero release did not stop immediately");
    }
    std::cout<<"PASS ADSR stages, unity defaults, endpoint zero, sample rates, zero release\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
