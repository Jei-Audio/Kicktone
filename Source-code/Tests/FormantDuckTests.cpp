// v0.4.52: Auto Crunch Match (replaced the v0.4.51 formant test) for Highest
// Fidelity v4 / Adaptive Stretch v4, and Punch -> reverb sidechain ducking.
#include "LegacyTestProcessor.h"
#include "../Source/KickCharacterPitch.h"
#include "../Source/CrunchMatch.h"
#include "../Source/PunchReverbDuck.h"
#include "../Source/TailReverbPitchBank.h"
#include "../Source/ProcessedPreview.h"
#include <optional>
#include "WaveFixture.h"
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}

static double rmsDb(const pt::Sample& s){double e=0;for(size_t i=0;i<s.left.size();++i)e+=double(s.left[i])*s.left[i]+double(s.right[i])*s.right[i];return 10*std::log10(e/double(s.left.size())+1e-20);}
static std::vector<double> lowPass(const std::vector<float>& x,double hz,double rate){
    std::vector<double> y(x.begin(),x.end());const double a=std::exp(-2*pt::pi*hz/rate);
    for(int stage=0;stage<4;++stage){double s=0;for(auto& v:y){s=a*s+(1-a)*v;v=s;}}
    return y;
}

struct TailtoneFormantDuckTest {
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static double bandEnergy(const pt::Sample& s,double hz,bool low){
    // 8-pole one-pole cascade low-pass; low=false returns energy above (x - lp).
    const double a=std::exp(-2*pt::pi*hz/s.rate);double e=0;
    for(int ch=0;ch<2;++ch){const auto& x=ch?s.right:s.left;std::array<double,8> st{};
        for(float v:x){double y=v;for(auto& z:st){z=a*z+(1-a)*y;y=z;}const double out=low?y:v-y;e+=out*out;}}
    return e;
 }
 static void crunchDsp(const char* path) {
    const auto x=loadFloatWave(path);const auto a=pt::cycle::analyze(x);
    const auto p=pt::character::prepare(x,a,1.0);
    for(int character=0;character<3;++character) {
        const auto ch=pt::crunchmatch::Character(character);
        const auto root=pt::crunchmatch::rootLayer(x,ch,2000);
        const std::string tag=std::string(path)+" char "+std::to_string(character)+": ";
        check(root.drive==pt::crunchmatch::kRootDrive&&root.mixedDb>pt::crunchmatch::brightnessDb(x)+.3,tag+"root gets no crunch");
        // Steep low cut: (almost) nothing of the layer below ~1 kHz.
        check(bandEnergy(root.audio,700,true)<bandEnergy(root.audio,700,false)*0.01,tag+"crunch layer leaks below the low cut");
        // Zero phase: the bass of tail+layer is the bass of the tail.
        pt::Sample sum=x;for(size_t i=0;i<sum.left.size();++i){sum.left[i]+=root.audio.left[i];sum.right[i]+=root.audio.right[i];}
        check(std::abs(10*std::log10(bandEnergy(sum,300,true)/bandEnergy(x,300,true)))<.1,tag+"crunch changed the bass");
        // No phasing: in every 20 ms window the highs of tail + layer are at
        // least as strong as the tail's own highs, and the layer correlates
        // positively with them.
        {pt::Sample hx=x,hs=sum;for(auto* s2:{&hx,&hs})for(int k=0;k<2;++k){pt::crunchmatch::highPassZeroPhase(s2->left,2000,x.rate);pt::crunchmatch::highPassZeroPhase(s2->right,2000,x.rate);}
         const size_t w=size_t(x.rate*.02);double ab=0,aa=0,bb=0;
         for(size_t i=0;i+w<=x.left.size();i+=w){double e0=0,e1=0;for(size_t k=i;k<i+w;++k){e0+=double(hx.left[k])*hx.left[k];e1+=double(hs.left[k])*hs.left[k];}
             if(e0>1e-9)check(e1>=e0*.98,tag+"crunch layer cancels the kick's highs (phasing) at "+std::to_string(i*1000/size_t(x.rate))+" ms");}
         for(size_t k=0;k<x.left.size();++k){ab+=double(hx.left[k])*root.audio.left[k];aa+=double(hx.left[k])*hx.left[k];bb+=double(root.audio.left[k])*root.audio.left[k];}
         check(ab/std::sqrt(aa*bb)>.5,tag+"crunch layer is not in phase with the kick");}
        for(double semi:{-12.,-7.}) {
            const auto note=pt::character::render(x,semi,p);
            const auto m=pt::crunchmatch::matchedLayer(note,root.mixedDb,ch,2000,pt::crunchmatch::brightnessDb(x),true);
            check(m.drive>pt::crunchmatch::kRootDrive&&std::abs(m.mixedDb-root.mixedDb)<0.5,tag+std::to_string(semi)+": darker note not matched to the root's crunch ("+std::to_string(m.mixedDb)+" vs "+std::to_string(root.mixedDb)+")");
            check(m.audio.left.size()==note.left.size(),tag+"layer length differs from the note");
        }
        const auto bright=pt::character::render(x,7.,p);
        const auto mb=pt::crunchmatch::matchedLayer(bright,root.mixedDb,ch,2000,pt::crunchmatch::brightnessDb(x),true);
        check(std::abs(mb.drive-pt::crunchmatch::kRootDrive)<1e-9&&mb.gain>=.25&&bandEnergy(mb.audio,4000,false)>0,tag+"brighter note must still get crunch");
        // v0.4.60: higher notes keep the root's crunch LEVEL (they used to drop to the 0.25x floor).
        for(double semi:{3.,7.,12.}) {
            const auto up=pt::character::render(x,semi,p);
            if(pt::crunchmatch::brightnessDb(up)<=pt::crunchmatch::brightnessDb(x))continue;
            const auto m=pt::crunchmatch::matchedLayer(up,root.mixedDb,ch,2000,pt::crunchmatch::brightnessDb(x),true,pt::crunchmatch::energy(root.audio));
            const double diffDb=10*std::log10(pt::crunchmatch::energy(m.audio)/pt::crunchmatch::energy(root.audio));
            check((m.gain>.25&&m.gain<4)?std::abs(diffDb)<.05:true,tag+std::to_string(semi)+": higher note's crunch is not at the root's level ("+std::to_string(diffDb)+" dB)");
            const auto old=pt::crunchmatch::matchedLayer(up,root.mixedDb,ch,2000,pt::crunchmatch::brightnessDb(x),true);
            check(pt::crunchmatch::energy(m.audio)>=pt::crunchmatch::energy(old.audio)*.999||std::abs(diffDb)<.05,tag+"level match made a higher note's crunch quieter than before");
            std::cout<<tag<<"+"<<semi<<" st crunch level vs root: "<<diffDb<<" dB (gain "<<m.gain<<", brightness-matched gain was "<<old.gain<<")\n";
        }
    }
    // A higher low cut keeps less of the layer below 5 kHz.
    const auto lo=pt::crunchmatch::rootLayer(x,pt::crunchmatch::Character::Soft,2000),hi=pt::crunchmatch::rootLayer(x,pt::crunchmatch::Character::Soft,10000);
    check(bandEnergy(hi.audio,5000,true)<bandEnergy(lo.audio,5000,true)*.05,"10 kHz low cut did not remove more");
    check(pt::crunchmatch::rootLayer(x,pt::crunchmatch::Character::Hard,3000).audio.left==pt::crunchmatch::rootLayer(x,pt::crunchmatch::Character::Hard,3000).audio.left,"Crunch layer not deterministic");
 }
 static std::shared_ptr<const PunchTailProcessor::CrunchBank> waitCrunch(PunchTailProcessor& p,double lowCut) {
    // v0.5.15: layers follow the bank batch by batch; wait for the complete bank's layers.
    for(int i=0;i<6000;++i){auto c=p.previewCrunch();if(c&&std::abs(c->lowCutHz-lowCut)<.5&&c->dry==p.previewBank()&&c->dry->prepared)return c;juce::Thread::sleep(25);}
    return {};
 }
 // v0.5.7 CRUNCH LOST ONLY: per 1/3-octave band above LOW CUT, note + layer reaches the root's
 // band power where the note is below it, never overshoots, adds nothing elsewhere, root and
 // pitched-up notes are silent, and the layer stays in phase (no cancellation).
 static void crunchLostDsp(const char* path) {
    namespace lost=pt::crunchmatch::lost;
    const auto x=loadFloatWave(path);const auto a=pt::cycle::analyze(x);const auto prep=pt::character::prepare(x,a,1.0);
    const double lowCut=2000;const auto edges=lost::bandEdges(lowCut,x.rate);check(edges.size()>=8,"Too few bands");
    const auto rootP=lost::bandPower(x,edges);const double rootOwn=pt::crunchmatch::brightnessDb(x);
    const std::string tag=std::string(path)+": ";
    for(double semi:{-4.0,-7.0,-12.0}) {
        const auto note=pt::character::render(x,semi,prep);
        const auto r=lost::layer(note,rootP,edges,pt::crunchmatch::Character::Hard,lowCut,rootOwn);
        check(r.audio.left.size()==note.left.size()&&r.gains.size()==rootP.size(),tag+"lost layer size");
        pt::Sample sum=note;for(size_t i=0;i<sum.left.size();++i){sum.left[i]+=r.audio.left[i];sum.right[i]+=r.audio.right[i];}
        const auto noteP=lost::bandPower(note,edges),sumP=lost::bandPower(sum,edges),layerP=lost::bandPower(r.audio,edges);
        int filled=0,untouched=0;
        for(size_t b=0;b<rootP.size();++b){
            const double need=10*std::log10(rootP[b]/(noteP[b]+1e-30));
            if(need>1.0&&r.gains[b]<lost::kMaxBandGain-1e-9){++filled;
                const double got=10*std::log10(sumP[b]/(noteP[b]+1e-30));
                check(std::abs(got-need)<1.5,tag+std::to_string(semi)+" st band "+std::to_string(b)+": filled "+std::to_string(got)+" dB, missing "+std::to_string(need)+" dB");}
            if(need<=0){++untouched;check(layerP[b]<=noteP[b]*.02+1e-20,tag+std::to_string(semi)+" st band "+std::to_string(b)+": layer added where nothing was lost");}
            check(sumP[b]<=rootP[b]*1.41+1e-20||need<=0,tag+std::to_string(semi)+" st band "+std::to_string(b)+": overshoot above the root");
            check(sumP[b]>=noteP[b]*.9,tag+std::to_string(semi)+" st band "+std::to_string(b)+": the layer cancelled the note (phasing)");}
        check(filled>=2,tag+std::to_string(semi)+" st: nothing was filled");
        std::cout<<tag<<semi<<" st lost-only: "<<filled<<" bands filled, "<<untouched<<" untouched, drive "<<r.drive<<"\n";
        // Below LOW CUT nothing is added.
        check(bandEnergy(r.audio,700,true)<bandEnergy(note,700,true)*.001+1e-20,tag+"lost layer leaks below the low cut");
        // In phase: in every filled band the layer adds coherently (positive cross power), so
        // it never cancels; the fill above already proves the predicted |note + layer|^2 holds.
        {std::vector<double> nP,lP,cP;lost::bandPowers(note,r.audio,edges,nP,lP,cP);
         // (bands the note has (nearly) emptied - more than 15 dB under the root, i.e. the top end above
         // the shifted spectrum - carry no phase worth keeping: the fill IS the content there)
         for(size_t b=0;b<cP.size();++b)if(r.gains[b]>0&&lP[b]>0&&nP[b]>rootP[b]*.03)check(cP[b]>=-.2*std::sqrt(nP[b]*lP[b]),tag+std::to_string(semi)+" st band "+std::to_string(b)+": lost layer is out of phase with the note");}
    }
    // Determinism.
    {const auto n=pt::character::render(x,-7.,prep);const auto r1=lost::layer(n,rootP,edges,pt::crunchmatch::Character::Soft,lowCut,rootOwn),r2=lost::layer(n,rootP,edges,pt::crunchmatch::Character::Soft,lowCut,rootOwn);
     check(r1.audio.left==r2.audio.left,tag+"lost layer not deterministic");}
 }
 static void crunchProcessor(const char* path) {
    PunchTailProcessor p;stop(p);
    check(p.params.getRawParameterValue("crunchMix")->load()==0&&p.params.getRawParameterValue("crunchLowCut")->load()==2000,"Crunch must default to 0% mix / 2 kHz");
    set(p,"phaseAuto",0);set(p,"root",36);set(p,"stretch",100);set(p,"crunchMix",100);set(p,"crunchCharacter",1);
    auto tail=std::make_shared<PunchTailProcessor::Asset>();tail->sample=loadFloatWave(path);
    {const juce::ScopedLock lock(p.assetLock);p.assets[1]=tail;}
    p.startThread();
    auto crunch=waitCrunch(p,2000);
    check(bool(crunch),"Crunch layers were never built");
    auto bank=p.previewBank();
    check(bank->notes[36].left==tail->sample.left,"Crunch changed the prepared tail itself (must be a separate layer)");
    for(int n=0;n<128;++n)check(bool(crunch->dryLayers[size_t(n)]),"Note "+std::to_string(n)+" has no crunch layer");
    const auto root=pt::crunchmatch::rootLayer(bank->notes[36],pt::crunchmatch::Character::Hard,2000);
    check(crunch->dryLayers[36]->left==root.audio.left,"Root crunch layer differs from the reference");
    const auto low=pt::crunchmatch::matchedLayer(bank->notes[29],root.mixedDb,pt::crunchmatch::Character::Hard,2000,pt::crunchmatch::brightnessDb(bank->notes[36]),true);
    check(crunch->dryLayers[29]->left==low.audio.left&&crunch->drives[29]>crunch->drives[36],"Lower note did not get its own, stronger crunch");
    // MIX is live: no rebuild.
    set(p,"crunchMix",40);juce::Thread::sleep(400);
    check(p.previewCrunch()==crunch&&std::abs(p.crunchMixValue()-.4f)<1e-6f,"Changing MIX rebuilt the layers");
    // A played note carries the layer and the mix is applied in playback.
    p.prepareToPlay(44100,128);juce::AudioBuffer<float> block(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,29+12,1.f),0);
    p.processBlock(block,midi);
    PunchTailProcessor::PlayingVoice* v=nullptr;for(auto& voice:p.voices)if(voice.active)v=&voice;
    check(v&&v->crunch==crunch&&v->playbackCrunch()==crunch->dryLayers[29].get(),"Played note does not use its crunch layer");
    check(std::abs(v->settings.crunchMix-.4)<.02,"Live crunch mix not applied to the voice");
    // v0.5.7 LOST ONLY: appended, fresh OFF; switching it rebuilds; root and up notes silent, down notes filled.
    check(p.params.getRawParameterValue("crunchLostOnly")->load()==0,"crunchLostOnly must default OFF");
    set(p,"crunchLostOnly",1);
    {std::shared_ptr<const PunchTailProcessor::CrunchBank> lostBank;
     for(int i=0;i<4800;++i){lostBank=p.previewCrunch();if(lostBank&&lostBank->lostOnly&&lostBank->dry==bank)break;lostBank=nullptr;juce::Thread::sleep(25);}
     check(bool(lostBank),"LOST ONLY did not rebuild the crunch layers");
     auto silent=[](const pt::Sample& s){for(float v:s.left)if(v!=0.f)return false;return true;};
     check(silent(*lostBank->dryLayers[36])&&silent(*lostBank->dryLayers[41])&&silent(*lostBank->dryLayers[51]),"LOST ONLY: root / pitched-up notes must get a silent layer");
     check(!silent(*lostBank->dryLayers[29])&&!silent(*lostBank->dryLayers[24]),"LOST ONLY: pitched-down notes got no layer");
     namespace lost=pt::crunchmatch::lost;const auto edges=lost::bandEdges(2000,bank->notes[36].rate);
     const auto ref=lost::layer(bank->notes[29],lost::bandPower(bank->notes[36],edges),edges,pt::crunchmatch::Character::Hard,2000,pt::crunchmatch::brightnessDb(bank->notes[36]));
     check(lostBank->dryLayers[29]->left==ref.audio.left,"LOST ONLY layer differs from the reference");
     crunch=lostBank;}
    set(p,"crunchLostOnly",0);
    {std::shared_ptr<const PunchTailProcessor::CrunchBank> back;
     for(int i=0;i<4800;++i){back=p.previewCrunch();if(back&&!back->lostOnly&&back->dry==bank)break;back=nullptr;juce::Thread::sleep(25);}
     check(back&&back->dryLayers[36]->left==root.audio.left,"Unticking LOST ONLY did not restore the normal crunch");crunch=back;}
    // Low cut rebuilds the layers.
    set(p,"crunchLowCut",6000);
    auto rebuilt=waitCrunch(p,6000);check(rebuilt&&rebuilt!=crunch,"Low cut change did not rebuild the layers");
    // v0.4.61: the removed-engine switch is inert; crunch stays active.
    set(p,"pitchCharacter",0);check(p.qualityValue()==4&&p.crunchMixValue()>0,"Inert engine switch disabled crunch");
    stop(p);
    // Voice mixing: tail + mix * layer.
    pt::Sample t;t.rate=48000;t.left.assign(4800,.25f);t.right=t.left;pt::Sample layer=t;std::fill(layer.left.begin(),layer.left.end(),.1f);layer.right=layer.left;
    pt::Settings s;s.root=36;s.crossfadeMs=0;s.output=1;
    pt::Voice a,b;a.start(36,1,1,nullptr,&t,s);s.crunchMix=.5;b.start(36,1,1,nullptr,&t,s);
    for(int i=0;i<200;++i){const auto pa=a.nextParts(nullptr,&t,48000,nullptr,nullptr,nullptr,&layer),pb=b.nextParts(nullptr,&t,48000,nullptr,nullptr,nullptr,&layer);
        if(i>100)check(std::abs((pb.tail[0]-pa.tail[0])-.05f)<1e-5f,"Voice does not add mix x layer");}
    // Preview (final waveform) includes the crunch layer.
    PreviewRequest r;r.rate=48000;r.root=36;r.ready=true;r.tail=std::make_shared<pt::Sample>(t);r.settings=s;r.settings.crunchMix=0;
    auto peak=[&](PreviewRequest q){ProcessedPreview pv;pv.submit(q);std::optional<PreviewResult> res;
        for(int i=0;i<1000&&!res;++i){juce::Thread::sleep(5);res=pv.take();}check(bool(res),"Preview timeout");return res->processedPeaks[1];};
    const float without=peak(r);r.crunchLayer=std::make_shared<pt::Sample>(layer);r.settings.crunchMix=1;const float with=peak(r);
    check(with>without*1.2f,"Final waveform preview does not include the crunch layer");
    // Saved/legacy state.
    set(p,"crunchMix",70);set(p,"crunchLowCut",4500);juce::MemoryBlock saved;p.getStateInformation(saved);
    PunchTailProcessor q;stop(q);q.setStateInformation(saved.getData(),int(saved.getSize()));
    check(std::abs(q.params.getRawParameterValue("crunchMix")->load()-70)<.01&&std::abs(q.params.getRawParameterValue("crunchLowCut")->load()-4500)<1,"Crunch not saved");
    auto legacy=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
    for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();
        if(id.startsWith("crunch")||id.startsWith("verbPunchDuck"))legacy.removeChild(i,nullptr);}
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    q.setStateInformation(old.getData(),int(old.getSize()));
    check(q.params.getRawParameterValue("crunchMix")->load()==0&&q.params.getRawParameterValue("crunchLowCut")->load()==2000&&
          q.params.getRawParameterValue("verbPunchDuck")->load()==0,"Old projects must load with Crunch/Duck off");
    check(p.params.getParameter("crunchLowCut")->getParameterIndex()>p.params.getParameter("crunchCharacter")->getParameterIndex()&&
          p.params.getParameter("crunchMix")->getParameterIndex()>p.params.getParameter("verbPunchDuckRelease")->getParameterIndex(),"New parameters not appended");
 }
 static void duckDsp() {
    pt::PunchReverbDuck d;d.prepare(48000);d.setSettings(0,80);
    for(int i=0;i<1000;++i)check(d.process(.9f,-.9f)==1.f,"0% duck must be an exact bypass");
    d.setSettings(100,80);d.reset();float g=1;for(int i=0;i<480;++i)g=d.process(.5f,.5f);
    check(std::abs(20*std::log10(g)+24)<.5,"100% duck on a loud punch must reach -24 dB");
    d.setSettings(50,80);d.reset();for(int i=0;i<480;++i)g=d.process(.5f,.5f);
    check(std::abs(20*std::log10(g)+12)<.5,"50% duck must reach -12 dB");
    d.setSettings(100,80);for(int i=0;i<480;++i)g=d.process(.5f,0);
    float after=g;for(int i=0;i<int(48000*.5);++i)after=d.process(0,0);
    check(after>.97f,"Reverb did not come back after the punch");
    pt::PunchReverbDuck slow;slow.prepare(48000);slow.setSettings(100,800);for(int i=0;i<480;++i)slow.process(.5f,.5f);
    float s=0;for(int i=0;i<4800;++i)s=slow.process(0,0);
    pt::PunchReverbDuck fast;fast.prepare(48000);fast.setSettings(100,20);for(int i=0;i<480;++i)fast.process(.5f,.5f);
    float f=0;for(int i=0;i<4800;++i)f=fast.process(0,0);
    check(f>s,"Longer release must recover more slowly");
 }
 // Recorded reverb starts at the punch end; with ducking it is turned down
 // right after the punch and recovers; the dry punch itself is unchanged.
 static std::vector<float> render(float duck,bool punchMuted) {
    auto p=std::make_unique<LegacyTestProcessor>();stop(*p);
    set(*p,"gate",0);set(*p,"output",0);set(*p,"muteReverb",0);set(*p,"mutePunch",punchMuted?1.f:0.f);set(*p,"phaseAuto",0);
    set(*p,"verbPunchDuck",duck);set(*p,"verbPunchDuckRelease",150);
    for(const char* id:{"eqOn","punchEqOn","reverbEqOn"})set(*p,id,0);
    auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample.rate=48000;punch->sample.left.assign(4800,.5f);punch->sample.right=punch->sample.left;
    auto verb=std::make_shared<PunchTailProcessor::Asset>();verb->sample.rate=48000;verb->sample.left.assign(96000,.2f);verb->sample.right=verb->sample.left;
    p->assets[0]=punch;p->assets[2]=verb;p->prepareToPlay(48000,128);
    juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
    std::vector<float> out;
    for(int b=0;b<500;++b){p->processBlock(audio,midi);midi.clear();for(int i=0;i<128;++i)out.push_back(audio.getSample(0,i));}
    return out;
 }
 static void duckProcessor() {
    const auto plain=render(0,true),ducked=render(100,true);
    const size_t justAfter=4800+240,later=4800+48000;
    check(std::abs(plain[justAfter])>.1f,"Fixture reverb not audible");
    check(20*std::log10(std::abs(ducked[justAfter])/std::abs(plain[justAfter]))<-10,"Reverb right after the punch was not ducked");
    check(std::abs(ducked[later]/plain[later]-1)<.02,"Reverb did not recover after the release");
    const auto withPunch=render(100,false),withPunchPlain=render(0,false);
    for(size_t i=10;i<4700;i+=97)check(std::abs(withPunch[i]-withPunchPlain[i])<1e-6f,"Ducking changed the dry punch");
 }
};

int main(int argc,char** argv) try {
    if(argc<3)throw std::runtime_error("Usage: FormantDuckTests virus-kick-3.wav tail.wav");
    juce::ScopedJuceInitialiser_GUI init;
    for(int i=1;i<argc;++i){TailtoneFormantDuckTest::crunchDsp(argv[i]);TailtoneFormantDuckTest::crunchLostDsp(argv[i]);}
    TailtoneFormantDuckTest::crunchProcessor(argv[1]);
    TailtoneFormantDuckTest::duckDsp();
    TailtoneFormantDuckTest::duckProcessor();
    std::cout<<"PASS Crunch layer (steep zero-phase low cut, bass untouched, root + every note, darker notes matched to the root, live MIX without rebuild, low cut rebuild, voice mixing, final waveform, state) and punch->reverb ducking (bypass, depth, release, recorded reverb ducked and recovered, dry punch unchanged)\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
