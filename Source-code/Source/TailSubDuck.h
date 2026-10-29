#pragma once
#include "TailReverb.h"
namespace pt {
// Complementary one-pole split: only the generated wet low band is reduced.
// Stereo-linked RMS follows this note's dry tail relative to its loudest level.
class TailSubDuck {
    double rate=44100,envelope=0,peak=0;
    float lowL=0,lowR=0,lowCoeff=0,attack=0,release=0,amount=0,target=0,amountCoeff=0;
    float lastHz=-1,lastRecovery=-1;
public:
    void prepare(double sr,TailReverbSettings s={}) {
        rate=sr;lastHz=lastRecovery=-1;amountCoeff=float(1-std::exp(-1/(sr*.01)));
        attack=float(1-std::exp(-2/(sr*.002)));clear();setSettings(s);amount=target;
    }
    void clear(){envelope=peak=0;lowL=lowR=0;amount=target;}
    void setSettings(TailReverbSettings s){
        target=std::clamp(s.duckAmount,0.f,100.f)*.01f;
        if(s.duckHz!=lastHz){lastHz=s.duckHz;lowCoeff=float(1-std::exp(-2*3.14159265358979323846*std::clamp(double(s.duckHz),40.,250.)/rate));}
        if(s.duckRecovery!=lastRecovery){lastRecovery=s.duckRecovery;release=float(1-std::exp(-2/(rate*std::clamp(double(s.duckRecovery),20.,300.)*.001)));}
    }
    void process(std::array<float,2>& wet,float dryL,float dryR){
        const double input=(double(dryL)*dryL+double(dryR)*dryR)*.5;
        envelope+=(input>envelope?attack:release)*(input-envelope);
        peak=(std::max)(peak,envelope);
        const float relative=peak>1.e-12?float(std::sqrt(std::clamp(envelope/peak,0.,1.))):0;
        amount+=amountCoeff*(target-amount);
        lowL+=lowCoeff*(wet[0]-lowL);lowR+=lowCoeff*(wet[1]-lowR);
        // Explicit zero Amount leaves the prior generated return bit-identical.
        if(amount==0)return;
        const float reduction=amount*relative;
        wet[0]-=lowL*reduction;wet[1]-=lowR*reduction;
    }
};
}
