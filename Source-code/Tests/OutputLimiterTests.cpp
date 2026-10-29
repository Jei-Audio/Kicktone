// v0.4.60 optional output limiter: DSP, processor integration, state and preview.
#include "LegacyTestProcessor.h"
#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* param=p.params.getParameter(id);param->setValueNotifyingHost(param->convertTo0to1(v));}
static float value(PunchTailProcessor& p,const char* id){return p.params.getRawParameterValue(id)->load();}

static void dsp() {
    constexpr float ceiling=tt::OutputLimiter::kCeiling;
    check(std::abs(juce::Decibels::gainToDecibels(ceiling)+0.3f)<.001f,"Ceiling is not -0.3 dBFS");
    tt::OutputLimiter lim;lim.prepare(48000);
    // Below the ceiling: bit-identical, gain exactly 1, for any sign/stereo mix.
    for(int i=0;i<48000;++i){const float a=.96f*std::sin(.013f*i),b=-.9f*std::cos(.021f*i);float l=a,r=b;
        check(lim.process(l,r,true)==1.f&&l==a&&r==b,"Limiter altered audio below the ceiling");}
    // Loud, stereo-linked: never above the ceiling, the same gain on both channels.
    float maxOut=0;int limited=0;
    for(int i=0;i<48000;++i){const float a=2.5f*std::sin(2*pt::pi*55*i/48000.),b=.3f*a;float l=a,r=b;
        const float g=lim.process(l,r,true);maxOut=(std::max)({maxOut,std::abs(l),std::abs(r)});
        if(g<1.f)++limited;
        check(std::abs(l-a*g)<1e-6f&&std::abs(r-b*g)<1e-6f,"Limiter is not stereo-linked");
        check(std::isfinite(l)&&std::isfinite(r),"Limiter produced a non-finite value");}
    check(maxOut<=ceiling+1e-6f&&maxOut>ceiling-.01f,"Limiter exceeded or badly undershot the ceiling");
    check(limited>40000,"Loud input was not limited");
    // Release: recovers to exactly 1 after the loud part (80 ms time constant).
    float l=0,r=0;for(int i=0;i<48000;++i){l=r=.1f;lim.process(l,r,true);}
    check(lim.currentGain()==1.f&&l==.1f,"Limiter did not fully recover after release");
    // A single hot sample: gain drops at once, is held 30 ms, then recovers smoothly.
    l=4.f;r=0;lim.process(l,r,true);check(std::abs(l-ceiling)<1e-6f,"Instant attack missed a single peak");
    const float attacked=lim.currentGain();
    float prev=attacked;for(int i=0;i<9600;++i){l=r=.1f;lim.process(l,r,true);
        if(i<1430)check(lim.currentGain()==attacked,"Gain was not held after the peak");
        check(lim.currentGain()>=prev&&lim.currentGain()-prev<.01f,"Release is not smooth");prev=lim.currentGain();}
    check(prev>attacked+.1f,"Gain did not release after the hold");
    // Steady loud bass: after the first cycle the gain is constant (scaled, not flattened).
    {tt::OutputLimiter s;s.prepare(48000);float gMin=1,gMax=0;
     for(int i=0;i<48000;++i){float a=1.8f*float(std::sin(2*pt::pi*40*i/48000.)),b=a;const float g=s.process(a,b,true);
        if(i>2400){gMin=(std::min)(gMin,g);gMax=(std::max)(gMax,g);}}
     check(gMax-gMin<1e-6f,"Limiter pumps on a steady loud bass tone");}
    // Switching OFF while limiting glides back instead of jumping; OFF never limits.
    l=4.f;r=0;lim.process(l,r,true);const float held=lim.currentGain();
    l=r=.5f;lim.process(l,r,false);check(lim.currentGain()>held&&lim.currentGain()<held+.01f,"Turning the limiter off jumped the gain");
    for(int i=0;i<96000;++i){l=r=.5f;lim.process(l,r,false);}
    l=3.f;r=-3.f;check(lim.process(l,r,false)==1.f&&l==3.f&&r==-3.f,"Bypassed limiter still reduced gain");
    // Sample-rate independent release time.
    for(double sr:{44100.,96000.}){tt::OutputLimiter x;x.prepare(sr);float a=2.f,b=0;x.process(a,b,true);
        int n=0;while(x.currentGain()<.99f&&n<int(sr)){a=b=0;x.process(a,b,true);++n;}
        check(std::abs(n/sr-(.030+.080*std::log((1-tt::OutputLimiter::kCeiling/2)/.01)))<.002,"Hold/release time depends on the sample rate");}
}

