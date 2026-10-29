#pragma once
#include "Distortion.h"
#include "MasterEnvelope.h"
#include "MasterEnvelopeTiming.h"
#include "ParametricEQ.h"
#include "PunchRegionEQ.h"
#include "PunchSplitTrim.h"
#include "LinearPhaseEQ.h"
#include "PunchTransient.h"
#include "TailReverb.h"
#include "TailSubDuck.h"
#include "PunchReverbDuck.h"
#include "OutputLimiter.h"
#include "BassMono.h"
#include <juce_events/juce_events.h>
#include "KickOverview.h"
#include <optional>

// Single, coalescing background renderer. Never called by processBlock.
// A preview is one full-velocity root note with settled effect parameters.
struct PreviewRequest {
    std::array<std::shared_ptr<const pt::Sample>,3> samples;
    std::shared_ptr<const pt::Sample> tail;
    std::shared_ptr<const pt::Sample> generatedTailWet;
    std::shared_ptr<const pt::Sample> crunchLayer; // v0.4.53: root note's crunch layer (settings.crunchMix)
    pt::Settings settings;
    tt::MasterEnvelopeSettings envelope;
    pt::TransientSettings transient;
    tt::PunchRegionSettings punchRegion;
    std::array<tt::Band,tt::kBands> attackBands,decayBands;
    bool attackEQ=true,decayEQ=true;
    pt::TailReverbSettings generated, punchGenerated;
    std::array<tt::Band,tt::kBands> generatedBands;bool generatedEQ=true;bool linearPhase=false;bool subMono=false;double subMonoHz=90;
    std::array<tt::Band,tt::kBands> punchGeneratedBands;bool punchGeneratedEQ=true;
    std::array<pt::DistortionSettings,2> distortion;
    std::array<std::array<tt::Band,tt::kBands>,3> bands;
    std::array<bool,3> eqOn{};
    std::array<bool,2> eqPost{};   // v0.5.20: Punch / Tail EQ after their distortion
    double rate=44100;
    int root=36;
    bool ready=false;
    std::array<bool,3> layerMuted{};
    std::array<float,2> normalizeGains{{1,1}};
    std::array<float,2> splitNormalizeGains{{1,1}};   // v0.5.21 Punch attack / decay trims (split = punchRegion)
    bool limiter=false; // v0.4.60 output limiter, mirrored on the final waveform
    bool bassMono=false; double bassMonoHz=tt::BassMono::kDefaultHz; float bassMonoMix=1.f; bool bassMonoLinear=false;   // v0.5 / v0.5.1
    uint64_t generation=0;
};
// v0.5.28: the two output buses of a finished render, so the Final Kick waveform
// can follow a Punch / Tail volume drag at once (remixPreview on the message
// thread) instead of waiting for a new render. Per frame: Punch after its attack /
// decay trims L/R, Tail + Reverb L/R, master envelope gain - exactly the values
// the output stage multiplies by the normalize gains. monoLow (linear MONO BASS
// only): the zero-phase low side band of each bus (x envelope).
struct PreviewBuses {
    double rate=44100; size_t bins=0;
    std::vector<std::array<float,5>> frames;
    std::vector<std::array<float,2>> monoLow;
    bool limiter=false, bassMono=false; double bassMonoHz=tt::BassMono::kDefaultHz; float bassMonoMix=1.f;
};
// v0.4.62: `audio` is the complete rendered stereo kick (what the Final Kick
// Waveform shows, after the limiter), used by Export WAV and drag-and-drop.
// processedPeaks (before punchNormalize / tailNormalize): 0 Punch after its attack /
// decay trims, 1 Tail + Reverb, v0.5.21: 2 attack and 3 decay part of the untrimmed Punch.
struct PreviewResult { PreviewRequest request; pt::KickOverview kick; std::array<float,4> processedPeaks{}; std::shared_ptr<const pt::Sample> audio; std::shared_ptr<const PreviewBuses> buses; };

