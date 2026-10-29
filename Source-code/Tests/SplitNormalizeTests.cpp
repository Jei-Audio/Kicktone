// v0.5.21 ATTACK / DECAY NORMALIZE: separate output trims for the processed Punch
// before / after the Punch Attack/Decay SPLIT, on top of (not instead of) the
// whole-Punch normalize. Portable weights, processor (zero-latency and linear-phase
// latency), live ramp, state / legacy, Final Kick preview and its peak analysis.
#include "LegacyTestProcessor.h"
#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* param=p.params.getParameter(id);param->setValueNotifyingHost(param->convertTo0to1(v));}
static float value(PunchTailProcessor& p,const char* id){return p.params.getRawParameterValue(id)->load();}
static float db(float g){return 20.f*std::log10(g);}
static float gainOf(float d){return std::pow(10.f,d/20.f);}

struct TailtoneSplitNormalizeTest {
static constexpr double kRate=48000;
static constexpr int kBlock=128;
static constexpr int kNoteBlock=8;
static constexpr double kSplit=50,kTransition=2;
// A loud attack and a quieter decay; both quiet around the 50 % split (0.2 s).
static pt::Sample punchSample() {
    pt::Sample s;s.rate=kRate;
    for(int i=0;i<int(.4*kRate);++i){const double t=i/kRate;const float a=t<.15?.3f:.05f;
        const float v=a*float(std::sin(2*pt::pi*180*t));s.left.push_back(v);s.right.push_back(.8f*v);}
    return s;
}
static void setup(PunchTailProcessor& p,bool distortion=true) {
    p.signalThreadShouldExit();p.notify();p.stopThread(-1);
    set(p,"output",0);set(p,"crossfade",0);set(p,"outputLimiter",0);set(p,"phaseAuto",0);set(p,"punchOffset",0);set(p,"punchExtra",0);
    set(p,"punchRegionSplit",float(kSplit));set(p,"punchRegionTransition",float(kTransition));   // ENABLE stays OFF: the trims split anyway
    set(p,"punchDistOn",distortion?1.f:0.f);set(p,"punchDistMode",1);set(p,"punchDistDrive",30);set(p,"punchDistGain",-18);
    auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample=punchSample();p.assets[0]=a;
}
struct Run { std::vector<float> out; double punchEnd=0; int latency=0; };
static Run render(PunchTailProcessor& p,int blocks,int switchBlock=-1,const char* id=nullptr,float to=0) {
    p.prepareToPlay(kRate,kBlock);juce::AudioBuffer<float> audio(2,kBlock);Run r;r.latency=p.getLatencySamples();
    for(int b=0;b<blocks;++b){juce::MidiBuffer midi;if(b==kNoteBlock)midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
        if(b==switchBlock)set(p,id,to);
        p.processBlock(audio,midi);
        if(b==kNoteBlock)for(const auto& v:p.voices)if(v.active)r.punchEnd=v.punchEnd;
        for(int i=0;i<kBlock;++i)for(int ch=0;ch<2;++ch)r.out.push_back(audio.getSample(ch,i));}
    return r;
}
// Expected linear trim at output frame f.
static float expected(const Run& r,size_t frame,float attack,float decay) {
    const double t=(double(frame)-kNoteBlock*kBlock-r.latency)/kRate;
    if(t<0)return 1.f;
    return tt::PunchSplitTrim::gain(attack,decay,float(tt::PunchSplitTrim::decayWeight(t,r.punchEnd,kSplit,kTransition)));
}

static void portable() {
    for(int i=0;i<=1000;++i){const float w=i/1000.f;check(tt::PunchSplitTrim::gain(1.f,1.f,w)==1.f,"0 dB / 0 dB must be exactly unity");
        check(tt::PunchSplitTrim::gain(.5f,.5f,w)==.5f,"Equal trims must be exactly that trim");}
    check(tt::PunchSplitTrim::gain(.5f,2.f,0.f)==.5f&&tt::PunchSplitTrim::gain(.5f,2.f,1.f)==2.f,"Weight 0 = attack, 1 = decay");
    const double end=.4,b=.2,half=.001;
    check(tt::PunchSplitTrim::decayWeight(b-half-1e-6,end,50,2)==0&&tt::PunchSplitTrim::decayWeight(b+half+1e-6,end,50,2)==1,"Transition wider than TRANSITION");
    check(std::abs(tt::PunchSplitTrim::decayWeight(b,end,50,2)-.5)<1e-9,"Transition not centred on the split");
    double last=-1;for(int i=0;i<=200;++i){const double w=tt::PunchSplitTrim::decayWeight(b-half+i*1e-5,end,50,2);check(w>=last,"Transition not monotonic");last=w;}
    check(tt::PunchSplitTrim::decayWeight(.01,end,0,2)==1&&tt::PunchSplitTrim::decayWeight(.39,end,100,2)==0,"SPLIT 0 % = all decay, 100 % = all attack");
    check(tt::PunchSplitTrim::decayWeight(b-1e-9,end,50,0)==0&&tt::PunchSplitTrim::decayWeight(b,end,50,0)==1,"TRANSITION 0 must be a hard switch");
    // Same boundary / curve as the Attack/Decay EQ.
    tt::PunchRegionDesign d;d.settings={true,50,2,0,0};std::array<tt::Band,tt::kBands> flat{};
    d.update(d.settings,flat,false,flat,false,kRate);d.settings.decayGainDb=-24;d.settings.attackGainDb=0;
    tt::PunchRegionEQ eq;eq.start(d,kRate,end);
    for(int i=0;i<int(end*kRate);++i){float l=1,r=1;const double t=i/kRate;eq.process(l,r,t);
        const double w=tt::PunchSplitTrim::decayWeight(t,end,50,2),want=(1-w)+w*std::pow(10.,-24./20.);
        check(std::abs(l-want)<1e-5,"Split weight differs from the Punch Attack/Decay boundary");}
}

static void parameters() {
    auto p=std::make_unique<PunchTailProcessor>();
    check(value(*p,"punchAttackNormalize")==0&&value(*p,"punchDecayNormalize")==0,"Attack / decay trims must start at 0 dB");
    const int last=p->params.getParameter("tailEqPost")->getParameterIndex();
    check(p->params.getParameter("punchAttackNormalize")->getParameterIndex()==last+1&&p->params.getParameter("punchDecayNormalize")->getParameterIndex()==last+2,
          "Attack / decay trims were not appended after tailEqPost");
    // v0.5.23: glideStretched is appended right after them (still nothing in between).
    check(p->params.getParameter("glideStretched")->getParameterIndex()==p->params.getParameter("punchDecayNormalize")->getParameterIndex()+1,"punchDecayNormalize must stay appended (only later parameters after it)");
    for(const char* id:{"punchAttackNormalize","punchDecayNormalize"}){auto* r=dynamic_cast<juce::RangedAudioParameter*>(p->params.getParameter(id));
        check(r&&r->getNormalisableRange().start==-48&&r->getNormalisableRange().end==36,"Attack / decay trim range must match punchNormalize");}
    check(p->splitNormalizeTarget(0)==1.f&&p->splitNormalizeTarget(1)==1.f,"0 dB target must be unity");
    check(!tt::defaults::storable("punchAttackNormalize")&&!tt::defaults::storable("punchDecayNormalize"),"Sample-specific trims must not become custom defaults");
}

// The trims scale exactly their part of the processed (distorted) Punch, whole-Punch trim on top.
template<class Processor> static void trims(bool distortion,bool latency=false) {
    const int blocks=8+int(.55*kRate/kBlock);
    auto refP=std::make_unique<Processor>();setup(*refP,distortion);const auto ref=render(*refP,blocks);
    check(latency?ref.latency>0:ref.latency==0,"Latency precondition (linear-phase Tail wet reports latency)");
    check(ref.punchEnd>.39&&ref.punchEnd<.41,"Punch fixture timing");
    double loud=0;for(float v:ref.out)loud=(std::max)(loud,double(std::abs(v)));check(loud>.05,"Punch fixture is silent");
    struct Case{float attack,decay,whole;};
    for(const Case c:{Case{-6,0,0},Case{0,4,0},Case{-6,4,-3},Case{-12,-12,0}}) {
        auto p=std::make_unique<Processor>();setup(*p,distortion);
        set(*p,"punchAttackNormalize",c.attack);set(*p,"punchDecayNormalize",c.decay);set(*p,"punchNormalize",c.whole);
        const auto x=render(*p,blocks);
        check(x.latency==ref.latency&&x.out.size()==ref.out.size(),"Render length");
        const float a=gainOf(c.attack),d=gainOf(c.decay),w=gainOf(c.whole);
        for(size_t i=0;i<x.out.size();++i){const float want=ref.out[i]*expected(ref,i/2,a,d)*w;
            check(std::abs(x.out[i]-want)<=1e-6f+2e-5f*std::abs(want),"Attack / decay trim is not exactly its part of the processed Punch");}
        // Each part really changed level by its own trim (the other part kept its own).
        const size_t attackFrame=size_t(kNoteBlock*kBlock+ref.latency+int(.05*kRate)),decayFrame=size_t(kNoteBlock*kBlock+ref.latency+int(.3*kRate));
        double ra=0,rx=0,da=0,dx=0;for(size_t f=attackFrame;f<attackFrame+2400;++f){ra+=std::abs(ref.out[2*f]);rx+=std::abs(x.out[2*f]);}
        for(size_t f=decayFrame;f<decayFrame+2400;++f){da+=std::abs(ref.out[2*f]);dx+=std::abs(x.out[2*f]);}
        check(std::abs(db(float(rx/ra))-(c.attack+c.whole))<.01f&&std::abs(db(float(dx/da))-(c.decay+c.whole))<.01f,"Attack / decay levels did not follow their trims");
    }
    // 0 dB / 0 dB is the exact old output (same processor, explicitly reset).
    auto p=std::make_unique<Processor>();setup(*p,distortion);set(*p,"punchAttackNormalize",5);set(*p,"punchAttackNormalize",0);
    const auto zero=render(*p,blocks);check(zero.out==ref.out,"0 dB attack / decay trims changed the output");
}

// A live change ramps over 10 ms (no jump), like Normalize Punch.
static void liveRamp() {
    const int blocks=8+int(.25*kRate/kBlock),switchBlock=kNoteBlock+20;
    auto refP=std::make_unique<LegacyTestProcessor>();setup(*refP);const auto ref=render(*refP,blocks);
    auto p=std::make_unique<LegacyTestProcessor>();setup(*p);const auto x=render(*p,blocks,switchBlock,"punchAttackNormalize",-12.f);
    const size_t at=size_t(switchBlock*kBlock),ramp=size_t(kRate*.01);
    auto ratio=[&](size_t from,size_t to){double a=0,b=0;for(size_t f=from;f<to;++f){a+=std::abs(x.out[2*f]);b+=std::abs(ref.out[2*f]);}return float(a/b);};
    check(std::abs(ratio(at-480,at)-1)<1e-5f,"Output changed before the trim moved");
    const float mid=ratio(at+ramp/2-24,at+ramp/2+24);check(mid<.9f&&mid>.35f,"Trim did not ramp");
    check(std::abs(db(ratio(at+ramp+48,at+ramp+960))+12)<.01f,"Trim did not reach its target");
    float worst=0;
    for(size_t f=at;f<at+ramp+48;++f)if(std::abs(ref.out[2*f])>.02f&&std::abs(ref.out[2*f-2])>.02f)
        worst=(std::max)(worst,std::abs(x.out[2*f]/ref.out[2*f]-x.out[2*f-2]/ref.out[2*f-2]));
    check(worst<.01f,"Trim jumped instead of ramping");
}

static void state() {
    auto p=std::make_unique<LegacyTestProcessor>();setup(*p);set(*p,"punchAttackNormalize",-7.5f);set(*p,"punchDecayNormalize",3.25f);
    juce::MemoryBlock saved;p->getStateInformation(saved);
    auto q=std::make_unique<LegacyTestProcessor>();setup(*q);q->setStateInformation(saved.getData(),int(saved.getSize()));
    check(value(*q,"punchAttackNormalize")==-7.5f&&value(*q,"punchDecayNormalize")==3.25f,"Attack / decay trims did not restore");
    auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
    for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();if(id=="punchAttackNormalize"||id=="punchDecayNormalize")legacy.removeChild(i,nullptr);}
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    q->setStateInformation(old.getData(),int(old.getSize()));
    check(value(*q,"punchAttackNormalize")==0&&value(*q,"punchDecayNormalize")==0,"Older projects must open without attack / decay trims");
}

