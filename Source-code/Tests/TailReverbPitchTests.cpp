#include "../Source/TailReverbPitchBank.h"
#include "WaveFixture.h"
#include <iostream>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

static void offsetTest(){
    pt::Sample tail;tail.rate=48000;tail.left.assign(2400,0);tail.right.assign(2400,0);
    pt::Sample wet;wet.rate=48000;wet.left.assign(2400,1);wet.right.assign(2400,-1);
    pt::Settings settings;settings.root=36;settings.output=1;settings.tailGain=1;
    pt::Voice voice;voice.start(36,1,1,nullptr,&tail,settings,nullptr,wet.duration(),25);
    check(std::abs(voice.audibleEnd()-.075)<1.e-9,"Tail offset did not extend the generated wet voice");
    for(int i=0;i<1200;++i){auto parts=voice.nextParts(nullptr,&tail,48000,nullptr,&wet);check(parts.generatedTailReverb==std::array<float,2>{0,0},"Tail reverb started before its offset");}
    bool heard=false;
    for(int i=0;i<3000;++i){auto parts=voice.nextParts(nullptr,&tail,48000,nullptr,&wet);heard|=std::abs(parts.generatedTailReverb[0])+std::abs(parts.generatedTailReverb[1])>0;}
    check(heard,"Offset Tail reverb never became audible");
}

static void compositeVoiceTest(){
    pt::Sample composite;composite.rate=48000;composite.left.resize(7200);composite.right.resize(7200);
    for(size_t i=0;i<composite.left.size();++i)composite.left[i]=composite.right[i]=.2f*std::sin(float(2*pt::pi*220*i/48000));
    pt::Settings settings;settings.root=36;settings.output=1;settings.tailGain=1;settings.crossfadeMs=0;
    pt::Voice voice;voice.start(36,1,1,nullptr,&composite,settings,nullptr,0,0,true,.025);
    check(std::abs(voice.audibleEnd()-.15)<1.e-9,"Composite voice did not retain the full rendered reverb tail");
    bool audibleAfterDry=false;
    for(int i=0;i<6000;++i){const auto parts=voice.nextParts(nullptr,&composite,48000);if(i>2400)audibleAfterDry|=std::abs(parts.tail[0])>1.e-5f;
        check(parts.generatedTailReverb==std::array<float,2>{0,0},"Composite playback created a second wet signal route");}
    check(audibleAfterDry,"Composite reverb was cut off at the original dry Tail duration");
    // A composite must preserve the dry route's onset and correlation-normalised
    // join before any added wet starts, without applying an extra onset ramp.
    for(bool withPunch:{false,true}) {
        pt::Sample punch=composite;punch.left.resize(2400);punch.right.resize(2400);
        settings.crossfadeMs=8;settings.joinCorrelation=1;
        pt::Voice dry,combined;const auto* p=withPunch?&punch:nullptr;
        dry.start(36,1,1,p,&composite,settings);
        combined.start(36,1,1,p,&composite,settings,nullptr,0,0,true,composite.duration());
        for(int i=0;i<4800;++i) {
            const auto a=dry.nextParts(p,&composite,48000);
            const auto b=combined.nextParts(p,&composite,48000);
            check(a.tail==b.tail,"Composite onset/join differs from the dry Tail before added wet begins");
        }
    }
}

