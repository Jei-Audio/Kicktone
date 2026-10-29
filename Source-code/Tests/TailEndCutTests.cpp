// v0.4.57 Tail END cut: shortens only the dry Tail, with an adjustable fade.
// The reverb (live send, duck key and Root Render wet) must stay unchanged.
#include "../Source/TailReverbPitchBank.h"
#include "../Source/KickOverview.h"
#include "../Source/MasterEnvelopeTiming.h"
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}

static pt::Sample sine(int frames,int rate,double hz,float amp){
    pt::Sample s;s.rate=rate;s.left.resize(size_t(frames));s.right.resize(size_t(frames));
    for(int i=0;i<frames;++i){s.left[size_t(i)]=float(amp*std::sin(2*pt::pi*hz*i/rate));s.right[size_t(i)]=-.6f*s.left[size_t(i)];}
    return s;
}
struct Run { std::vector<std::array<float,2>> tail,send; pt::Voice voice; };
static Run play(const pt::Sample* punch,const pt::Sample& tail,pt::Settings s,int frames,const pt::Sample* crunch=nullptr){
    Run r;r.voice.start(36,1,1,punch,&tail,s);
    for(int i=0;i<frames;++i){auto p=r.voice.nextParts(punch,&tail,48000,nullptr,nullptr,nullptr,crunch);r.tail.push_back(p.tail);r.send.push_back(p.tailSend);}
    return r;
}

static void shapeAndKeyTest(){
    check(pt::tailEndGain(1.0,1.0,.01)==0 && pt::tailEndGain(.99,1.0,.01)==1 && std::abs(pt::tailEndGain(.995,1.0,.01)-.5)<1e-12,"Fade shape/endpoints wrong");
    check(pt::tailEndGain(1.2,1.0,.01)==0,"Audio after the cut is not silent");
    check(pt::tailEndFadeMsClamped(0)==1 && pt::tailEndFadeMsClamped(5000)==2000 && pt::tailEndFadeMsClamped(std::nan(""))==10,"Fade not clamped to 1..2000 ms");
    check(pt::sameTailEnd(0,10,0,500),"Fade change while the cut is off must not rebuild anything");
    check(!pt::sameTailEnd(0,10,300,10) && !pt::sameTailEnd(300,10,300,40) && pt::sameTailEnd(300,40,300,40),"Tail end key mismatch");
}

static void voiceTest(){
    const auto punch=sine(4800,48000,180,.4f);      // 100 ms
    const auto tail=sine(72000,48000,55,.3f);       // 1.5 s
    pt::Settings base;base.root=36;base.output=1;base.crossfadeMs=8;base.gate=false;base.trimMs=100;
    const int frames=96000;
    const auto uncut=play(&punch,tail,base,frames);
    // Off (trim 0) is bit-identical whatever the fade, and send == tail.
    auto offSettings=base;offSettings.tailEndFadeMs=700;
    const auto off=play(&punch,tail,offSettings,frames);
    for(int i=0;i<frames;++i)check(off.tail[size_t(i)]==uncut.tail[size_t(i)] && uncut.send[size_t(i)]==uncut.tail[size_t(i)],"Tail end OFF changed audio");
    check(uncut.voice.dryEnd==uncut.voice.tailEnd,"Uncut dryEnd differs from tailEnd");
    // Cut 600 ms from the file end with a 50 ms fade.
    auto cutSettings=base;cutSettings.tailEndTrimMs=600;cutSettings.tailEndFadeMs=50;
    const auto cut=play(&punch,tail,cutSettings,frames);
    const double tailStart=cut.voice.tailStart;
    const double cutTime=tailStart+(1.5-.6-.1);   // keep = duration - end trim - start trim
    check(std::abs(cut.voice.dryEnd-cutTime)<1e-9,"dryEnd not at the cut point");
    check(cut.voice.tailEnd==uncut.voice.tailEnd && cut.voice.audibleEnd()==uncut.voice.audibleEnd(),"Cut shortened the voice the reverb send needs");
    double fadeEnergy=0,afterEnergy=0;
    for(int i=0;i<frames;++i){
        const double t=double(i)/48000;
        check(cut.send[size_t(i)]==uncut.tail[size_t(i)],"Reverb send is not the uncut Tail");
        if(t<cutTime-.05-1e-4)check(cut.tail[size_t(i)]==uncut.tail[size_t(i)],"Audio before the fade changed");
        else if(t>=cutTime)afterEnergy+=std::abs(cut.tail[size_t(i)][0])+std::abs(cut.tail[size_t(i)][1]);
        else {check(std::abs(cut.tail[size_t(i)][0])<=std::abs(uncut.tail[size_t(i)][0])+1e-7f,"Fade boosted the Tail");fadeEnergy+=std::abs(cut.tail[size_t(i)][0]);}
    }
    check(afterEnergy==0,"Dry Tail sounded after the cut");
    check(fadeEnergy>1,"Fade region silent (cut too early)");
    // Glide-free stretch scaling: prepared notes scale the trim exactly like trimMs.
    // A crunch layer is cut with the Tail it belongs to.
    auto crunch=sine(72000,48000,2000,.2f);
    auto crunchSettings=cutSettings;crunchSettings.crunchMix=1;
    const auto withCrunch=play(&punch,tail,crunchSettings,frames,&crunch);
    for(int i=int(cutTime*48000)+1;i<frames;++i)check(withCrunch.tail[size_t(i)][0]==0 && withCrunch.tail[size_t(i)][1]==0,"Crunch layer escaped the Tail end cut");
    // Overview and envelope duration follow the cut when nothing else rings.
    const auto view=pt::buildKickOverview(&punch,&tail,cutSettings,36,880);
    check(std::abs(view.tailEndMs-cutTime*1000)<.01,"Kick overview ignores the cut");
    pt::TailReverbSettings noVerb;noVerb.enabled=false;
    const double total=tt::completeOutputDuration(cut.voice,true,true,noVerb,noVerb,{{false,false,false}});
    check(std::abs(total-cutTime)<1e-9,"Envelope duration ignores the cut");
    pt::TailReverbSettings verb;verb.enabled=true;verb.mix=50;verb.tailSend=true;verb.tailRootPitch=false;
    const double withVerb=tt::completeOutputDuration(cut.voice,true,true,verb,noVerb,{{false,false,false}});
    const double uncutVerb=tt::completeOutputDuration(uncut.voice,true,true,verb,noVerb,{{false,false,false}});
    check(withVerb==uncutVerb,"Live reverb ring-out was shortened by the dry cut");
    // Cutting past the START marker silences the dry Tail without breaking anything.
    auto all=base;all.tailEndTrimMs=1450;
    const auto silent=play(&punch,tail,all,frames);
    for(int i=int(silent.voice.tailStart*48000)+1;i<frames;++i)check(silent.tail[size_t(i)][0]==0,"Over-long cut left audio");
}