// Final Kick waveform / Export WAV: same trims, and the analysis behind the two buttons.
static void preview() {
    const auto punch=std::make_shared<pt::Sample>(punchSample());
    auto request=[&](float attack,float decay){PreviewRequest r;r.rate=kRate;r.root=36;r.ready=true;r.samples[0]=punch;
        r.settings.crossfadeMs=0;r.punchRegion={false,kSplit,kTransition,0,0};r.splitNormalizeGains={{attack,decay}};
        r.distortion[0]={true,1,30.f,16000.f,.3f,-18.f};return r;};   // parallel: a full hard clip would level both parts
    auto renderPreview=[](PreviewRequest r){ProcessedPreview pv;pv.submit(std::move(r));std::optional<PreviewResult> res;
        const auto deadline=juce::Time::getMillisecondCounter()+20000;
        while(!res&&juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);res=pv.take();}
        check(res&&res->audio,"Preview timeout");return *res;};
    const auto plain=renderPreview(request(1,1));
    const float pa=plain.processedPeaks[2],pd=plain.processedPeaks[3];
    std::cout<<"preview peaks attack "<<pa<<" decay "<<pd<<'\n';
    check(pa>1e-3f&&pd>1e-3f&&pa>2*pd,"Attack / decay peak analysis");
    check(std::abs(plain.processedPeaks[0]-(std::max)(pa,pd))<1e-6f,"Punch peak must be the larger part when untrimmed");
    const float target=gainOf(-1);
    const auto normal=renderPreview(request(target/pa,target/pd));
    check(normal.processedPeaks[2]==pa&&normal.processedPeaks[3]==pd,"Attack / decay analysis must be measured before the trims (idempotent)");
    check(std::abs(db(normal.processedPeaks[0])+1)<.05f,"Normalized Punch peak is not -1 dBFS");
    check(normal.processedPeaks[1]==plain.processedPeaks[1],"Attack / decay trims leaked into the Tail bus");
    const double end=plain.kick.punchEndMs*.001;check(end>.39&&end<.41,"Preview punch timing");
    const auto& a=*plain.audio;const auto& b=*normal.audio;check(a.left.size()==b.left.size(),"Preview length");
    float attackPeak=0,decayPeak=0;
    for(size_t f=0;f<a.left.size();++f){const double t=f/kRate;const float g=tt::PunchSplitTrim::gain(target/pa,target/pd,float(tt::PunchSplitTrim::decayWeight(t,end,kSplit,kTransition)));
        check(std::abs(b.left[f]-a.left[f]*g)<=1e-6f+2e-5f*std::abs(a.left[f]*g),"Preview attack / decay trim is not exactly its part");
        (t<.199?attackPeak:decayPeak)=(std::max)(t<.199?attackPeak:decayPeak,(std::max)(std::abs(b.left[f]),std::abs(b.right[f])));}
    check(std::abs(db(attackPeak)+1)<.05f&&std::abs(db(decayPeak)+1)<.05f,"Attack and decay must each peak at -1 dBFS after normalizing");
}
};
int main()try{juce::ScopedJuceInitialiser_GUI init;
    TailtoneSplitNormalizeTest::portable();TailtoneSplitNormalizeTest::parameters();
    TailtoneSplitNormalizeTest::trims<LegacyTestProcessor>(true);TailtoneSplitNormalizeTest::trims<LegacyTestProcessor>(false);
    TailtoneSplitNormalizeTest::trims<PunchTailProcessor>(true,true);   // linear-phase Tail wet: the trim follows the reported latency
    TailtoneSplitNormalizeTest::liveRamp();TailtoneSplitNormalizeTest::state();TailtoneSplitNormalizeTest::preview();
    std::cout<<"PASS attack / decay normalize: appended params (fresh/legacy 0 dB), exact part trims on the processed Punch with "
               "the whole-Punch trim on top, latency aligned, 10 ms ramp, state, preview + peak analysis\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