static void pitchBankTest(const char* path){
    const auto tail=loadFloatWave(path);
    pt::TailReverbSettings settings;settings.enabled=true;settings.tailSend=true;settings.mix=55;settings.spaceMs=180;settings.width=140;settings.distance=12;
    constexpr double root=36,stretch=1.12;
    auto bank=pt::renderRootTailCompositeBank(tail,root,0,settings,stretch,4,false);
    check(bank.rootRender && bank.rootRender->left.size()>tail.left.size(),"Root composite did not include the generated reverb decay");
    check(bank.notes[36]->left.size()==size_t(std::llround(bank.rootRender->left.size()*stretch)),"Root wet did not receive the selected stretch");
    check(bank.notes[41]->left.size()==bank.notes[36]->left.size(),"Pitched wet duration differs from root wet duration");
    const auto tailAnalysis=pt::cycle::analyze(tail,440.0*std::pow(2.0,(root-69.0)/12.0));
    check(std::equal(tailAnalysis.marks.begin(),tailAnalysis.marks.end(),bank.phaseMap.marks.begin()),"Wet pitch processing diverged from the Tail cycle timeline");
    const auto& analysis=bank.phaseMap;
    const auto expected=pt::character::render(*bank.rootRender,5,pt::character::prepare(*bank.rootRender,analysis,stretch));
    check(bank.notes[41]->left==expected.left && bank.notes[41]->right==expected.right,
          "Root-rendered Tail + reverb did not use one selected Tail pitch/stretch algorithm");
    check(bank.notes[14]==bank.notes[36] && bank.notes[52]==bank.notes[36],"Unsupported notes must retain the root Tail composite");
    check(bank.dryTailDurations[41]>0 && bank.dryTailDurations[41]<bank.notes[41]->duration(),"Composite bank lost the dry Tail join duration");
    settings.spaceMs=80;
    {   // v0.4.61: Highest Fidelity v4 is the only tail engine (Legacy/Extreme removed).
        auto other=pt::renderRootTailCompositeBank(tail,root,0,settings,1.0,4,false);
        check(other.notes[36]==other.rootRender,"Neutral root composite must retain exact root render identity");
        auto expectedOther=pt::character::render(*other.rootRender,5,pt::character::prepare(*other.rootRender,other.phaseMap,1.0));
        check(other.notes[41]->left==expectedOther.left && other.notes[41]->right==expectedOther.right,
              "Composite did not use the Highest Fidelity v4 Tail engine");
    }
}

