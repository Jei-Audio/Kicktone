// v0.5.20 EQ POSITION: Punch EQ / Tail EQ before or after their distortion.
// After = the EQ follows the distortion; the Tail EQ then covers the whole
// Tail + Reverb distortion bus. Processor, smooth live switching, state, preview.
#include "LegacyTestProcessor.h"
#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* param=p.params.getParameter(id);param->setValueNotifyingHost(param->convertTo0to1(v));}
static float value(PunchTailProcessor& p,const char* id){return p.params.getRawParameterValue(id)->load();}

struct TailtoneEqPositionTest {
static constexpr double kRate=48000;
static constexpr int kBlock=128;
static constexpr int kNoteBlock=8;   // the note starts once the distortion's 15 ms parameter ramps have settled
struct Setup {
    int bus=1;                // 0 Punch EQ, 1 Tail EQ
    bool eqOn=true,post=false,distortion=true;
    bool punch=false,tail=true,reverb=false,muteTail=false;
};
static std::shared_ptr<PunchTailProcessor::Asset> sine(float amplitude,double hz,double seconds) {
    auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=kRate;
    for(int i=0;i<int(seconds*kRate);++i){const float v=amplitude*float(std::sin(2*pt::pi*hz*i/kRate));a->sample.left.push_back(v);a->sample.right.push_back(.8f*v);}
    return a;
}
static std::unique_ptr<LegacyTestProcessor> make(const Setup& s) {
    auto p=std::make_unique<LegacyTestProcessor>();p->signalThreadShouldExit();p->notify();p->stopThread(-1);
    set(*p,"output",0);set(*p,"crossfade",0);set(*p,"outputLimiter",0);set(*p,"phaseAuto",0);
    set(*p,"muteReverb",0);set(*p,"muteTail",s.muteTail?1.f:0.f);set(*p,"reverbEqOn",0);
    set(*p,"eqOn",s.bus==1&&s.eqOn?1.f:0.f);set(*p,"punchEqOn",s.bus==0&&s.eqOn?1.f:0.f);
    for(const char* prefix:{"eq1","punchEq1"}){const juce::String b(prefix);   // a +12 dB bell on each signal's fundamental
        set(*p,(b+"Freq").toRawUTF8(),b=="eq1"?80.f:240.f);set(*p,(b+"Gain").toRawUTF8(),12);set(*p,(b+"Q").toRawUTF8(),1);}
    for(const char* prefix:{"punchDist","tailDist"}){const juce::String d(prefix);
        set(*p,(d+"On").toRawUTF8(),s.distortion?1.f:0.f);set(*p,(d+"Mode").toRawUTF8(),1);   // hard clip
        set(*p,(d+"Drive").toRawUTF8(),30);set(*p,(d+"Gain").toRawUTF8(),-18);}
    set(*p,s.bus==0?"punchEqPost":"tailEqPost",s.post?1.f:0.f);
    if(s.punch)p->assets[0]=sine(.08f,240,.4);
    if(s.tail){auto t=sine(.08f,80,.4);p->assets[1]=t;p->bank=std::make_shared<PunchTailProcessor::Bank>();for(auto& n:p->bank->notes)n=t->sample;}
    if(s.reverb)p->assets[2]=sine(.08f,120,.4);
    return p;
}
// Interleaved stereo output; optionally switch a parameter at the start of a block.
static std::vector<float> render(PunchTailProcessor& p,int blocks,int switchBlock=-1,const char* id=nullptr,float to=0) {
    p.prepareToPlay(kRate,kBlock);juce::AudioBuffer<float> audio(2,kBlock);std::vector<float> out;
    for(int b=0;b<blocks;++b){juce::MidiBuffer midi;if(b==kNoteBlock)midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
        if(b==switchBlock)set(p,id,to);
        p.processBlock(audio,midi);
        for(int i=0;i<kBlock;++i)for(int ch=0;ch<2;++ch)out.push_back(audio.getSample(ch,i));}
    return out;
}
static double rms(const std::vector<float>& x,size_t from,size_t to){double s=0;for(size_t i=from;i<to;++i)s+=double(x[i])*x[i];return std::sqrt(s/double(to-from));}
// The same EQ applied, from a fresh state, to an output rendered with that EQ off.
static std::vector<float> equalised(const std::array<tt::Band,tt::kBands>& bands,const std::vector<float>& in,int warmSamples=0) {
    tt::ParametricEQ eq;eq.update(bands,true,kRate);
    for(int i=0;i<warmSamples;++i){float l=0,r=0;eq.process(l,r);}
    auto out=in;for(size_t i=0;i+1<out.size();i+=2)eq.process(out[i],out[i+1]);
    return out;
}
static float maxDiff(const std::vector<float>& a,const std::vector<float>& b,size_t from,size_t to){float m=0;for(size_t i=from;i<to;++i)m=(std::max)(m,std::abs(a[i]-b[i]));return m;}
static float maxStep(const std::vector<float>& x,size_t from,size_t to){float m=0;for(size_t i=from+2;i<to;++i)m=(std::max)(m,std::abs(x[i]-x[i-2]));return m;}

static void parameters() {
    auto p=std::make_unique<PunchTailProcessor>();
    check(value(*p,"punchEqPost")==0&&value(*p,"tailEqPost")==0,"EQ position must start BEFORE the distortion");
    const int last=p->params.getParameter("attackLayerEnd")->getParameterIndex();
    check(p->params.getParameter("punchEqPost")->getParameterIndex()==last+1&&p->params.getParameter("tailEqPost")->getParameterIndex()==last+2,
          "EQ position parameters were not appended after attackLayerEnd");
    // v0.5.21 appended the attack / decay normalize trims right after it (still appended).
    check(p->params.getParameter("punchAttackNormalize")->getParameterIndex()==p->params.getParameter("tailEqPost")->getParameterIndex()+1,"tailEqPost must stay right before the v0.5.21 parameters (appended)");
    check(!p->eqAfterDistortion(0)&&!p->eqAfterDistortion(1),"eqAfterDistortion default");
    set(*p,"tailEqPost",1);check(p->eqAfterDistortion(1)&&!p->eqAfterDistortion(0),"eqAfterDistortion does not follow its own parameter");
}

// AFTER = exactly the EQ applied to the distorted bus (an EQ-off render through the same EQ).
static void afterIsEqOfDistortedBus() {
    const int blocks=100;
    for(int bus=0;bus<2;++bus)for(bool reverbOnly:{false,true}) {
        if(bus==0&&reverbOnly)continue;
        Setup s;s.bus=bus;s.punch=bus==0;s.tail=bus==1;s.reverb=bus==1;s.muteTail=reverbOnly;
        Setup off=s;off.eqOn=false;
        auto after=make([&]{auto x=s;x.post=true;return x;}());auto plain=make(off);
        const auto a=render(*after,blocks),b=render(*plain,blocks);
        check(rms(b,2000,a.size())>.01,"EQ position fixture is silent");
        const auto ref=equalised(after->eqSettings(bus==0?1:0),b);
        check(maxDiff(a,ref,0,a.size())<1e-6f,bus==0?"Punch EQ AFTER is not the EQ of the distorted punch":
              reverbOnly?"Tail EQ AFTER does not EQ the recorded reverb on the Tail + Reverb bus":"Tail EQ AFTER is not the EQ of the distorted Tail + Reverb bus");
        check(rms(a,4800,a.size())>1.5*rms(b,4800,b.size()),"EQ AFTER did not boost the distorted signal");
    }
}

// BEFORE (the old order) is unchanged: the hard clip flattens a boost placed before it.
static void beforeAndAfterDiffer() {
    for(int bus=0;bus<2;++bus) {
        Setup s;s.bus=bus;s.punch=bus==0;s.tail=bus==1;
        Setup off=s;off.eqOn=false;Setup post=s;post.post=true;
        auto pb=make(s),pn=make(off),pa=make(post);
        const auto before=render(*pb,100),plain=render(*pn,100),after=render(*pa,100);
        const double rb=rms(before,4800,before.size()),rp=rms(plain,4800,plain.size()),ra=rms(after,4800,after.size());
        check(rb<1.5*rp,"A boost BEFORE a hard clip should be flattened by the clip");
        check(ra>2.5*rp&&ra>2.0*rb,"EQ AFTER the distortion did not shape the distorted sound");
    }
}

// Distortion off: EQ position makes (almost) no difference, the EQ is linear.
static void distortionOffIsLinear() {
    for(int bus=0;bus<2;++bus) {
        Setup s;s.bus=bus;s.punch=bus==0;s.tail=bus==1;s.distortion=false;   // no reverb: BEFORE leaves it un-EQ'd
        Setup post=s;post.post=true;
        auto pb=make(s),pa=make(post);
        const auto before=render(*pb,100),after=render(*pa,100);
        check(rms(before,4800,before.size())>.02,"Distortion-off fixture too quiet");
        check(maxDiff(before,after,0,before.size())<2e-5f,"With the distortion off, BEFORE and AFTER should sound the same");
    }
}

// A live switch crossfades over 10 ms: no click, then the same as starting in the new position.
static void liveSwitch() {
    for(int bus=0;bus<2;++bus)for(bool toPost:{true,false}) {
        Setup s;s.bus=bus;s.punch=bus==0;s.tail=bus==1;s.reverb=bus==1;s.post=!toPost;
        Setup target=s;target.post=toPost;
        const char* id=bus==0?"punchEqPost":"tailEqPost";
        const int switchBlock=30,blocks=110;
        auto switched=make(s),from=make(s),to=make(target);
        const auto x=render(*switched,blocks,switchBlock,id,toPost?1.f:0.f),a=render(*from,blocks),b=render(*to,blocks);
        const size_t at=size_t(switchBlock*kBlock*2),fade=size_t(kRate*.010)*2;
        check(maxDiff(x,a,0,at)==0.f,"Output changed before the EQ position switch");
        const float steady=(std::max)(maxStep(a,4800,a.size()),maxStep(b,4800,b.size()));
        check(maxStep(x,at-2*kBlock,at+fade+2*kBlock)<=1.1f*steady,"EQ position switch clicked");
        // Mid-fade the output lies between both positions (not a jump to either).
        check(maxDiff(x,a,at+fade/2,at+fade/2+80)>1e-4f&&maxDiff(x,b,at+fade/2,at+fade/2+80)>1e-4f,"EQ position switch did not crossfade");
        const size_t settled=at+fade+size_t(kRate*.1)*2;
        check(maxDiff(x,b,settled,x.size())<1e-5f,"After the switch the output differs from starting in the new EQ position");
        check(switched->eqPostWeight[size_t(bus)].getCurrentValue()==(toPost?1.f:0.f),"EQ position weight did not settle");
    }
}

static void state() {
    auto p=make(Setup{});set(*p,"punchEqPost",1);set(*p,"tailEqPost",0);
    juce::MemoryBlock saved;p->getStateInformation(saved);
    auto q=make(Setup{});q->setStateInformation(saved.getData(),int(saved.getSize()));
    check(value(*q,"punchEqPost")==1&&value(*q,"tailEqPost")==0,"Punch EQ AFTER did not restore");
    set(*p,"punchEqPost",0);set(*p,"tailEqPost",1);p->getStateInformation(saved);q->setStateInformation(saved.getData(),int(saved.getSize()));
    check(value(*q,"punchEqPost")==0&&value(*q,"tailEqPost")==1,"Tail EQ AFTER did not restore");
    // Projects saved before v0.5.20 (no such parameters) open with both EQs BEFORE the distortion.
    set(*p,"punchEqPost",1);p->getStateInformation(saved);
    auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
    for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();if(id=="punchEqPost"||id=="tailEqPost")legacy.removeChild(i,nullptr);}
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    q->setStateInformation(old.getData(),int(old.getSize()));
    check(value(*q,"punchEqPost")==0&&value(*q,"tailEqPost")==0,"Older projects must keep their EQs before the distortion");
}

