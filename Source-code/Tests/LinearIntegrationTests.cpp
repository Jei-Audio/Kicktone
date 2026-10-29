#include "../Source/PluginProcessor.h"
#include "../Source/AdaptiveStretch.h"
#include "../Source/ProcessedPreview.h"
#include "WaveFixture.h"
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}
struct TailtoneLinearIntegrationTest {
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static void setup(PunchTailProcessor& p,bool linear,bool wet,bool eq){
    stop(p);set(p,"tailVerbLinearPhase",linear?1:0);set(p,"output",0);set(p,"gate",0);set(p,"crossfade",0);set(p,"phaseAuto",0);set(p,"muteReverb",0);
    set(p,"tailVerbOn",wet?1:0);set(p,"tailVerbMix",70);set(p,"tailVerbDuck",0);set(p,"tailVerbSpace",120);set(p,"tailVerbDistance",0);set(p,"tailVerbEqOn",eq?1:0);
    if(eq){set(p,"tailVerbEq1Gain",8);set(p,"tailVerbEq1Freq",180);}
    p.bank=std::make_shared<PunchTailProcessor::Bank>();p.bank->prepared=true;
    auto& sample=p.bank->notes[36];sample.rate=48000;sample.left.resize(24000);sample.right.resize(24000);
    for(size_t i=0;i<sample.left.size();++i){sample.left[i]=float(.25*std::sin(2*pt::pi*65.4*i/48000));sample.right[i]=-.5f*sample.left[i];}
    p.bank->notes[37].rate=48000;p.bank->notes[37].left.assign(24000,0);p.bank->notes[37].right.assign(24000,0);
    p.prepareToPlay(48000,64);
 }
 static std::vector<float> render(PunchTailProcessor& p,int size,const std::vector<std::pair<int,int>>& notes,float* peakOut=nullptr,size_t length=24000){
    std::vector<float> out(length);juce::AudioBuffer<float> b(2,size);juce::MidiBuffer midi;
    for(int pos=0;pos<int(out.size());pos+=size){const int count=(std::min)(size,int(out.size())-pos);b.setSize(2,count,false,false,true);
        for(const auto& n:notes)if(n.first>=pos&&n.first<pos+count)midi.addEvent(juce::MidiMessage::noteOn(1,(n.second)+12,1.f),n.first-pos);
        p.processBlock(b,midi);for(int i=0;i<count;++i){out[size_t(pos+i)]=b.getSample(0,i);if(peakOut)*peakOut=(std::max)({*peakOut,std::abs(b.getSample(0,i)),std::abs(b.getSample(1,i))});}}
    return out;
 }
 static void run(const char* fixture){
    // v0.5.26: every processor is heap-owned (Windows stack rule): a Debug build keeps each
    // local in its own stack slot, and a dozen ~160 KB processors overflowed the 1 MB stack.
    auto freshO=std::make_unique<PunchTailProcessor>();auto& fresh=*freshO;stop(fresh);check(fresh.params.getRawParameterValue("tailVerbLinearPhase")->load()==1,"Fresh Tail wet mode is not linear phase");
    check(fresh.params.getRawParameterValue("tailVerbSubMono")->load()==0,"Fresh mono sub default changed stereo wet");
    check(fresh.params.getRawParameterValue("tailVerbSubMonoHz")->load()==90,"Fresh mono sub frequency is not 90 Hz");
    check(fresh.params.getParameter("tailVerbSubMonoHz")->getParameterIndex()>fresh.params.getParameter("crunchLowCut")->getParameterIndex(),"Mono sub frequency was not appended");
    // v0.4.56: the frequency updates live (worker FIR) without changing latency.
    {auto pO=std::make_unique<PunchTailProcessor>();auto& p=*pO;setup(p,true,true,true);set(p,"tailVerbSubMono",1);p.prepareToPlay(48000,64);
        check(std::abs(p.linearKernel->subMonoHz-90)<.01,"Prepared mono FIR not at 90 Hz");
        auto oldKernel=p.linearKernel;set(p,"tailVerbSubMonoHz",240);p.rebuildLinearEQ();
        check(p.linearKernel!=oldKernel && std::abs(p.linearKernel->subMonoHz-240)<.01 && std::abs(p.tailWetSubMonoHz()-240)<.01,"Mono sub frequency did not publish a new FIR");
        juce::AudioBuffer<float> block(2,128);juce::MidiBuffer none;
        for(int i=0;i<30;++i){p.processBlock(block,none);for(int n=0;n<128;++n)check(std::isfinite(block.getSample(0,n)),"Mono frequency transition not finite");}
        check(p.getLatencySamples()==2175,"Mono sub frequency changed host latency");}
    check(fresh.params.getRawParameterValue("pitchAdaptive")->load()==0,"Adaptive changed existing default engine");
    for(bool sourcePunch:{false,true}){auto offO=std::make_unique<PunchTailProcessor>(),onO=std::make_unique<PunchTailProcessor>();auto& off=*offO;auto& on=*onO;
        for(auto* p:{&off,&on}){setup(*p,true,sourcePunch,false);set(*p,"tailVerbTailSend",0);
            if(sourcePunch){set(*p,"tailVerbPunchSend",1);auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=48000;a->sample.left.resize(4800);a->sample.right.resize(4800);for(size_t i=0;i<4800;++i){a->sample.left[i]=float(.2*std::sin(2*pt::pi*1000*i/48000));a->sample.right[i]=-.4f*a->sample.left[i];}p->assets[0]=a;}
        }
        set(on,"tailVerbSubMono",1);off.prepareToPlay(48000,64);on.prepareToPlay(48000,64);
        auto a=render(off,128,{{0,36}}),b=render(on,128,{{0,36}});check(a==b,"Tail wet mono changed dry or independent Punch wet audio");
    }
    for(bool wet:{false,true}){auto legacyO=std::make_unique<PunchTailProcessor>(),linearO=std::make_unique<PunchTailProcessor>();auto& legacy=*legacyO;auto& linear=*linearO;setup(legacy,false,wet,false);setup(linear,true,wet,false);
        const int delay=linear.getLatencySamples();check(delay==2175&&legacy.getLatencySamples()==0,"Host latency mismatch");
        const std::vector<std::pair<int,int>> notes{{0,36},{17,36},{127,36},{530,36},{700,36},{4500,37}};
        const auto a=render(legacy,127,notes),b=render(linear,1024,notes);
        for(int i=0;i<delay;++i)check(b[size_t(i)]==0,"New note leaked before compensated boundary");
        double error=0;for(size_t i=size_t(delay);i<b.size();++i)error=(std::max)(error,std::abs(double(b[i]-a[i-size_t(delay)])));
        check(error<3.e-5,"Flat linear mode shifted dry/wet relative alignment or altered rapid retrigger audio");
    }
    for(int offset:{0,17,127,530}){auto pO=std::make_unique<PunchTailProcessor>();auto& p=*pO;setup(p,true,true,true);set(p,"muteTail",1);p.prepareToPlay(48000,64);
        const int boundary=8000+offset;const auto y=render(p,1024,{{0,36},{boundary,37}});float peak=0;for(int i=3000;i<boundary;++i)peak=(std::max)(peak,std::abs(y[size_t(i)]));check(peak>1.e-4,"Non-flat linear wet fixture silent");
        for(size_t i=size_t(boundary+p.getLatencySamples());i<y.size();++i)check(std::abs(y[i])<1.e-8,"Linear wet ringing crossed the next compensated note boundary");
    }
    // v0.5.1 LIVE MODE SWITCH (user request): LINEAR PHASE and MONO SUB change while audio
    // runs: 5 ms fade out, swap at a block start, 5 ms fade in, latency reported again.
    // After the FIR history has refilled the output equals a processor started in that mode.
    {auto aO=std::make_unique<PunchTailProcessor>(),bO=std::make_unique<PunchTailProcessor>(),cO=std::make_unique<PunchTailProcessor>();
     auto& a=*aO;auto& b=*bO;auto& c=*cO;setup(a,false,true,true);setup(b,true,true,true);setup(c,false,true,true);
     check(a.getLatencySamples()==0&&b.getLatencySamples()==2175&&!a.tailWetUsesLinearPhase(),"Live switch fixture");
     const int total=96000,size=256,on=16128,off=56064;std::array<std::vector<float>,3> out;for(auto& o:out)o.assign(size_t(total),0.f);
     std::array<PunchTailProcessor*,3> ps{&a,&b,&c};juce::AudioBuffer<float> buf(2,size);juce::MidiBuffer midi;
     for(int pos=0;pos<total;pos+=size){
         if(pos==on){set(a,"tailVerbLinearPhase",1);a.rebuildLinearEQ();}
         if(pos==off){set(a,"tailVerbLinearPhase",0);a.rebuildLinearEQ();}
         for(size_t k=0;k<3;++k){midi.clear();if(pos%16000<size)midi.addEvent(juce::MidiMessage::noteOn(1,36+12,1.f),pos%16000);
             ps[k]->processBlock(buf,midi);for(int i=0;i<size;++i)out[k][size_t(pos+i)]=buf.getSample(0,i);}
         if(pos==on+4096){check(a.tailWetUsesLinearPhase()&&a.linearActive.load()&&a.wetSwapPhase==0&&a.wetSwapGain==1.f,"LINEAR PHASE on did not switch the running Tail wet mode");
             a.refreshReportedLatency();check(a.getLatencySamples()==2175,"Latency not reported after LINEAR PHASE on");}
         if(pos==off+4096){check(!a.tailWetUsesLinearPhase()&&!a.linearActive.load()&&a.wetSwapPhase==0,"LINEAR PHASE off did not switch the running Tail wet mode");
             a.refreshReportedLatency();check(a.getLatencySamples()==0,"Latency not dropped after LINEAR PHASE off");}}
     int muted=0;for(int i=on;i<on+1024;++i)if(out[0][size_t(i)]==0.f)++muted;check(muted>0,"Mode swap was not done while muted");
     double fade=0;for(int i=on+1;i<on+1024;++i)fade=(std::max)(fade,double(std::abs(out[0][size_t(i)]-out[0][size_t(i-1)])));
     double ref=0;for(int i=on+1;i<on+1024;++i)ref=(std::max)(ref,double(std::abs(out[1][size_t(i)]-out[1][size_t(i-1)]))+double(std::abs(out[2][size_t(i)]-out[2][size_t(i-1)])));
     check(fade<=ref+1e-6,"Mode swap clicked");
     double eOn=0,eOff=0;for(int i=on+8192;i<off;++i)eOn=(std::max)(eOn,double(std::abs(out[0][size_t(i)]-out[1][size_t(i)])));
     for(int i=off+16384;i<total;++i)eOff=(std::max)(eOff,double(std::abs(out[0][size_t(i)]-out[2][size_t(i)])));
     check(eOn<1e-5,"After LINEAR PHASE on, output differs from a processor started in linear phase");
     check(eOff<1e-5,"After LINEAR PHASE off, output differs from a processor started in minimum phase");
     // MONO SUB from zero-latency mode switches the FIR path in live too.
     set(c,"tailVerbSubMono",1);for(int k=0;k<20;++k){midi.clear();c.processBlock(buf,midi);}
     c.refreshReportedLatency();check(c.tailWetUsesSubMono()&&c.linearActive.load()&&!c.tailWetUsesLinearPhase()&&c.getLatencySamples()==2175,"MONO SUB did not switch on live");}
    // Mono also supports legacy EQ: its own FIR is delayed/reported while
    // the old EQ remains minimum phase before the stereo matrix.
    {auto pO=std::make_unique<PunchTailProcessor>();auto& p=*pO;setup(p,false,true,true);set(p,"tailVerbSubMono",1);p.prepareToPlay(48000,64);
        check(!p.tailWetUsesLinearPhase()&&p.tailWetUsesSubMono()&&p.getLatencySamples()==2175,"Legacy EQ mono mode/PDC not active");
        set(p,"muteTail",1);p.prepareToPlay(48000,64);const auto y=render(p,1024,{{0,36},{9000,37}});
        for(size_t i=size_t(9000+p.getLatencySamples());i<y.size();++i)check(std::abs(y[i])<1.e-8,"Legacy EQ mono history crossed compensated note cutoff");
    }
    // Preview must show the latency-compensated FIR output used in playback.
    // v0.4.59: checked for both reverb engines. Playback renders 1 s so the
    // whole 0.5 s Tail plus its wet ring-out is compared (V2 peaks at Tail end).
    for(int engine:{0,1})for(bool linearMode:{true,false}){auto pO=std::make_unique<PunchTailProcessor>();auto& p=*pO;setup(p,linearMode,true,true);set(p,"tailVerbEngine",float(engine));set(p,"tailVerbSubMono",1);set(p,"muteTail",1);p.prepareToPlay(48000,128);
        PreviewRequest request;request.rate=48000;request.root=36;request.ready=true;
        request.tail=std::make_shared<pt::Sample>(p.bank->notes[36]);request.settings=p.settings();
        request.generated=p.tailReverbSettings();request.punchGenerated=p.punchReverbSettings();
        request.generatedBands=p.eqSettings(3);request.generatedEQ=true;request.linearPhase=linearMode;request.subMono=true;
        request.punchGeneratedBands=p.eqSettings(4);request.layerMuted={false,true,false};
        ProcessedPreview preview;preview.submit(request);std::optional<PreviewResult> result;
        for(int i=0;i<1200;++i){result=preview.take();if(result)break;juce::Thread::sleep(10);}
        check(bool(result),"Linear preview did not finish");float peak=0;render(p,128,{{0,36}},&peak,48000);
        check(peak>1.e-4&&std::abs(result->processedPeaks[1]-peak)<.002,"Linear wet preview/normalization differs from playback");
        auto oldKernel=p.linearKernel;set(p,"tailVerbEq1Gain",-7);p.rebuildLinearEQ();
        check(p.linearKernel!=oldKernel,"Worker EQ edit failed to publish a new FIR");
        juce::AudioBuffer<float> block(2,128);juce::MidiBuffer none;
        for(int i=0;i<30;++i){p.processBlock(block,none);for(int n=0;n<128;++n)check(std::isfinite(block.getSample(0,n)),"Live FIR transition not finite");}
        check(p.getLatencySamples()==2175,"EQ band edit changed host latency");
    }
    set(fresh,"pitchAdaptive",1);set(fresh,"adaptiveAmount",73);juce::MemoryBlock state;fresh.getStateInformation(state);auto restoredO=std::make_unique<PunchTailProcessor>();auto& restored=*restoredO;stop(restored);restored.setStateInformation(state.getData(),int(state.getSize()));
    check(restored.qualityValue()==3&&restored.params.getRawParameterValue("adaptiveAmount")->load()==73&&restored.params.getRawParameterValue("tailVerbLinearPhase")->load()==1,"New controls did not restore");
    set(fresh,"tailVerbSubMono",1);set(fresh,"tailVerbSubMonoHz",275);set(fresh,"adaptiveAmount",400);fresh.getStateInformation(state);restored.setStateInformation(state.getData(),int(state.getSize()));
    check(restored.params.getRawParameterValue("adaptiveAmount")->load()==400,"Stronger Amount was clamped or lost in saved state");
    check(restored.params.getRawParameterValue("tailVerbSubMono")->load()==1,"Mono sub state was lost");
    check(restored.params.getRawParameterValue("tailVerbSubMonoHz")->load()==275,"Mono sub frequency state was lost");
    auto old=fresh.params.copyState();for(int i=old.getNumChildren()-1;i>=0;--i){auto id=old.getChild(i).getProperty("id").toString();if(id=="pitchCharacter"||id=="pitchAdaptive"||id=="adaptiveAmount"||id=="tailVerbLinearPhase"||id=="tailVerbSubMono"||id=="tailVerbSubMonoHz")old.removeChild(i,nullptr);}
    juce::MemoryBlock oldState;{juce::MemoryOutputStream stream(oldState,false);old.writeToStream(stream);}restored.setStateInformation(oldState.getData(),int(oldState.getSize()));
    check(restored.qualityValue()==4&&restored.params.getRawParameterValue("adaptiveAmount")->load()==50&&restored.params.getRawParameterValue("tailVerbLinearPhase")->load()==0,"Old (Extreme) state did not open on Highest Fidelity v4 or changed wet EQ phase");
    check(restored.params.getRawParameterValue("tailVerbSubMono")->load()==0,"Legacy state unexpectedly enabled mono sub");
    check(restored.params.getRawParameterValue("tailVerbSubMonoHz")->load()==90,"Legacy state did not restore 90 Hz mono sub");
    // Prepare an actual adaptive bank on the worker, then verify trim and voice pinning.
    auto adaptiveO=std::make_unique<PunchTailProcessor>();auto& adaptive=*adaptiveO;stop(adaptive);set(adaptive,"pitchAdaptive",1);set(adaptive,"adaptiveAmount",100);set(adaptive,"root",36);set(adaptive,"trim",100);set(adaptive,"phaseAuto",0);
    auto source=std::make_shared<PunchTailProcessor::Asset>();source->sample=loadFloatWave(fixture);
    {const juce::ScopedLock lock(adaptive.assetLock);adaptive.assets[1]=source;}
    adaptive.startThread();
    std::shared_ptr<const PunchTailProcessor::Bank> bank;
    for(int i=0;i<2400;++i){bank=adaptive.previewBank();if(bank&&bank->prepared&&bank->quality==3&&bank->adaptiveAmount==100)break;juce::Thread::sleep(25);}
    check(bank&&bank->prepared&&bank->fallback==0&&bank->quality==3,"Adaptive worker bank failed");
    check(std::abs(bank->stretchForNote(48)-.5)<1./source->sample.rate,"Worker did not shorten upward octave");check(std::abs(bank->stretchForNote(24)-std::sqrt(.5))<1./source->sample.rate,"Worker did not shorten downward octave gently");
    adaptive.prepareToPlay(48000,128);juce::AudioBuffer<float> b(2,128);juce::MidiBuffer events;events.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);adaptive.processBlock(b,events);
    check(std::abs(adaptive.voices[0].settings.trimMs-50)<.01,"Adaptive trim did not track prepared duration");
    set(adaptive,"adaptiveAmount",0);
    std::shared_ptr<const PunchTailProcessor::Bank> replacement;
    for(int i=0;i<2400;++i){replacement=adaptive.previewBank();if(replacement&&replacement!=bank&&replacement->prepared&&replacement->adaptiveAmount==0)break;juce::Thread::sleep(25);}
    check(replacement&&replacement!=bank&&replacement->adaptiveAmount==0&&replacement->fallback==0,"Amount automation failed to rebuild bank");
    adaptive.processBlock(b,events);check(adaptive.voices[0].pitches==bank,"Amount automation changed a held voice");
    events.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);adaptive.processBlock(b,events);check(adaptive.voices[1].pitches==replacement,"New note failed to use updated amount");
    set(adaptive,"adaptiveAmount",400);
    std::shared_ptr<const PunchTailProcessor::Bank> strongest;
    for(int i=0;i<2400;++i){strongest=adaptive.previewBank();if(strongest&&strongest!=replacement&&strongest->prepared&&strongest->adaptiveAmount==400)break;juce::Thread::sleep(25);}
    check(strongest&&strongest!=replacement&&strongest->adaptiveAmount==400&&strongest->fallback==0,"400% worker bank failed or amount was clamped");
    check(std::abs(strongest->stretchForNote(39)-.5)<1./source->sample.rate,"400% worker failed to shorten nearby higher bass note");
    check(std::abs(strongest->stretchForNote(33)-std::sqrt(.5))<1./source->sample.rate,"400% worker failed to shorten nearby lower bass note gently");
    adaptive.processBlock(b,events);check(!adaptive.voices[0].active&&adaptive.voices[1].pitches==replacement,"400% update changed held voice banks");
    events.addEvent(juce::MidiMessage::noteOn(1,51,1.f),0);adaptive.processBlock(b,events);
    check(adaptive.voices[2].pitches==strongest&&std::abs(adaptive.voices[2].settings.trimMs-50)<.01,"400% note failed to use shorter prepared audio and matching trim");
    std::cout<<"PASS default/state migration, host PDC, dry/wet alignment, rapid/block/chunk cutoff, adaptive worker amount, trim and held voice ownership\n";
 }
};
int main(int argc,char** argv)try{juce::ScopedJuceInitialiser_GUI init;check(argc>1,"Missing supplied kick");TailtoneLinearIntegrationTest::run(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