static pt::Sample compactTail() {
    pt::Sample s;s.rate=8000;s.left.resize(1600);s.right.resize(1600);
    const double hz=440*std::pow(2.,(36.-69)/12.);
    for(size_t i=0;i<s.left.size();++i){s.left[i]=float(.3*std::sin(2*pt::pi*hz*i/s.rate));s.right[i]=s.left[i]*.7f;}
    return s;
}
static void stretchAndCacheTest() {
    const auto tail=compactTail();pt::TailReverbSettings s;s.enabled=true;s.mix=70;s.spaceMs=20;s.distance=0;
    pt::TailCompositeRenderSettings p;p.adaptiveAmount=400;
    // Highest Fidelity v4 and Adaptive Stretch v4 at every base stretch.
    for(int quality:{4,3})for(double stretch:{.8,1.12,1.5}) {
        auto b=pt::renderRootTailCompositeBank(tail,36,0,s,stretch,quality,false,p);
        check(b.notes[36]->left.size()==size_t(std::llround(b.rootRender->left.size()*stretch)),"Composite root lost base stretch");
        for(int note=pt::kLowestPitchNote;note<=pt::kHighestPitchNote;++note) {
            const double factor=stretch*(quality==3?pt::adaptiveDurationRatio(note-36,p.adaptiveAmount):1.);
            check(pt::usablePitchResult(*b.rootRender,*b.notes[size_t(note)],factor),"Composite base/adaptive duration mismatch");
            check(std::abs(b.dryTailDurations[size_t(note)]-tail.duration()*b.notes[size_t(note)]->duration()/b.rootRender->duration())<1e-10,
                  "Composite dry join duration disagrees with selected note duration");
        }
        // v0.4.49: Adaptive Stretch is built on Highest Fidelity v4 (Kick Character).
        auto expected=quality==3
            ? pt::character::render(*b.rootRender,5,pt::character::prepare(*b.rootRender,b.phaseMap,stretch),{},nullptr,stretch*pt::adaptiveDurationRatio(5,p.adaptiveAmount))
            : pt::character::render(*b.rootRender,5,pt::character::prepare(*b.rootRender,b.phaseMap,stretch));
        check(b.notes[41]->left==expected.left && b.notes[41]->right==expected.right,"Cached phase render differs from ordinary selected engine");
    }
    pt::TailCompositeCache cache;p.linearPhase=true;p.subMono=true;
    auto first=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false,p,{},&cache);
    check(cache.analysisBuilds==1 && cache.wetBuilds==1,"Cold composite cache did not prepare source and wet");
    s.mix=42;s.levelDb=-3;s.tailOffsetMs=-10;s.keepDry=false;
    auto reused=pt::renderRootTailCompositeBank(tail,36,0,s,.8,3,false,p,{},&cache);
    auto reference=pt::renderRootTailCompositeBank(tail,36,0,s,.8,3,false,p);
    check(cache.analysisBuilds==1 && cache.wetBuilds==1,"Mix/level/offset/stretch/engine unnecessarily regenerated root wet or source analysis");
    for(int note=15;note<=51;++note)
        check(reused.notes[size_t(note)]->left==reference.notes[size_t(note)]->left && reused.notes[size_t(note)]->right==reference.notes[size_t(note)]->right,
              "Reused composite cache changed stereo audio");
    auto changedStretch=pt::renderRootTailCompositeBank(tail,36,0,s,1.5,4,false,p,{},&cache);
    auto freshStretch=pt::renderRootTailCompositeBank(tail,36,0,s,1.5,4,false,p);
    check(cache.analysisBuilds==1 && cache.wetBuilds==1,"Stretch/engine change unnecessarily repeated source analysis or wet");
    for(int note=15;note<=51;++note)
        check(changedStretch.notes[size_t(note)]->left==freshStretch.notes[size_t(note)]->left &&
              changedStretch.notes[size_t(note)]->right==freshStretch.notes[size_t(note)]->right,"Cached source changed selected stretch audio");
    // Every wet generation input must invalidate the cache.
    auto rebuild=[&]{pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false,p,{},&cache);};
    size_t count=cache.wetBuilds;
    s.spaceMs=25;rebuild();check(cache.wetBuilds==++count,"Space did not invalidate cached wet");
    s.width=150;rebuild();check(cache.wetBuilds==++count,"Width did not invalidate cached wet");
    s.distance=10;rebuild();check(cache.wetBuilds==++count,"Distance did not invalidate cached wet");
    s.duckAmount=50;rebuild();check(cache.wetBuilds==++count,"Duck amount did not invalidate cached wet");
    s.duckHz=90;rebuild();check(cache.wetBuilds==++count,"Duck frequency did not invalidate cached wet");
    s.duckRecovery=100;rebuild();check(cache.wetBuilds==++count,"Duck recovery did not invalidate cached wet");
    p.bands[0].gain=3;rebuild();check(cache.wetBuilds==++count,"EQ band did not invalidate cached wet");
    p.eqOn=false;rebuild();check(cache.wetBuilds==++count,"EQ bypass did not invalidate cached wet");
    p.linearPhase=false;rebuild();check(cache.wetBuilds==++count,"Phase mode did not invalidate cached wet");
    p.subMonoHz=220;rebuild();check(cache.wetBuilds==++count,"Mono sub frequency did not invalidate cached wet");
    p.subMono=false;rebuild();check(cache.wetBuilds==++count,"Sub mono did not invalidate cached wet");
    p.subMonoHz=150;rebuild();check(cache.wetBuilds==count,"Mono sub frequency rebuilt wet while mono sub is off");
    pt::renderRootTailCompositeBank(tail,36,10,s,1.,4,false,p,{},&cache);
    check(cache.wetBuilds==++count,"Trim did not invalidate cached wet");
    pt::renderRootTailCompositeBank(tail,37,10,s,1.,4,false,p,{},&cache);
    check(cache.analysisBuilds==2 && cache.wetBuilds==count,"Root change failed source-analysis invalidation or unnecessarily regenerated wet");
    auto replacement=tail;replacement.left[100]*=.5f;
    pt::renderRootTailCompositeBank(replacement,37,10,s,1.,4,false,p,{},&cache);
    check(cache.analysisBuilds==3 && cache.wetBuilds==++count,"Replacement source did not invalidate composite cache");
    bool cancelled=false;
    try{pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false,p,[]{return true;},&cache);}catch(const std::exception&){cancelled=true;}
    check(cancelled,"Cached preparation ignored cancellation");
}
static void negativeOffsetTest() {
    const auto tail=compactTail();pt::TailReverbSettings s;s.enabled=true;s.mix=100;s.keepDry=false;s.spaceMs=20;s.distance=0;s.duckAmount=0;
    const auto zero=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false);
    for(int ms:{-10,-300}) {
        s.tailOffsetMs=float(ms);auto advanced=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false);
        const size_t skip=size_t(-ms*8);
        double energy=0;
        const size_t stop=(std::min)(advanced.rootRender->left.size()-16,zero.rootRender->left.size()>skip?zero.rootRender->left.size()-skip-16:size_t(0));
        for(size_t i=0;i<stop;++i) {
            check(advanced.rootRender->left[i]==zero.rootRender->left[i+skip] && advanced.rootRender->right[i]==zero.rootRender->right[i+skip],
                  "Negative offset did not advance only the prepared wet signal");
            energy+=std::abs(advanced.rootRender->left[i]);
        }
        if(ms==-10)check(energy>1e-4,"Advanced wet became silent");
        check(advanced.dryTailDurations[36]==tail.duration(),"Negative wet offset moved dry Tail timing");
        check(advanced.rootRender->left.size()>=tail.left.size(),"Negative offset shortened the dry Tail window");
    }
}

