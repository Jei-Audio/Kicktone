#include "../Source/PunchRegionEQ.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static bool countAlloc=false;static size_t allocations=0;
void* operator new(std::size_t n){if(countAlloc)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static void ck(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static tt::PunchRegionDesign design(bool enabled,double attackGain,double decayGain,double split=50,double width=2){
 std::array<tt::Band,tt::kBands> a{},d{};a[0]={true,tt::LowShelf,0,400,attackGain,.71};d[0]=a[0];d[0].gain=decayGain;
 tt::PunchRegionDesign out;out.update({enabled,split,width},a,true,d,true,48000);return out;
}
static void regions(){
 auto off=design(false,12,-12),on=design(true,12,-12),decayOnly=design(true,0,-12);
 tt::PunchRegionEQ dry,wet,dec;dry.start(off,48000,.2);wet.start(on,48000,.2);dec.start(decayOnly,48000,.2);
 double inEarly=0,outEarly=0,inLate=0,outLate=0;
 for(int i=0;i<9600;++i){float l=float(.1*std::sin(2*tt::pi*85*i/48000)),r=l*.7f;float dl=l,dr=r,wl=l,wr=r,cl=l,cr=r;
  const double t=double(i)/48000;dry.process(dl,dr,t);wet.process(wl,wr,t);dec.process(cl,cr,t);
  ck(dl==l&&dr==r,"OFF did not retain exact Punch identity");
  if(i<4000)ck(std::abs(cl-l)<1e-7&&std::abs(cr-r)<1e-7,"Decay EQ affected Attack before transition");
  if(i>6000)ck(wl==cl&&wr==cr,"Attack EQ affected Decay after transition");
  if(i>1000&&i<4000){inEarly+=l*l;outEarly+=wl*wl;}
  if(i>6000){inLate+=l*l;outLate+=wl*wl;}
 }
 ck(outEarly>inEarly*5,"Attack EQ boost not audible in attack region");ck(outLate<inLate*.3,"Decay EQ cut not audible in decay region");
 float l=.5f,r=.5f;wet.process(l,r,.2);ck(l==0&&r==0,"Regional filters leaked after selected Punch End");
 // New note state must not inherit ringing from a previous Punch.
 auto d=design(true,12,-12);tt::PunchRegionEQ fresh;wet.start(d,48000,.2);fresh.start(d,48000,.2);
 for(int i=0;i<1000;++i){float a=i==0?.1f:0,b=a,c=a,e=a;wet.process(a,b,double(i)/48000);fresh.process(c,e,double(i)/48000);ck(a==c&&b==e,"Retrigger retained filter history");}
 // Continuous, constant-amplitude region weights; no overlap boost at unity.
 auto flat=design(true,0,0,50,10);wet.start(flat,48000,.2);
 for(int i=0;i<9600;++i){float a=.5f,b=-.5f;wet.process(a,b,double(i)/48000);ck(std::abs(a-.5f)<1e-7&&std::abs(b+.5f)<1e-7,"Neutral transition changed gain or stereo");}
 // Split endpoints select one treatment over the complete Punch.
 auto zero=design(true,12,-12,0),full=design(true,12,-12,100);wet.start(zero,48000,.1);fresh.start(full,48000,.1);
 double ez=0,ef=0;for(int i=0;i<4800;++i){float a=float(.1*std::sin(2*tt::pi*85*i/48000)),b=a,c=a,e=a;wet.process(a,b,double(i)/48000);fresh.process(c,e,double(i)/48000);if(i>500){ez+=a*a;ef+=c*c;}}
 ck(ef>ez*20,"0/100% region endpoints failed");
 // Preserve the split fraction when a prepared/resampled Punch is shorter.
 wet.start(on,48000,.1);fresh.start(on,48000,.2);
 float a=.1f,b=.1f,c=.1f,e=.1f;wet.process(a,b,.08);fresh.process(c,e,.08);ck(std::abs(a-c)>1e-5,"Region timing ignored per-note Punch duration");
 // A timeline automation change must stay finite and bounded.
 wet.start(on,48000,.2);float last=0;double jump=0;
 for(int i=0;i<9600;++i){if(i==2000){auto next=on;next.settings.splitPercent=5;next.settings.enabled=false;wet.update(next);}float a=float(.02*std::sin(2*tt::pi*85*i/48000)),b=a;wet.process(a,b,double(i)/48000);ck(std::isfinite(a)&&std::isfinite(b),"Region automation produced invalid audio");jump=std::max(jump,double(std::abs(a-last)));last=a;}
 ck(jump<.01,"Region enable/boundary automation caused a discontinuity");
 // Regional output level must work even with neutral/bypassed EQ banks.
 auto levels=flat;levels.settings.attackGainDb=6;levels.settings.decayGainDb=-6;
 wet.start(levels,48000,.2);
 float early=.1f,er=.07f,late=.1f,lr=.07f;
 wet.process(early,er,.01);wet.process(late,lr,.15);
 ck(std::abs(early-.1*std::pow(10.,6./20))<1e-7 && std::abs(late-.1*std::pow(10.,-6./20))<1e-7,"Independent Attack/Decay output gain incorrect");
 levels.settings.enabled=false;wet.start(levels,48000,.2);early=.1f;er=-.1f;wet.process(early,er,.01);
 ck(early==.1f && er==-.1f,"Regional gain changed disabled Punch");
 levels.settings.enabled=true;levels.settings.attackGainDb=0;levels.settings.decayGainDb=0;wet.start(levels,48000,.2);
 levels.settings.attackGainDb=24;levels.settings.decayGainDb=-24;wet.update(levels);
 float previous=.01f;double gainJump=0;
 for(int i=0;i<480;++i){float l=.01f,r=l;wet.process(l,r,double(i)/48000);gainJump=(std::max)(gainJump,double(std::abs(l-previous)));previous=l;}
 ck(gainJump<.001 && std::abs(previous-.01*std::pow(10.,24./20))<1e-6,"Regional gain automation is not smoothly ramped to target");
 // All note start/update/sample work is fixed-size and allocation-free.
 allocations=0;countAlloc=true;wet.start(on,48000,.2);wet.update(flat);
 for(int i=0;i<1000;++i){float a=.1f,b=.1f;wet.process(a,b,double(i)/48000);}countAlloc=false;
 ck(allocations==0,"Regional EQ allocated on its audio path");
}
int main()try{regions();std::cout<<"PASS Punch region isolation, bypass identity, transition, retrigger, duration, automation and allocation checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
