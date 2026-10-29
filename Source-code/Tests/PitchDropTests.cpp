#include "../Source/PitchDrop.h"
#include "../Source/TailReverbPitchBank.h"
#include "WaveFixture.h"
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
static double hz(const pt::Sample& s,double a,double b){
    double first=-1,last=-1;int crossings=0;
    for(size_t i=size_t(a*s.rate)+1;i<size_t(b*s.rate);++i)if(s.left[i-1]<0&&s.left[i]>=0){
        const double pos=double(i-1)+double(-s.left[i-1])/(s.left[i]-s.left[i-1]);
        if(first<0)first=pos;last=pos;++crossings;
    }
    return crossings>1?(crossings-1)*s.rate/(last-first):0;
}
int main(int argc,char** argv)try{
    pt::PitchDropSettings d;d.enabled=true;d.amount=12;d.timeMs=500;d.curve=1;
    check(d.semitones(0)==12&&d.semitones(.5)==0&&d.semitones(3)==0,"Drop endpoints wrong");
    check(std::abs(d.semitones(.25)-6)<1e-9,"Linear semitone curve wrong");
    auto fast=d;fast.curve=4;check(fast.semitones(.25)<d.semitones(.25),"Curve direction wrong");
    for(double sr:{44100.,48000.,96000.}) {
        pt::Sample in;in.rate=sr;in.left.resize(size_t(sr));in.right.resize(in.left.size());
        for(size_t i=0;i<in.left.size();++i){in.left[i]=float(.4*std::sin(2*pt::pi*220*i/sr));in.right[i]=in.left[i]*.5f;}
        auto off=d;off.enabled=false;auto exact=pt::drop::render(in,off,220);
        check(exact.left==in.left&&exact.right==in.right,"OFF not exact");
        off=d;off.amount=0;check(pt::drop::render(in,off,220).left==in.left,"Zero amount not exact");
        const auto out=pt::drop::render(in,d,220);
        check(out.left.size()==in.left.size()&&out.rate==in.rate,"Drop changed duration/rate");
        check(hz(out,.01,.04)>390,"Drop did not start above the target pitch");
        check(std::abs(hz(out,.65,.9)-220)<1,"Drop did not settle at target pitch");
        for(size_t i=0;i<out.left.size();++i){check(std::isfinite(out.left[i]),"Non-finite drop");check(std::abs(out.right[i]-.5f*out.left[i])<1e-6,"Stereo decision mismatch");}
        check(out.left.back()==0&&out.right.back()==0,"Drop final boundary not faded");
        auto trimmed=pt::drop::render(in,d,220,.1);
        check(std::equal(in.left.begin(),in.left.begin()+size_t(.1*sr),trimmed.left.begin()),"Drop changed audio before trim");
        check(hz(trimmed,.11,.14)>390,"Drop did not restart at trimmed onset");
        bool cancelled=false;int calls=0;try{pt::drop::render(in,d,220,0,[&]{return ++calls>3;});}catch(const std::runtime_error&){cancelled=true;}
        check(cancelled,"Drop ignored cancellation");
    }
    for(double frequency:{19.445,65.406,400.}) {
        pt::Sample in;in.rate=44100;in.left.resize(44100);in.right.resize(44100);
        for(size_t i=0;i<in.left.size();++i)in.left[i]=in.right[i]=float(.4*std::sin(2*pt::pi*frequency*i/in.rate));
        d.amount=24;d.timeMs=300;d.curve=.25;
        const auto out=pt::drop::render(in,d,frequency);
        check(std::abs(1200*std::log2(hz(out,.5,.95)/frequency))<10,"Maximum drop did not settle in tune");
    }
    for(size_t frames:{size_t(1),size_t(32),size_t(400)}) {
        pt::Sample tiny;tiny.left.assign(frames,.1f);tiny.right=tiny.left;
        const auto out=pt::drop::render(tiny,d,9.);
        check(out.left.size()==frames,"Short punch duration changed");
        for(float x:out.left)check(std::isfinite(x),"Short punch invalid");
    }
    if(argc>1){auto in=loadFloatWave(argv[1]);
        for(double amount:{1.,12.,24.})for(double curve:{.25,1.,4.}){
            d.amount=amount;d.curve=curve;d.timeMs=150;
            auto out=pt::drop::render(in,d,65.406);
            check(out.left.size()==in.left.size(),"Real-kick duration changed");
            double energy=0;for(auto x:out.left){check(std::isfinite(x)&&std::abs(x)<10,"Invalid real-kick output");energy+=x*x;}
            check(energy>1e-6,"Real kick went silent");
        }
    }
    // Composite route uses the same drop for the combined dry+wet signal,
    // and excludes Adaptive even if a caller supplies enabled settings.
    pt::Sample in;in.rate=16000;in.left.resize(6400);in.right.resize(6400);
    for(size_t i=0;i<in.left.size();++i)in.left[i]=in.right[i]=float(.3*std::sin(2*pt::pi*65.406*i/in.rate));
    pt::TailReverbSettings verb;verb.enabled=false;
    pt::TailCompositeRenderSettings wet;wet.drop=d;wet.drop.amount=12;wet.drop.timeMs=60;
    auto normal=pt::renderRootTailCompositeBank(in,36,0,verb,1,4,false);
    auto dropped=pt::renderRootTailCompositeBank(in,36,0,verb,1,4,false,wet);
    check(dropped.notes[36]->left!=normal.notes[36]->left,"Root composite did not receive pitch drop");
    check(dropped.notes[36]->left.size()==normal.notes[36]->left.size(),"Composite duration changed");
    // v0.4.61: Legacy was removed; Adaptive Stretch v4 (3) must still ignore the drop.
    auto adaptive=pt::renderRootTailCompositeBank(in,36,0,verb,1,3,false);
    auto adaptiveDrop=pt::renderRootTailCompositeBank(in,36,0,verb,1,3,false,wet);
    check(adaptive.notes[36]->left==adaptiveDrop.notes[36]->left,"Drop affected Adaptive Stretch v4");
    std::cout<<"Pitch drop DSP passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