static void compositeTest(){
    pt::Sample tail;tail.rate=8000;tail.left.resize(9600);tail.right.resize(9600);     // 1.2 s
    const double hz=440*std::pow(2.,(36.-69)/12.);
    for(size_t i=0;i<tail.left.size();++i){const float e=std::exp(-float(i)/6000.f);tail.left[i]=float(.3*e*std::sin(2*pt::pi*hz*i/tail.rate));tail.right[i]=tail.left[i]*.7f;}
    pt::TailReverbSettings s;s.enabled=true;s.mix=60;s.spaceMs=120;s.distance=0;s.keepDry=true;s.wetAutoMatch=true;s.wetPhaseLock=true;
    pt::TailCompositeRenderSettings p;
    for(bool withWet:{true,false}) {
        auto settings=s;settings.enabled=withWet;
        pt::TailCompositeCache cache;
        auto full=pt::renderRootTailCompositeBank(tail,36,50,settings,1.0,4,false,p,{},&cache);
        const size_t builds=cache.wetBuilds;
        auto cutP=p;cutP.tailEndTrimMs=400;cutP.tailEndFadeMs=30;
        auto cut=pt::renderRootTailCompositeBank(tail,36,50,settings,1.0,4,false,cutP,{},&cache);
        check(cache.wetBuilds==builds,"Tail end cut regenerated the reverb");
        check(cut.wetMatch.shift==full.wetMatch.shift && cut.wetMatch.polarity==full.wetMatch.polarity,"Auto match changed with the dry cut");
        // composite = dry*gain + wet: the difference must be exactly the removed dry part.
        const double cutPos=1.2-.05-.4,fade=.03;
        const auto& a=*full.rootRender;const auto& b=*cut.rootRender;
        check(a.left.size()==b.left.size(),"Cut changed the composite length (reverb ring-out)");
        const size_t dryFrames=size_t(std::ceil((1.2-.05)*8000));
        const size_t finalFade=size_t(std::ceil(.002*8000));
        for(size_t i=0;i<a.left.size()-finalFade;++i) {
            double dry=0;
            if(i<dryFrames){const double t=double(i)/8000;dry=tail.at(0,(0.05+t)*8000)*std::clamp(float(t/.0005),0.f,1.f)*std::clamp(float((1.15-t)/.002),0.f,1.f);}
            const double expected=dry*(pt::tailEndGain(double(i)/8000,cutPos,fade)-1.0);
            check(std::abs((b.left[i]-a.left[i])-expected)<2e-6,"Composite cut changed more than the dry Tail");
        }
        check(std::abs(cut.dryTailDurations[36]-cutPos)<1e-9 && std::abs(full.dryTailDurations[36]-1.15)<1e-9,"Composite dry duration ignores the cut");
        check(cut.dryTailDurations[41]<full.dryTailDurations[41],"Pitched composite note dry duration ignores the cut");
    }
}

int main()try{
    shapeAndKeyTest();voiceTest();compositeTest();
    std::cout<<"PASS Tail end cut: dry-only, adjustable fade, OFF identity, uncut reverb send/duck/Root Render wet, overview/envelope duration\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
