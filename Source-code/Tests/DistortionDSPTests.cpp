#include "LegacyTestProcessor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void set(PunchTailProcessor& p,const char* id,float val){auto* par=p.params.getParameter(id);par->setValueNotifyingHost(par->convertTo0to1(val));}
struct PunchTailDistortionTest {
    static std::shared_ptr<PunchTailProcessor::Asset> make(float magnitude,double time) {
        auto asset=std::make_shared<PunchTailProcessor::Asset>();asset->sample.rate=48000;
        asset->sample.left.assign(size_t(time*48000),magnitude);asset->sample.right=asset->sample.left;
        return asset;
    }
    static float render(bool punchOn,bool tailOn,int type,bool withPunch,bool withTail,bool withReverb,float punchNorm=0,float tailNorm=0,bool generated=false,float generatedMix=0) {
        LegacyTestProcessor p;
        p.signalThreadShouldExit();p.notify();p.stopThread(-1);
        if(withPunch)p.assets[0]=make(0.32f,.35);
        if(withTail){ auto t=make(0.40f,.35);p.assets[1]=t;
            p.bank=std::make_shared<PunchTailProcessor::Bank>();
            for(auto& n:p.bank->notes)n=t->sample;
        }
        if(withReverb)p.assets[2]=make(0.20f,.35);
        set(p,"muteReverb",0);set(p,"output",0);set(p,"crossfade",0);
        set(p,"punchDistOn",punchOn?1:0);set(p,"tailDistOn",tailOn?1:0);
        set(p,"punchDistMode",float(type));set(p,"tailDistMode",float(type));
        set(p,"punchDistDrive",24);set(p,"tailDistDrive",24);
        set(p,"tailVerbOn",generated?1:0);set(p,"tailVerbMix",generatedMix);
        set(p,"punchNormalize",punchNorm);set(p,"tailNormalize",tailNorm);
        p.prepareToPlay(48000,128);
        juce::AudioBuffer<float> buf(2,128);juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1,48,1.0f),0);
        float result=0;
        for(int j=0;j<80;++j){p.processBlock(buf,midi);if(j>12&&j<25)result+=std::abs(buf.getSample(0,64));}
        return result;
    }
};
int main()try{
    juce::ScopedJuceInitialiser_GUI init;
    for(int m=0;m<3;++m){
        const auto cleanPunch=PunchTailDistortionTest::render(false,false,m,true,false,false);
        const auto tailOnly=PunchTailDistortionTest::render(false,true,m,true,false,false);
        check(std::abs(cleanPunch-tailOnly)<1.0f,"Tail distortion altered punch-only samples");
        const auto cleanTail=PunchTailDistortionTest::render(false,false,m,false,true,false);
        const auto punchOnly=PunchTailDistortionTest::render(true,false,m,false,true,false);
        check(std::abs(cleanTail-punchOnly)<1.0f,"Punch distortion altered tail-only samples");
        const auto normalizedPunch=PunchTailDistortionTest::render(true,false,m,true,false,false,-6,0);
        const auto processedPunch=PunchTailDistortionTest::render(true,false,m,true,false,false);
        check(std::abs(normalizedPunch/processedPunch-std::pow(10.,-6./20.))<.005,"Punch normalization must apply after distortion");
        const auto isolatedPunch=PunchTailDistortionTest::render(false,false,m,true,false,false,0,-12);
        check(std::abs(isolatedPunch-cleanPunch)<1.e-5,"Tail normalization changed punch");
        const auto generatedPunch=PunchTailDistortionTest::render(true,false,m,true,false,false,0,0,true,80);
        check(generatedPunch==processedPunch,"Generated reverb changed punch-only audio");
        const auto zeroMixTail=PunchTailDistortionTest::render(false,true,m,false,true,false,0,0,true,0);
        const auto originalTail=PunchTailDistortionTest::render(false,true,m,false,true,false);
        check(zeroMixTail==originalTail,"Zero Mix did not preserve processed tail identity");
        const auto normalizedTail=PunchTailDistortionTest::render(false,true,m,false,true,false,0,-6);
        const auto processedTail=PunchTailDistortionTest::render(false,true,m,false,true,false);
        check(std::abs(normalizedTail/processedTail-std::pow(10.,-6./20.))<.005,"Tail normalization must apply after distortion");
        const auto isolatedTail=PunchTailDistortionTest::render(false,false,m,false,true,false,-12,0);
        check(std::abs(isolatedTail-cleanTail)<1.e-5,"Punch normalization changed tail");
        const auto driven=PunchTailDistortionTest::render(false,true,m,false,true,true);
        check(driven>0.01f,"Tail + reverb distortion unexpectedly silent");
    }
    std::cout<<"PASS three distortion modes, independent routing, 4x DSP without crash\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
