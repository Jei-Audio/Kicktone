#include "../Source/TailSubDuck.h"
#include <stdexcept>
#include <iostream>
static void ck(bool v,const char* m){if(!v)throw std::runtime_error(m);}
static double band(float hz){
 pt::TailSubDuck duck;pt::TailReverbSettings s;s.duckAmount=100;s.duckHz=200;duck.prepare(48000,s);
 double energy=0;for(int i=0;i<24000;++i){float x=float(.2*std::sin(2*3.14159265358979323846*hz*i/48000));std::array<float,2> wet{x,x*.5f};duck.process(wet,.6f,.6f);ck(std::abs(wet[1]-wet[0]*.5f)<1.e-6,"Stereo duck gain is not linked");if(i>12000)energy+=wet[0]*wet[0];}return energy;
}
int main(){
 ck(band(5000)>230,"Sub duck reduced the high band");
 ck(band(50)<band(5000)*.12,"Ducking did not isolate the sub band");
 pt::TailSubDuck duck;pt::TailReverbSettings s;s.duckAmount=100;s.duckRecovery=40;duck.prepare(48000,s);std::array<float,2> wet{};
 for(int i=0;i<24000;++i){wet={.2f,.2f};duck.process(wet,.6f,.6f);}ck(std::abs(wet[0])<.001,"Loud dry tail did not duck wet sub");
 float halfway=0;for(int i=0;i<12000;++i){wet={.2f,.2f};duck.process(wet,0,0);if(i==960)halfway=wet[0];}
 ck(halfway>0 && wet[0]>.195f,"Sub did not smoothly return as dry tail faded");
 duck.prepare(48000,s);for(int i=0;i<24000;++i){wet={.2f,.2f};duck.process(wet,.6f,.6f);}
 float previous=-1;for(int i=0;i<24000;++i){const float dry=.6f*(1-float(i)/24000);wet={.2f,.2f};duck.process(wet,dry,dry);if(i%4000==0){ck(wet[0]>previous,"Sub did not track the gradual dry fade");previous=wet[0];}}
 duck.clear();for(int i=0;i<128;++i){wet={0,0};duck.process(wet,0,0);ck(wet[0]==0&&wet[1]==0,"Note reset leaked crossover history");}
 s.duckAmount=0;duck.prepare(48000,s);for(int i=0;i<2400;++i){wet={.123f,-.321f};duck.process(wet,.7f,.7f);ck(wet[0]==.123f&&wet[1]==-.321f,"Zero Amount changed wet return");}
 std::cout<<"PASS linked sub isolation, continuous duck/recovery, exact note reset and zero Amount identity\n";
}
