// v0.4.47: a tail with no detectable note must still be pitchable from the
// manually set TAIL ROOT, instead of falling back to the unpitched dry tail.
#include "../Source/PluginProcessor.h"
#include "V4Render.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}

static pt::Sample noiseTail(double rate,size_t frames) {
    pt::Sample s;s.rate=rate;s.left.resize(frames);s.right.resize(frames);
    uint32_t seed=0x1234567u;
    auto next=[&]{seed=seed*1664525u+1013904223u;return float(double(seed>>8)/double(1u<<24)*2.0-1.0);};
    // Band-limited (rumble-like) noise: aperiodic, but with room above it for
    // an octave-up shift to measurably raise the spectrum.
    double l1=0,l2=0,r1=0,r2=0;const double k=1.0-std::exp(-2.0*pt::pi*900.0/rate);
    for(size_t i=0;i<frames;++i){
        const float fade=float(1.0-0.6*double(i)/double(frames));
        l1+=k*(next()-l1);l2+=k*(l1-l2);r1+=k*(next()-r1);r2+=k*(r1-r2);
        s.left[i]=float(1.2*fade*l2);s.right[i]=float(1.2*fade*r2);
    }
    return s;
}
static double zeroCrossingRate(const std::vector<float>& x) {
    size_t z=0;for(size_t i=1;i<x.size();++i)if((x[i-1]<0)!=(x[i]<0))++z;
    return double(z)/double(x.size());
}
static bool finiteAndAudible(const pt::Sample& s) {
    float peak=0;for(size_t i=0;i<s.left.size();++i){
        if(!std::isfinite(s.left[i])||!std::isfinite(s.right[i]))return false;
        peak=(std::max)({peak,std::abs(s.left[i]),std::abs(s.right[i])});}
    return peak>1.e-3f;
}

struct TailtoneManualRootTest {
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static std::shared_ptr<const PunchTailProcessor::Bank> waitForBank(PunchTailProcessor& p,double root,int quality) {
    std::shared_ptr<const PunchTailProcessor::Bank> bank;
    for(int i=0;i<4800;++i){bank=p.previewBank();
        if(bank&&bank->prepared&&std::abs(bank->root-root)<1e-4&&bank->quality==quality&&!p.rebuilding)return bank;
        juce::Thread::sleep(25);}
    return bank;
 }
 static void portable() {
    const double rate=48000,rootHz=440.0*std::pow(2.0,(36-69.0)/12.0);
    const auto noise=noiseTail(rate,24000);
    // Old behaviour: detection fails and plain analysis refuses this material.
    check(!pt::detect(noise).valid,"Fixture unexpectedly has a detectable note");
    bool used=false;
    const auto a=pt::cycle::analyzeWithManualRoot(noise,rootHz,&used);
    check(used,"Manual root fallback path was not exercised");
    check(std::abs(a.fundamentalHz-rootHz)<1e-9,"Manual root was not used as the fundamental");
    check(pt::cycle::plausibleMarks(a.marks,noise.left.size(),rate/rootHz),"Manual-root cycle map is not usable");
    const double baseZcr=zeroCrossingRate(noise.left);
    for(double stretch:{1.0,1.3}) {   // v0.4.61: Highest Fidelity v4 (only engine)
        const auto up=v4::render(noise,12.0,a,stretch);
        const auto down=v4::render(noise,-12.0,a,stretch);
        const size_t frames=size_t(std::llround(noise.left.size()*stretch));
        check(up.left.size()==frames&&down.left.size()==frames,"Manual-root render changed duration");
        check(finiteAndAudible(up)&&finiteAndAudible(down),"Manual-root render silent or invalid");
        check(pt::usablePitchResult(noise,up,stretch)&&pt::usablePitchResult(noise,down,stretch),"Bank policy rejects manual-root render");
        check(up.left!=noise.left&&down.left!=noise.left,"Manual-root note is unpitched");
        check(zeroCrossingRate(up.left)>baseZcr*1.15,"Octave up did not raise the spectrum");
        check(zeroCrossingRate(down.left)<baseZcr*.85,"Octave down did not lower the spectrum");
    }
    const auto fifthUp=v4::render(noise,5.0,a);
    check(fifthUp.left.size()==noise.left.size()&&finiteAndAudible(fifthUp)&&fifthUp.left!=noise.left,
          "Highest Fidelity v4 cannot pitch a manual-root tail");
    // Root note stays bit-identical through the fallback analysis too.
    const auto root=pt::character::render(noise,0.0,pt::character::prepare(noise,a,1.0));
    check(root.left==noise.left&&root.right==noise.right,"Manual-root root note is not the exact source");

    // Detected tails keep the exact original analysis.
    pt::Sample tone;tone.rate=rate;tone.left.resize(24000);tone.right.resize(24000);
    for(size_t i=0;i<tone.left.size();++i){tone.left[i]=float(.5*std::tanh(4*std::sin(2*pt::pi*55*double(i)/rate)));tone.right[i]=tone.left[i];}
    const auto original=pt::cycle::analyze(tone,rootHz);
    const auto viaManual=pt::cycle::analyzeWithManualRoot(tone,rootHz,&used);
    check(!used&&viaManual.marks==original.marks&&viaManual.fundamentalHz==original.fundamentalHz,
          "Detected tails no longer use their own analysis");
 }
 static void integration() {
    // Worker bank: every D#1..D#4 note must be pitched, and changing TAIL ROOT
    // must rebuild the bank around the new manual root.
    for(int quality:{4,3}) {   // v0.4.61: Highest Fidelity v4 and Adaptive Stretch v4
        PunchTailProcessor p;stop(p);
        set(p,"phaseAuto",0);set(p,"root",36);set(p,"stretch",100);
        set(p,"pitchAdaptive",quality==3?1:0);
        check(p.qualityValue()==quality,"Fixture engine selection failed");
        auto asset=std::make_shared<PunchTailProcessor::Asset>();asset->sample=noiseTail(48000,24000);
        asset->pitch=pt::refinedTailPitch(asset->sample);
        check(!asset->pitch.valid,"Integration fixture unexpectedly detected a note");
        {const juce::ScopedLock lock(p.assetLock);p.assets[1]=asset;}
        p.startThread();
        auto bank=waitForBank(p,36,quality);
        check(bank&&bank->prepared,"Bank for undetectable tail never finished");
        check(bank->fallback==0,"Undetectable tail still falls back to dry unpitched notes");
        check(bank->pitched==pt::kPitchNoteCount-1,"Not every playable note was pitched");
        check(bank->notes[41].left!=asset->sample.left&&bank->notes[30].left!=asset->sample.left,"Pitched notes equal the dry tail");
        check(bank->notes[36].left==asset->sample.left,"Manual root note is not the exact source");
        set(p,"root",30);
        auto moved=waitForBank(p,30,quality);
        check(moved&&moved!=bank&&moved->fallback==0&&std::abs(moved->root-30)<1e-4,"Changing manual TAIL ROOT did not rebuild a pitched bank");
        check(moved->notes[30].left==asset->sample.left&&moved->notes[36].left!=asset->sample.left,"Manual root change did not move the unshifted note");
        stop(p);
    }
 }
};

int main() try {
    juce::ScopedJuceInitialiser_GUI init;
    TailtoneManualRootTest::portable();
    TailtoneManualRootTest::integration();
    std::cout<<"PASS undetectable tails pitch from manual TAIL ROOT in Highest Fidelity v4 / Adaptive Stretch v4; detected analysis unchanged\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
