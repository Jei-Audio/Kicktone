#pragma once
#include "KickCharacterPitch.h"
#include "ExtremeV5.h"   // v0.4.68
#include "PitchDrop.h"
#include "WetAlign.h"
#include "CrunchMatch.h"
#include "PitchBankPolicy.h"
#include "TailReverb.h"
#include "TailSubDuck.h"
#include "LinearPhaseEQ.h"
#include "AdaptiveStretch.h"
#include "ParallelRender.h"   // v0.4.65
#include <functional>
#include <memory>
#include <string>

namespace pt {
struct PreparedTailReverbBank {
    // Each note is a single, already-combined Tail + generated Tail reverb
    // render. It is pitched/stretched once using the dry Tail's cycle map.
    std::array<std::shared_ptr<const Sample>,128> notes{};
    std::array<double,128> dryTailDurations{};
    std::shared_ptr<const Sample> rootRender;
    cycle::Analysis phaseMap;
    wetalign::Match wetMatch;   // v0.4.54: chosen reverb shift/polarity (identity when off)
    bool phaseLocked=false;
};
struct TailCompositeRenderSettings {
    std::array<tt::Band,tt::kBands> bands{};
    bool eqOn=true,linearPhase=false,subMono=false;
    double adaptiveAmount=0;
    double formantAmount=0;   // unused since v0.4.53 (crunch is a separate layer bank)
    double subMonoHz=90;      // v0.4.56: mono sub crossover; appended so aggregate init stays valid
    // v0.4.57: dry-only Tail end cut (source ms from the file end; 0 = off).
    // Not part of the wet cache key: the reverb is generated from the uncut Tail.
    double tailEndTrimMs=0, tailEndFadeMs=10;
    PitchDropSettings drop;
};

// A bounded, single-entry cache owned exclusively by the preparation worker.
// The worker pins the immutable source while this cache is alive.
struct TailCompositeCache {
    const Sample* cycleSource=nullptr;
    double cycleRoot=0;
    cycle::Analysis sourceAnalysis;
    const Sample* wetSource=nullptr;
    double wetTrimMs=0;
    TailReverbSettings wetSettings;
    TailCompositeRenderSettings wetProcessing;
    Sample filteredWet;
    std::shared_ptr<const Sample> composite;
    size_t analysisBuilds=0,wetBuilds=0;
    bool wetMatches(const Sample& source,double trim,const TailReverbSettings& s,
                    const TailCompositeRenderSettings& p) const {
        const auto& a=wetSettings;const auto& b=wetProcessing;
        if(wetSource!=&source || wetTrimMs!=trim ||
           a.engine!=s.engine || a.spaceMs!=s.spaceMs || a.width!=s.width || a.distance!=s.distance ||
           a.duckAmount!=s.duckAmount || a.duckHz!=s.duckHz || a.duckRecovery!=s.duckRecovery ||
           b.eqOn!=p.eqOn || b.linearPhase!=p.linearPhase || b.subMono!=p.subMono ||
           (p.subMono && std::abs(b.subMonoHz-p.subMonoHz)>.01))return false;
        for(size_t i=0;i<p.bands.size();++i) {
            const auto& x=b.bands[i];const auto& y=p.bands[i];
            if(x.enabled!=y.enabled||x.type!=y.type||x.slope!=y.slope||x.hz!=y.hz||x.gain!=y.gain||x.q!=y.q)return false;
        }
        return true;
    }
};

struct RootTailCompositePrep;
inline Sample renderRootTailCompositeShift(const RootTailCompositePrep& prep,double shift,const std::function<bool()>& cancel);
// v0.5.15 PLAYED KEYS FIRST: the composite is split into a one-time setup
// (root composite, wet render, cycle map, engine preparation: milliseconds)
// and an independent per-note render, so the worker can render only the keys
// that are needed now. renderRootTailCompositeBank() below runs both exactly
// as before (bit-identical: the same calls in the same order per note).
struct RootTailCompositePrep {
    std::shared_ptr<const Sample> rootSample, unchanged;
    PreparedTailReverbBank base;      // notes = unchanged, dryTailDurations = the unshifted value
    double root=36, stretch=1, rootHz=0, cutPosition=0, sourcePeak=0;
    bool adaptiveEngine=false, extremeEngine=false;
    TailCompositeRenderSettings wetProcessing;
    character::Prepared characterPrepared; extreme5::Prepared extremePrepared;
};
inline RootTailCompositePrep prepareRootTailComposite(
    const Sample& tail,double root,double trimMs,const TailReverbSettings& settings,
    double stretch,int quality,bool crunch,const TailCompositeRenderSettings& wetProcessing={},
    const std::function<bool()>& cancel={},TailCompositeCache* cache=nullptr) {
    if(tail.left.empty() || tail.left.size()!=tail.right.size() || tail.rate<8000 || stretch<.8 || stretch>1.5)
        throw std::invalid_argument("Invalid root Tail composite source");
    const auto checkpoint=[&] { if(cancel && cancel())throw std::runtime_error("Root Tail composite preparation cancelled"); };
    checkpoint();
    const double trim=std::clamp(trimMs*.001,0.0,tail.duration());
    const double sourceDuration=tail.duration()-trim;
    // v0.4.57: dry keep length after the optional end cut (source seconds).
    const bool cutDry=wetProcessing.tailEndTrimMs>0;
    const double cutPosition=cutDry?std::clamp(sourceDuration-wetProcessing.tailEndTrimMs*.001,0.0,sourceDuration):sourceDuration;
    const bool renderWet=settings.enabled&&settings.tailSend&&settings.mix>0.f;
    const size_t dryFrames=(std::max)(size_t(1),size_t(std::ceil(sourceDuration*tail.rate)));
    const auto applyDryCut=[&](Sample& dry) {
        if(!cutDry)return;
        const double fade=tailEndFadeMsClamped(wetProcessing.tailEndFadeMs)*.001;
        for(size_t i=0;i<dryFrames && i<dry.left.size();++i) {
            if((i&2047u)==0)checkpoint();
            const float g=float(tailEndGain(double(i)/tail.rate,cutPosition,fade));
            dry.left[i]*=g;dry.right[i]*=g;
        }
    };
    const double ringOut=renderWet
        ? std::clamp(double(settings.spaceMs),20.0,3000.0)*.001*1.5+
          std::clamp(double(settings.distance),0.0,100.0)*.00035+.047 : 0.0;
    const size_t wetFrames=renderWet
        ? (std::max)(size_t(1),size_t(std::ceil((sourceDuration+ringOut)*tail.rate))) : 0;
    const auto offsetFrames=renderWet
        ? std::llround(std::clamp(double(settings.tailOffsetMs),-300.0,300.0)*.001*tail.rate) : 0LL;
    // Advance only wet content; anything before the composite onset is cropped.
    const auto wetEnd=(std::max)(0LL,offsetFrames+static_cast<long long>(wetFrames));
    const size_t totalFrames=(std::max)(dryFrames,static_cast<size_t>(wetEnd));
    wetalign::Match matchResult;bool lockedResult=false;
    Sample rootComposite;rootComposite.rate=tail.rate;rootComposite.left.assign(totalFrames,0);rootComposite.right.assign(totalFrames,0);
    const float wetMix=renderWet?std::clamp(settings.mix,0.f,100.f)*.01f:0.f;
    const float wetGain=std::pow(10.f,std::clamp(settings.levelDb,-60.f,12.f)/20.f)*wetMix;
    const float dryGain=settings.keepDry?1.f:1.f-wetMix;
    for(size_t i=0;i<dryFrames;++i) {
        if((i&2047u)==0)checkpoint();
        const double t=double(i)/tail.rate;
        const float inFade=std::clamp(float(t/.0005),0.f,1.f);
        const float outFade=std::clamp(float((sourceDuration-t)/.002),0.f,1.f);
        const double pos=(trim+t)*tail.rate;
        rootComposite.left[i]=tail.at(0,pos)*inFade*outFade*dryGain;
        rootComposite.right[i]=tail.at(1,pos)*inFade*outFade*dryGain;
    }
    if(wetFrames>0) {
        Sample generatedWet;
        const Sample* wet=cache && cache->wetMatches(tail,trimMs,settings,wetProcessing)
            ? &cache->filteredWet : nullptr;
        if(!wet) {
            generatedWet.rate=tail.rate;generatedWet.left.assign(wetFrames,0);generatedWet.right.assign(wetFrames,0);
            TailReverb reverb;reverb.prepare(tail.rate);auto reverbSettings=settings;
            reverbSettings.punchPreDelayMs=0;reverb.setSettings(reverbSettings);
            tt::ParametricEQ wetEQ;wetEQ.update(wetProcessing.bands,wetProcessing.eqOn,tail.rate);
            TailSubDuck duck;duck.prepare(tail.rate,settings);
            std::shared_ptr<const tt::linear::Kernel> kernel;
            tt::linear::Convolver fir;
            size_t firLatency=0;
            if(wetProcessing.linearPhase||wetProcessing.subMono) {
                kernel=tt::linear::design(wetProcessing.bands,wetProcessing.linearPhase&&wetProcessing.eqOn,tail.rate,wetProcessing.subMono,wetProcessing.subMonoHz);
                firLatency=size_t(kernel->latency());fir.prepare(kernel);
            }
            for(size_t i=0;i<wetFrames+firLatency;++i) {
                if((i&2047u)==0)checkpoint();
                float inL=0,inR=0;std::array<float,2> wet{};
                if(i<wetFrames) {
                    const double t=double(i)/tail.rate;
                    if(t<sourceDuration) {
                        const float fi=std::clamp(float(t/.0005),0.f,1.f),fo=std::clamp(float((sourceDuration-t)/.002),0.f,1.f);
                        const double pos=(trim+t)*tail.rate;
                        inL=tail.at(0,pos)*fi*fo;inR=tail.at(1,pos)*fi*fo;
                    }
                    wet=reverb.process(inL,inR);
                    if(!wetProcessing.linearPhase)wetEQ.process(wet[0],wet[1]);
                    duck.process(wet,inL,inR);
                }
                std::array<float,2> filtered=wet;
                if(firLatency)filtered=fir.process(wet[0],wet[1]);
                if(i>=firLatency && i-firLatency<wetFrames) {
                    const size_t dst=i-firLatency;
                    generatedWet.left[dst]=filtered[0];generatedWet.right[dst]=filtered[1];
                }
            }
            checkpoint();
            if(cache) {
                cache->filteredWet=std::move(generatedWet);
                cache->wetSource=&tail;cache->wetTrimMs=trimMs;
                cache->wetSettings=settings;cache->wetProcessing=wetProcessing;++cache->wetBuilds;
                wet=&cache->filteredWet;
            } else wet=&generatedWet;
        }
        // Place the scaled reverb on the composite timeline (Tail Reverb Offset).
        Sample placed;placed.rate=tail.rate;placed.left.assign(totalFrames,0);placed.right.assign(totalFrames,0);
        const size_t first=offsetFrames<0?static_cast<size_t>(-offsetFrames):0;
        // v0.4.59: Tail fade-in, measured on the wet's own timeline from the
        // moment the room returns (its Distance pre-delay). Not part of the
        // filtered-wet cache: it is applied here, after the cached wet render.
        const double wetOnsetMs=.35*std::clamp(double(settings.distance),0.0,100.0);
        for(size_t i=first;i<wet->left.size();++i) {
            if((i&2047u)==0)checkpoint();
            const auto dst=static_cast<size_t>(offsetFrames+static_cast<long long>(i));
            if(dst<totalFrames) {
                const float g=settings.tailFadeMs>0.f?wetGain*wetFadeIn(double(i)*1000.0/tail.rate,wetOnsetMs,settings.tailFadeMs):wetGain;
                placed.left[dst]=wet->left[i]*g;
                placed.right[dst]=wet->right[i]*g;
            }
        }
        // v0.4.54: keep the reverb from phasing out the dry tail's low end.
        if(settings.wetAutoMatch) {
            matchResult=wetalign::findMatch(rootComposite,placed,dryFrames,checkpoint);checkpoint();
            wetalign::applyMatch(placed,matchResult);
        }
        if(settings.wetPhaseLock) {wetalign::phaseLock(placed,rootComposite,dryFrames,checkpoint);lockedResult=true;checkpoint();}
        // Match/lock above saw the UNCUT dry Tail, so the reverb is unchanged.
        applyDryCut(rootComposite);
        for(size_t i=0;i<totalFrames;++i){rootComposite.left[i]+=placed.left[i];rootComposite.right[i]+=placed.right[i];}
    } else applyDryCut(rootComposite);
    checkpoint();
    const size_t finalFade=(std::min)(totalFrames,size_t(std::ceil(.002*tail.rate)));
    for(size_t i=0;i<finalFade;++i) {
        const float gain=float(finalFade-i)/float(finalFade);
        rootComposite.left[totalFrames-finalFade+i]*=gain;
        rootComposite.right[totalFrames-finalFade+i]*=gain;
    }
    std::shared_ptr<const Sample> rootSample;
    if(cache && cache->composite && cache->composite->rate==rootComposite.rate &&
       cache->composite->left==rootComposite.left && cache->composite->right==rootComposite.right)
        rootSample=cache->composite;
    else {
        rootSample=std::make_shared<const Sample>(std::move(rootComposite));
        if(cache)cache->composite=rootSample;
    }
    PreparedTailReverbBank result;result.rootRender=rootSample;result.wetMatch=matchResult;result.phaseLocked=lockedResult;
    const double rootHz=440.0*std::pow(2.0,(root-69.0)/12.0);
    cycle::Analysis analysis;
    if(cache && cache->cycleSource==&tail && cache->cycleRoot==root)analysis=cache->sourceAnalysis;
    else {
        analysis=cycle::analyzeWithManualRoot(tail,rootHz);checkpoint();
        if(cache){cache->cycleSource=&tail;cache->cycleRoot=root;cache->sourceAnalysis=analysis;++cache->analysisBuilds;}
    }
    for(auto& mark:analysis.marks)mark-=trim*tail.rate;
    if(analysis.marks.size()<3)throw std::runtime_error("Root Tail cycle map is empty");
    const auto count=analysis.marks.size();const double finalPeriod=(std::max)(1.0,analysis.marks[count-1]-analysis.marks[count-2]);
    while(analysis.marks.back()<double(rootSample->left.size())+2*finalPeriod)analysis.marks.push_back(analysis.marks.back()+finalPeriod);
    result.phaseMap=analysis;
    // v0.4.61: only the Highest Fidelity v4 family remains (Legacy v1, the old
    // High Fidelity and Extreme engines were removed). quality 3 = Adaptive
    // Stretch v4 (per-note shorter tails), anything else = Highest Fidelity v4.
    (void)crunch;
    RootTailCompositePrep prep;
    prep.adaptiveEngine=quality==3;prep.extremeEngine=quality==5;   // v0.4.68 Extreme v5
    prep.root=root;prep.stretch=stretch;prep.rootHz=rootHz;prep.cutPosition=cutPosition;prep.wetProcessing=wetProcessing;
    prep.rootSample=rootSample;
    if(prep.extremeEngine)prep.extremePrepared=extreme5::prepare(*rootSample,analysis,stretch,cancel);
    else prep.characterPrepared=character::prepare(*rootSample,analysis,stretch,cancel);
    // Stereo phase analysis and 4x SRC are source-only. Reuse their exact data;
    // every note still receives the original single-route pitch algorithm.
    checkpoint();
    std::shared_ptr<const Sample> unchanged=rootSample;
    if(std::abs(stretch-1.0)>1.e-9 || (!prep.adaptiveEngine && wetProcessing.drop.active()))
        unchanged=std::make_shared<const Sample>(renderRootTailCompositeShift(prep,0.0,cancel));
    prep.unchanged=unchanged;
    const double rootDurationScale=unchanged->duration()/rootSample->duration();
    result.notes.fill(unchanged);
    for(size_t i=0;i<result.dryTailDurations.size();++i)result.dryTailDurations[i]=cutPosition*rootDurationScale;
    float sourcePeak=0;
    for(size_t i=0;i<rootSample->left.size();++i) {
        if((i&2047u)==0)checkpoint();
        sourcePeak=(std::max)({sourcePeak,std::abs(rootSample->left[i]),std::abs(rootSample->right[i])});
    }
    prep.sourcePeak=sourcePeak;prep.base=std::move(result);
    checkpoint();return prep;
}
// One pitched composite note (shift in semitones from the root) - the exact
// per-note render of the former single loop.
inline Sample renderRootTailCompositeShift(const RootTailCompositePrep& prep,double shift,const std::function<bool()>& cancel) {
    const double adapt=prep.adaptiveEngine?prep.stretch*adaptiveDurationRatio(shift,prep.wetProcessing.adaptiveAmount):0.0;
    auto out=prep.extremeEngine?extreme5::render(*prep.rootSample,shift,prep.extremePrepared,cancel)
                               :character::render(*prep.rootSample,shift,prep.characterPrepared,cancel,nullptr,adapt);
    if(!prep.adaptiveEngine && prep.wetProcessing.drop.active())out=drop::render(out,prep.wetProcessing.drop,prep.rootHz*std::pow(2.,shift/12.),0.,cancel);
    return out;
}
struct RootTailCompositeNote { std::shared_ptr<const Sample> sample; double dryTailDuration=0; bool rendered=false; };
// The note's composite and dry-tail duration; the root (zero shift) keeps the
// unchanged render. Throws on an invalid render or cancellation.
inline RootTailCompositeNote renderRootTailCompositeNote(const RootTailCompositePrep& prep,int note,const std::function<bool()>& cancel) {
    RootTailCompositeNote r;r.sample=prep.base.notes[size_t(note)];r.dryTailDuration=prep.base.dryTailDurations[size_t(note)];
    const double shift=double(note)-prep.root;
    if(std::abs(shift)<1.e-5 || note<kLowestPitchNote || note>kHighestPitchNote)return r;
    if(cancel && cancel())throw std::runtime_error("Root Tail composite preparation cancelled");
    Sample rendered=renderRootTailCompositeShift(prep,shift,cancel);
    const double duration=prep.adaptiveEngine?prep.stretch*adaptiveDurationRatio(shift,prep.wetProcessing.adaptiveAmount):prep.stretch;
    if(!usablePitchResult(*prep.rootSample,rendered,duration,float(prep.sourcePeak)))
        throw std::runtime_error("Root Tail composite note "+std::to_string(note)+" invalid: got "+
            std::to_string(rendered.left.size())+" frames, expected "+
            std::to_string(std::llround(prep.rootSample->left.size()*duration))+
            "; check stereo, sample rate and finite/audible output");
    const double scale=rendered.duration()/prep.rootSample->duration();
    r.dryTailDuration=prep.cutPosition*scale;
    r.sample=std::make_shared<const Sample>(std::move(rendered));r.rendered=true;
    return r;
}
inline PreparedTailReverbBank renderRootTailCompositeBank(
    const Sample& tail,double root,double trimMs,const TailReverbSettings& settings,
    double stretch,int quality,bool crunch,const TailCompositeRenderSettings& wetProcessing={},
    const std::function<bool()>& cancel={},TailCompositeCache* cache=nullptr) {
    auto prep=prepareRootTailComposite(tail,root,trimMs,settings,stretch,quality,crunch,wetProcessing,cancel,cache);
    PreparedTailReverbBank result=prep.base;
    // v0.4.65: notes render in parallel; each note's output is bit-identical to
    // the former sequential loop (pure function of the shared root composite and
    // its cycle map). The first failure/cancellation is rethrown after all tasks end.
    parallel::forEach(size_t(kPitchNoteCount),[&](size_t index) {
        const int note=kLowestPitchNote+int(index);
        if(cancel && cancel())throw std::runtime_error("Root Tail composite preparation cancelled");
        auto r=renderRootTailCompositeNote(prep,note,cancel);
        if(!r.rendered)return;
        result.dryTailDurations[size_t(note)]=r.dryTailDuration;          // distinct slot per note
        result.notes[size_t(note)]=std::move(r.sample);
    });
    if(cancel && cancel())throw std::runtime_error("Root Tail composite preparation cancelled");
    return result;
}
}