// The Final Kick waveform / Export WAV follow the same order.
static void preview() {
    const auto tail=std::make_shared<pt::Sample>(sine(.08f,80,.4)->sample);
    const auto reverb=std::make_shared<pt::Sample>(sine(.08f,120,.4)->sample);
    std::array<tt::Band,tt::kBands> bands{};bands[1]={true,tt::Bell,0,80.0,12.0,1.0};
    auto request=[&](bool eqOn,bool post,bool distortion,bool withReverb=true){PreviewRequest r;r.rate=kRate;r.root=36;r.ready=true;
        r.samples[1]=tail;if(withReverb)r.samples[2]=reverb;r.settings.crossfadeMs=0;r.settings.reverbGain=1;
        r.bands[0]=bands;r.eqOn[0]=eqOn;r.eqPost[1]=post;
        r.distortion[1]={distortion,1,30.f,16000.f,1.f,-18.f};return r;};
    auto renderPreview=[](PreviewRequest r){ProcessedPreview pv;pv.submit(std::move(r));std::optional<PreviewResult> res;
        const auto deadline=juce::Time::getMillisecondCounter()+20000;
        while(!res&&juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);res=pv.take();}
        check(res&&res->audio,"Preview timeout");std::vector<float> out;
        for(size_t i=0;i<res->audio->left.size();++i){out.push_back(res->audio->left[i]);out.push_back(res->audio->right[i]);}
        return out;};
    const auto plain=renderPreview(request(false,false,true)),before=renderPreview(request(true,false,true)),after=renderPreview(request(true,true,true));
    check(plain.size()==after.size()&&plain.size()>size_t(kRate*.2)*2&&rms(plain,4800,plain.size())>.01,"Preview fixture");
    // The preview settles its EQ on 0.15 s of silence (29 blocks of 256) before the note.
    const auto ref=equalised(bands,plain,29*256);
    const size_t end=plain.size()-size_t(kRate*.01)*2;   // the master release fades the last millisecond
    check(maxDiff(after,ref,0,end)<1e-6f,"Preview Tail EQ AFTER is not the EQ of the distorted Tail + Reverb bus");
    check(rms(before,4800,end)<1.5*rms(plain,4800,end)&&rms(after,4800,end)>2.0*rms(before,4800,end),"Preview ignores the EQ position");
    // Distortion off (and no reverb, which BEFORE leaves un-EQ'd): BEFORE and AFTER agree in the preview as well.
    const auto linBefore=renderPreview(request(true,false,false,false)),linAfter=renderPreview(request(true,true,false,false));
    check(maxDiff(linBefore,linAfter,0,end)<2e-5f,"Preview BEFORE/AFTER differ with the distortion off");
}
};
int main()try{juce::ScopedJuceInitialiser_GUI init;
    TailtoneEqPositionTest::parameters();TailtoneEqPositionTest::afterIsEqOfDistortedBus();TailtoneEqPositionTest::beforeAndAfterDiffer();
    TailtoneEqPositionTest::distortionOffIsLinear();TailtoneEqPositionTest::liveSwitch();TailtoneEqPositionTest::state();TailtoneEqPositionTest::preview();
    std::cout<<"PASS EQ position: appended params (fresh/legacy BEFORE), AFTER = EQ of the distorted Punch / Tail + Reverb bus, "
               "BEFORE flattened by the clip, linear with distortion off, click-free 10 ms switch, state, preview\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