// v0.5.28: the render's output stage again for other normalize gains: gains and
// envelope, mono bass, limiter, clamped waveform columns, in render()'s order, so
// the result equals a new render at those gains (linear MONO BASS: up to
// rounding, its low band is the sum of the per-bus bands). A few ms per call.
inline pt::WaveformEnvelope remixPreview(const PreviewBuses& b,std::array<float,2> gains) {
    pt::WaveformEnvelope out;
    const size_t frames=b.frames.size(),bins=b.bins;
    if(frames==0||bins==0)return out;
    out.bins.resize(bins);
    for(auto& bin:out.bins){bin.minimum=std::numeric_limits<float>::max();bin.maximum=std::numeric_limits<float>::lowest();}
    tt::OutputLimiter limiter;limiter.prepare(b.rate);
    tt::BassMono bassMono;bassMono.prepare(b.rate);bassMono.settle(b.bassMono,b.bassMonoMix);
    const bool linearMono=b.monoLow.size()==frames;
    const float mixAmount=std::clamp(b.bassMonoMix,0.f,1.f);
    for(size_t f=0;f<frames;++f) {
        const auto& x=b.frames[f];
        std::array<float,2> s{};
        for(int ch=0;ch<2;++ch)s[size_t(ch)]=(x[size_t(ch)]*gains[0]+x[size_t(2+ch)]*gains[1])*x[4];
        if(linearMono){const double low=double(gains[0])*b.monoLow[f][0]+double(gains[1])*b.monoLow[f][1];s={float(s[0]-mixAmount*low),float(s[1]+mixAmount*low)};}
        else bassMono.process(s[0],s[1],b.bassMono,b.bassMonoHz,b.bassMonoMix);
        limiter.process(s[0],s[1],b.limiter);
        auto& bin=out.bins[(std::min)(bins-1,f*bins/frames)];
        for(float v:s){const float c=std::clamp(v,-1.0f,1.0f);bin.minimum=(std::min)(bin.minimum,c);bin.maximum=(std::max)(bin.maximum,c);}
    }
    for(auto& bin:out.bins){if(bin.minimum>bin.maximum)bin={};out.peak=(std::max)({out.peak,std::abs(bin.minimum),std::abs(bin.maximum)});}
    return out;
}
// Low side band of both buses in one zero-phase FFT pass (Punch real, Tail
// imaginary: the kernel is real, so the two convolutions stay apart).
inline std::vector<std::array<float,2>> previewBusLowBands(const PreviewBuses& b,double hz) {
    const int radius=tt::bassmono::radiusFor(b.rate);const auto taps=tt::bassmono::lowpassTaps(hz,b.rate,radius);
    const size_t frames=b.frames.size();size_t n=1;while(n<frames+taps.size())n*=2;
    std::vector<std::complex<double>> x(n),h(n);
    for(size_t i=0;i<frames;++i){const auto& f=b.frames[i];x[i]={.5*(double(f[0])-double(f[1]))*f[4],.5*(double(f[2])-double(f[3]))*f[4]};}
    for(size_t i=0;i<taps.size();++i)h[i]=taps[i];
    tt::linear::fft(x,false);tt::linear::fft(h,false);for(size_t i=0;i<n;++i)x[i]*=h[i];tt::linear::fft(x,true);
    std::vector<std::array<float,2>> out(frames);
    for(size_t i=0;i<frames;++i){const auto v=x[i+size_t(radius)];out[i]={float(v.real()),float(v.imag())};}
    return out;
}

