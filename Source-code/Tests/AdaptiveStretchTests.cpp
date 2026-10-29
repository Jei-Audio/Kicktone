#include "../Source/AdaptiveStretch.h"
#include "../Source/KickCharacterPitch.h" // v0.4.61: Adaptive Stretch v4 = Highest Fidelity v4 engine
#include "../Source/PitchBankPolicy.h"
#include "WaveFixture.h"
#include <iostream>
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(int argc,char** argv)try{
 check(argc>1,"Missing user kick fixture");const auto x=loadFloatWave(argv[1]);const auto analysis=pt::cycle::analyze(x);
 for(double amount:{0.,25.,50.,100.}){
    check(pt::adaptiveDurationRatio(0,amount)==1,"Root duration changed");
    for(int distance=1;distance<=24;++distance){const auto up=pt::adaptiveDurationRatio(distance,amount),down=pt::adaptiveDurationRatio(-distance,amount);check(up<=down&&down<=1,"Adaptation direction reversed");if(amount>0)check(up<down&&down<1,"Higher/lower shortening is not asymmetric");}
 }
 check(std::abs(pt::adaptiveDurationRatio(12,100)-.5)<1.e-12,"Upward octave ratio");check(std::abs(pt::adaptiveDurationRatio(-12,100)-std::sqrt(.5))<1.e-12,"Downward octave ratio");
 for(double amount:{200.,400.}){
    check(pt::adaptiveDurationRatio(0,amount)==1,"Stronger amount changed root duration");
    for(int distance=1;distance<=24;++distance){const auto up=pt::adaptiveDurationRatio(distance,amount),down=pt::adaptiveDurationRatio(-distance,amount);check(.25<=up&&up<=down&&down<1,"Stronger adaptation lost direction or duration bound");if(down>.25)check(up<down,"Stronger unsaturated adaptation lost asymmetry");}
 }
 check(std::abs(pt::adaptiveDurationRatio(3,400)-.5)<1.e-12,"400% did not halve a nearby higher bass note");
 check(std::abs(pt::adaptiveDurationRatio(-3,400)-std::sqrt(.5))<1.e-12,"400% did not shorten nearby lower bass note gently");
 check(pt::adaptiveDurationRatio(3,200)<pt::adaptiveDurationRatio(3,100),"Amount above 100% was still clamped");
 for(double base:{.8,1.,1.5}){
    const auto prepared=pt::character::prepare(x,analysis,base);
    for(double amount:{100.,400.})for(int note=15;note<=51;++note){const double shift=note-36.;const double stretch=base*pt::adaptiveDurationRatio(shift,amount);const auto y=pt::character::render(x,shift,prepared,{},nullptr,stretch);
        check(y.left.size()==size_t(std::llround(x.left.size()*stretch)),"Adaptive output duration wrong");check(pt::usablePitchResult(x,y,stretch),"Adaptive user kick lost usable audio");
        if(note==36&&base==1)check(y.left==x.left&&y.right==x.right,"Adaptive root is not exact source");
        double maxJump=0,peak=0;for(size_t i=1;i<y.left.size();++i){maxJump=(std::max)(maxJump,std::abs(double(y.left[i]-y.left[i-1])));peak=(std::max)(peak,std::abs(double(y.left[i])));check(std::isfinite(y.right[i]),"Adaptive stereo output not finite");}check(peak<4,"Adaptive runaway level");
    }
    const auto ordinary=pt::character::render(x,7,prepared),zero=pt::character::render(x,7,prepared,{},nullptr,base*pt::adaptiveDurationRatio(7,0));check(ordinary.left==zero.left&&ordinary.right==zero.right,"0% adaptation differs from Highest Fidelity v4");
 }
 // A changing envelope must reach its late portion, rather than truncate it.
 pt::Sample tone;tone.rate=24000;tone.left.resize(24000);tone.right.resize(24000);
 for(size_t i=0;i<tone.left.size();++i){const double amp=i<12000?.1:.7;tone.left[i]=float(amp*std::sin(2*pt::pi*65.406*i/tone.rate));tone.right[i]=-.5f*tone.left[i];}
 const auto a=pt::cycle::analyze(tone);const auto p=pt::character::prepare(tone,a);
 for(double amount:{100.,400.})for(double shift:{-3.,3.,-12.,12.}){const auto y=pt::character::render(tone,shift,p,{},nullptr,pt::adaptiveDurationRatio(shift,amount));double early=0,late=0;std::vector<double> crossings;
    for(size_t i=y.left.size()/10;i<y.left.size()*9/10;++i){if(i<y.left.size()*4/10)early+=y.left[i]*y.left[i];if(i>y.left.size()*6/10){late+=y.left[i]*y.left[i];if(y.left[i-1]<0&&y.left[i]>=0)crossings.push_back(double(i-1)-y.left[i-1]/double(y.left[i]-y.left[i-1]));}check(std::abs(y.right[i]+.5f*y.left[i])<2.e-6,"Adaptive stereo coherence lost");}
    check(late>early*20,"Adaptive truncated the late source instead of time compressing");check(crossings.size()>2,"Adaptive pitch fixture lacks cycles");const double measured=(crossings.size()-1)*y.rate/(crossings.back()-crossings.front());check(std::abs(1200*std::log2(measured/(65.406*std::pow(2.,shift/12.))))<22,"Adaptive changed requested note pitch");
 }
 std::cout<<"PASS adaptive full note range on supplied kick, stretch limits, root/zero identity, asymmetry, complete-envelope compression and pitch\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