// v0.4.59: Root Render + Pitch bakes the Tail fade-in into the composite wet,
// after the cached wet render; the reverb engine selection invalidates it.
static void rootFadeAndEngineTest() {
    const auto tail=compactTail();pt::TailReverbSettings s;s.enabled=true;s.mix=100;s.keepDry=true;s.spaceMs=120;s.distance=0;s.duckAmount=0;
    pt::TailCompositeCache cache;
    const auto immediate=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false,{},{},&cache);
    const size_t builds=cache.wetBuilds;
    s.tailFadeMs=250;
    const auto faded=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false,{},{},&cache);
    check(cache.wetBuilds==builds,"Tail fade-in regenerated the cached root wet");
    const auto reference=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false);
    check(faded.rootRender->left==reference.rootRender->left,"Cached faded composite differs from a cold render");
    s.tailFadeMs=0;const auto dryOnly=[&]{auto d=s;d.mix=0;return pt::renderRootTailCompositeBank(tail,36,0,d,1.,4,false);}();
    s.tailFadeMs=250;
    const auto& a=*immediate.rootRender;const auto& b=*faded.rootRender;const auto& dry=*dryOnly.rootRender;
    const size_t fadeEnd=size_t(.25*tail.rate);
    check(std::abs(b.left[0]-dry.left[0])<1e-9f,"Faded Tail wet was audible at its onset");
    const size_t earlyEnd=(std::min)(fadeEnd*2/5,dry.left.size()); // first 100 ms: fade gain <= 0.4
    double early=0,earlyFaded=0;for(size_t i=0;i<earlyEnd;++i){early+=std::pow(a.left[i]-dry.left[i],2);earlyFaded+=std::pow(b.left[i]-dry.left[i],2);}
    check(early>1e-8 && earlyFaded<.2*early,"Root composite Tail fade-in did not attenuate the early wet");
    for(size_t i=fadeEnd+1;i+32<a.left.size();++i)check(a.left[i]==b.left[i] && a.right[i]==b.right[i],"Root composite changed after the Tail fade completed");
    // Engine change must rebuild wet, produce different wet, and keep the dry identical.
    const size_t before=cache.wetBuilds;s.engine=1;
    const auto v2=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false,{},{},&cache);
    check(cache.wetBuilds==before+1,"Reverb engine change did not invalidate the cached root wet");
    check(v2.rootRender->left!=faded.rootRender->left,"V2 engine produced the Legacy root wet");
    s.mix=0;s.engine=0;const auto dryLegacy=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false);s.engine=1;const auto dryV2=pt::renderRootTailCompositeBank(tail,36,0,s,1.,4,false);
    check(dryLegacy.rootRender->left==dryV2.rootRender->left,"Reverb engine changed the dry Tail");
}

int main(int argc,char** argv)try{
    if(argc<2)throw std::runtime_error("Missing Tail WAV path");
    offsetTest();compositeVoiceTest();pitchBankTest(argv[1]);stretchAndCacheTest();negativeOffsetTest();rootFadeAndEngineTest();
    std::cout<<"PASS Tail + experimental reverb uses one pitch/stretch route and retains the full composite tail\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
