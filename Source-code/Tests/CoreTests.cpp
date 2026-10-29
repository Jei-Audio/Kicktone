#include "../Source/Engine.h"
#include "../Source/PitchShift.h"
#include "V4Render.h"
#include <iostream>
#include <stdexcept>
#include <random>
#include <fstream>
#include <cstring>
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); std::cout<<"PASS "<<what<<'\n'; }
pt::Sample tone(double hz,double seconds=0.5,int sr=44100) {
    pt::Sample s; s.rate=sr; s.left.resize(size_t(seconds*sr));
    for(size_t i=0;i<s.left.size();++i) s.left[i]=float(0.6*std::sin(2*pt::pi*hz*i/sr)+0.1*std::sin(4*pt::pi*hz*i/sr));
    s.right=s.left; return s;
}
int main(int argc,char** argv) {
 try {
    for(const double hz:{32.7032,55.0,77.7817,110.0,440.0}) { auto p=pt::detect(tone(hz)); check(p.valid && std::abs(12*std::log2(p.hz/hz))<0.05,"Pitch detection within 5 cents"); }
    auto silent=tone(55); std::fill(silent.left.begin(),silent.left.end(),0.0f); silent.right=silent.left; check(!pt::detect(silent).valid,"Silence rejected");
    auto noise=silent; std::mt19937 generator(1); std::uniform_real_distribution<float> random(-0.5f,0.5f); for(auto& x:noise.left) x=random(generator); noise.right=noise.left;
    check(!pt::detect(noise).valid,"Noise rejected");
    auto punch=tone(120,0.104), tail=tone(55,0.396); pt::Settings s; s.crossfadeMs=0;
    pt::Voice v; v.start(33,1,1,&punch,&tail,s); check(std::abs(v.tailStart-punch.duration())<1e-9,"Zero fade starts exactly at punch end");
    s.crossfadeMs=10; s.root=33; v.start(33,1,1,&punch,&tail,s); check(std::abs(v.overlap-0.01)<1e-9,"10 ms overlap");
    s.trimMs=25; v.start(33,1,1,&punch,&tail,s); check(std::abs((v.tailEnd-v.tailStart)-(tail.duration()-0.025))<1e-9,"Tail offset trims exactly 25 ms");
    // Punch offset is applied to the END of the punch only.
    s.punchEndOffsetMs=22.5; v.start(33,1,1,&punch,&tail,s);
    check(std::abs(v.punchEnd-(punch.duration()-0.0225))<1e-9,"Punch end offset removes exactly 22.5 ms");
    s.punchEndOffsetMs=600; v.start(33,1,1,&punch,&tail,s);
    check(v.punchEnd==0 && v.tailStart==0 && v.active,"600 ms punch end offset leaves tail playable");
    s.punchEndOffsetMs=0;
    s.trimMs=600; v.start(33,1,1,&punch,&tail,s);
    check(v.tailTrim==tail.duration() && std::abs(v.tailEnd-v.punchEnd)<1e-9,"600 ms tail offset removes only the tail");
    s.trimMs=25;
    s.joinMs=30; v.start(33,1,1,&punch,&tail,s); check(v.overlap==0 && std::abs(v.tailStart-(punch.duration()+0.02))<1e-9,"Positive join shift permits deliberate gap");
    // Root pitch is bit-identical; all shifted notes use cycle-aligned SRC.
    const auto root=v4::pitchShift(tail,0.0);
    check(root.left==tail.left && root.right==tail.right,"Root-note waveform remains bit-identical");
    bool rejectedNoise=false;
    try { (void)v4::pitchShift(noise,7.0); }
    catch(const std::runtime_error&) {rejectedNoise=true;}
    check(rejectedNoise,"Non-periodic input is explicitly rejected rather than silently pitched incorrectly");
    // A high-harmonic square-like waveform must keep pitch and sample count.
    auto square=tone(55,0.4);
    for (size_t i=0;i<square.left.size();++i) {
        const float v=float(.58*std::tanh(12*std::sin(2*pt::pi*55*double(i)/square.rate)));
        square.left[i]=v; square.right[i]=v;
    }
    const auto analysis=pt::cycle::analyze(square);
    for (double shift:{-12.,-5.,3.,7.,12.}) {
        auto y=v4::pitchShift(square,shift,analysis);
        auto estimate=pt::detect(y);
        check(y.left.size()==square.left.size() && y.right.size()==square.right.size(),"Square-like tail keeps source length");
        check(estimate.valid && std::abs(12.0*std::log2(estimate.hz/(55.0*std::pow(2,shift/12))))<.18,
              "Square-like tail shifted to target fundamental");
    }
    for(double shift:{-12.,-7.,-5.,3.,7.,12.}) {
        auto played=v4::pitchShift(tail,shift);
        check(played.left.size()==tail.left.size() && played.right.size()==tail.right.size(),
              "Cycle engine renders identical sample count across all target notes");
        for(auto x:played.left) if(!std::isfinite(x)) throw std::runtime_error("Cycle engine produced nonfinite output");
    }
    for(double shift:{-12.,-5.,3.,7.,12.}) {
        auto pitched=v4::pitchShift(tail,shift); auto detected=pt::detect(pitched);
        check(pitched.left.size()==tail.left.size(),"r8brain cycle keeps exact sample count");
        const double target=55*std::pow(2,shift/12);
        std::cout<<"shift "<<shift<<" target "<<target<<" measured "<<detected.hz<<'\n';
        check(detected.valid && std::abs(12*std::log2(detected.hz/target))<0.15,"Shifted fundamental within 15 cents");
        for(float x:pitched.left) if(!std::isfinite(x)) throw std::runtime_error("Nonfinite shifted sample");
        auto locked=s; locked.root=33+shift; locked.joinMs=0; locked.trimMs=0;
        v.start(int(33+shift),1,1,&punch,&pitched,locked); check(std::abs(v.tailEnd-v.tailStart-tail.duration())<1e-9,"Playback duration independent of note");
    }
    for(int sr:{44100,48000,96000}) { s.root=33; s.joinMs=0; s.trimMs=0; v.start(33,1,1,&punch,&tail,s); int n=0; while(v.active && n<sr*2) {auto out=v.next(&punch,&tail,sr);if(!std::isfinite(out[0])) throw std::runtime_error("Nonfinite voice sample");++n;} check(n<sr*2,"Voice terminates at each host rate"); }
    if(argc>1) {
        // Optional stereo float raw fixtures generated from supplied WAVs.
        auto read=[](const char* path){ pt::Sample a; std::ifstream f(path,std::ios::binary); float l,r; while(f.read(reinterpret_cast<char*>(&l),4) && f.read(reinterpret_cast<char*>(&r),4)) { a.left.push_back(l); a.right.push_back(r); } return a; };
        auto realTail=read(argv[1]); auto p=pt::detect(realTail); std::cout<<"USER TAIL hz="<<p.hz<<" midi="<<p.midi<<" confidence="<<p.confidence<<" valid="<<p.valid<<'\n';
        if(argc>2) { auto realPunch=read(argv[2]); std::ofstream demo("Demo-Progression.f32",std::ios::binary);
            for(int delta:{0,-2,-5,-3}) { auto pitch=v4::pitchShift(realTail,delta); pt::Settings ds; ds.root=36+delta; ds.crossfadeMs=8; ds.output=0.5;
                pt::Voice dv; dv.start(36+delta,1,1,&realPunch,&pitch,ds); for(int i=0;i<26460;++i){auto x=dv.next(&realPunch,&pitch,44100);demo.write(reinterpret_cast<char*>(x.data()),8);} }
        }
    }
    std::cout<<"ALL TESTS PASSED\n"; return 0;
 } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;}
}
