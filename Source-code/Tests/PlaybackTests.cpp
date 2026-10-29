#include "LegacyTestProcessor.h"
#include "../Source/PunchPitch.h"
#include <future>
#include <memory>
#include <thread>
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
// v0.4.68: every processor in these tests lives on the heap. A processor is
// ~160 KB and this function held six of them as locals, which overflowed the
// 1 MB default thread stack on Windows (MSVC reserves the whole frame on
// entry, so the test crashed before printing anything). Linux has 8 MB.
struct PunchTailPlaybackTest {
    static double muteRender(bool mutePunch,bool muteTail,bool muteReverb) {
        auto pOwner=std::make_unique<LegacyTestProcessor>();auto& p=*pOwner;p.signalThreadShouldExit();p.notify();p.stopThread(-1);
        auto makeTone=[](double hz,double seconds){pt::Sample s;s.rate=48000;const auto n=size_t(48000*seconds);s.left.resize(n);s.right.resize(n);for(size_t i=0;i<n;++i)s.left[i]=s.right[i]=float(.12*std::sin(2*pt::pi*hz*double(i)/48000));return s;};
        auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample=makeTone(880,.03);p.assets[0]=punch;
        auto reverb=std::make_shared<PunchTailProcessor::Asset>();reverb->sample=makeTone(660,.3);p.assets[2]=reverb;
        p.bank=std::make_shared<PunchTailProcessor::Bank>();p.bank->notes[36]=makeTone(110,.3);
        auto set=[&](const char* id,float value){auto* q=p.params.getParameter(id);q->setValueNotifyingHost(q->convertTo0to1(value));};
        set("mutePunch",mutePunch?1:0);set("muteTail",muteTail?1:0);set("muteReverb",muteReverb?1:0);set("gate",0);set("output",0);
        p.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,1.f),0);
        double energy=0;size_t frames=0;
        for(int j=0;j<75;++j){p.processBlock(audio,midi);if(j>=4){for(int i=0;i<128;++i)energy+=double(audio.getSample(0,i))*audio.getSample(0,i);frames+=128;}}
        return std::sqrt(energy/double(frames));
    }
    static void layerMuteTest(){
        const double full=muteRender(false,false,false);
        const double punchOnly=muteRender(false,true,true),tailOnly=muteRender(true,false,true),reverbOnly=muteRender(true,true,false);
        const double allOff=muteRender(true,true,true);
        check(full>0.01,"Unmuted layers did not produce audio");
        check(punchOnly>0.01 && tailOnly>0.01 && reverbOnly>0.01,"An individually unmuted layer was not audible");
        check(allOff<1.e-7,"Muting all three layers did not silence output");
    }
    static void run() {
        layerMuteTest();
        auto pOwner=std::make_unique<LegacyTestProcessor>();auto& p=*pOwner;
        p.signalThreadShouldExit();p.notify();p.stopThread(-1);
        // v0.4.50: Highest Fidelity v4 is the new-instance default (only engine since v0.4.61).
        check(p.params.getRawParameterValue("phaseAuto")->load()==1 && p.qualityValue()==4,
              "Auto Match and Highest Fidelity v4 should be the new-instance defaults");
        check(p.params.getRawParameterValue("punchPitchMode")->load()==1,
              "New instances must default punch pitch to Resample");
        check(p.params.getRawParameterValue("uiScale")->load()==75,
              "New instances must default the UI scale to 75%");
        {
            auto* root=p.params.getParameter("root");
            check(root->convertFrom0to1(0)==24 && root->convertFrom0to1(1)==39 &&
                  root->getNumSteps()==16 && p.settings().root==36,
                  "Tail Root must span FL Studio C2-D#3 with C3 default");
            for(float savedRoot:{12.f,24.f,31.f,39.f,51.f}) {
                auto restored=p.params.copyState();
                for(int i=0;i<restored.getNumChildren();++i) {
                    auto value=restored.getChild(i);
                    if(value.getProperty("id").toString()=="root")
                        value.setProperty("value",savedRoot,nullptr);
                }
                juce::MemoryBlock bytes;juce::MemoryOutputStream stream(bytes,false);
                restored.writeToStream(stream);stream.flush();
                p.setStateInformation(bytes.getData(),int(bytes.getSize()));
                const float expected=std::clamp(savedRoot,24.f,39.f);
                check(p.settings().root==expected,"Saved Tail Root escaped C2-D#3 or lost an in-range note");
                auto roundtrip=p.params.copyState();
                for(int i=0;i<roundtrip.getNumChildren();++i) {
                    auto value=roundtrip.getChild(i);
                    if(value.getProperty("id").toString()=="root")
                        check(float(value.getProperty("value"))==expected,"Saved root differs from live parameter");
                }
            }
            pt::Pitch outside;outside.valid=true;
            outside.midi=12.31;p.applyPitch(outside);
            check(p.settings().root==24,"Low detected root did not clamp to C2");
            outside.midi=51.31;p.applyPitch(outside);
            check(p.settings().root==39,"High detected root did not clamp to D#3");
        }
        pt::Pitch detection;detection.valid=true;detection.midi=27.31;
        p.applyPitch(detection);
        check(p.settings().root==27 && p.params.getRawParameterValue("cents")->load()==0,"Detection must zero source cents");
        auto* cents=p.params.getParameter("cents");cents->setValueNotifyingHost(cents->convertTo0to1(-17));
        check(std::abs(p.settings().root-26.83)<1.e-5,"Manual source cents unavailable");
        p.applyPitch(detection);check(p.settings().root==27,"Redetection retained manual cents");
        {
            auto laneOwner=std::make_unique<LegacyTestProcessor>();auto& lane=*laneOwner;lane.signalThreadShouldExit();lane.notify();lane.stopThread(-1);
            auto source=std::make_shared<PunchTailProcessor::Asset>();source->sample.rate=44100;
            source->sample.left.assign(8820,.2f);source->sample.right=source->sample.left;
            for(int n=40;n<=51;++n){auto a=std::make_shared<PunchTailProcessor::Asset>();
                a->sample=pt::renderPunchPitch(source->sample,n-27,pt::PunchPitchMode::Resample);
                a->punchTimeScale=a->sample.duration()/source->sample.duration();source->midiPunches[size_t(n-pt::kLowestPitchNote)]=a;}
            lane.assets[0]=source;lane.prepareToPlay(44100,128);
            juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer events;
            int slot=0;
            for(int n:{39,40,51,52}){
                events.addEvent(juce::MidiMessage::noteOn(1,(n)+12,1.f),0);lane.processBlock(audio,events);
                const auto& v=lane.voices[size_t(slot++)];
                check(v.punch==source,"Voice must pin whole immutable punch bank");
                check(std::abs(v.punchEnd-source->forNote(v.note).sample.duration())<1.e-9,"MIDI did not select prepared punch duration");
                int active=0;for(const auto& voice:lane.voices)active+=voice.active;
                check(active==1,"New MIDI note failed to choke previous punch");
            }
            auto replacement=std::make_shared<PunchTailProcessor::Asset>();replacement->sample=source->sample;
            lane.retiredAssets.push_back(source);lane.assets[0]=replacement;
            lane.processBlock(audio,events);
            check(lane.voices[3].punch==source,"Bank replacement changed held upper punch");
        }
        auto a=std::make_shared<PunchTailProcessor::Asset>();
        a->sample.rate=44100;a->sample.left.resize(44100);a->sample.right.resize(44100);
        for(size_t i=0;i<a->sample.left.size();++i)a->sample.left[i]=a->sample.right[i]=float(.4*std::sin(2*pt::pi*55*i/44100));
        auto bank=std::make_shared<PunchTailProcessor::Bank>();bank->prepared=true;
        for(auto& n:bank->notes)n=a->sample;
        p.assets[1]=a;p.bank=bank;p.prepareToPlay(44100,128);
        juce::AudioBuffer<float> buffer(2,128);juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1,48,1.0f),0);p.processBlock(buffer,midi);
        pt::Voice reference;auto settings=p.settings();settings.root=36;
        reference.start(36,1,1,nullptr,&a->sample,settings);
        for(int i=0;i<128;++i)reference.next(nullptr,&a->sample,44100);
        std::promise<void> locked,release;auto unlock=release.get_future();
        std::thread holder([&]{const juce::ScopedLock l(p.assetLock);locked.set_value();unlock.wait();});
        locked.get_future().wait();
        p.processBlock(buffer,midi);
        release.set_value();holder.join();
        for(int i=0;i<128;++i)check(std::abs(buffer.getSample(0,i)-reference.next(nullptr,&a->sample,44100)[0])<1e-7,"Lock contention interrupted audio");
        // A replacement bank must not change a note already playing.
        auto newer=std::make_shared<PunchTailProcessor::Bank>(*bank);
        for(auto& n:newer->notes)for(auto* c:{&n.left,&n.right})for(auto& x:*c)x=-x;
        p.retiredBanks.push_back(p.bank);p.bank=newer;
        p.processBlock(buffer,midi);
        for(int i=0;i<128;++i)check(std::abs(buffer.getSample(0,i)-reference.next(nullptr,&a->sample,44100)[0])<1e-7,"Bank swap changed an active kick");
        std::weak_ptr<const PunchTailProcessor::Bank> old=bank;bank.reset();
        p.collectRetired();check(!old.expired(),"Retired bank freed during active note");
        midi.addEvent(juce::MidiMessage::noteOn(1,49,1.0f),0);p.processBlock(buffer,midi);
        check(!p.voices[0].active && p.voices[1].pitches==newer,"New notes did not adopt replacement bank");
        // Check MIDI delivery while publication is busy too.
        std::promise<void> locked2,release2;auto unlock2=release2.get_future();
        std::thread holder2([&]{const juce::ScopedLock l(p.assetLock);locked2.set_value();unlock2.wait();});
        locked2.get_future().wait();midi.addEvent(juce::MidiMessage::noteOn(1,50,1.0f),0);p.processBlock(buffer,midi);
        release2.set_value();holder2.join();check(p.voices[2].active && p.voices[2].note==38,"MIDI was lost under lock contention");
        check(!old.expired(),"Audio thread destroyed old bank");
        p.collectRetired();check(old.expired(),"Worker failed to reclaim old bank");
        auto set=[&](float value){auto* param=p.params.getParameter("stretch");param->setValueNotifyingHost(param->convertTo0to1(value));};
        set(150);juce::MemoryBlock state;p.getStateInformation(state);set(80);p.setStateInformation(state.getData(),int(state.getSize()));
        check(std::abs(p.stretchValue()-1.5)<1e-6,"Stretch did not round trip in saved state");
        // v0.4.61: Legacy v1 / High Fidelity / Extreme were removed. Their IDs stay
        // registered (host indices, old projects) but are inert and not automatable.
        auto* extremeParam=p.params.getParameter("pitchExtreme");
        auto* qualityParam=p.params.getParameter("pitchQuality");
        check(qualityParam->convertFrom0to1(1)==1,"Existing quality parameter range changed");
        for(auto* id:{"pitchQuality","pitchExtreme","pitchCharacter","pitchCrunch"})
            check(!p.params.getParameter(id)->isAutomatable(),"Removed-engine placeholder is still automatable");
        {auto* hf4=p.params.getParameter("pitchCharacter");hf4->setValueNotifyingHost(0);}
        extremeParam->setValueNotifyingHost(0);qualityParam->setValueNotifyingHost(0);
        check(p.qualityValue()==4,"Removed engine switches still select a removed engine");
        // A project saved with Extreme (or Legacy) opens on Highest Fidelity v4 with canonical placeholders.
        juce::MemoryBlock extremeState;p.getStateInformation(extremeState);
        p.setStateInformation(extremeState.getData(),int(extremeState.getSize()));
        check(p.qualityValue()==4 && p.params.getRawParameterValue("pitchCharacter")->load()==1 &&
              p.params.getRawParameterValue("pitchExtreme")->load()==1,"Removed-engine project did not migrate to Highest Fidelity v4");
        auto legacy=p.params.copyState();
        for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();
            if(id=="stretch" || id=="pitchExtreme" || id=="pitchCharacter")legacy.removeChild(i,nullptr);}
        juce::MemoryBlock bytes;{juce::MemoryOutputStream out(bytes,false);legacy.writeToStream(out);}
        p.setStateInformation(bytes.getData(),int(bytes.getSize()));
        check(p.stretchValue()==1.0,"Legacy state did not restore 100% stretch");
        check(p.qualityValue()==4,"Old (pre-v4) project did not open on Highest Fidelity v4");
        // v0.4.7: REVERB LEVEL automation must affect an active sounding note,
        // including fades to an actual mute (not just newly triggered notes).
        auto levelPluginOwner=std::make_unique<LegacyTestProcessor>();auto& levelPlugin=*levelPluginOwner;
        levelPlugin.signalThreadShouldExit();levelPlugin.notify();levelPlugin.stopThread(-1);
        auto reverb=std::make_shared<PunchTailProcessor::Asset>();
        reverb->sample.rate=44100;
        reverb->sample.left.assign(44100,0.5f);
        reverb->sample.right=reverb->sample.left;
        levelPlugin.assets[2]=reverb;
        // Fresh v0.4.40 instances intentionally mute this layer. This test
        // measures recorded-reverb gain/filter automation with it audible.
        auto* reverbMute=levelPlugin.params.getParameter("muteReverb");
        reverbMute->setValueNotifyingHost(reverbMute->convertTo0to1(0.f));
        levelPlugin.prepareToPlay(44100,128);
        juce::MidiBuffer oneShot;
        oneShot.addEvent(juce::MidiMessage::noteOn(1,48,1.0f),0);
        levelPlugin.processBlock(buffer,oneShot);
        const float original=buffer.getSample(0,127);
        check(original>.2f && original<.3f,"Original reverb not playing at 0 dB");
        const double originalReverbEnd=levelPlugin.voices[0].reverbEnd;
        auto* levelParam=levelPlugin.params.getParameter("reverbLevel");
        levelParam->setValueNotifyingHost(levelParam->convertTo0to1(-60.f));
        levelPlugin.processBlock(buffer,oneShot);
        const float ramping=buffer.getSample(0,127);
        check(ramping>0 && ramping<original,"Active voice reverb level did not smoothly decrease");
        for(int j=0;j<4;++j)levelPlugin.processBlock(buffer,oneShot);
        check(buffer.getSample(0,127)==0,"Active voice reverb level did not reach mute");
        check(levelPlugin.voices[0].active,"Volume automation ended the playing voice");
        check(std::abs(levelPlugin.voices[0].reverbEnd-originalReverbEnd)<1e-12,
              "Reverb volume automation changed playback duration");
        levelParam->setValueNotifyingHost(levelParam->convertTo0to1(0.f));
        for(int j=0;j<5;++j)levelPlugin.processBlock(buffer,oneShot);
        check(std::abs(buffer.getSample(0,127)-original)<1e-5,
              "Active voice reverb did not return to unchanged level");
        // v0.4.8: Low-cut automation on a currently sounding reverb.
        // A constant reverb waveform is DC: the high-pass should remove it.
        auto* filterParam=levelPlugin.params.getParameter("reverbLowCut");
        filterParam->setValueNotifyingHost(filterParam->convertTo0to1(3.f));
        levelPlugin.processBlock(buffer,oneShot);
        check(levelPlugin.voices[0].filterSlope()==48,"Active note did not select 48 dB/oct");
        check(levelPlugin.voices[0].reverbFilterTransition>0,"Slope changed without smoothing");
        for(int j=0;j<120;++j)levelPlugin.processBlock(buffer,oneShot);
        check(std::abs(buffer.getSample(0,127))<.002f,"High-pass did not reject DC on active voice");
        check(std::abs(levelPlugin.voices[0].reverbEnd-originalReverbEnd)<1e-12,
              "High-pass changed reverb duration");
        filterParam->setValueNotifyingHost(filterParam->convertTo0to1(0.f));
        for(int j=0;j<8;++j)levelPlugin.processBlock(buffer,oneShot);
        check(levelPlugin.voices[0].filterSlope()==0,"OFF automation did not bypass reverb filter");
        check(std::abs(buffer.getSample(0,127)-original)<1e-5,
              "Filter OFF did not restore original reverb audio");
        // v0.4.10: Unload affects ONLY its sample slot, including an already
        // sounding MIDI voice. It must not stop other buses or allow a queued
        // file selection to materialize after the user clears that slot.
        auto unloadedOwner=std::make_unique<LegacyTestProcessor>();auto& unloaded=*unloadedOwner;
        unloaded.signalThreadShouldExit(); unloaded.notify(); unloaded.stopThread(-1);
        auto makeAsset=[](float level) {
            auto x=std::make_shared<PunchTailProcessor::Asset>();
            x->sample.rate=44100;
            x->sample.left.assign(44100,level);
            x->sample.right=x->sample.left;
            return x;
        };
        unloaded.assets[0]=makeAsset(0.2f);
        unloaded.assets[1]=makeAsset(0.3f);
        unloaded.assets[2]=makeAsset(0.1f);
        unloaded.bank=std::make_shared<PunchTailProcessor::Bank>();
        for(auto& note:unloaded.bank->notes) note=unloaded.assets[1]->sample;
        unloaded.prepareToPlay(44100,128);
        oneShot.addEvent(juce::MidiMessage::noteOn(1,48,1.0f),0);
        unloaded.processBlock(buffer,oneShot);
        check(unloaded.voices[0].punch && unloaded.voices[0].pitches && unloaded.voices[0].reverb,
              "Unloading test requires three active lanes");
        unloaded.unloadFile(1);
        check(!unloaded.asset(1) && !unloaded.previewBank(),"Tail was not unloaded or its pitch bank remained");
        check(unloaded.asset(0) && unloaded.asset(2),"Unloading tail deleted a different lane");
        unloaded.processBlock(buffer,oneShot);
        check(!unloaded.voices[0].pitches && unloaded.voices[0].punch && unloaded.voices[0].reverb,
              "Tail unload did not remove tail from active voice only");
        check(buffer.getSample(0,30)>0.01f,"Tail unload interrupted ongoing punch playback");
        unloaded.unloadFile(2);
        unloaded.processBlock(buffer,oneShot);
        check(!unloaded.voices[0].reverb && unloaded.voices[0].punch,
              "Reverb unload interrupted punch playback or retained reverb");
        unloaded.loadFile(2,juce::File("queued-reverb.wav"));
        check(unloaded.requests[2].pending,"Reverb replacement was not queued");
        unloaded.unloadFile(2);
        check(!unloaded.requests[2].pending,"Unload did not cancel pending reverb request");
        unloaded.loadFile(1,juce::File("queued-tail.wav"));
        unloaded.unloadFile(2);
        check(unloaded.requests[1].pending,"Unloading reverb cancelled a pending tail load");
        unloaded.unloadFile(1);
        juce::MemoryBlock noTailState;
        unloaded.getStateInformation(noTailState);
        auto noTailTree=juce::ValueTree::readFromData(noTailState.getData(),int(noTailState.getSize()));
        check(!noTailTree.getChildWithName("Tail").isValid() &&
              !noTailTree.getChildWithName("Reverb").isValid() &&
              noTailTree.getChildWithName("Punch").isValid(),
              "Unloaded sounds were still serialized or kept samples were lost");
        unloaded.unloadFile(0);
        unloaded.processBlock(buffer,oneShot);
        check(!unloaded.voices[0].punch && !unloaded.asset(0),
              "Unloading punch did not remove it from active voices");
        // Real worker integration: punch pitch must publish only new immutable
        // snapshots, preserve raw saved audio, and never rebuild the tail bank.
        auto pitchedPunchOwner=std::make_unique<LegacyTestProcessor>();auto& pitchedPunch=*pitchedPunchOwner;
        auto punchSource=std::make_shared<PunchTailProcessor::Asset>();
        punchSource->sample.rate=44100;punchSource->sample.left.resize(8820);
        for(size_t i=0;i<8820;++i)punchSource->sample.left[i]=float(.3*std::sin(2*pt::pi*220*i/44100.));
        punchSource->sample.right=punchSource->sample.left;
        auto independentBank=std::make_shared<PunchTailProcessor::Bank>();
        independentBank->prepared=true;
        for(auto& note:independentBank->notes)note=punchSource->sample;
        {const juce::ScopedLock lock(pitchedPunch.assetLock);
            pitchedPunch.assets[0]=punchSource;pitchedPunch.bank=independentBank;
        }
        pitchedPunch.prepareToPlay(44100,128);
        auto setPunch=[&](float value,int mode=0) {
            auto* modeParam=pitchedPunch.params.getParameter("punchPitchMode");
            auto* fidelityParam=pitchedPunch.params.getParameter("punchPitchFidelity"); // v0.4.63 mode 2
            fidelityParam->setValueNotifyingHost(mode==2?1.f:0.f);
            if(mode!=2)modeParam->setValueNotifyingHost(modeParam->convertTo0to1(float(mode)));
            auto* param=pitchedPunch.params.getParameter("punchPitch");
            param->setValueNotifyingHost(param->convertTo0to1(value));pitchedPunch.notify();
            bool ready=false;
            for(int i=0;i<5000;++i) {
                {const juce::ScopedLock lock(pitchedPunch.assetLock);
                    ready=pitchedPunch.preparedPunchSource==punchSource &&
                          std::abs(pitchedPunch.preparedPunchPitch-value)<.001 && pitchedPunch.preparedPunchMode==mode;
                }
                if(ready)break;juce::Thread::sleep(1);
            }
            check(ready,"Punch worker did not publish requested pitch");
        };
        setPunch(7);const auto plus=pitchedPunch.previewPunch();
        check(plus!=punchSource && plus->sample.left.size()==punchSource->sample.left.size(),"Punch preparation failed");
        check(pitchedPunch.asset(0)==punchSource && pitchedPunch.previewBank()==independentBank,"Punch pitch changed source or tail bank");
        juce::MidiBuffer notes;notes.addEvent(juce::MidiMessage::noteOn(1,39,1.f),0);
        pitchedPunch.processBlock(buffer,notes);
        const auto end=pitchedPunch.voices[0].punchEnd,tailStart=pitchedPunch.voices[0].tailStart;
        setPunch(-12);notes.addEvent(juce::MidiMessage::noteOn(1,40,1.f),0);
        pitchedPunch.processBlock(buffer,notes);
        check(!pitchedPunch.voices[0].active && pitchedPunch.voices[1].punch==pitchedPunch.previewPunch(),"Punch update altered a held voice");
        check(pitchedPunch.voices[1].punchEnd==end && pitchedPunch.voices[1].tailStart==tailStart,"Punch pitch changed join timing");
        setPunch(12,1);const auto resampled=pitchedPunch.previewPunch();
        check(resampled->sample.left.size()==4410 && resampled->punchTimeScale==.5,"Resample mode was not published");
        notes.addEvent(juce::MidiMessage::noteOn(1,41,1.f),0);pitchedPunch.processBlock(buffer,notes);
        check(std::abs(pitchedPunch.voices[2].punchEnd-.1)<1.e-9,"New voice did not adopt resampled length");
        check(!pitchedPunch.voices[1].active,"Switching note did not choke previous voice");
        check(pitchedPunch.previewBank()==independentBank,"Punch mode modified tail bank");
        setPunch(12,0);check(pitchedPunch.previewPunch()->sample.left.size()==8820,"Switching back did not rebuild aligned punch");
        {   // v0.4.63 Punch Highest Fidelity: fixed length, own renderer, held voices pinned.
            notes.clear();notes.addEvent(juce::MidiMessage::noteOn(1,39,1.f),0);pitchedPunch.processBlock(buffer,notes);
            size_t heldIndex=0;for(size_t v=0;v<pitchedPunch.voices.size();++v)if(pitchedPunch.voices[v].active)heldIndex=v;
            check(pitchedPunch.voices[heldIndex].active,"No voice for the held note");
            const auto held=pitchedPunch.voices[heldIndex].punch;
            setPunch(12,2);const auto fidelity=pitchedPunch.previewPunch();
            const auto expected=pt::renderPunchPitch(punchSource->sample,12,pt::PunchPitchMode::HighestFidelity);
            check(fidelity->sample.left.size()==8820 && fidelity->punchTimeScale==1. &&
                  fidelity->sample.left==expected.left && fidelity->sample.right==expected.right,"Highest Fidelity punch was not published");
            check(fidelity->sample.left!=pt::renderPunchPitch(punchSource->sample,12,pt::PunchPitchMode::StretchAligned).left,
                  "Highest Fidelity punch rendered like Stretch Aligned");
            check(pitchedPunch.voices[heldIndex].punch==held,"Punch mode change altered a held voice");
            check(pitchedPunch.previewBank()==independentBank,"Punch Highest Fidelity modified tail bank");
            setPunch(12,0);
        }
        // The immutable snapshot also owns the upper MIDI lane, so it must be
        // separate from the saved source. Zero manual pitch preserves samples exactly.
        for(int mode:{0,1,2}) {
            setPunch(0,mode);const auto neutral=pitchedPunch.previewPunch();
            check(neutral && neutral->sample.left==punchSource->sample.left &&
                  neutral->sample.right==punchSource->sample.right &&
                  neutral->sample.rate==punchSource->sample.rate && neutral->punchTimeScale==1.,
                  "Zero punch pitch changed original audio or timing");
            check(pitchedPunch.asset(0)==punchSource && pitchedPunch.previewBank()==independentBank,
                  "Zero punch pitch changed saved source or tail bank");
            for(int note=40;note<=51;++note) {
                check(bool(neutral->midiPunches[size_t(note-pt::kLowestPitchNote)]),"Zero manual pitch lost upper MIDI bank");
                const auto expected=pt::renderPunchPitch(punchSource->sample,
                    pt::punchPitchForNote(note,pitchedPunch.settings().root,0),
                    mode==2?pt::PunchPitchMode::HighestFidelity:mode==1?pt::PunchPitchMode::Resample:pt::PunchPitchMode::StretchAligned);
                check(neutral->forNote(note).sample.left==expected.left &&
                      neutral->forNote(note).sample.right==expected.right,
                      "Upper MIDI punch does not match selected pitch mode");
            }
            check(&neutral->forNote(39)==neutral.get() && &neutral->forNote(52)==neutral.get(),
                  "Manual-only keys unexpectedly select upper punch bank");
        }
        {   // v0.4.67 PUNCH FOLLOWS TAIL: OFF = v0.4.66 lane (E4..D#5 only); ON = the
            // punch is pitched with the tail on every pitched tail note (15..51).
            setPunch(2,0);const auto off=pitchedPunch.previewPunch();
            check(pitchedPunch.params.getRawParameterValue("punchFollowTail")->load()<.5f,"PUNCH FOLLOWS TAIL must default OFF");
            for(int note=pt::kLowestPitchNote;note<40;++note)
                check(!off->midiPunches[size_t(note-pt::kLowestPitchNote)] && &off->forNote(note)==off.get(),"OFF must not pitch the punch below E4");
            auto* follow=pitchedPunch.params.getParameter("punchFollowTail");
            follow->setValueNotifyingHost(1.f);pitchedPunch.notify();
            for(int i=0;i<20000;++i){{const juce::ScopedLock lock(pitchedPunch.assetLock);if(pitchedPunch.preparedPunchFollow&&pitchedPunch.preparedPunchSource==punchSource)break;}juce::Thread::sleep(1);}
            const auto on=pitchedPunch.previewPunch();const double root=pitchedPunch.settings().root;
            check(pitchedPunch.preparedPunchFollow && on!=off,"PUNCH FOLLOWS TAIL did not rebuild the punch");
            for(int note=pt::kLowestPitchNote;note<=pt::kHighestPitchNote;++note) {
                const auto expected=pt::renderPunchPitch(punchSource->sample,2+note-root,pt::PunchPitchMode::StretchAligned);
                check(on->forNote(note).sample.left==expected.left && on->forNote(note).sample.right==expected.right,"punch not pitched with the tail on this note");
                check(std::abs(pt::punchPitchForNote(note,root,2,true)-(2+note-root))<1e-12,"follow pitch formula");
            }
            for(int note=40;note<=51;++note)check(on->forNote(note).sample.left==off->forNote(note).sample.left,"E4..D#5 must be unchanged by PUNCH FOLLOWS TAIL");
            check(&on->forNote(pt::kLowestPitchNote-1)==on.get() && &on->forNote(pt::kHighestPitchNote+1)==on.get() && on->sample.left==off->sample.left,
                  "notes outside the tail range must keep the unpitched punch");
            check(!pt::followsPunchMidi(14,true)&&pt::followsPunchMidi(15,true)&&pt::followsPunchMidi(51,true)&&!pt::followsPunchMidi(52,true)&&
                  !pt::followsPunchMidi(39,false)&&pt::followsPunchMidi(40,false),"follow ranges");
            follow->setValueNotifyingHost(0.f);pitchedPunch.notify();
            for(int i=0;i<20000;++i){{const juce::ScopedLock lock(pitchedPunch.assetLock);if(!pitchedPunch.preparedPunchFollow)break;}juce::Thread::sleep(1);}
            check(!pitchedPunch.preparedPunchFollow && !pitchedPunch.previewPunch()->midiPunches[0],"turning PUNCH FOLLOWS TAIL off did not restore the lane");
            // Saved with the project (restored into a fresh instance).
            follow->setValueNotifyingHost(1.f);
            juce::MemoryBlock followState;pitchedPunch.getStateInformation(followState);
            {auto restoredOwner=std::make_unique<PunchTailProcessor>();auto& restored=*restoredOwner;restored.setStateInformation(followState.getData(),int(followState.getSize()));
             check(restored.params.getRawParameterValue("punchFollowTail")->load()>.5f,"PUNCH FOLLOWS TAIL state lost");}
            follow->setValueNotifyingHost(0.f);
        }
        setPunch(12,1);juce::MemoryBlock pitchState;pitchedPunch.getStateInformation(pitchState);
        pitchedPunch.setStateInformation(pitchState.getData(),int(pitchState.getSize()));
        check(pitchedPunch.params.getRawParameterValue("punchPitch")->load()==12,"Punch pitch state lost");
        check(pitchedPunch.params.getRawParameterValue("punchPitchMode")->load()==1,"Punch pitch mode state lost");
        auto legacyPunch=pitchedPunch.params.copyState();
        for(int i=legacyPunch.getNumChildren()-1;i>=0;--i)
            if(legacyPunch.getChild(i).getProperty("id").toString()=="punchPitch" || legacyPunch.getChild(i).getProperty("id").toString()=="punchPitchMode")legacyPunch.removeChild(i,nullptr);
        juce::MemoryBlock oldPunch;{juce::MemoryOutputStream out(oldPunch,false);legacyPunch.writeToStream(out);}
        pitchedPunch.setStateInformation(oldPunch.getData(),int(oldPunch.getSize()));
        check(pitchedPunch.params.getRawParameterValue("punchPitch")->load()==0,"Old state did not neutralize punch pitch");
        check(pitchedPunch.params.getRawParameterValue("punchPitchMode")->load()==0,"Old state did not restore aligned mode");
        {   // v0.4.63 Punch Highest Fidelity state + legacy migration.
            pitchedPunch.params.getParameter("punchPitchFidelity")->setValueNotifyingHost(1.f);
            juce::MemoryBlock hfState;pitchedPunch.getStateInformation(hfState);
            pitchedPunch.setStateInformation(hfState.getData(),int(hfState.getSize()));
            check(pitchedPunch.punchPitchModeValue()==2,"Punch Highest Fidelity state lost");
            auto old=pitchedPunch.params.copyState();
            for(int i=old.getNumChildren()-1;i>=0;--i)if(old.getChild(i).getProperty("id").toString()=="punchPitchFidelity")old.removeChild(i,nullptr);
            juce::MemoryBlock oldBlock;{juce::MemoryOutputStream out(oldBlock,false);old.writeToStream(out);}
            pitchedPunch.setStateInformation(oldBlock.getData(),int(oldBlock.getSize()));
            check(pitchedPunch.params.getRawParameterValue("punchPitchFidelity")->load()==0,"Pre-v0.4.63 projects must load without Punch Highest Fidelity");
            check(pitchedPunch.params.getParameter("punchPitchFidelity")->getParameterIndex()==pitchedPunch.params.getParameter("attackLayerLevel")->getParameterIndex()+1,
                  "punchPitchFidelity must be appended after every earlier parameter");
        }
        pitchedPunch.unloadFile(0);check(!pitchedPunch.previewPunch(),"Unloaded punch retained prepared audio");
        std::cout<<"PASS punch pitch worker, voice pinning, independent bank, fixed joins and state migration\n";
        std::cout<<"PASS per-lane unload, active note isolation, pending-request cancellation and project persistence\n";
        std::cout<<"PASS lock contention, MIDI delivery, pinned voices, stretch state, realtime reverb level/200-Hz slope automation\n";
    }
};
int main()try{juce::ScopedJuceInitialiser_GUI init;PunchTailPlaybackTest::run();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
