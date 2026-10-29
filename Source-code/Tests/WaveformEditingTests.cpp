#include "../Source/WaveformEdit.h"
#include "../Source/PunchDetection.h"
#include "../Source/KickOverview.h"
#include <iostream>
#include <chrono>
#include <stdexcept>
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static pt::Sample make(double length,bool impact,bool silent=false) {
    pt::Sample s;s.rate=48000;s.left.resize(size_t(length*s.rate));s.right.resize(s.left.size());
    uint32_t seed=42;
    for(size_t i=0;i<s.left.size();++i){
        double t=i/s.rate;seed=seed*1664525u+1013904223u;
        const double noise=(double(seed)/4294967295.*2-1)*.8;
        const float value=silent?0.f:float(impact && t<.040?noise:.75*std::sin(2*pt::pi*55*t));
        s.left[i]=value;s.right[i]=-value;
    }return s;
}
int main()try {
    for(double trim:{0.,599.9,600.,600.1,2900.,5000.}){auto p=pt::splitTrim(trim);check(std::abs(p.base+p.extra-trim)<1e-9,"Split trim changed value");}
    check(pt::trimAtPosition(true,12,12,400,3000)==3000,"Punch left edge must remove entire file");
    check(pt::trimAtPosition(false,412,12,400,3000)==3000,"Tail right edge must remove entire file");
    // An on/off Shift toggle affects ONLY new pixels moved, not the whole
    // accumulated distance. This is the regression behind the v0.4.16 failed
    // WaveformMouseAndState test and covers all three source lanes.
    for(bool punch:{false,true}) {
        const double dir=punch?-1.0:1.0;
        const double w=330.0,duration=2000.0;
        double value=1000.0;
        value=pt::dragWaveformTrim(punch,value,100,110,duration,w,true);
        check(std::abs(value-(1000+dir*2000/w))<1e-9,"Fine waveform trim step incorrect");
        value=pt::dragWaveformTrim(punch,value,110,110,duration,w,false);
        check(std::abs(value-(1000+dir*2000/w))<1e-9,"Releasing Shift moved stationary waveform trim");
        value=pt::dragWaveformTrim(punch,value,110,120,duration,w,false);
        check(std::abs(value-(1000+dir*22000/w))<1e-9,"Fine to coarse drag jumped waveform trim");
        value=pt::dragWaveformTrim(punch,value,120,120,duration,w,true);
        check(std::abs(value-(1000+dir*22000/w))<1e-9,"Pressing Shift moved stationary waveform trim");
        value=pt::dragWaveformTrim(punch,value,120,110,duration,w,true);
        check(std::abs(value-(1000+dir*20000/w))<1e-9,"Coarse to fine drag jumped waveform trim");
        value=pt::dragWaveformTrim(punch,value,110,100,duration,w,false);
        check(std::abs(value-1000)<1e-9,"Reverse movement did not restore start trim");
        check(pt::dragWaveformTrim(punch,1000,100,100,2000,w,true)==1000,
              "Stationary mouse moved waveform trim");
        check(pt::dragWaveformTrim(punch,0,100,1000,2000,w,false)>=0,
              "Trim went below zero");
        check(pt::dragWaveformTrim(punch,2000,100,-900,2000,w,false)<=2000,
              "Trim exceeded source duration");
    }
    check(pt::dragKickValue(pt::KickHandle::punchEnd,2500,100,3000)==2400,"Punch handle direction");
    check(pt::dragKickValue(pt::KickHandle::tailStart,0,100,3000)==30,"Join handle limit");
    check(pt::dragKickValue(pt::KickHandle::crossfade,8,-4,3000)==16,"Crossfade handle mapping");
    const auto longPunch=make(3,true);
    const auto start=std::chrono::steady_clock::now();const auto guess=pt::detectPunchEnd(longPunch);
    auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    check(guess.valid && guess.endMs>=25 && guess.endMs<=85,"Loud sustained tail not separated from impact");
    pt::Settings settings;settings.punchEndOffsetMs=3000-guess.endMs;
    const auto preview=pt::buildKickOverview(&longPunch,nullptr,settings,36);
    check(settings.punchEndOffsetMs>600 && std::abs(preview.punchEndMs-guess.endMs)<1e-8,"Long tail trim stopped at old 600ms cap");
    check(!pt::detectPunchEnd(make(3,false,true)).valid,"Silence must not produce suggestion");
    check(!pt::detectPunchEnd(make(3,false)).valid,"Steady tone must not pretend to contain impact");
    auto shortPunch=make(.2,false);
    for(size_t i=0;i<shortPunch.left.size();++i){const double t=i/shortPunch.rate;shortPunch.left[i]*=float(std::exp(-t/.015));shortPunch.right[i]=shortPunch.left[i];}
    auto shortGuess=pt::detectPunchEnd(shortPunch);
    check(shortGuess.valid && shortGuess.endMs>35 && shortGuess.endMs<150,"Short punch decay cut incorrectly");
    std::cout<<"PASS waveform mapping, full-file trims, loud-tail separation ("<<guess.endMs<<" ms), silence/steady-tone guards, short decay; 3s analysis "<<elapsed<<" ms\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