struct TailtoneOutputLimiterTest {
static std::unique_ptr<LegacyTestProcessor> fixture(float amplitude) {
    auto p=std::make_unique<LegacyTestProcessor>();p->signalThreadShouldExit();p->notify();p->stopThread(-1);
    set(*p,"gate",0);set(*p,"output",0);set(*p,"eqOn",0);set(*p,"punchEqOn",0);set(*p,"reverbEqOn",0);set(*p,"phaseAuto",0);
    auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=48000;
    for(int i=0;i<24000;++i){const float v=amplitude*float(std::sin(2*pt::pi*60*i/48000.));a->sample.left.push_back(v);a->sample.right.push_back(-v);}
    p->assets[0]=a;return p;
}
static std::vector<float> render(PunchTailProcessor& p,float& peak) {
    p.prepareToPlay(48000,256);juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;std::vector<float> out;peak=0;
    midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
    for(int b=0;b<60;++b){p.processBlock(audio,midi);
        for(int i=0;i<256;++i)for(int ch=0;ch<2;++ch){out.push_back(audio.getSample(ch,i));peak=(std::max)(peak,std::abs(audio.getSample(ch,i)));}}
    return out;
}

static void processor() {
    {auto fresh=std::make_unique<PunchTailProcessor>();check(value(*fresh,"outputLimiter")==1,"Output limiter must be ticked for new instances");
     check(fresh->params.getParameter("outputLimiter")->getParameterIndex()>fresh->params.getParameter("tailVerbEngine")->getParameterIndex()&&
           fresh->params.getParameter("outputLimiter")->getParameterIndex()==fresh->params.getParameter("attackLayerOn")->getParameterIndex()-1,"outputLimiter was not appended after every earlier parameter");} // v0.4.62 attack layer follows it
    // Quiet kick: ON and OFF are bit-identical; no latency is added.
    {auto on=fixture(.5f),off=fixture(.5f);set(*off,"outputLimiter",0);float pa=0,pb=0;
     const auto a=render(*on,pa),b=render(*off,pb);
     check(pa>.45f&&a==b,"Limiter changed a kick that stays below the ceiling");
     check(on->getLatencySamples()==off->getLatencySamples(),"Limiter changed host latency");}
    // Loud kick (+6 dB OUTPUT on a 0.9 sine = 1.8 peak).
    {auto on=fixture(.9f),off=fixture(.9f);set(*on,"output",6);set(*off,"output",6);set(*off,"outputLimiter",0);float pa=0,pb=0;
     const auto a=render(*on,pa),b=render(*off,pb);
     check(pa<=tt::OutputLimiter::kCeiling+1e-6f&&pa>tt::OutputLimiter::kCeiling-.01f,"Limiter ON exceeded the ceiling");
     check(!on->clipLatched.load(),"CLIP lit although the limiter held the output under 0 dBFS");
     check(on->limiterGain.load()<.6f,"Limiter gain reduction was not published to the editor");
     check(pb==1.f&&off->clipLatched.load(),"Limiter OFF must keep the old 0 dBFS hard clip");
     // Limiting, not clipping: the waveform keeps its sine shape (no flat tops).
     // (The first rising quarter cycle is caught instantly; skip the first 20 ms.)
     int flat=0;for(size_t i=2*960;i<a.size();i+=2)if(std::abs(a[i])>.95f&&a[i]==a[i-2])++flat;
     int clipped=0;for(size_t i=2;i<b.size();i+=2)if(std::abs(b[i])==1.f&&b[i]==b[i-2])++clipped;
     check(flat<20&&clipped>200,"Limiter output still looks hard-clipped");}
    // State: saved value restores; projects saved before v0.4.60 load with the limiter OFF.
    {auto p=fixture(.5f);set(*p,"outputLimiter",0);juce::MemoryBlock saved;p->getStateInformation(saved);
     auto q=fixture(.5f);check(value(*q,"outputLimiter")==1,"Fixture default");q->setStateInformation(saved.getData(),int(saved.getSize()));
     check(value(*q,"outputLimiter")==0,"Limiter OFF did not restore");
     set(*p,"outputLimiter",1);p->getStateInformation(saved);q->setStateInformation(saved.getData(),int(saved.getSize()));
     check(value(*q,"outputLimiter")==1,"Limiter ON did not restore");
     auto legacy=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
     for(int i=legacy.getNumChildren()-1;i>=0;--i)if(legacy.getChild(i).getProperty("id").toString()=="outputLimiter")legacy.removeChild(i,nullptr);
     juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
     q->setStateInformation(old.getData(),int(old.getSize()));
     check(value(*q,"outputLimiter")==0,"Pre-v0.4.60 project must keep its former hard clip (limiter OFF)");}
    // Processed waveform preview mirrors the switch.
    for(bool on:{true,false}) {
        auto p=fixture(.9f);PreviewRequest request;request.samples[0]=std::make_shared<pt::Sample>(p->assets[0]->sample);request.rate=48000;
        request.settings.output=juce::Decibels::decibelsToGain(6.f);request.limiter=on;
        ProcessedPreview preview;preview.submit(request);std::optional<PreviewResult> result;auto deadline=juce::Time::getMillisecondCounter()+10000;
        while(!result&&juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);result=preview.take();}
        check(bool(result),"Preview timeout");
        check(on?result->kick.envelope.peak<=tt::OutputLimiter::kCeiling+1e-6f:result->kick.envelope.peak==1.f,"Final waveform preview ignores the limiter switch");
    }
}
};
int main()try{juce::ScopedJuceInitialiser_GUI init;dsp();TailtoneOutputLimiterTest::processor();
    std::cout<<"PASS output limiter: -0.3 dBFS ceiling, bit-identical below it, stereo link, smooth release/bypass, no latency, fresh ON / legacy OFF, preview\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
