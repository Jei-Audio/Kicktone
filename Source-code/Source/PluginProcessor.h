#pragma once
#include "PunchRegionEQ.h"
#include "PunchSplitTrim.h"   // v0.5.21
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"
#include "PitchDropSettings.h"
#include "MasterEnvelope.h"
#include "PitchBankPolicy.h"
#include "PunchDetection.h"
#include "PunchReverbDuck.h"
#include "Distortion.h"
#include "ParametricEQ.h"
#include "LinearPhaseEQ.h"
#include "PunchTransient.h"
#include "PhaseAlignment.h"
#include "TailReverb.h"
#include "TailSubDuck.h"
#include "OutputLimiter.h"
#include "BassMono.h"
#include "UserDefaults.h"
#include "PunchDecayNote.h"
#include <bitset>
namespace pt { struct TailCompositeCache; struct RootTailCompositePrep; }
#include <cstring>   // v0.5.10 KeyHash
#include "PunchHarmony.h"   // v0.5.10
#include "ParallelRender.h"   // v0.5.16 shared preparation slots
#include "PreparationProgress.h"   // v0.5.19 progress bar

class PunchTailProcessor : public juce::AudioProcessor, private juce::Thread, private juce::Timer {
public:
    PunchTailProcessor();
    ~PunchTailProcessor() override;
    juce::AudioProcessorValueTreeState params;
    juce::MidiKeyboardState keyboard;
    struct Asset { int trimFirst=0, trimLast=-1; double punchTimeScale=1; pt::Sample sample; pt::Pitch pitch; pt::PunchEndSuggestion punchEnd; juce::String name; juce::MemoryBlock bytes;
        pt::decaynote::Result decayNote;   // v0.5.5: measured on the prepared main punch (after ATTACK END)
        // Punches pitched with the tail, indexed by internal note - 15 (15..51).
        // v0.4.67: only 40..51 are filled unless PUNCH FOLLOWS TAIL is on.
        std::array<std::shared_ptr<const Asset>,pt::kPitchNoteCount> midiPunches{};
        std::array<std::shared_ptr<const Asset>,12> lowPunches{}; // keys D1..C#2, Resample, one octave below the tail
        // note: internal note 0..127, or a low-lane punch index 128..139.
        const Asset& forNote(int note) const {
            if(note>=pt::kLowPunchSlot){const auto& low=lowPunches[size_t((std::min)(note-pt::kLowPunchSlot,11))];return low?*low:*this;}
            if(note>=pt::kLowestPitchNote && note<=pt::kHighestPitchNote && midiPunches[size_t(note-pt::kLowestPitchNote)])
                return *midiPunches[size_t(note-pt::kLowestPitchNote)];
            return *this;
        }
    };
    struct Bank { pt::PitchDropSettings drop; double dropOnset=0; std::array<pt::Sample,128> notes; std::array<double,128> noteStretches{}; double adaptiveAmount=0; double formant=0; double stretchForNote(int n) const { return noteStretches[size_t(n)]>0?noteStretches[size_t(n)]:stretch; } double root=36, stretch=1.0, requestedStretch=1.0; int quality=0; bool crunch=false; bool prepared=false; int pitched=0; int fallback=0;
        // v0.5.15 PLAYED KEYS FIRST: a bank can be published while only some
        // pitched notes are rendered (partial). real[n] = note n holds its final
        // audio (rendered, the unshifted root, outside 15..51, or a failed note's
        // dry fallback); other notes still hold the dry tail. prepared = complete.
        // key = content key (tail audio + every render setting + version).
        uint64_t key=0; std::bitset<128> real; bool partial=false;
        bool usable() const { return prepared || partial; }
        bool hasNote(int n) const { return prepared || (n>=0 && n<128 && real[size_t(n)]); }
    };
    struct TailWetBank {
        std::array<std::shared_ptr<const pt::Sample>,128> notes{};
        std::array<double,128> dryTailDurations{};
        std::shared_ptr<const Asset> source;
        std::shared_ptr<const Bank> dryBank;
        std::array<tt::Band,tt::kBands> wetBands{};
        double root=36,stretch=1.0,trimMs=0,spaceMs=600,width=100,distance=20;
        double mix=25,levelDb=0,duckAmount=85,duckHz=120,duckRecovery=60,tailOffsetMs=0,adaptiveAmount=0,formant=0;
        bool autoMatch=false,phaseLock=false; double matchShiftMs=0; int matchPolarity=1;
        int quality=0;bool crunch=false,prepared=false,enabled=false,keepDry=true,tailSend=true,wetEQ=true,linearPhase=false,subMono=false;
        double subMonoHz=90;
        double tailEndTrimMs=0,tailEndFadeMs=10; // v0.4.57 dry-only end cut (source ms)
        double tailFadeMs=0; // v0.4.59 Tail wet fade-in baked into the composite
        int engine=0;        // v0.4.59 generated reverb engine (0 Legacy, 1 V2)
        // v0.5.15: partial composite (see Bank); key includes the dry key.
        uint64_t key=0; std::bitset<128> real; bool partial=false;
        bool usable() const { return prepared || partial; }
        bool hasNote(int n) const { return prepared || (n>=0 && n<128 && real[size_t(n)]); }
        bool matches(const Bank* dry,const pt::TailReverbSettings& s,
                     const std::array<tt::Band,tt::kBands>& bands,bool eq,bool linear,bool mono,double monoHz,double adaptive,
                     double endTrim=0,double endFade=10) const {
            return usable() && dry && dryBank.get()==dry && dry->usable() && dry->fallback==0 &&
                std::abs(root-dry->root)<.0001 && std::abs(stretch-dry->stretch)<.00001 &&
                quality==dry->quality && crunch==dry->crunch && std::abs(adaptiveAmount-adaptive)<.001 && std::abs(formant-dry->formant)<1e-9 &&
                enabled==s.enabled && keepDry==s.keepDry && tailSend==s.tailSend &&
                std::abs(mix-s.mix)<.01 && std::abs(levelDb-s.levelDb)<.01 &&
                std::abs(spaceMs-s.spaceMs)<.01 && std::abs(width-s.width)<.01 &&
                std::abs(distance-s.distance)<.01 && std::abs(duckAmount-s.duckAmount)<.01 &&
                std::abs(duckHz-s.duckHz)<.01 && std::abs(duckRecovery-s.duckRecovery)<.01 &&
                std::abs(tailOffsetMs-s.tailOffsetMs)<.01 && std::abs(tailFadeMs-s.tailFadeMs)<.01 && engine==s.engine && autoMatch==s.wetAutoMatch && phaseLock==s.wetPhaseLock && wetEQ==eq && linearPhase==linear && subMono==mono && (!mono || std::abs(subMonoHz-monoHz)<.01) && pt::sameTailEnd(tailEndTrimMs,tailEndFadeMs,endTrim,endFade) &&
                [&]{for(int i=0;i<tt::kBands;++i){const auto& a=wetBands[size_t(i)];const auto& b=bands[size_t(i)];
                    if(a.enabled!=b.enabled||a.type!=b.type||a.slope!=b.slope||a.hz!=b.hz||a.gain!=b.gain||a.q!=b.q)return false;}return true;}();
        }
    };
    // v0.4.54 parallel crunch layers (worker-built, immutable). Playback adds
    // tail + crunchMix * layer for the played note; MIX never re-prepares.
    struct CrunchBank {
        std::shared_ptr<const Bank> dry;
        std::shared_ptr<const TailWetBank> wet;   // composite (Root Render + Pitch) or null
        int character=0; double lowCutHz=2000; bool lostOnly=false;   // v0.5.7
        std::array<std::shared_ptr<const pt::Sample>,128> dryLayers{}, wetLayers{};
        std::array<double,128> drives{};
        // v0.5.15: the root layer's match targets, kept so later batches of
        // notes reuse them (layers are filled as their tail notes are rendered).
        struct Side { bool valid=false; double target=0, rootOwn=0, rootEnergy=0; std::vector<double> rootPower; std::shared_ptr<const pt::Sample> rootLayer; double rootDrive=0; };
        Side drySide, wetSide;
    };
    struct PhaseMap {
        std::shared_ptr<const Asset> punch;
        std::shared_ptr<const Bank> pitches;
        // v0.4.54: with Root Render + Pitch the played tail is the composite
        // (tail + baked reverb); Auto Match is measured on that exact tail.
        std::shared_ptr<const TailWetBank> composite;
        std::array<pt::phase::Result,pt::kPunchIndexCount> notes{}; // 128..139: low punch lane
        double trimMs=0, punchEndOffsetMs=0, joinMs=0, crossfadeMs=0, manualMs=0;
        bool invert=false, invertPunch=false, automatic=false;
        std::bitset<pt::kPunchIndexCount> done;   // v0.5.15: indices analysed (their tail note was final)
        bool sameSettings(const pt::Settings& s, const Asset* p) const {
            return punch.get()==p && std::abs(trimMs-s.trimMs)<0.001 && std::abs(punchEndOffsetMs-s.punchEndOffsetMs)<0.001 &&
             std::abs(joinMs-s.joinMs)<0.001 && std::abs(crossfadeMs-s.crossfadeMs)<0.001 &&
             std::abs(manualMs-s.phaseShiftMs)<0.001 && invert==s.invertTail && invertPunch==s.invertPunch;
        }
        bool matches(const pt::Settings& s, const Asset* p, const Bank* b, int note=-1) const {
            return punch.get()==p && pitches.get()==b &&
             std::abs(trimMs*(note>=0 && b?b->stretchForNote(note)/b->stretch:1.)-s.trimMs)<0.001 && std::abs(punchEndOffsetMs-s.punchEndOffsetMs)<0.001 &&
             std::abs(joinMs-s.joinMs)<0.001 && std::abs(crossfadeMs-s.crossfadeMs)<0.001 &&
             std::abs(manualMs-s.phaseShiftMs)<0.001 && invert==s.invertTail && invertPunch==s.invertPunch;
        }
    };
    std::shared_ptr<const PhaseMap> previewPhase() const;
    std::shared_ptr<const CrunchBank> previewCrunch() const { const juce::ScopedLock l(assetLock); return crunchBank; }
    // v0.4.62 attack layer settings (also used by the editor's processed preview).
    void applyAttackLayer(pt::Settings& s) const {
        auto get=[&](const char* id){return params.getRawParameterValue(id)->load();};
        s.attackOn=get("attackLayerOn")>.5f; s.attackLengthMs=get("attackLayerLength"); s.attackStartHz=get("attackLayerStart");
        s.attackGain=juce::Decibels::decibelsToGain(get("attackLayerLevel"),-48.f);
        s.attackEndHz=get("attackLayerEnd");   // v0.5.13: below 20 Hz = KEY
    }
    // v0.4.63: effective punch pitch mode, 0 Stretch Aligned, 1 Resample,
    // 2 Highest Fidelity (punchPitchFidelity overrides punchPitchMode).
    int punchPitchModeValue() const {
        if(params.getRawParameterValue("punchPitchFidelity")->load()>.5f)return 2;
        return params.getRawParameterValue("punchPitchMode")->load()>.5f?1:0;
    }
    float crunchMixValue() const { return params.getRawParameterValue("crunchMix")->load()*.01f; } // both remaining engines (v0.4.61)
    bool compositeUsable(const TailWetBank* wet,const Bank* dry) const;
    std::shared_ptr<const Asset> previewPunch() const;
    void loadFile(int slot, const juce::File&);
    // v0.4.66 LOCK SPLIT: a split pair = the same sample in both lanes (same
    // bytes and bounds, e.g. one file loaded into Punch and Tail by hand). With
    // LOCK SPLIT on, the editor keeps Punch END + Tail START at one cut, so
    // moving either marker moves the other. (v0.5.13: AUTO-SPLIT KICK removed,
    // user request; projects saved with an auto-split pair still form one.)
    static bool isSplitPair(const Asset* punch,const Asset* tail);
    // v0.4.68: current tail engine (3 Adaptive Stretch v4, 4 Highest Fidelity v4, 5 Extreme v5) and its name.
    int tailEngine() const { return qualityValue(); }
    static const char* engineName(int quality) { return quality==5?"Extreme v5":quality==3?"Adaptive Stretch v4":"Highest Fidelity v4"; }
    bool lockSplitOn() const { return params.getRawParameterValue("lockSplit")->load()>.5f; }
    // Forget a single source without touching the other two lanes or their settings.
    void unloadFile(int slot);
    void trimSilence(int slot);
    void redetect();
    std::array<tt::Band,tt::kBands> eqSettings(int view=0) const;
    tt::PunchRegionSettings punchRegionSettings() const;
    std::shared_ptr<const Asset> asset(int slot) const;
    // Share the immutable prepared pitch bank with the editor for visual previews.
    std::shared_ptr<const Bank> previewBank() const;
    std::shared_ptr<const pt::Sample> previewTailWet(int note) const;
    double previewTailCompositeDryDuration(int note) const;
    juce::String status() const;
    // v0.5.19: how far the 37 tail keys are and the estimated time left (message thread).
    pt::progress::Snapshot preparationProgress() const { return progress.snapshot(progressNow()); }
    static double progressNow() { return juce::Time::getMillisecondCounterHiRes()*.001; }
    std::atomic<float> peak {0};
    std::array<std::atomic<float>,3> busPeaks{{0.0f,0.0f,0.0f}};
    std::atomic<bool> clipLatched {false};
    // v0.4.60 output limiter: lowest gain applied since the editor last read it (1 = not limiting).
    std::atomic<float> limiterGain {1.0f};
    bool outputLimiterOn() const { return params.getRawParameterValue("outputLimiter")->load()>.5f; }
    // v0.5.20 EQ POSITION: bus 0 = Punch EQ, 1 = Tail EQ; true = after the distortion.
    bool eqAfterDistortion(int bus) const { return params.getRawParameterValue(bus==0?"punchEqPost":"tailEqPost")->load()>.5f; }
    float eqPostTarget(int bus) const { return eqAfterDistortion(bus)?1.f:0.f; }
    // v0.5.21 ATTACK / DECAY NORMALIZE: 0 = attack, 1 = decay output trim (linear gain).
    float splitNormalizeTarget(int part) const { return juce::Decibels::decibelsToGain(params.getRawParameterValue(part==0?"punchAttackNormalize":"punchDecayNormalize")->load()); }
    // v0.5 MONO BASS settings (also mirrored on the Final Kick waveform / Export WAV).
    struct BassMonoSettings { bool enabled=false; double hz=tt::BassMono::kDefaultHz; float mix=1.f; bool linear=false; };
    BassMonoSettings bassMonoSettings() const {
        return {params.getRawParameterValue("bassMono")->load()>.5f,double(params.getRawParameterValue("bassMonoHz")->load()),
                params.getRawParameterValue("bassMonoMix")->load()*.01f,params.getRawParameterValue("bassMonoLinear")->load()>.5f};
    }
    // v0.5.1: true while the audio engine uses the linear-phase mono bass. It follows
    // ENABLE + LINEAR PHASE live (10 ms crossfade); before audio starts, the switches.
    bool bassMonoUsesLinearPhase() const {
        if(linearRate.load()>0)return monoLinearActive.load();
        const auto m=bassMonoSettings();return m.enabled&&m.linear;
    }
    bool tailWetUsesLinearPhase() const {return linearRate.load()>0?linearEQActive.load():params.getRawParameterValue("tailVerbLinearPhase")->load()>.5f;}
    double tailEndTrimValue() const { return (std::max)(0.f,params.getRawParameterValue("tailEndTrim")->load()); }
    double tailEndFadeValue() const { return pt::tailEndFadeMsClamped(params.getRawParameterValue("tailEndFade")->load()); }
    double tailWetSubMonoHz() const {return tt::linear::clampSubMonoHz(params.getRawParameterValue("tailVerbSubMonoHz")->load());}
    bool tailWetUsesSubMono() const {return params.getRawParameterValue("tailVerbSubMono")->load()>.5f && (linearRate.load()<=0 || linearActive.load());}
    pt::PitchDropSettings pitchDropSettings(int lane) const;
    pt::TailReverbSettings tailReverbSettings() const;
    pt::TailReverbSettings punchReverbSettings() const;
    pt::TransientSettings transientSettings() const;
    pt::DistortionSettings distortionSettings(const char* prefix) const;
    void prepareToPlay(double, int) override;
    tt::MasterEnvelopeSettings masterEnvelopeSettings() const;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    // v0.5: permanent DAW-visible name (= PRODUCT_NAME). The version is shown in
    // the editor and About page, never in the name, so hosts keep recognising it.
    const juce::String getName() const override { return "Tailtone"; }
    static juce::String versionString() { return JucePlugin_VersionString; }
    // v0.5.22 (user request): the UI size is SHOWN relative to the default size,
    // so the stored uiScale 75 reads "100%". Display only; the parameter keeps
    // its 50..200 values, so every project opens at the same window size.
    static int displayedUiScale(float storedPercent) { return juce::roundToInt(storedPercent*4.f/3.f); }
    // v0.5.3: apply the user's custom defaults (About > DEFAULT SETTINGS). Done
    // automatically for a new instance created by a host / the standalone app;
    // a project opened afterwards overrides it. Returns how many were applied.
    int applyUserDefaults() { return tt::defaults::apply(params,tt::defaults::load()); }
    // v0.5.5: the decay note of the punch as it plays on the root key (message thread).
    pt::decaynote::Result punchDecayNote() const { const auto p=previewPunch(); return p?p->decayNote:pt::decaynote::Result{}; }
    // USE DECAY NOTE: retune so the decay's mid tone lands on TAIL ROOT. With the
    // Punch layer the PUNCH 2 TUNE knob moves (attack untouched), else PUNCH PITCH.
    // Returns false when there is no measured note.
    bool useDecayNote();
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 60; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    pt::Settings settings() const;
    void run() override;
    std::shared_ptr<Asset> decode(const juce::MemoryBlock&,const juce::String&,juce::String&,bool detectPitch);
    void applyPitch(const pt::Pitch&);
    void midi(const juce::MidiMessage&);
    mutable juce::CriticalSection assetLock, requestLock, statusLock;
    std::array<std::shared_ptr<Asset>,4> assets; // 0 punch, 1 tail, 2 reverb, 3 punch layer (v0.4.54)
    std::shared_ptr<Asset> preparedPunch;
    std::shared_ptr<const Asset> preparedPunchSource;
    double preparedPunchPitch=0, preparedPunchRoot=0;
    pt::PitchDropSettings preparedPunchDrop;
    std::shared_ptr<const Asset> preparedPunchLayer; // Punch 2 mixed into the prepared punch (null: none)
    double preparedLayerSplit=-1, preparedLayerTransition=-1, preparedLayerOffset=0, preparedLayerEnd=-1;
    int preparedPunchMode=0;
    bool preparedPunchFollow=false;       // v0.4.67 PUNCH FOLLOWS TAIL
    double preparedPunch2Pitch=0;         // v0.4.67 PUNCH 2 TUNE (semitones)
    bool preparedPunchDecayOnly=false;    // v0.5.4 follow pitches only Punch 2
    // v0.5.10 PUNCH HARMONY: mode / key / scale the lanes were prepared with.
    struct PunchHarmony {
        int mode=0,key=0,scale=0;
        bool operator==(const PunchHarmony& o) const { return mode==o.mode&&key==o.key&&scale==o.scale; }
        int interval(int note) const { return pt::harmony::interval(mode,key,scale,note); }
    };
    PunchHarmony preparedPunchHarmony;
    PunchHarmony punchHarmonySettings() const;
    // Caller holds assetLock. Raw source remains the saved/trimmed asset.
    std::shared_ptr<const Asset> currentPunch() const {
        return preparedPunch && preparedPunchSource==assets[0] ? preparedPunch : assets[0];
    }
    void rebuildPunch();
    std::shared_ptr<Bank> bank;
    std::shared_ptr<TailWetBank> tailWetBank;
    std::shared_ptr<PhaseMap> phaseMap;
    std::vector<std::shared_ptr<const PhaseMap>> retiredPhaseMaps;
    std::atomic<bool> rebuilding {false};
    // v0.5.10 CPU: a heavy re-render (tail bank, Root Render composite, crunch
    // layers) starts only once the settings it depends on have been still for
    // kSettleMs. While a knob is dragged, renders used to start and be cancelled
    // on every intermediate value, keeping every preparation core busy for the
    // whole drag. The render of the final value is the same; loading a sample or
    // an unchanged key is never delayed. Worker thread only.
    struct RenderGate {
        static constexpr juce::uint32 kSettleMs=150;
        uint64_t key=0; juce::uint32 since=0; bool primed=false;
        bool ready(uint64_t k) {
            const auto now=juce::Time::getMillisecondCounter();
            if(!primed){primed=true;key=k;since=0;return true;}
            if(k!=key){key=k;since=now;}
            return since==0 || now-since>=kSettleMs;
        }
    };
    struct KeyHash {   // FNV-1a over the exact bit patterns of the key values
        uint64_t h=1469598103934665603ull;
        KeyHash& add(double v){uint64_t u;std::memcpy(&u,&v,8);for(int i=0;i<8;++i){h^=(u>>(8*i))&0xffu;h*=1099511628211ull;}return *this;}
        KeyHash& add(int v){return add(double(v));}
        KeyHash& add(bool v){return add(v?1.0:0.0);}
        KeyHash& addU64(uint64_t u){for(int i=0;i<8;++i){h^=(u>>(8*i))&0xffu;h*=1099511628211ull;}return *this;}
    };
    // ---- v0.5.15 PLAYED KEYS FIRST + prepared notes saved in the project ----
    // Internal tail notes played since the project was made (audio thread sets
    // bits lock-free; saved as "playedKeys"). The worker renders these keys
    // first, publishes, then renders the rest in small background batches.
    std::array<std::atomic<uint64_t>,2> playedMask;
    void markPlayed(int note) { if(note>=0 && note<128) playedMask[size_t(note>>6)].fetch_or(1ull<<(note&63),std::memory_order_relaxed); }
    bool wasPlayed(int note) const { return note>=0 && note<128 && ((playedMask[size_t(note>>6)].load(std::memory_order_relaxed)>>(note&63))&1u); }
    struct DryEngine;                                   // analysis + engine preparation of one dry key (worker only)
    std::shared_ptr<DryEngine> dryEngine;
    std::shared_ptr<pt::RootTailCompositePrep> wetPrep; uint64_t wetPrepKey=0;
    std::shared_ptr<const Asset> hashedTail; uint64_t hashedTailValue=0;
    uint64_t tailContentKey(const std::shared_ptr<const Asset>& tail);
    std::atomic<uint64_t> failedWetKey{0};   // Root Render key that failed (not retried until it changes)
    // Saved prepared notes of the played keys (restored from a project; used
    // once their keys match the current settings, then released).
    struct ProjectCache {
        uint64_t dryKey=0, wetKey=0;
        std::array<std::shared_ptr<const pt::Sample>,128> dry{}, wet{};
        std::array<double,128> noteStretch{}, dryTailDuration{};
        // v0.5.16 (format 2): the base notes (dry unshifted render when stretch
        // or pitch drop is active; the unshifted Root Render composite), so a
        // reopened project needs neither the engine nor the Root Render setup.
        std::shared_ptr<const pt::Sample> baseDry, baseWet;
        double baseWetDryTail=0, matchShiftMs=0; int matchPolarity=1;
    };
    std::shared_ptr<const ProjectCache> pendingCache;   // protected by assetLock
    bool prepareTailBanks(pt::TailCompositeCache& compositeCache);   // true = more background work pending
    std::shared_ptr<CrunchBank> crunchFor(const std::shared_ptr<const CrunchBank>& previous,const std::shared_ptr<const Bank>& dry,
        const std::shared_ptr<const TailWetBank>& wet,const std::function<bool()>& stale,bool& aborted,bool announce);
    std::shared_ptr<PhaseMap> phaseFor(const std::shared_ptr<const PhaseMap>& previous,const std::shared_ptr<const Asset>& punch,
        const std::shared_ptr<const Bank>& pitches,const std::shared_ptr<const TailWetBank>& composite,const std::function<bool()>& stale);
    void writePreparedCache(juce::ValueTree& tree) const;
    void readPreparedCache(const juce::ValueTree& tree);
    bool offlineNoteReady(int note) const;
    void waitForOfflineNotes(const juce::MidiBuffer& events);
    juce::String backgroundStatus;   // worker only
    RenderGate dryGate, wetGate, crunchGate;
    bool renderGated=false;   // a gated render is waiting: poll the worker sooner
    // v0.5.16: this worker's claim on the slots shared by every Tailtone
    // instance in the host (held for one pass; worker only), and how long the
    // background keys still wait for the host to go idle (0 = not deferred).
    pt::parallel::PreparationSlot prepSlot;
    int backgroundDeferMs=0;
    // v0.5.16: how often the dry engine / Root Render setup ran (tests: a
    // reopened project with saved notes must publish its keys without either).
    std::atomic<int> enginePrepares{0}, wetPrepares{0};
    // Takes this pass's turn: foreground (Urgent while the host renders offline)
    // or one background try. While a foreground claim waits for another
    // instance, the status line says so. False = cancelled.
    bool claimPreparation(bool foreground,const std::function<bool()>& cancel);
    // v0.5.19: written by the worker (and its render threads), read by the editor.
    pt::progress::Tracker progress;
    struct ActivityScope {
        pt::progress::Tracker& t;
        ActivityScope(pt::progress::Tracker& tracker,pt::progress::Activity a):t(tracker){t.setActivity(a);}
        ~ActivityScope(){t.setActivity(pt::progress::Activity::None);}
    };
public:
    // v0.5.16: named locks that extend the shared preparation limit to every
    // process on the computer (bridged / separate-process plugins). Plugin
    // instances use them; tests construct processors without a host wrapper.
    static pt::parallel::SystemSlots& namedPreparationSlots();
    static juce::String waitingForTurnText() { return "Waiting for another Tailtone to finish preparing (instances take turns to keep the CPU low)..."; }
private:
    struct Request { bool pending=false; juce::File file; juce::MemoryBlock bytes; juce::String name; bool fromState=false; uint64_t generation=0; bool trimSilence=false; int trimFirst=0, trimLast=-1; };
    std::array<Request,4> requests;
    // Individually invalidate in-flight decodes; unloading one lane must not
    // cancel or reinsert a sample in a different lane. Protected by requestLock.
    std::array<uint64_t,4> requestGenerations{};
    juce::String statusText {"Load punch, tail and optional reverb WAV/MP3 files."};
    void setStatus(juce::String text) { const juce::ScopedLock l(statusLock); statusText=std::move(text); }
    // Published objects are immutable. Old objects stay worker-owned until
    // audio/UI snapshots release them, so the audio thread never frees a bank.
    std::vector<std::shared_ptr<const Asset>> retiredAssets;
    std::vector<std::shared_ptr<const Bank>> retiredBanks;
    std::vector<std::shared_ptr<const TailWetBank>> retiredTailWetBanks;
    std::shared_ptr<const Asset> audioPunch, audioReverb;
    std::shared_ptr<const Bank> audioBank;
    std::shared_ptr<const TailWetBank> audioTailWetBank;
    std::shared_ptr<const CrunchBank> crunchBank, audioCrunch;
    std::vector<std::shared_ptr<const CrunchBank>> retiredCrunch;
    juce::SmoothedValue<float> crunchMixSmoother;
    void rebuildCrunch();
    int crunchCharacterValue() const { return int(params.getRawParameterValue("crunchCharacter")->load()+.5f); }
    double crunchLowCutValue() const { return params.getRawParameterValue("crunchLowCut")->load(); }
    bool crunchLostOnlyValue() const { return params.getRawParameterValue("crunchLostOnly")->load()>.5f; }   // v0.5.7
    std::shared_ptr<const PhaseMap> audioPhase;
    uint64_t stateEpoch=0, audioEpoch=0; // stateEpoch protected by assetLock
    // Only explicit unload increments these counters. On the next audio block,
    // affected active voices drop that lane, but other sounds keep playing.
    std::array<uint64_t,4> unloadEpochs{}, audioUnloadEpochs{};
    std::array<bool,128> heldKeys{}; // audio thread only: keys currently held (legato glide)
    struct PlayingVoice : pt::Voice {
        tt::PunchRegionEQ regionEQ;
        std::shared_ptr<const Asset> punch, reverb;
        std::shared_ptr<const Bank> pitches;
        std::shared_ptr<const TailWetBank> tailWetPitches;
        std::shared_ptr<const CrunchBank> crunch;
        const pt::Sample* playbackCrunch() const {
            if(!crunch)return nullptr;
            const auto& l=compositeTail?crunch->wetLayers[size_t(note)]:crunch->dryLayers[size_t(note)];
            return l.get();
        }
        // v0.5.10: crunch layer of the note a legato glide started from (crossfade only).
        const pt::Sample* previousPlaybackCrunch() const {
            if(!crunch || !glideFadeActive() || previousNote<0)return nullptr;
            const auto& l=compositeTail?crunch->wetLayers[size_t(previousNote)]:crunch->dryLayers[size_t(previousNote)];
            return l.get();
        }
        const pt::Sample* playbackTail() const {
            if(compositeTail)return tailWetPitches?tailWetPitches->notes[size_t(note)].get():nullptr;
            return pitches?&pitches->notes[size_t(note)]:nullptr;
        }
        // Tail of the note a legato glide started from (only during its crossfade).
        const pt::Sample* previousPlaybackTail() const {
            if(!glideFadeActive() || previousNote<0)return nullptr;
            if(compositeTail)return tailWetPitches?tailWetPitches->notes[size_t(previousNote)].get():nullptr;
            return pitches?&pitches->notes[size_t(previousNote)]:nullptr;
        }
    };
    std::array<PlayingVoice,8> voices;
    void advanceStretchedGlide(PlayingVoice& v);   // v0.5.23, audio thread
    tt::MasterEnvelope masterEnvelope;
    tt::OutputLimiter outputLimiter; // v0.4.60, prepared in prepareToPlay
    tt::BassMono bassMono;           // v0.5, prepared in prepareToPlay
    size_t nextVoice=0;
    std::vector<float> masterGainSamples;
    friend struct TailtoneMasterEnvelopeTest;
    friend struct TailtoneManualRootTest;
    friend struct TailtoneKickCharacterTest;
    friend struct TailtonePitchDropTest;
    friend struct TailtoneKeyMappingTest;
    friend struct TailtoneGlideLayerTest;
    friend struct TailtoneFormantDuckTest;
    friend struct TailtoneRootRenderPhaseTest;
    friend struct TailtoneWetAlignTest;
    friend struct TailtoneReverbV059Test;
    friend struct TailtoneOutputLimiterTest;
    friend struct TailtoneBassMonoTest;   // v0.5
    friend struct TailtoneAttackLayerTest;
    friend struct TailtoneEqPositionTest;   // v0.5.20
    friend struct TailtoneSplitNormalizeTest;   // v0.5.21
    tt::PunchRegionDesign punchRegionDesign;
    std::array<std::atomic<float>*,5> regionControls{};
    std::array<std::atomic<float>*,2> regionEqEnable{};
    std::array<std::array<std::array<std::atomic<float>*,6>,tt::kBands>,2> regionBandParameters{};
    // v0.5.10 CPU: EQ views 0..4 and both distortion units, resolved once. eqSettings()
    // and distortionSettings() run every audio block; looking 300+ parameters up by
    // freshly built name strings cost ~11 % of the audio thread and allocated memory there.
    std::array<std::array<std::array<std::atomic<float>*,6>,tt::kBands>,5> eqBandParameters{};
    std::array<std::array<std::atomic<float>*,6>,2> distortionParameters{};   // punchDist, tailDist: On Mode Drive Tone Mix Gain
    void updatePunchRegionDesign(double sr);
    void rebuildPhaseMap();
    double stretchValue() const { return params.getRawParameterValue("stretch")->load() / 100.0; }
    int qualityValue() const {
        // v0.4.54: Adaptive Stretch v4 is itself built on Highest Fidelity v4,
        // so an explicit Adaptive choice wins over the (now default ON) switch.
        // v0.4.61: Legacy v1 (0), old High Fidelity (1) and Extreme (2) were
        // removed. Only Adaptive Stretch v4 (3) and Highest Fidelity v4 (4) remain;
        // projects that used a removed engine play with Highest Fidelity v4.
        // v0.4.68: Extreme v5 (5) is an appended switch that wins over both.
        if(params.getRawParameterValue("pitchExtremeV5")->load()>.5f)return 5;
        return params.getRawParameterValue("pitchAdaptive")->load()>.5f?3:4;
    }

