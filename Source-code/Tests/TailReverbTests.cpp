#include "../Source/TailReverb.h"
#include <stdexcept>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
static void ck(bool v,const char* m){if(!v)throw std::runtime_error(m);}
static double energy(float space,float width,float distance,int from,int to){
 pt::TailReverb fx;fx.prepare(48000);pt::TailReverbSettings s;s.spaceMs=space;s.width=width;s.distance=distance;fx.setSettings(s);
 double e=0;for(int i=0;i<to;++i){auto w=fx.process(i==0?1.f:0,0);ck(std::isfinite(w[0])&&std::isfinite(w[1]),"Nonfinite reverb");if(width==0)ck(w[0]==w[1],"Zero width is not mono");if(i>=from)e+=w[0]*w[0]+w[1]*w[1];}return e;
}
static void diffusionTest(){
 for(double sr:{44100.,48000.,96000.,192000.}){
  pt::TailReverb fx;fx.prepare(sr);pt::TailReverbSettings s;s.spaceMs=1500;s.distance=0;fx.setSettings(s);
  double ll=0,rr=0,lr=0;int nonzero=0,count=0;
  for(int i=0;i<int(sr*.5);++i){auto w=fx.process(i==0?1.f:0,i==0?1.f:0);
   ck(std::isfinite(w[0])&&std::isfinite(w[1]),"Diffused network became nonfinite");
   if(i<int(sr*.030))ck(w[0]==0&&w[1]==0,"Room paths unexpectedly reverted to tiny delays");
   if(i>=int(sr*.1)&&i<int(sr*.25)){++count;nonzero+=std::abs(w[0])+std::abs(w[1])>1.e-7f;ll+=w[0]*w[0];rr+=w[1]*w[1];lr+=w[0]*w[1];}}
  ck(nonzero>count*.95,"Late reverb must have a dense response, not isolated reflections");
  ck(ll>1.e-8&&rr>1.e-8&&std::abs(lr/std::sqrt(ll*rr))<.8,"Mono send did not produce a spacious stereo field");
  // Reset all diffusion/modulation histories deterministically, too.
  fx.clear();pt::TailReverb fresh;fresh.prepare(sr);fresh.setSettings(s);
  for(int i=0;i<int(sr*.3);++i){auto a=fx.process(i==0?.3f:0,0),b=fresh.process(i==0?.3f:0,0);ck(a==b,"Clear did not fully reset the new diffusers/modulation");}
 }
}
static void punchPreDelayTest(){
 pt::TailReverb fx;fx.prepare(48000);pt::TailReverbSettings s;s.distance=0;s.punchPreDelayMs=100;s.spaceMs=600;fx.setSettings(s);
 bool heard=false;for(int i=0;i<12000;++i){auto w=fx.process(i==0?1.f:0.f,0);if(i<4800)ck(w[0]==0&&w[1]==0,"Punch pre-delay leaked before its selected offset");if(i>=4800)heard|=std::abs(w[0])+std::abs(w[1])>0;}
 ck(heard,"Punch pre-delay never released the send into the reverb");
 s.punchPreDelayMs=0;fx.setSettings(s);fx.clear();for(int i=0;i<12000;++i){auto w=fx.process(i==0?1.f:0.f,0);if(i<1000)ck(w[0]==0&&w[1]==0,"Room network should retain its own early diffusion time");}
}
// v0.4.59 V2 engine: stability at every extreme, exact epoch reset, Space as
// RT60, denser echoes than Legacy, matched loudness, mono at zero width,
// decorrelated stereo, runtime engine switching, and the shared fade helper.
static std::vector<float> impulse(int engine,double sr,float space,float width,float distance,size_t n,std::vector<float>* right=nullptr){
 pt::TailReverb fx;fx.prepare(sr);pt::TailReverbSettings s;s.engine=engine;s.spaceMs=space;s.width=width;s.distance=distance;fx.setSettings(s);
 std::vector<float> out(n);if(right)right->assign(n,0);
 for(size_t i=0;i<n;++i){auto w=fx.process(i==0?1.f:0,i==0?.7f:0);ck(std::isfinite(w[0])&&std::isfinite(w[1]),"Nonfinite V2 reverb");out[i]=w[0];if(right)(*right)[i]=w[1];}
 return out;
}
static double echoDensity(const std::vector<float>& x,size_t start,size_t n){
 double m=0;for(size_t i=0;i<n;++i)m+=double(x[start+i])*x[start+i];const double sd=std::sqrt(m/double(n));
 size_t c=0;for(size_t i=0;i<n;++i)if(std::abs(x[start+i])>sd)++c;return double(c)/double(n)/.3173;
}
static double rt60(const std::vector<float>& x,double sr){
 std::vector<double> edc(x.size());double acc=0;for(size_t i=x.size();i-->0;){acc+=double(x[i])*x[i];edc[i]=acc;}
 double t5=-1,t25=-1;for(size_t i=0;i<x.size();++i){const double db=10*std::log10(edc[i]/edc[0]+1e-30);if(t5<0&&db<=-5)t5=double(i)/sr;if(db<=-25){t25=double(i)/sr;break;}}
 return (t25-t5)*3;
}
static void v2Test(){
 for(double sr:{44100.,48000.,96000.,192000.})for(float space:{20.f,600.f,3000.f})for(float width:{0.f,100.f,200.f})for(float distance:{0.f,100.f}){
  pt::TailReverb fx;fx.prepare(sr);pt::TailReverbSettings s;s.engine=1;s.spaceMs=space;s.width=width;s.distance=distance;s.punchPreDelayMs=300;fx.setSettings(s);
  float peak=0;double tailEnergy=0;const size_t n=size_t(sr*(space*.001*1.5+.6));
  for(size_t i=0;i<n;++i){auto w=fx.process(i<64?.9f:0,i<64?-.6f:0);ck(std::isfinite(w[0])&&std::isfinite(w[1]),"V2 nonfinite at extreme");
   if(width==0)ck(w[0]==w[1],"V2 zero width is not mono");peak=(std::max)({peak,std::abs(w[0]),std::abs(w[1])});if(i+size_t(sr*.05)>=n)tailEnergy+=w[0]*w[0];}
  ck(peak<4,"V2 output unbounded");ck(tailEnergy<1e-5,"V2 did not decay within 1.5 x Space");
  for(int i=0;i<int(sr*.2);++i)fx.process(.4f,-.2f);
  fx.clear();for(int i=0;i<int(sr*.3);++i){auto w=fx.process(0,0);ck(w[0]==0&&w[1]==0,"V2 reset leaked previous history");}
  // After clear() the engine must sound exactly like a freshly prepared one.
  for(int i=0;i<int(sr*.1);++i)fx.process(.3f,.1f);fx.clear(); // modulation phase must restart too
  pt::TailReverb fresh;fresh.prepare(sr);fresh.setSettings(s);
  for(int i=0;i<int(sr*.4);++i){const float x=i<200?std::sin(float(i)*.1f):0;auto a=fx.process(x,-x),b=fresh.process(x,-x);ck(a==b,"V2 clear() is not identical to a fresh prepare");}
 }
 constexpr double sr=48000;
 for(float space:{300.f,1200.f,2500.f}){
  const auto x=impulse(1,sr,space,100,20,size_t(sr*space*.0025)+24000);
  const double measured=rt60(x,sr)*1000;
  ck(measured>space*.8 && measured<space*1.4,"V2 Space no longer follows RT60");
 }
 const auto legacy=impulse(0,sr,1200,100,20,72000),v2r=impulse(1,sr,1200,100,20,72000);
 ck(echoDensity(v2r,size_t(sr*.08),size_t(sr*.02))>echoDensity(legacy,size_t(sr*.08),size_t(sr*.02))+.15,"V2 is not denser than Legacy");
 ck(echoDensity(v2r,size_t(sr*.08),size_t(sr*.02))>.8,"V2 late field is not near-Gaussian dense");
 double eL=0,eV=0;{const auto a=impulse(0,sr,600,100,20,48000),b=impulse(1,sr,600,100,20,48000);for(float v:a)eL+=v*v;for(float v:b)eV+=v*v;}
 ck(std::abs(10*std::log10(eV/eL))<1.5,"V2 loudness is not matched to Legacy at default settings");
 std::vector<float> right;const auto left=impulse(1,sr,1200,100,20,72000,&right);
 double lr=0,ll=0,rr=0;for(size_t i=4800;i<left.size();++i){lr+=left[i]*right[i];ll+=left[i]*left[i];rr+=right[i]*right[i];}
 ck(std::abs(lr/std::sqrt(ll*rr))<.35,"V2 stereo field is not decorrelated");
 // Runtime engine switch reuses prepared buffers; switching back is Legacy again.
 pt::TailReverb a,b;a.prepare(sr);b.prepare(sr);pt::TailReverbSettings s;a.setSettings(s);b.setSettings(s);
 s.engine=1;b.setSettings(s);s.engine=0;b.setSettings(s);b.clear();a.clear();
 for(int i=0;i<24000;++i){auto x=a.process(i==0?1.f:0,0),y=b.process(i==0?1.f:0,0);ck(x==y,"Switching back to Legacy is not exact Legacy");}
 ck(pt::wetFadeIn(0,10,0)==1.f && pt::wetFadeIn(5,10,100)==0.f && pt::wetFadeIn(60,10,100)==.5f && pt::wetFadeIn(500,10,100)==1.f,"wetFadeIn ramp is wrong");
 pt::TailReverbSettings live;live.distance=20;live.tailOffsetMs=-50;ck(std::abs(pt::tailWetOnsetMs(.1,live)-107)<1e-9,"Live Tail onset must ignore negative offsets");
 live.tailOffsetMs=40;ck(std::abs(pt::tailWetOnsetMs(.1,live)-147)<1e-9,"Live Tail onset must include offset and distance pre-delay");
}
int main(){diffusionTest();v2Test();
 ck(energy(3000,100,0,100000,170000)>energy(1200,100,0,100000,170000)*100,"3-second decay is still capped at 1.2 seconds");
 ck(energy(1000,100,0,12000,40000)>energy(80,100,0,12000,40000)*100,"Space did not lengthen decay");
 ck(energy(200,100,100,0,2000)==0 && energy(200,100,0,0,2000)>0,"Distance did not add predelay");
 ck(energy(200,0,0,0,12000)>0,"Mono reverb silent");
 punchPreDelayTest();
 for(double sr:{44100.,48000.,96000.}){
  pt::TailReverb fx;fx.prepare(sr);pt::TailReverbSettings s;s.spaceMs=1200;s.distance=100;fx.setSettings(s);
  for(int i=0;i<int(sr*.2);++i)fx.process(.4f,-.2f);
  fx.clear();for(int i=0;i<int(sr*.2);++i){auto w=fx.process(0,0);ck(w[0]==0&&w[1]==0,"Reset leaked previous delay/lowpass/predelay history");}
 }
 std::cout<<"PASS Space, Distance, stereo mono, stability and constant-time exact reset\n";
}