class ProcessedPreview final : private juce::Thread {
public:
    ProcessedPreview():juce::Thread("Kicktone waveform") { startThread(); }
    ~ProcessedPreview() override { signalThreadShouldExit(); notify(); stopThread(-1); }
    uint64_t submit(PreviewRequest request) {
        const juce::ScopedLock lock(mutex);
        request.generation=++latest; pending=std::move(request); notify(); return latest;
    }
    std::optional<PreviewResult> take() {
        const juce::ScopedLock lock(mutex);
        auto result=std::move(finished); finished.reset(); return result;
    }
private:
    juce::CriticalSection mutex;
    std::optional<PreviewRequest> pending;
    std::optional<PreviewResult> finished;
    std::atomic<uint64_t> latest{0};
    void run() override {
        while(!threadShouldExit()) {
            std::optional<PreviewRequest> job;
            {const juce::ScopedLock lock(mutex);job=std::move(pending);pending.reset();}
            if(!job) {wait(100);continue;}
            PreviewResult result; result.request=*job;
            auto audio=std::make_shared<pt::Sample>();auto buses=std::make_shared<PreviewBuses>();
            if(render(*job,result.kick,result.processedPeaks,audio.get(),buses.get())) {
                if(!audio->left.empty())result.audio=std::move(audio);
                if(!buses->frames.empty())result.buses=std::move(buses);
                const juce::ScopedLock lock(mutex);
                if(job->generation==latest)finished=std::move(result);
            }
        }
    }
    bool render(const PreviewRequest& r,pt::KickOverview& k,std::array<float,4>& peaks,pt::Sample* audio=nullptr,PreviewBuses* buses=nullptr) {
        const auto* punch=r.samples[0].get();
        const auto* tail=r.tail?r.tail.get():r.samples[1].get();
        const auto* reverb=r.samples[2].get();
        if(!punch&&!tail&&!reverb)return true;
        const double rate=std::clamp(r.rate,8000.0,192000.0);
        auto settings=r.settings;settings.root=r.root;settings.gate=false;
        const auto* generatedTailWet=(!r.settings.preparedCompositeTail&&r.generated.tailRootPitch&&r.generated.tailSend)?r.generatedTailWet.get():nullptr;
        pt::Voice voice;voice.start(r.root,1,1,punch,tail,settings,reverb,
            generatedTailWet?generatedTailWet->duration():0.0,
            r.generated.tailOffsetMs,r.settings.preparedCompositeTail,r.settings.compositeDryDuration);
        voice.setReverbFilter(pt::makeReverbHPF(rate,settings.reverbLowCutSlope),rate,true);
        k.punchEndMs=voice.punchEnd*1000;k.tailStartMs=voice.tailStart*1000;
        k.tailEndMs=voice.dryEnd*1000;k.overlapMs=voice.overlap*1000;
        k.totalMs=tt::completeOutputDuration(voice,punch!=nullptr,tail!=nullptr,r.generated,r.punchGenerated,r.layerMuted)*1000;
        if(k.totalMs<=0)return true;
        tt::MasterEnvelope envelope;envelope.start(rate,k.totalMs*.001,r.envelope);
        tt::OutputLimiter limiter;limiter.prepare(rate);
        tt::BassMono bassMono;bassMono.prepare(rate);bassMono.settle(r.bassMono,r.bassMonoMix);   // v0.5
        constexpr int block=256;
        pt::TailReverb generated;generated.prepare(rate);auto tailGeneratedSettings=r.generated;tailGeneratedSettings.punchPreDelayMs=r.generated.tailOffsetMs;generated.setSettings(tailGeneratedSettings);
        pt::TailReverb punchGenerated;punchGenerated.prepare(rate);punchGenerated.setSettings(r.punchGenerated);
        pt::TailSubDuck duck,punchDuck;duck.prepare(rate,r.generated);punchDuck.prepare(rate,r.punchGenerated);
        tt::ParametricEQ wetEQ;wetEQ.update(r.generatedBands,r.generatedEQ,rate);
        tt::ParametricEQ punchWetEQ;punchWetEQ.update(r.punchGeneratedBands,r.punchGeneratedEQ,rate);
        pt::ReverbHPFState punchGeneratedHPF;punchGeneratedHPF.reset(pt::makeReverbHPFAt(rate,12,250.0));
        const float mix=r.generated.enabled?r.generated.mix*.01f:0;
        const float punchMix=r.generated.enabled?r.punchGenerated.mix*.01f:0;
        const float wetLevel=juce::Decibels::decibelsToGain(r.generated.levelDb,-60.f);
        pt::PunchReverbDuck verbDuck;verbDuck.prepare(rate);verbDuck.setSettings(r.generated.punchDuck,r.generated.punchDuckReleaseMs);
        const float punchWetLevel=juce::Decibels::decibelsToGain(r.punchGenerated.levelDb,-60.f);
        std::array<std::array<float,2>,block> wetBuffer{};
        std::array<float,block> splitWeight{};   // v0.5.21: decay share of each sample (0 = attack)
        tt::PunchSplitTrim splitTrim;splitTrim.prepare(rate,r.punchRegion.splitPercent,r.punchRegion.transitionMs);
        tt::ParametricEQ eq[3];
        for(int i=0;i<3;++i)eq[i].update(r.bands[size_t(i)],r.eqOn[size_t(i)],rate);
        tt::PunchRegionDesign regionDesign;regionDesign.update(r.punchRegion,r.attackBands,r.attackEQ,r.decayBands,r.decayEQ,rate);
        tt::PunchRegionEQ region;region.start(regionDesign,rate,voice.punchEnd);
        pt::PunchTransient transient;transient.prepare(rate);transient.setSettings(r.transient);
        pt::StereoDistortion distortion[2];
        juce::AudioBuffer<float> p(2,block),t(2,block);
        const bool distort=r.distortion[0].enabled||r.distortion[1].enabled;
        if(distort)for(auto& d:distortion)d.prepare(rate,block);
        // Let coefficient/gain ramps settle on silence, as before a played note.
        for(int warm=0;warm<int(rate*.15);warm+=block) {
            if(threadShouldExit()||r.generation!=latest)return false;
            p.clear();t.clear();
            for(int i=0;i<block;++i) {float l=0,rr=0;for(auto& e:eq)e.process(l,rr);transient.process(l,rr);wetEQ.process(l,rr);}
            if(distort){distortion[0].process(p,block,r.distortion[0]);distortion[1].process(t,block,r.distortion[1]);}
        }
        const size_t frames=std::max<size_t>(1,size_t(std::ceil(k.totalMs*rate/1000)));
        std::vector<std::array<float,2>> linearWet;
        if((r.linearPhase || r.subMono) && mix>0){
            auto kernel=tt::linear::design(r.generatedBands,r.linearPhase&&r.generatedEQ,rate,r.subMono,r.subMonoHz);
            tt::linear::NoteFilter fir;fir.prepare(kernel);fir.startNote();
            linearWet.resize(frames);
            auto inputVoice=voice;
            tt::ParametricEQ legacyWetEQ;legacyWetEQ.update(r.generatedBands,r.generatedEQ,rate);
            if(!r.linearPhase)for(int i=0;i<int(rate*.15);++i){float l=0,rr=0;legacyWetEQ.process(l,rr);}
            pt::TailReverb sourceVerb;sourceVerb.prepare(rate);sourceVerb.setSettings(tailGeneratedSettings);
            const size_t latency=size_t(kernel->latency());
            for(size_t i=0;i<frames+latency;++i){
                if(i%block==0 && (threadShouldExit()||r.generation!=latest))return false;
                const double time=inputVoice.time;
                auto parts=inputVoice.nextParts(punch,tail,rate,reverb,generatedTailWet,nullptr,r.crunchLayer.get());
                const bool send=r.generated.tailSend&&!r.generated.tailRootPitch&&time>=inputVoice.punchEnd;
                auto wet=sourceVerb.process(send?parts.tailSend[0]:0.f,send?parts.tailSend[1]:0.f);
                if(r.generated.tailFadeMs>0.f){const float g=pt::wetFadeIn(time*1000.0,pt::tailWetOnsetMs(inputVoice.punchEnd,r.generated),r.generated.tailFadeMs);wet[0]*=g;wet[1]*=g;} // v0.4.59, mirrors processBlock
                wet[0]+=parts.generatedTailReverb[0];wet[1]+=parts.generatedTailReverb[1];
                if(!r.linearPhase)legacyWetEQ.process(wet[0],wet[1]);
                const auto filtered=fir.process(wet[0],wet[1]);
                if(i>=latency)linearWet[i-latency]=filtered;
            }
        }
        const size_t bins=(std::min)(size_t(7200),frames);
        if(audio){audio->rate=rate;audio->left.assign(frames,0.f);audio->right.assign(frames,0.f);}
        if(buses){buses->rate=rate;buses->bins=bins;buses->frames.assign(frames,{});buses->limiter=r.limiter;
                  buses->bassMono=r.bassMono;buses->bassMonoHz=r.bassMonoHz;buses->bassMonoMix=r.bassMonoMix;}
        k.envelope.bins.resize(bins);
        for(auto& b:k.envelope.bins){b.minimum=std::numeric_limits<float>::max();b.maximum=std::numeric_limits<float>::lowest();}
        // v0.5.1: linear-phase MONO BASS is applied to the whole render afterwards
        // (zero-phase, aligned), then the limiter, so the waveform/export match playback.
        const bool linearMono=r.bassMono&&r.bassMonoLinear&&r.bassMonoMix>0.f;
        std::vector<float> preL,preR;if(linearMono){preL.assign(frames,0.f);preR.assign(frames,0.f);}
        for(size_t start=0;start<frames;start+=block) {
            if(threadShouldExit()||r.generation!=latest)return false;
            const int count=int((std::min)(size_t(block),frames-start));
            for(int i=0;i<count;++i) {
                const double voiceTime=voice.time;
                auto parts=voice.nextParts(punch,tail,rate,reverb,generatedTailWet,nullptr,r.crunchLayer.get());
                const float tailSendL=(r.generated.tailSend&&!r.generated.tailRootPitch&&voiceTime>=voice.punchEnd)?parts.tailSend[0]:0.f;
                const float tailSendR=(r.generated.tailSend&&!r.generated.tailRootPitch&&voiceTime>=voice.punchEnd)?parts.tailSend[1]:0.f;
                const float punchSendL=r.generated.punchSend?parts.punch[0]*0.25118864f:0.f;
                const float punchSendR=r.generated.punchSend?parts.punch[1]*0.25118864f:0.f;
                auto tailWet=generated.process(tailSendL,tailSendR);auto punchWet=punchGenerated.process(punchSendL,punchSendR);
                const float punchFade=r.punchGenerated.punchFadeMs<=0.f?1.f:std::clamp(float(voiceTime*1000.0-r.punchGenerated.punchPreDelayMs-.35f*r.punchGenerated.distance)/r.punchGenerated.punchFadeMs,0.f,1.f);
                punchWet[0]*=punchFade;punchWet[1]*=punchFade;
                if(r.generated.tailFadeMs>0.f){const float g=pt::wetFadeIn(voiceTime*1000.0,pt::tailWetOnsetMs(voice.punchEnd,r.generated),r.generated.tailFadeMs);tailWet[0]*=g;tailWet[1]*=g;} // v0.4.59, mirrors processBlock
                tailWet[0]+=parts.generatedTailReverb[0];tailWet[1]+=parts.generatedTailReverb[1];
                std::array<float,2> tailWetProcessed=tailWet;if(!linearWet.empty())tailWetProcessed=linearWet[start+size_t(i)];else wetEQ.process(tailWetProcessed[0],tailWetProcessed[1]);punchWetEQ.process(punchWet[0],punchWet[1]);punchWet[0]=punchGeneratedHPF.process(0,punchWet[0]);punchWet[1]=punchGeneratedHPF.process(1,punchWet[1]);
                const float duckL=(r.generated.tailSend&&voiceTime>=voice.punchEnd)?parts.tailSend[0]:0.f;
                const float duckR=(r.generated.tailSend&&voiceTime>=voice.punchEnd)?parts.tailSend[1]:0.f;
                duck.process(tailWetProcessed,duckL,duckR);punchDuck.process(punchWet,parts.punch[0],parts.punch[1]);
                const float duckGain=verbDuck.process(parts.punch[0],parts.punch[1]);   // v0.4.51, mirrors processBlock
                parts.reverb[0]*=duckGain;parts.reverb[1]*=duckGain;
                wetBuffer[size_t(i)]={(tailWetProcessed[0]*mix*wetLevel+punchWet[0]*punchMix*punchWetLevel)*duckGain,(tailWetProcessed[1]*mix*wetLevel+punchWet[1]*punchMix*punchWetLevel)*duckGain};
                if(!r.generated.tailRootPitch&&!r.generated.keepDry){parts.tail[0]*=1-mix;parts.tail[1]*=1-mix;}
                region.process(parts.punch[0],parts.punch[1],voiceTime);
                splitWeight[size_t(i)]=splitTrim.next(voiceTime,voice.punchEnd,r.punchRegion.splitPercent,r.punchRegion.transitionMs);
                if(!r.eqPost[0])eq[1].process(parts.punch[0],parts.punch[1]);
                transient.process(parts.punch[0],parts.punch[1]);
                if(!r.eqPost[1])eq[0].process(parts.tail[0],parts.tail[1]);
                eq[2].process(parts.reverb[0],parts.reverb[1]);
                const float punchGain=r.layerMuted[0]?0.f:1.f,tailGain=r.layerMuted[1]?0.f:1.f,reverbGain=r.layerMuted[2]?0.f:1.f;
                parts.punch[0]*=punchGain;parts.punch[1]*=punchGain;
                parts.tail[0]*=tailGain;parts.tail[1]*=tailGain;
                parts.reverb[0]*=reverbGain;parts.reverb[1]*=reverbGain;
                wetBuffer[size_t(i)][0]*=reverbGain;wetBuffer[size_t(i)][1]*=reverbGain;
                for(int ch=0;ch<2;++ch){p.setSample(ch,i,parts.punch[size_t(ch)]);t.setSample(ch,i,parts.tail[size_t(ch)]+parts.reverb[size_t(ch)]+(r.generated.distortWet?wetBuffer[size_t(i)][size_t(ch)]:0.f));}
            }
            if(distort){distortion[0].process(p,count,r.distortion[0]);distortion[1].process(t,count,r.distortion[1]);}
            for(int i=0;i<count;++i) {
                const float envelopeGain=envelope.next();
                auto& bin=k.envelope.bins[(std::min)(bins-1,(start+size_t(i))*bins/frames)];
                std::array<float,2> out{};
                // v0.5.20 EQ POSITION: after the distortion, the Tail EQ covers the whole Tail + Reverb bus.
                std::array<float,2> punchBus{{p.getSample(0,i),p.getSample(1,i)}},tailBus{{t.getSample(0,i),t.getSample(1,i)}};
                if(r.eqPost[0])eq[1].process(punchBus[0],punchBus[1]);
                if(r.eqPost[1])eq[0].process(tailBus[0],tailBus[1]);
                const float w=splitWeight[size_t(i)],splitGain=tt::PunchSplitTrim::gain(r.splitNormalizeGains[0],r.splitNormalizeGains[1],w);
                auto* busFrame=buses?&buses->frames[start+size_t(i)]:nullptr;
                if(busFrame)(*busFrame)[4]=envelopeGain;
                for(int ch=0;ch<2;++ch){const float raw=punchBus[size_t(ch)],punch=raw*splitGain,tail=tailBus[size_t(ch)]+(r.generated.distortWet?0.f:wetBuffer[size_t(i)][size_t(ch)]);
                    peaks[0]=(std::max)(peaks[0],std::abs(punch));peaks[1]=(std::max)(peaks[1],std::abs(tail));
                    peaks[2]=(std::max)(peaks[2],std::abs(raw*(1.f-w)));peaks[3]=(std::max)(peaks[3],std::abs(raw*w));
                    if(busFrame){(*busFrame)[size_t(ch)]=punch;(*busFrame)[size_t(2+ch)]=tail;}
                    out[size_t(ch)]=(punch*r.normalizeGains[0]+tail*r.normalizeGains[1])*envelopeGain;}
                if(linearMono){preL[start+size_t(i)]=out[0];preR[start+size_t(i)]=out[1];continue;}
                bassMono.process(out[0],out[1],r.bassMono,r.bassMonoHz,r.bassMonoMix);
                limiter.process(out[0],out[1],r.limiter);
                if(audio){audio->left[start+size_t(i)]=out[0];audio->right[start+size_t(i)]=out[1];}
                for(float v:out){const float x=std::clamp(v,-1.0f,1.0f);bin.minimum=(std::min)(bin.minimum,x);bin.maximum=(std::max)(bin.maximum,x);}
            }
        }
        if(linearMono) {
            if(threadShouldExit()||r.generation!=latest)return false;
            const auto low=tt::bassmono::sideLowBand(preL,preR,r.bassMonoHz,rate);
            const float mixAmount=std::clamp(r.bassMonoMix,0.f,1.f);
            for(size_t f=0;f<frames;++f) {
                std::array<float,2> out{float(preL[f]-mixAmount*low[f]),float(preR[f]+mixAmount*low[f])};
                limiter.process(out[0],out[1],r.limiter);
                if(audio){audio->left[f]=out[0];audio->right[f]=out[1];}
                auto& bin=k.envelope.bins[(std::min)(bins-1,f*bins/frames)];
                for(float v:out){const float x=std::clamp(v,-1.0f,1.0f);bin.minimum=(std::min)(bin.minimum,x);bin.maximum=(std::max)(bin.maximum,x);}
            }
            if(buses){
                if(threadShouldExit()||r.generation!=latest)return false;
                buses->monoLow=previewBusLowBands(*buses,r.bassMonoHz);
            }
        }
        for(auto& b:k.envelope.bins){if(b.minimum>b.maximum)b={};k.envelope.peak=(std::max)({k.envelope.peak,std::abs(b.minimum),std::abs(b.maximum)});}
        return true;
    }
};