    double activeAdaptiveValue() const { return qualityValue()==3?double(params.getRawParameterValue("adaptiveAmount")->load()):0.0; }
    // The old High Fidelity "Crunch Preserve" option was removed with that engine (v0.4.61).
    bool crunchValue() const { return false; }
    void collectRetired();
    friend struct PunchTailPlaybackTest;
    friend struct PunchTailSplitTest;   // v0.4.66 LOCK SPLIT editor fixture
    friend struct TailtonePunchRegionTest;
    friend struct TailtoneLinearIntegrationTest;
    friend struct TailtonePhaseIntegrationTest;
    friend struct PunchTailDistortionTest;
    friend struct TailtoneEQTest;
    friend struct TailtoneReverbRoutingTest;
    bool tailCompositeMatches(const TailWetBank* b) const;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reverbLevelSmoother;
    std::array<pt::ReverbHPFDesign,4> reverbHPFChoices;
    const pt::ReverbHPFDesign& selectedHPF() const {
        const int choice=juce::jlimit(0,3,juce::roundToInt(params.getRawParameterValue("reverbLowCut")->load()));
        return reverbHPFChoices[size_t(choice)];
    }
    // Two independent POST-voice effect buses. Allocated/prepared outside audio.
    juce::AudioBuffer<float> punchBus, tailReverbBus, dryPunchBus, dryTailReverbBus;
    std::array<juce::SmoothedValue<float,juce::ValueSmoothingTypes::Linear>,2> normalizationGain;
    std::array<juce::SmoothedValue<float,juce::ValueSmoothingTypes::Linear>,3> layerMuteGain;
    pt::StereoDistortion punchDistortion, tailReverbDistortion;
    pt::PunchTransient punchTransient;
    juce::SmoothedValue<float,juce::ValueSmoothingTypes::Linear> distortionBlend;
    int processingChunk=512;
    pt::TailReverb generatedVerb, generatedPunchVerb;
    pt::TailSubDuck generatedDuck, generatedPunchDuck;
    pt::PunchReverbDuck punchReverbDuck; // v0.4.54: punch -> all reverb returns
    tt::ParametricEQ generatedVerbEQ, generatedPunchVerbEQ;
    pt::ReverbHPFDesign generatedPunchHPFDesign;
    pt::ReverbHPFState generatedPunchHPF;
    juce::AudioBuffer<float> generatedWetBus;
    juce::SmoothedValue<float,juce::ValueSmoothingTypes::Linear> generatedMix,generatedPunchMix,generatedLevel,generatedPunchLevel,generatedDry;
    std::vector<uint8_t> wetResetSamples;
    std::vector<float> wetMixSamples;
    int wetOwner=-1;double wetOwnerElapsedSeconds=0,wetOwnerPunchEndSeconds=0;bool wetResetPending=true;
    // Kick means the pitched tail; punch and recorded reverb stay independent.
    tt::ParametricEQ kickEQ, punchEQ, reverbEQ;
    // v0.5.20 EQ POSITION: the same Punch/Tail EQ settings after the distortion.
    // Both positions always run (warm state), the weight crossfades 10 ms on a switch.
    tt::ParametricEQ punchPostEQ, tailPostEQ;
    std::array<juce::SmoothedValue<float,juce::ValueSmoothingTypes::Linear>,2> eqPostWeight;   // 0 punch, 1 tail
    std::vector<float> punchEqPostSamples, tailEqPostSamples;
    // v0.5.21 ATTACK / DECAY NORMALIZE: the newest note's attack/decay trim per sample,
    // latency-aligned with the Punch (DelayedFrame) and applied with punchNormalize.
    std::array<juce::SmoothedValue<float,juce::ValueSmoothingTypes::Linear>,2> splitNormalizeGain;   // 0 attack, 1 decay
    tt::PunchSplitTrim punchSplitTrim;
    std::vector<float> punchSplitGainSamples;
    struct DelayedFrame {
        float pl=0,pr=0,tl=0,tr=0,rl=0,rr=0,duckL=0,duckR=0,punchDuckL=0,punchDuckR=0;
        std::array<float,2> punchWet{};
        float tailWetGain=0,punchWetGain=0,dryGain=0,punchMute=0,tailMute=0,reverbMute=0;
        bool reset=false;
        float masterGain=0;
        float punchSplitGain=1;   // v0.5.21
    };
    std::vector<DelayedFrame> linearDelay;
    size_t linearPosition=0;
    std::atomic<bool> linearEQActive{false},linearActive{false}; // the RUNNING Tail wet mode (v0.5.1: follows the switches live)
    // v0.5.1 live Tail wet LINEAR PHASE / MONO SUB: the FIR and the aligned delay
    // always run; a mode change mutes the output for 5 ms, swaps, and fades back in.
    int wetSwapPhase=0;            // 0 idle, 1 fading out, 2 fading in (audio thread)
    float wetSwapGain=1.f,wetSwapStep=.004f;
    std::atomic<int> wetKernelLatency{0};
    tt::linear::NoteFilter linearWet;
    std::shared_ptr<const tt::linear::Kernel> linearKernel;
    std::vector<std::shared_ptr<const tt::linear::Kernel>> retiredLinearKernels;
    std::atomic<double> linearRate{0};
    void rebuildLinearEQ();
    // v0.5.1 linear-phase MONO BASS: latched at prepareToPlay (host latency stays
    // fixed while running); kernels designed on the worker, retired there too.
    std::atomic<bool> monoLinearActive{false};   // engine state, written by the audio thread (and prepareToPlay)
    std::atomic<bool> monoLinearTarget{false};   // written by the audio thread (and prepareToPlay)
    int monoSwitchRemaining=0,monoSwitchLength=480;   // audio thread only
    std::atomic<int> monoLatencySamples{0};       // set in prepareToPlay
    int reportedLatency() const;                  // what the host should be told right now
    void refreshReportedLatency();                // message thread: tell the host when it changed
    void timerCallback() override { refreshReportedLatency(); }
    tt::bassmono::LinearBassMono monoLinear;
    std::shared_ptr<const tt::bassmono::LinearKernel> monoKernel;
    std::vector<std::shared_ptr<const tt::bassmono::LinearKernel>> retiredMonoKernels;
    void rebuildBassMonoKernel();
    double rate=44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PunchTailProcessor)
};
