// v0.4.68 Extreme v5 tail engine (Source/ExtremeV5.h): exact root, exact
// duration at every note and stretch, tuning along the sustained tail, verbatim
// share and handover length against Highest Fidelity v4, no clicks at joins,
// stereo lock, cancel/exception, invalid input.
// v0.5.26: the faster join search is bit-identical to the v0.5.25 engine
// (Tests/ExtremeV5Reference.h), join decisions included.
#include "../Source/ExtremeV5.h"
#include "../Source/KickCharacterPitch.h"
#include "../Source/TailReverbPitchBank.h"
#include "ExtremeV5Reference.h"
#include "WaveFixture.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
static void check(bool b,const std::string& m){if(!b)throw std::runtime_error(m);}
static bool sameBits(const std::vector<float>& a,const std::vector<float>& b) {
    return a.size()==b.size() && std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0;
}
// One note rendered by the shipped engine and by the v0.5.25 reference: the
// same samples (bit for bit) and the same joins. Returns {shipped ms, reference ms}.
static std::pair<double,double> sameAsV0525(const pt::Sample& x,const pt::cycle::Analysis& analysis,double shift,double stretch,
                                            double stretchOverride,const std::string& what) {
    const auto p=pt::extreme5::prepare(x,analysis,stretch);
    const auto q=pt::ref0525::extreme5::prepare(x,analysis,stretch);
    pt::extreme5::RenderStats s;pt::ref0525::extreme5::RenderStats r;
    auto t=std::chrono::steady_clock::now();
    const auto y=pt::extreme5::render(x,shift,p,{},&s,stretchOverride);
    const double shippedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    t=std::chrono::steady_clock::now();
    const auto z=pt::ref0525::extreme5::render(x,shift,q,{},&r,stretchOverride);
    const double referenceMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    const std::string at=what+" at "+std::to_string(shift)+" st, stretch "+std::to_string(stretchOverride>0?stretchOverride:stretch);
    check(y.rate==z.rate && sameBits(y.left,z.left) && sameBits(y.right,z.right),"Extreme v5 differs from v0.5.25 on "+at);
    check(s.joins.size()==r.joins.size() && s.repeats==r.repeats && s.skips==r.skips,"Extreme v5 joins differ from v0.5.25 on "+at);
    for(size_t i=0;i<s.joins.size();++i) {
        const auto& a=s.joins[i];const auto& b=r.joins[i];
        check(a.outputFrame==b.outputFrame && a.lag==b.lag && a.fade==b.fade && a.mismatch==b.mismatch && a.correlation==b.correlation &&
              a.fromCycle==b.fromCycle && a.toCycle==b.toCycle,"Extreme v5 join "+std::to_string(i)+" differs from v0.5.25 on "+at);
    }
    return {shippedMs,referenceMs};
}
// Tuning along the tail: the output's cycle period at output time t, against
// the SOURCE's cycle period at the corresponding source time t/stretch, scaled
// by the pitch ratio. Median absolute offset in semitones over the sustained
// part (30-80 %), so a source whose pitch moves is judged against its own
// movement (the brief: keep the source's pitch movement, add no new one).
static double tuningErrorSt(const pt::Sample& out,const pt::Sample& src,const std::vector<double>& srcMarks,double ratio,double stretch) {
    const auto marks=pt::cycle::findPhaseMarks(out,out.rate/(ratio*src.rate/((srcMarks[srcMarks.size()/2]-srcMarks[srcMarks.size()/2-1]))));
    std::vector<double> offsets;
    for(size_t i=1;i<marks.size();++i) {
        const double t=marks[i-1];if(t<out.left.size()*.3 || t>out.left.size()*.8)continue;
        const double outPeriod=marks[i]-marks[i-1];
        const double srcT=t/stretch;
        auto it=std::upper_bound(srcMarks.begin(),srcMarks.end(),srcT);
        if(it==srcMarks.begin() || it==srcMarks.end())continue;
        const double srcPeriod=*it-*(it-1);
        offsets.push_back(12*std::log2(outPeriod*ratio/srcPeriod));
    }
    if(offsets.size()<3)return 99;
    std::sort(offsets.begin(),offsets.end());
    return offsets[offsets.size()/2];
}
// Click check: the largest second difference within +-8 samples of a join,
// relative to the largest within one cycle around it (a join must not be
// sharper than the waveform it sits in).
static double seam(const pt::Sample& s,const std::vector<double>& joins,double cycle) {
    auto d2=[&](long i){return std::abs(double(s.left[size_t(i+1)])-2*s.left[size_t(i)]+s.left[size_t(i-1)]);};
    double worst=0;
    for(double j:joins) {
        double in=0,around=0;const long c=long(j),r=long(cycle);
        for(long i=c-r;i<=c+r;++i){if(i<=0||i+1>=long(s.left.size()))continue;const double v=d2(i);if(std::abs(i-c)<=8)in=(std::max)(in,v);else around=(std::max)(around,v);}
        if(around>1e-6)worst=(std::max)(worst,in/around);
    }
    return worst;
}
int main(int argc,char** argv) try {
    check(argc>2,"usage: ExtremeV5Tests <virus-kick-3.wav> <kick-nc-lol-2.0.wav> [tail.wav]");
    for(int f=1;f<argc;++f) {
        const auto x=loadFloatWave(argv[f]);
        const auto analysis=pt::cycle::analyze(x);
        const auto p5=pt::extreme5::prepare(x,analysis,1.0);
        const auto p4=pt::character::prepare(x,analysis,1.0);
        // Exact root.
        {const auto y=pt::extreme5::render(x,0.0,p5);check(y.left==x.left&&y.right==x.right&&y.rate==x.rate,"root is not the exact source");}
        double sumV5=0,sumV4=0,fade5=0,fade4=0,worstSeam5=0,worstSeam4=0,ms5=0,ms4=0,worstTune=0;int n=0;
        for(double st:{-12.0,-7.0,-3.0,-1.0,2.0,5.0,7.0,12.0}) {
            pt::extreme5::RenderStats s5;pt::character::RenderStats s4;
            auto t=std::chrono::steady_clock::now();
            const auto y5=pt::extreme5::render(x,st,p5,{},&s5);
            ms5+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
            t=std::chrono::steady_clock::now();
            const auto y4=pt::character::render(x,st,p4,{},&s4);
            ms4+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
            check(y5.left.size()==x.left.size()&&y5.right.size()==x.left.size()&&y5.rate==x.rate,"duration changed with pitch");
            for(float v:y5.left)check(std::isfinite(v),"non-finite output");
            // No silence padding or truncation: the end still carries the tail.
            {double tailEnd=0,src=0;const size_t w=x.left.size()/20;
             for(size_t i=x.left.size()-w;i<x.left.size();++i){tailEnd+=double(y5.left[i])*y5.left[i];src+=double(x.left[i])*x.left[i];}
             check(tailEnd>src*.1,"the end of the output is padded silence");}
            // Tuning along the sustained part.
            const double ratio=std::pow(2.0,st/12.0);
            const double err5=tuningErrorSt(y5,x,analysis.marks,ratio,1.0),err4=tuningErrorSt(y4,x,analysis.marks,ratio,1.0);
            check(std::abs(err5)<=0.25,"tuning off on "+std::string(argv[f])+" at "+std::to_string(st)+" st: "+std::to_string(err5)+" (v4 "+std::to_string(err4)+")");
            worstTune=(std::max)(worstTune,std::abs(err5));
            // Verbatim share: at least 85 %, and handovers no longer than HF v4's on average.
            check(s5.verbatimShare>=0.85,"too much blended output");
            double b4=0;for(const auto& j:s4.splices)b4+=j.fade;
            sumV5+=s5.verbatimShare;sumV4+=1.0-b4/double(y4.left.size());fade5+=s5.meanCrossfadeCycles;fade4+=s4.meanCrossfadeCycles;++n;
            // Joins never click: every handover at least 1 ms, seam figure bounded.
            std::vector<double> j5,j4;
            for(const auto& j:s5.joins){check(j.fade>=x.rate*.001-1,"handover shorter than 1 ms");j5.push_back(j.outputFrame+j.fade*.5);}
            for(const auto& j:s4.splices)j4.push_back(j.outputFrame+j.fade*.5);
            const double cycle=x.rate/(analysis.fundamentalHz*ratio);
            const double sm5=seam(y5,j5,cycle),sm4=seam(y4,j4,cycle);worstSeam5=(std::max)(worstSeam5,sm5);worstSeam4=(std::max)(worstSeam4,sm4);
            check(sm5<1.2,"a join is sharper than the waveform around it (seam "+std::to_string(sm5)+")");
            // Stereo lock: both channels blended identically (mono source => identical channels).
            if(x.left==x.right)check(y5.left==y5.right,"stereo image not locked");
            // Deterministic.
            check(pt::extreme5::render(x,st,p5).left==y5.left,"render is not reproducible");
        }
        std::cout<<argv[f]<<": verbatim v5 "<<sumV5/n<<" v4 "<<sumV4/n<<" | mean handover (cycles) v5 "<<fade5/n<<" v4 "<<fade4/n
                 <<" | worst seam v5 "<<worstSeam5<<" v4 "<<worstSeam4<<" | worst tuning offset v5 "<<worstTune<<" st"<<" | render ms/note v5 "<<ms5/n<<" v4 "<<ms4/n<<"\n";
        check(fade5/n<=fade4/n,"Extreme v5 handovers are longer than Highest Fidelity v4 on average");
        check(sumV5/n>=sumV4/n-0.02,"Extreme v5 blends more than Highest Fidelity v4");
        // Stretch: exact duration, and Adaptive-style overrides down to 20 %.
        for(double stretch:{0.8,1.25,1.5}) {
            const auto ps=pt::extreme5::prepare(x,analysis,stretch);
            for(double st:{-5.0,0.0,4.0}){const auto y=pt::extreme5::render(x,st,ps);check(y.left.size()==size_t(std::llround(x.left.size()*stretch)),"stretch duration wrong");}
        }
        {const auto y=pt::extreme5::render(x,3.0,p5,{},nullptr,0.25);check(y.left.size()==size_t(std::llround(x.left.size()*.25)),"stretch override duration wrong");}
    }
    {   // v0.5.26 faster join search: exactly the v0.5.25 notes. Every key of the
        // prepared bank (MIDI 15..51 from the detected root, as the processor
        // clamps it) on the difficult fixture, a spread of keys and stretches on
        // every fixture, Adaptive-style overrides, and a Root Render composite
        // (a much longer, reverb-mixed source with the same cycle map).
        double shippedMs=0,referenceMs=0;int notes=0;
        const auto add=[&](std::pair<double,double> t){shippedMs+=t.first;referenceMs+=t.second;++notes;};
        for(int f=1;f<argc;++f) {
            const auto x=loadFloatWave(argv[f]);
            const auto analysis=pt::cycle::analyze(x);
            const double root=std::clamp(std::round(69.0+12.0*std::log2(analysis.fundamentalHz/440.0)),24.0,39.0);
            const std::string name=argv[f];
            if(f==1)for(int n=pt::kLowestPitchNote;n<=pt::kHighestPitchNote;++n)add(sameAsV0525(x,analysis,n-root,1.0,0.0,name));
            for(double stretch:{0.8,1.25,1.5})for(double st:{-12.0,-5.0,4.0,12.0})add(sameAsV0525(x,analysis,st,stretch,0.0,name));
            for(double st:{-3.0,3.0})add(sameAsV0525(x,analysis,st,1.0,0.25,name+" (stretch override)"));
            if(f==1) {
                pt::TailReverbSettings reverb;reverb.enabled=true;reverb.tailSend=true;reverb.mix=25;reverb.spaceMs=600;reverb.engine=1;
                const auto prep=pt::prepareRootTailComposite(x,root,0,reverb,1.0,5,false,{});
                check(prep.rootSample->duration()>x.duration()*1.5,"Root Render composite fixture is not longer than the tail");
                for(double st:{-12.0,-4.0,7.0})add(sameAsV0525(*prep.rootSample,prep.base.phaseMap,st,1.0,0.0,name+" Root Render composite"));
            }
        }
        std::cout<<"v0.5.26 search: "<<notes<<" notes bit-identical to v0.5.25 | render ms "<<shippedMs<<" vs "<<referenceMs
                 <<" ("<<referenceMs/(std::max)(1e-9,shippedMs)<<"x faster)\n";
    }
    {   // Invalid input and cancellation.
        const auto x=loadFloatWave(argv[1]);const auto analysis=pt::cycle::analyze(x);
        bool threw=false;try{pt::extreme5::prepare(x,analysis,0.5);}catch(const std::invalid_argument&){threw=true;}check(threw,"stretch 50 % accepted");
        threw=false;try{pt::cycle::Analysis none;none.fundamentalHz=50;pt::extreme5::prepare(x,none,1.0);}catch(const std::invalid_argument&){threw=true;}check(threw,"empty cycle map accepted");
        const auto p=pt::extreme5::prepare(x,analysis,1.0);
        threw=false;try{pt::extreme5::render(x,60.0,p);}catch(const std::invalid_argument&){threw=true;}check(threw,"+60 st accepted");
        int polls=0;threw=false;
        try{pt::extreme5::render(x,7.0,p,[&]{return ++polls>3;});}catch(const std::runtime_error&){threw=true;}
        check(threw&&polls>3,"cancel did not stop the render");
        pt::Sample mono=x;mono.right.pop_back();threw=false;try{pt::extreme5::render(mono,3.0,p);}catch(const std::invalid_argument&){threw=true;}check(threw,"mismatched channels accepted");
    }
    std::cout<<"ExtremeV5 OK\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
