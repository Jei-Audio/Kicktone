#include "../Source/LinearPhaseEQ.h"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static double response(const std::vector<float>& taps,int radius,double hz,double rate){
    std::complex<double> value{};for(size_t i=0;i<taps.size();++i)value+=double(taps[i])*std::polar(1.,-2*tt::pi*hz*(double(i)-radius)/rate);return std::abs(value);
}
int main()try{
    std::array<tt::Band,tt::kBands> bands{};
    for(double rate:{8000.,44100.,48000.,96000.,192000.}){
        auto normal=tt::linear::design(bands,false,rate);
        auto mono=tt::linear::design(bands,false,rate,true);
        check(mono->latency()==normal->latency(),"Mono sub changed the linear path's latency");
        check(mono->taps==normal->taps,"Mono sub changed mid/EQ response");
        check(mono->matches(bands,false,rate,true)&&!mono->matches(bands,false,rate,false),"Mono sub missing from worker cache key");
        for(size_t i=0;i<mono->sideTaps.size();++i)check(mono->sideTaps[i]==mono->sideTaps[mono->sideTaps.size()-1-i],"Mono crossover is not linear phase");
        for(double hz:{20.,40.,65.,80.,90.})check(response(mono->sideTaps,mono->radius,hz,rate)<.002,"Stereo sub leaked through the 90 Hz mono band");
        for(double hz:{200.,500.,1000.})check(std::abs(response(mono->sideTaps,mono->radius,hz,rate)-1)<.002,"Mono sub removed upper stereo width");
    }
    // v0.4.56 adjustable crossover: 90 Hz must stay sample-identical to the
    // original switch; 90..300 Hz keeps the same latency/mid path and guard.
    for(double rate:{44100.,48000.,96000.}){
        auto original=tt::linear::design(bands,false,rate,true);
        auto explicit90=tt::linear::design(bands,false,rate,true,90.);
        check(original->sideTaps==explicit90->sideTaps && original->taps==explicit90->taps,"Default 90 Hz mono sub changed from v0.4.55");
        for(double cut:{120.,150.,200.,250.,300.}){
            auto k=tt::linear::design(bands,false,rate,true,cut);
            check(k->latency()==original->latency() && k->taps==original->taps,"Mono sub frequency changed latency or mid/EQ response");
            check(k->matches(bands,false,rate,true,cut) && !k->matches(bands,false,rate,true,cut+5) && !k->matches(bands,false,rate,true,90.),"Mono sub frequency missing from worker cache key");
            for(size_t i=0;i<k->sideTaps.size();++i)check(k->sideTaps[i]==k->sideTaps[k->sideTaps.size()-1-i],"Adjustable mono crossover is not linear phase");
            for(double hz:{20.,cut*.5,cut*.8,cut})check(response(k->sideTaps,k->radius,hz,rate)<.002,"Stereo sub leaked below the selected mono frequency");
            for(double hz:{cut+80.,cut+200.,1000.,4000.})check(std::abs(response(k->sideTaps,k->radius,hz,rate)-1)<.002,"Mono sub removed stereo above the selected frequency");
        }
        check(tt::linear::design(bands,false,rate,true,40.)->subMonoHz==90 && tt::linear::design(bands,false,rate,true,900.)->subMonoHz==300 &&
              tt::linear::design(bands,false,rate,true,std::nan(""))->subMonoHz==90,"Mono sub frequency not clamped to 90..300 Hz");
        auto off=tt::linear::design(bands,false,rate,false,250.);
        check(off->matches(bands,false,rate,false,90.),"Frequency changes rebuilt the FIR while mono sub is off");
    }
    {
        auto k=tt::linear::design(bands,false,48000,true,300.);tt::linear::Convolver c;c.prepare(k);double side250=0;
        for(int i=0;i<30000;++i){const float a=float(.1*std::sin(2*tt::pi*250*i/48000)),b=float(.05*std::sin(2*tt::pi*1000*i/48000));
            const auto y=c.process(a+b,-(a+b));if(i>12000){const int d=i-k->latency();const double expected=.05*std::sin(2*tt::pi*1000*d/48000);
                side250=(std::max)(side250,std::abs(.5*(y[0]-y[1])-expected));}}
        check(side250<5.e-5,"300 Hz mono sub kept 250 Hz side or altered 1 kHz side");
    }
    auto normal=tt::linear::design(bands,false,48000),mono=tt::linear::design(bands,false,48000,true);
    tt::linear::Convolver processor;processor.prepare(mono);
    double midError=0,sideError=0;
    for(int i=0;i<30000;++i){
        const auto mid=[](int t){return float(.12*std::sin(2*tt::pi*55*t/48000)+.04*std::sin(2*tt::pi*700*t/48000));};
        const auto highSide=[](int t){return float(.06*std::sin(2*tt::pi*1000*t/48000));};
        const float side=float(.1*std::sin(2*tt::pi*65*i/48000))+highSide(i);
        const auto y=processor.process(mid(i)+side,mid(i)-side);
        if(i>12000){const int delayed=i-mono->latency();midError=(std::max)(midError,std::abs(double(.5f*(y[0]+y[1])-mid(delayed))));sideError=(std::max)(sideError,std::abs(double(.5f*(y[0]-y[1])-highSide(delayed))));}
    }
    check(midError<5.e-5,"Stereo matrix altered the mono sum or dry gain");
    check(sideError<5.e-5,"Stereo matrix failed sub removal/high-band preservation");
    // Disabling the switch restores the full low-frequency stereo image.
    processor.setKernel(normal);double leftEnergy=0,rightEnergy=0;
    for(int i=0;i<18000;++i){const float x=float(.1*std::sin(2*tt::pi*65*i/48000));const auto y=processor.process(x,-x);if(i>12000){leftEnergy+=y[0]*y[0];rightEnergy+=y[1]*y[1];check(std::abs(y[0]+y[1])<5.e-6,"Mono toggle damaged side polarity");}}
    check(leftEnergy>20&&rightEnergy>20,"Disabling mono failed to restore stereo sub");
    // Note resets must cut the mono FIR just as exactly as the ordinary wet FIR.
    tt::linear::NoteFilter notes;notes.prepare(mono);notes.startNote();
    for(int i=0;i<10000;++i)notes.process(.1f,-.06f);
    notes.startNote();float oldPeak=0;
    for(int i=0;i<mono->latency()+4000;++i){const auto y=notes.process(0,0);if(i<mono->latency())oldPeak=(std::max)(oldPeak,std::abs(y[0]));else check(std::abs(y[0])<1.e-8&&std::abs(y[1])<1.e-8,"Mono sub FIR spilled past a note boundary");}
    check(oldPeak>.01,"Mono reset lost compensated previous-note audio");
    std::cout<<"PASS 90..300 Hz adjustable mono sub (90 Hz identical to v0.4.55), upper stereo, invariant mid/EQ/latency, rates, live OFF restoration and exact note cutoff\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
