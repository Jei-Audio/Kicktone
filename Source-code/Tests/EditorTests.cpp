#include "../Source/KickExport.h"
#include "../Source/PluginEditor.h"
#include "../Source/ParallelRender.h"
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
struct PunchTailPlaybackTest {
    // v0.4.69: queued load requests (this processor's worker is stopped by setup()).
    static uint64_t requestGeneration(PunchTailProcessor& p,int slot){const juce::ScopedLock l(p.requestLock);return p.requestGenerations[size_t(slot)];}
    static juce::File pendingFile(PunchTailProcessor& p,int slot){const juce::ScopedLock l(p.requestLock);return p.requests[size_t(slot)].pending?p.requests[size_t(slot)].file:juce::File{};}
    static void clearRequest(PunchTailProcessor& p,int slot){const juce::ScopedLock l(p.requestLock);p.requests[size_t(slot)]={};}
    static void setup(PunchTailProcessor& p){
        p.signalThreadShouldExit();p.notify();p.stopThread(-1);
        for(int slot=0;slot<2;++slot){
            auto a=std::make_shared<PunchTailProcessor::Asset>();a->sample.rate=44100;a->name=slot?"Tail test":"Long punch + tail test";
            a->sample.left.resize(slot?88200:132301);a->sample.right.resize(a->sample.left.size());
            uint32_t seed=42;
            for(size_t i=0;i<a->sample.left.size();++i){double t=i/a->sample.rate;seed=1664525u*seed+1013904223u;
                float v=float(!slot && t<.04?(double(seed)/4294967295.*2-1)*.8:.75*std::sin(2*pt::pi*55*t));
                a->sample.left[i]=v;a->sample.right[i]=v;}
            a->punchEnd=pt::detectPunchEnd(a->sample);p.assets[size_t(slot)]=a;
        }
        auto reverb=std::make_shared<PunchTailProcessor::Asset>();
        reverb->sample=p.assets[1]->sample;reverb->name="Reverb test";
        p.assets[2]=reverb;
        p.bank=std::make_shared<PunchTailProcessor::Bank>();p.bank->prepared=true;p.bank->notes[36]=p.assets[1]->sample;
    }
};
// v0.4.66 LOCK SPLIT fixture: the same sample (600 ms: 40 ms noise punch, then
// a 55 Hz tail) in both lanes, as loading one kick into Punch and Tail does.
struct PunchTailSplitTest {
    static void pair(PunchTailProcessor& p,const char* tailBytes="same file") {
        pt::Sample w;w.rate=44100;uint32_t seed=9;
        for(int i=0;i<26460;++i){const double t=i/w.rate;seed=1664525u*seed+1013904223u;
            const float v=float(t<.04?(double(seed)/4294967295.*2-1)*.8:.7*std::sin(2*pt::pi*55*(t-.04))*std::exp(-(t-.04)*2));
            w.left.push_back(v);w.right.push_back(v);}
        const juce::ScopedLock l(p.assetLock);
        for(int slot=0;slot<2;++slot) {
            auto a=std::make_shared<PunchTailProcessor::Asset>();a->name="split.wav";a->sample=w;a->trimFirst=0;a->trimLast=26460;
            const char* bytes=slot?tailBytes:"same file";a->bytes=juce::MemoryBlock(bytes,std::strlen(bytes));
            if(!slot)a->punchEnd={50.0,.9,true};
            p.assets[size_t(slot)]=a;
        }
    }
};
struct PunchTailEditorTest {
    static juce::MouseEvent event(juce::Component& c,juce::Point<float> point,bool fine=false) {
        return {juce::Desktop::getInstance().getMainMouseSource(),point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|(fine?juce::ModifierKeys::shiftModifier:0)),
            1,0,0,0,0,&c,&c,juce::Time::getCurrentTime(),point,juce::Time::getCurrentTime(),1,true};
    }
    static void refresh(PunchTailEditor& e) {
        e.timerCallback();
        const auto deadline=juce::Time::getMillisecondCounter()+15000;
        const auto first=e.previewGeneration;
        while(e.completedKick.updating && juce::Time::getMillisecondCounter()<deadline){juce::Thread::sleep(1);e.timerCallback();}
        if(e.completedKick.updating)std::cerr<<"preview generations during wait: "<<(e.previewGeneration-first)<<"\n";
        check(!e.completedKick.updating,"Background waveform did not finish");
    }
    static void checkEffectLayout(PunchTailEditor& e) {
        check(e.punchRegionWave.isVisible() && e.punchRegionPanel.getX()>=e.completedKick.getRight() &&
              e.punchRegionPanel.getY()>=e.effectPanels[2].getBottom() && e.content.getLocalBounds().contains(e.punchRegionPanel),
              "Additional Punch region waveform must fit at bottom right below EQ");
        check(e.punchRegionPanel.contains(e.punchRegionWave.getBounds()) && e.punchRegionPanel.contains(e.regionTransition.getBounds()) &&
              e.punchRegionPanel.contains(e.attackGain.getBounds()) && e.punchRegionPanel.contains(e.decayGain.getBounds()) && !e.punchRegionPanel.intersects(e.keys.getBounds()) && !e.punchRegionPanel.intersects(e.statusLabel.getBounds()),
              "Punch region waveform/controls overlap or escape the panel");
        check(e.masterEnvelopePanel.getX()>e.punchRegionPanel.getRight() && e.masterEnvelopePanel.getY()==e.effectPanels[0].getY() && e.content.getLocalBounds().contains(e.masterEnvelopePanel),"Master envelope must sit in the new column beside Distortion"); // v0.4.64
        for(const auto& slider:e.masterEnvelopeSliders)check(slider.isVisible() && e.masterEnvelopePanel.contains(slider.getBounds()),"ADSR control escaped panel");
        check(!e.masterEnvelopePanel.intersects(e.keys.getBounds()),"ADSR overlaps keyboard");
        check(e.pitchDropPanel.getY()>e.masterEnvelopePanel.getBottom()&&e.content.getLocalBounds().contains(e.pitchDropPanel),"Pitch Drop panel does not fit");
        for(const auto& slider:e.pitchDropSliders)check(slider.isVisible()&&e.pitchDropPanel.contains(slider.getBounds()),"Pitch Drop knob escaped panel");
        check(e.pitchDropPanel.contains(e.pitchDropEnable.getBounds())&&e.pitchDropPanel.contains(e.pitchDropTarget.getBounds())&&!e.pitchDropPanel.intersects(e.keys.getBounds())&&!e.pitchDropPanel.intersects(e.statusLabel.getBounds()),"Pitch Drop controls overlap");
        check(e.generatedPanel.contains(e.generatedKeepDry.getBounds()),"Keep Dry escapes generated reverb panel");
        // v0.4.54: larger Experimental Reverb knobs, still inside their panel without overlaps.
        const auto checkReverbKnobs=[&](const auto& bank){for(size_t i=0;i<8;++i){
            const auto& knob=bank[i];
            if(i!=7)check(knob.slider.getHeight()>=82,"Experimental Reverb knobs were not enlarged");
            check(e.generatedPanel.contains(knob.slider.getBounds())&&e.generatedPanel.contains(knob.label.getBounds()),"Experimental Reverb knob escapes its panel");
            if(i<3)check(!knob.slider.getBounds().intersects(bank[i+5].label.getBounds()),"Experimental Reverb knob rows overlap");
        }};
        checkReverbKnobs(e.generatedControls);checkReverbKnobs(e.punchGeneratedControls);
        for(size_t i=8;i<12;++i){const auto& c=e.generatedControls[i];check(e.generatedPanel.contains(c.slider.getBounds())&&!c.label.getBounds().intersects(e.generatedControls[5].slider.getBounds()),"Reverb timing slider overlaps enlarged knobs");}
        check(!e.generatedPanel.intersects(e.statusLabel.getBounds())&&!e.generatedPanel.intersects(e.keys.getBounds()),"Experimental Reverb panel overlaps the status/keyboard");
        if(e.punchInvert.isVisible()){check(e.content.getLocalBounds().contains(e.punchInvert.getBounds()),"Punch polarity escapes editor");check(!e.punchInvert.getBounds().intersects(e.punchPitchMode.getBounds()) && !e.punchInvert.getBounds().intersects(e.controls[8].slider.getBounds()),"Punch polarity overlaps adjacent settings");}
        for(const auto& rect:e.effectPanels)check(rect.getX()>=e.completedKick.getRight() && e.content.getLocalBounds().contains(rect),"Effect panels must be visible beside the waveform");
        check(!e.effectPanels[0].intersects(e.effectPanels[1]),"Effect panels overlap");
        for(const auto& fx:e.distortion)for(const auto& knob:fx.knobs) {
            if(knob.slider.isVisible())check(e.effectPanels[0].contains(knob.slider.getBounds()),
                "Visible distortion control escapes its panel");
        }
        for(const auto& knob:e.transientControls) {
            if(knob.slider.isVisible())check(e.effectPanels[1].contains(knob.slider.getBounds()),
                "Visible transient control escapes its panel");
        }
    }
    // v0.4.66 LOCK SPLIT in the editor: with the same sample in both lanes the
    // Punch END and Tail START are one cut; every way of moving either marker
    // moves the other in the same host gesture; unlock = independent lanes.
    static void lockSplit(const char* snapshot) {
        auto owner=std::make_unique<PunchTailProcessor>();auto& p=*owner;PunchTailPlaybackTest::setup(p);
        PunchTailSplitTest::pair(p);
        auto get=[&](const char* id){return double(p.params.getRawParameterValue(id)->load());};
        auto setP=[&](const char* id,double v){auto* q=p.params.getParameter(id);q->setValueNotifyingHost(q->convertTo0to1(float(v)));};
        auto tailMs=[&]{return get("trim")+get("tailExtra");};
        auto punchMs=[&]{return get("punchOffset")+get("punchExtra");};
        const double D=600.0;
        // Punch ends at the cut; the Tail starts CROSSFADE ms before it.
        auto oneCut=[&](double cut,const char* m){check(std::abs((D-punchMs())-cut)<.051 && std::abs(tailMs()-(std::max)(0.0,cut-get("crossfade")))<.051,m);};
        auto editorOwner=std::make_unique<PunchTailEditor>(p);auto& e=*editorOwner;
        check(!p.lockSplitOn() && !e.lockSplitButton.getToggleState(),"LOCK SPLIT must default OFF");
        check(get("crossfade")==8,"crossfade default changed");
        setP("trim",0);setP("punchOffset",480);e.timerCallback();
        check(e.lockedPair && !e.lockedSplitActive() && e.punchWave.lockCaptionText().isEmpty() && tailMs()==0,"unlocked pair must stay independent");
        check(e.lockSplitButton.isVisible()&&e.lockSplitButton.getY()==e.punchDetectButton.getY()&&e.lockSplitButton.getX()>e.pitchLabel.getRight()&&
              e.lockSplitButton.getRight()<e.detectButton.getX()&&e.content.getLocalBounds().contains(e.lockSplitButton.getBounds()),"LOCK SPLIT button placement");
        e.lockSplitButton.setToggleState(true,juce::sendNotificationSync);
        check(p.lockSplitOn(),"LOCK SPLIT button is not bound to the lockSplit parameter");
        e.timerCallback();
        oneCut(120,"engaging the lock did not join the Tail START to the Punch END");
        check(e.punchWave.lockCaptionText().contains("CUT 120.0") && e.tailWave.lockCaptionText()==e.punchWave.lockCaptionText(),"locked caption missing");
        e.timerCallback();
        check(std::abs(e.punchWave.guideMs-(D-120))<.051 && std::abs(e.tailWave.guideMs-112)<.051,"markers are not at the shared cut");
        if(snapshot){refresh(e);juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-lock.png")};
            out.setPosition(0);out.truncate();check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render lock snapshot");}
        // Tail START 10 ms LEFT -> Punch END 10 ms left, in ONE gesture over all four trims.
        e.tailWave.onGesture(true);
        check(e.gestureParams.size()==4,"a locked drag must bracket all four trim parameters");
        e.tailWave.onTrim(102);oneCut(110,"Punch END did not follow the Tail START");
        e.tailWave.onGesture(false);check(e.gestureParams.empty(),"gesture not closed");
        e.timerCallback();check(std::abs(e.punchWave.guideMs-(D-110))<.051 && e.punchWave.lockCaptionText().contains("110.0"),"Punch marker / caption did not follow");
        // Punch END 20 ms right -> Tail START follows.
        e.punchWave.onGesture(true);e.punchWave.onTrim(D-130);e.punchWave.onGesture(false);oneCut(130,"Tail START did not follow the Punch END");
        // Real mouse drag on the Tail waveform moves both.
        e.timerCallback();
        {auto& w=e.tailWave;const auto plot=w.plotArea().toFloat();const float x=plot.getX()+plot.getWidth()*float(122/D);
         w.mouseDown(event(w,{x,60}));w.mouseDrag(event(w,{x-float(plot.getWidth()*20/D),60}));w.mouseUp(event(w,{x-float(plot.getWidth()*20/D),60}));
         check(D-punchMs()<120 && D-punchMs()>100,"mouse drag on the locked Tail did not move the Punch END left");oneCut(D-punchMs(),"mouse drag broke the shared cut");}
        // Limits: >= 3 ms of punch, >= 40 ms of tail.
        e.punchWave.onTrim(D);oneCut(3,"cut not clamped at the punch minimum");
        e.tailWave.onTrim(D);oneCut(D-40,"cut not clamped at the tail minimum");
        // PUNCH END knob, Final Kick END handle and Detect Punch End move the cut too.
        e.controls[3].slider.setValue(D-200,juce::sendNotificationSync);oneCut(200,"PUNCH END knob did not move the shared cut");
        e.controls[2].slider.setValue(150,juce::sendNotificationSync);oneCut(158,"TAIL OFFSET knob did not move the shared cut");
        e.completedKick.onGesture(pt::KickHandle::punchEnd,true);check(e.gestureParams.size()==4,"Final Kick END gesture must cover all four trims");
        e.completedKick.onEdit(pt::KickHandle::punchEnd,D-90);e.completedKick.onGesture(pt::KickHandle::punchEnd,false);oneCut(90,"Final Kick END handle did not move the shared cut");
        e.punchDetectButton.onClick();oneCut(50,"Detect Punch End did not return the shared cut to the detected cut");
        // A CROSSFADE change keeps the cut and moves the Tail START with it.
        setP("crossfade",12);e.timerCallback();oneCut(50,"CROSSFADE change did not re-align the locked Tail START");
        check(std::abs(tailMs()-38)<.051,"Tail START is not CROSSFADE before the cut");
        setP("crossfade",8);e.timerCallback();oneCut(50,"CROSSFADE reset did not re-align");
        // Unlock: independent lanes again.
        setP("lockSplit",0);e.timerCallback();
        check(!e.lockedSplitActive() && e.tailWave.lockCaptionText().isEmpty(),"unlock did not clear the lock");
        const double punchBefore=punchMs();
        e.tailWave.onGesture(true);check(e.gestureParams.size()==2,"unlocked drag must cover only its own lane");
        e.tailWave.onTrim(70);e.tailWave.onGesture(false);
        check(std::abs(tailMs()-70)<.051 && punchMs()==punchBefore,"unlocked Tail START must trim only its own lane");
        // Different files in the two lanes are never linked.
        setP("lockSplit",1);PunchTailSplitTest::pair(p,"another file");e.timerCallback();
        check(!e.lockedPair && !e.lockedSplitActive() && e.tailWave.lockCaptionText().isEmpty(),"different files must not lock");
        e.tailWave.onTrim(80);check(std::abs(tailMs()-80)<.051 && punchMs()==punchBefore,"different files: trims must stay independent");
        std::cout<<"PASS LOCK SPLIT editor: shared cut both ways, one gesture, mouse drag, clamps, knobs, Final Kick handle, Detect, unlock, different files\n";
    }
    static void run(const char* snapshot) {
        {
            PunchTailKickDisplay d;d.setSize(810,225);
            pt::Sample p;p.left.assign(4800,.2f);p.right=p.left;
            d.setAudio(&p,&p,pt::Settings{},36,true);
            std::vector<std::pair<bool,bool>> outputGestures;std::vector<std::pair<bool,double>> outputEdits;
            d.onOutputGesture=[&](bool tail,bool begin){outputGestures.emplace_back(tail,begin);};
            d.onOutputEdit=[&](bool tail,double db){outputEdits.emplace_back(tail,db);};
            const auto plot=d.plotBounds();
            d.mouseDown(event(d,{plot.getX()+plot.getWidth()*.1f,65}));
            d.mouseDrag(event(d,{plot.getX()+plot.getWidth()*.1f,55}));
            d.mouseUp(event(d,{plot.getX()+plot.getWidth()*.1f,55}));
            check(outputEdits.size()==1 && !outputEdits[0].first && std::abs(outputEdits[0].second-2)<.001 && outputGestures.size()==2 && outputGestures[0]==std::make_pair(false,true) && outputGestures[1]==std::make_pair(false,false),"Orange waveform drag must edit punch post-effects output and bracket a host gesture");
            d.mouseDown(event(d,{plot.getRight()-10,65}));
            d.mouseDrag(event(d,{plot.getRight()-10,55}));
            d.mouseUp(event(d,{plot.getRight()-10,55}));
            check(outputEdits.size()==2 && outputEdits[1].first && std::abs(outputEdits[1].second-2)<.001,"White waveform drag must edit tail post-effects output");
            int calls=0;double edited=-1;pt::KickHandle selected{};
            d.onEdit=[&](pt::KickHandle h,double v){++calls;selected=h;edited=v;};
            d.joinControls[0].setValue(3.5,juce::sendNotificationSync);
            check(calls==1 && selected==pt::KickHandle::tailStart && edited==3.5,"Direct join millisecond editor");
            d.plusButtons[0].onClick();
            check(std::abs(d.joinControls[0].getValue()-3.6)<1e-6,"Fine increment");
            d.resetButtons[0].onClick();check(d.joinControls[0].getValue()==0,"Join reset");
            check(d.joinControls.size()==2 && d.joinLabels[0].getText().startsWith("TAIL JOIN"),"Punch End must be absent from final waveform table");
            const int previousCalls=calls;
            d.mouseDown(event(d,{40,70}));d.mouseDrag(event(d,{60,70}));d.mouseUp(event(d,{60,70}));
            check(calls==previousCalls,"Waveform must not contain hidden editing handles");
            d.zoomBox.setSelectedId(4,juce::sendNotificationSync);
            check(d.zoom==8 && d.scroll.isEnabled(),"Waveform zoom and scroll");
            d.scroll.setCurrentRangeStart(10,juce::sendNotificationSync);
            check(d.viewStart==10,"Waveform horizontal scrolling");
            for(const auto& c:d.joinControls)check(d.getLocalBounds().contains(c.getBounds()),"Join controls clipped");
        }
        lockSplit(snapshot);
        auto processor=std::make_unique<PunchTailProcessor>();auto& p=*processor;PunchTailPlaybackTest::setup(p);
        auto editor=std::make_unique<PunchTailEditor>(p);auto& e=*editor;
        check(e.reverbMinimized && !e.reverbWave.isVisible() && e.reverbFold.isVisible(),"Reverb must start minimized");
        const int minimizedPunchWidth=e.punchWave.getWidth();
        e.reverbFold.onClick();check(!e.reverbMinimized && e.reverbWave.isVisible() && e.punchWave.getWidth()<minimizedPunchWidth,"Reverb restore must return its waveform and redistribute width");
        e.reverbFold.onClick();
        {auto reopenedOwner=std::make_unique<PunchTailEditor>(p);auto& reopened=*reopenedOwner;check(reopened.reverbMinimized && !reopened.reverbWave.isVisible(),"Reverb collapse must survive editor reopen");}
        auto value=[&](const char* id){return p.params.getRawParameterValue(id)->load();};
        // v0.5.13: the Experimental Reverb panel minimizes to its title row (+ ENABLE).
        {check(!e.generatedMinimized&&e.generatedFold.isVisible()&&e.generatedFold.getButtonText()=="- EXPERIMENTAL REVERB"&&e.generatedPanel.getHeight()==e.generatedPanelHeight,"Experimental Reverb must start expanded");
         check(e.generatedPanel.contains(e.generatedFold.getBounds())&&e.generatedKeepDry.isVisible()&&e.generatedControls[0].slider.isVisible()&&!e.generatedTitle.isVisible(),"Experimental Reverb fold button / controls");
         const int expandedHeight=e.baseHeight;
         e.generatedFold.onClick();
         check(e.generatedMinimized&&e.generatedPanel.getHeight()==e.generatedMinimizedHeight&&e.generatedFold.getButtonText()=="+ EXPERIMENTAL REVERB","Experimental Reverb did not minimize");
         check(e.generatedEnable.isVisible()&&e.generatedPanel.contains(e.generatedEnable.getBounds())&&!e.generatedEnable.getBounds().intersects(e.generatedFold.getBounds()),"Minimized Experimental Reverb must keep ENABLE in its title row");
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.generatedKeepDry,&e.generatedDistortWet,&e.generatedPunchSend,&e.generatedTailSend,&e.generatedRootPitch,&e.wetAutoMatch,&e.wetPhaseLock,
                 &e.generatedEQButton,&e.generatedTailSettings,&e.generatedPunchSettings,&e.generatedEngine,&e.generatedEngineLabel,&e.verbDuckControls[0].slider,&e.verbDuckControls[1].slider})
             check(!c->isVisible(),"A control of the minimized Experimental Reverb is still shown");
         for(const auto& c:e.generatedControls)check(!c.slider.isVisible()&&!c.label.isVisible(),"A reverb knob of the minimized panel is still shown");
         for(const auto& c:e.punchGeneratedControls)check(!c.slider.isVisible(),"A Punch reverb knob of the minimized panel is still shown");
         check(e.baseHeight<=expandedHeight&&!e.generatedPanel.intersects(e.statusLabel.getBounds())&&e.content.getLocalBounds().contains(e.generatedPanel),"Minimized panel layout");
         check(bool(p.params.state.getProperty("uiGeneratedMinimized",false)),"Minimized Experimental Reverb not saved with the project");
         e.generatedEnable.setToggleState(true,juce::sendNotificationSync);check(value("tailVerbOn")==1,"ENABLE must work while minimized");
         e.generatedEnable.setToggleState(false,juce::sendNotificationSync);e.setParameter("muteReverb",1);   // ENABLE unmuted the Reverb layer; restore the fresh state
         e.generatedPunchSettings.onClick();check(!e.punchGeneratedControls[0].slider.isVisible()&&!e.generatedControls[8].slider.isVisible(),"Changing the reverb page reopened the minimized panel");
         e.generatedTailSettings.onClick();
         {auto reopenedOwner=std::make_unique<PunchTailEditor>(p);auto& r=*reopenedOwner;r.timerCallback();
          check(r.generatedMinimized&&!r.generatedControls[0].slider.isVisible()&&r.generatedPanel.getHeight()==r.generatedMinimizedHeight,"Reopened editor forgot the minimized Experimental Reverb");}
         e.generatedFold.onClick();
         check(!e.generatedMinimized&&e.generatedKeepDry.isVisible()&&e.generatedControls[0].slider.isVisible()&&!e.punchGeneratedControls[0].slider.isVisible()&&e.generatedRootPitch.isVisible()&&
               e.generatedPanel.getHeight()==e.generatedPanelHeight&&e.baseHeight==expandedHeight,"Experimental Reverb did not restore");
         check(!bool(p.params.state.getProperty("uiGeneratedMinimized",true)),"Restored Experimental Reverb not saved");}
        e.pitchDropEnable.setToggleState(true,juce::sendNotificationSync);check(value("pitchDropOn")==1,"Pitch Drop enable not host-bound");
        e.pitchDropSliders[0].setValue(18,juce::sendNotificationSync);check(value("pitchDropAmount")==18,"Pitch Drop amount not host-bound");
        e.pitchDropSliders[1].setValue(120,juce::sendNotificationSync);check(value("pitchDropTime")==120,"Pitch Drop time not host-bound");
        e.pitchDropSliders[2].setValue(2,juce::sendNotificationSync);check(value("pitchDropCurve")==2,"Pitch Drop curve not host-bound");
        e.pitchDropTarget.setSelectedId(3,juce::sendNotificationSync);check(value("pitchDropTarget")==2,"Pitch Drop target not host-bound");
        e.qualityMode.setSelectedId(5,juce::sendNotificationSync);e.timerCallback();check(e.pitchDropEnable.isEnabled(),"Highest Fidelity did not enable drop UI");
        e.qualityMode.setSelectedId(4,juce::sendNotificationSync);e.timerCallback();check(!e.pitchDropEnable.isEnabled()&&!e.pitchDropSliders[0].isEnabled(),"Adaptive enabled drop UI");
        e.qualityMode.setSelectedId(5,juce::sendNotificationSync);
        e.pitchDropEnable.setToggleState(false,juce::sendNotificationSync);
        e.pitchDropSliders[0].setValue(12,juce::sendNotificationSync);e.pitchDropSliders[1].setValue(60,juce::sendNotificationSync);e.pitchDropSliders[2].setValue(1,juce::sendNotificationSync);e.pitchDropTarget.setSelectedId(2,juce::sendNotificationSync);
        auto& rootSlider=e.controls[0].slider;
        check(rootSlider.getMinimum()==24 && rootSlider.getMaximum()==39 &&
              rootSlider.getInterval()==1,"Tail Root editor range is not C2-D#3");
        check(rootSlider.getTextFromValue(24).startsWith("C2 /") &&
              rootSlider.getTextFromValue(39).startsWith("D#3 /"),
              "Tail Root labels do not use FL Studio octaves");
        rootSlider.setValue(0,juce::sendNotificationSync);
        check(value("root")==24,"Tail Root slider allowed a note below C2");
        rootSlider.setValue(127,juce::sendNotificationSync);
        check(value("root")==39,"Tail Root slider allowed a note above D#3");
        rootSlider.setValue(36,juce::sendNotificationSync);
        check(value("pitchCharacter")==1 && value("pitchExtreme")==1 && value("phaseAuto")==1,
              "New projects must default to Highest Fidelity v4 and Auto Match");
        e.timerCallback();check(e.qualityMode.getSelectedId()==5,"Engine menu must show Highest Fidelity v4 by default");
        // v0.4.54 test controls.
        e.selectLayer(1);e.timerCallback();
        // v0.4.54: CRUNCH (Auto Crunch Match) + character replace FORMANT.
        check(e.formantAmount.isVisible()&&e.formantLabel.isVisible()&&e.crunchCharacter.isVisible()&&e.formantLabel.getText()=="CRUNCH","CRUNCH hidden on Highest Fidelity v4");
        e.formantAmount.setValue(60,juce::sendNotificationSync);check(std::abs(value("crunchMix")-60)<.2&&value("crunchMatch")==0&&value("formantPreserve")==0,"Crunch MIX not host-bound");
        check(e.crunchLowCut.isVisible()&&e.crunchLowCut.getMinimum()==2000&&e.crunchLowCut.getMaximum()==10000,"Crunch LOW CUT missing or wrong range");
        e.crunchLowCut.setValue(7000,juce::sendNotificationSync);check(std::abs(value("crunchLowCut")-7000)<2,"Crunch LOW CUT not host-bound");
        e.crunchLowCut.setValue(2000,juce::sendNotificationSync);
        for(juce::Component* c:std::initializer_list<juce::Component*>{&e.crunchLowCut,&e.crunchLowCutLabel})
            for(juce::Component* d:std::initializer_list<juce::Component*>{&e.formantAmount,&e.crunchCharacter,&e.formantLabel,&e.qualityMode,&e.completedKick,&e.adaptiveLabel})
                check(!c->getBounds().intersects(d->getBounds()),"Crunch LOW CUT overlaps nearby controls");
        e.crunchCharacter.setSelectedId(3,juce::sendNotificationSync);check(value("crunchCharacter")==2,"Crunch character not host-bound");
        for(juce::Component* c:std::initializer_list<juce::Component*>{&e.formantLabel,&e.formantAmount,&e.crunchCharacter})
            for(juce::Component* d:std::initializer_list<juce::Component*>{&e.qualityMode,&e.adaptiveLabel,&e.adaptiveAmount})
                check(!c->getBounds().intersects(d->getBounds()),"CRUNCH controls overlap the engine row");
        check(!e.formantLabel.getBounds().intersects(e.formantAmount.getBounds())&&!e.formantAmount.getBounds().intersects(e.crunchCharacter.getBounds()),"CRUNCH controls overlap each other");
        // ADAPTIVE AMOUNT label sits right next to its knob.
        check(e.adaptiveLabel.getJustificationType()==juce::Justification::centredRight&&e.adaptiveAmount.getX()-e.adaptiveLabel.getRight()<=6&&e.adaptiveAmount.getX()>=e.adaptiveLabel.getRight(),"Adaptive Amount label not next to its knob");
        {const auto layout=e.adaptiveAmount.getLookAndFeel().getSliderLayout(e.adaptiveAmount);check(layout.sliderBounds.getX()<=12,"Adaptive knob drawn far from its label");}
        e.formantAmount.setValue(0,juce::sendNotificationSync);e.crunchCharacter.setSelectedId(1,juce::sendNotificationSync);
        for(size_t i=0;i<2;++i){const auto& c=e.verbDuckControls[i];
            check(c.slider.isVisible()&&e.generatedPanel.contains(c.slider.getBounds())&&e.generatedPanel.contains(c.label.getBounds()),"Punch duck control missing or outside the reverb panel");
            for(size_t k=0;k<8;++k){check(!c.slider.getBounds().intersects(e.generatedControls[k].slider.getBounds())&&!c.slider.getBounds().intersects(e.punchGeneratedControls[k].slider.getBounds()),"Punch duck overlaps reverb knobs");}}
        // v0.4.54 Root Render reverb alignment switches (Tail page only).
        e.showGeneratedSettings(false);e.timerCallback();
        check(e.wetAutoMatch.isVisible()&&e.wetPhaseLock.isVisible()&&e.wetAutoMatch.getToggleState()&&!e.wetPhaseLock.getToggleState(),"Wet auto match/phase lock switches missing or wrong defaults");
        check(e.generatedPanel.contains(e.wetAutoMatch.getBounds())&&e.generatedPanel.contains(e.wetPhaseLock.getBounds())&&
              !e.wetAutoMatch.getBounds().intersects(e.generatedRootPitch.getBounds())&&!e.wetPhaseLock.getBounds().intersects(e.wetAutoMatch.getBounds()),"Wet alignment switches overlap or escape the panel");
        e.wetPhaseLock.setToggleState(true,juce::sendNotificationSync);check(value("tailVerbPhaseLock")==1,"Phase lock not host-bound");
        e.wetPhaseLock.setToggleState(false,juce::sendNotificationSync);
        e.showGeneratedSettings(true);check(!e.wetAutoMatch.isVisible()&&!e.wetPhaseLock.isVisible(),"Wet alignment switches shown on the Punch page");
        e.showGeneratedSettings(false);
        e.verbDuckControls[0].slider.setValue(70,juce::sendNotificationSync);check(std::abs(value("verbPunchDuck")-70)<.2,"Punch duck not host-bound");
        e.verbDuckControls[0].slider.setValue(0,juce::sendNotificationSync);
        check(value("punchPitchMode")==1 && value("uiScale")==75,
              "Resample and 75% UI scale must be fresh-instance defaults");
        check(e.baseHeight==1244 && e.getWidth()==juce::roundToInt(PunchTailEditor::baseWidth*.75) && e.getHeight()==juce::roundToInt(1244*.75),
              "New editor did not open at the 75% default scale");
        check(e.punchPitchMode.getSelectedId()==2 && e.uiScaleBox.getSelectedId()==75,
              "UI selectors did not display the Resample and 75% defaults");
        // v0.5.22: the stored 75 (default size) reads "100%"; same sizes, relabelled x4/3.
        {const char* shown[]={"67%","100%","133%","167%","200%","267%"};
         check(e.uiScaleBox.getNumItems()==6 && e.uiScaleBox.getText()=="100%","Default UI size is not shown as 100%");
         for(int i=0;i<6;++i)check(e.uiScaleBox.getItemText(i)==shown[i],"UI size menu text is not relative to the default size");
         auto* scaleParam=p.params.getParameter("uiScale");
         check(scaleParam->getCurrentValueAsText()=="100%" && std::abs(scaleParam->convertFrom0to1(scaleParam->getValueForText("100%"))-75.f)<.01f,"Host text of the UI size is not relative to the default size");}
        // v0.5.24: open no larger than the screen (host instances only), never below 50 %.
        {const auto fit=[](double wanted,int h,juce::Rectangle<int> area){return PunchTailEditor::screenFitScale(wanted,h,area);};
         const int W=PunchTailEditor::baseWidth,mx=PunchTailEditor::screenMarginX,my=PunchTailEditor::screenMarginY;
         check(fit(.75,1244,{0,0,3840,2100})==.75 && fit(2.0,1244,{0,0,6000,4000})==2.0,"Screen fit shrank a window that fits");
         check(std::abs(fit(.75,1244,{0,0,1536,824})-double(824-my)/1244)<1.e-12,"Screen fit is not height-limited on a 1080p / 125 % laptop");
         check(std::abs(fit(.75,600,{0,0,1536,824})-double(1536-mx)/W)<1.e-12,"Screen fit is not width-limited");
         check(fit(.75,1244,{0,0,800,600})==.5 && fit(.75,1244,{})==.75,"Screen fit must stop at 50 % and ignore an unknown screen");
         const auto fitted=fit(.75,1244,{100,40,1536,824});
         check(juce::roundToInt(W*fitted)<=1536-mx && juce::roundToInt(1244*fitted)<=824-my,"Fitted window does not fit");
         check(e.fitToScreen(.75)==.75 && e.fitToScreen(1.37)==1.37,"Test editors must keep the stored size (no screen fit)");
         e.setUIScale(.6);
         check(e.uiScaleBox.getSelectedId()==0 && e.uiScaleBox.getText()=="80%" && value("uiScale")==60,"A size between presets must be shown as text");
         e.setUIScale(.75);
         check(e.uiScaleBox.getSelectedId()==75 && e.uiScaleBox.getText()=="100%" && value("uiScale")==75,"UI size preset not restored");}
        e.selectLayer(1);
        check(e.qualityMode.isVisible(),"Tail pitch quality switch is hidden");
        // v0.4.61 menu: Legacy v1, the old High Fidelity and Extreme were removed.
        e.timerCallback();
        check(e.qualityMode.getNumItems()==3&&e.qualityMode.getItemText(0)=="Adaptive Stretch v4"&&e.qualityMode.getItemText(1)=="Highest Fidelity v4"&&e.qualityMode.getItemText(2)=="Extreme v5"&&
              e.qualityMode.indexOfItemId(1)<0&&e.qualityMode.indexOfItemId(2)<0&&e.qualityMode.indexOfItemId(3)<0,
              "Engine menu must list Adaptive Stretch v4, Highest Fidelity v4 and Extreme v5");
        // Old projects/automation that set a removed engine still show Highest Fidelity v4.
        {auto* q=e.proc.params.getParameter("pitchQuality");q->setValueNotifyingHost(q->convertTo0to1(1));}
        {auto* x=e.proc.params.getParameter("pitchCharacter");x->setValueNotifyingHost(0);}
        e.timerCallback();
        check(e.qualityMode.getSelectedId()==5&&e.qualityMode.getNumItems()==3&&value("pitchAdaptive")==0,"Removed-engine values changed the engine menu");
        // v0.4.68 Extreme v5: menu item 6 <-> pitchExtremeV5, default OFF, wins over Adaptive, Pitch Drop usable, Adaptive Amount off.
        check(value("pitchExtremeV5")==0,"Extreme v5 must default OFF");
        e.qualityMode.setSelectedId(6,juce::sendNotificationSync);e.timerCallback();
        check(value("pitchExtremeV5")==1&&value("pitchAdaptive")==0&&e.proc.tailEngine()==5&&e.qualityMode.getSelectedId()==6,"Extreme v5 menu choice not bound");
        check(e.pitchDropEnable.isEnabled()&&!e.adaptiveAmount.isEnabled(),"Extreme v5 must allow Pitch Drop and disable Adaptive Amount");
        {auto* x=e.proc.params.getParameter("pitchAdaptive");x->setValueNotifyingHost(1);}e.timerCallback();
        check(e.proc.tailEngine()==5&&e.qualityMode.getSelectedId()==6,"Extreme v5 must win over an automated Adaptive switch");
        e.qualityMode.setSelectedId(5,juce::sendNotificationSync);e.timerCallback();
        check(value("pitchExtremeV5")==0&&value("pitchAdaptive")==0&&e.proc.tailEngine()==4,"Leaving Extreme v5 did not clear both switches");
        e.aboutButton.onClick();e.refreshAboutInfo();e.qualityMode.setSelectedId(6,juce::sendNotificationSync);e.timerCallback();e.refreshAboutInfo();
        check(e.aboutPage.allText().contains("Extreme v5"),"About page does not report the Extreme v5 engine");
        e.aboutPage.backButton.onClick();e.qualityMode.setSelectedId(5,juce::sendNotificationSync);e.timerCallback();
        {auto* x=e.proc.params.getParameter("pitchCharacter");x->setValueNotifyingHost(1);}
        e.qualityMode.setSelectedId(4,juce::sendNotificationSync);e.timerCallback();
        check(value("pitchAdaptive")==1&&e.adaptiveAmount.isEnabled()&&e.adaptiveAmount.isVisible(),"Adaptive engine/knob not bound");
        e.adaptiveAmount.setValue(73,juce::sendNotificationSync);check(value("adaptiveAmount")==73,"Adaptive amount not host-bound");
        e.adaptiveAmount.setValue(400,juce::sendNotificationSync);check(value("adaptiveAmount")==400,"Adaptive Amount UI still stops at 100%");
        // v0.4.54: Punch layer and Glide are clear, unticked opt-in switches.
        check(!e.punchLayer.getToggleState()&&!e.glide.getToggleState()&&!e.punch2Load.isVisible()&&!e.punch2Unload.isVisible(),"Punch layer/Glide must start unticked with Punch 2 hidden");
        e.timerCallback();check(!e.glideTime.isEnabled(),"Glide time active while Glide is off");
        // v0.5.25: glide is always stretched; the STRETCHED switch is gone from the header.
        {bool found=false;for(auto* c:e.content.getChildren())if(auto* b=dynamic_cast<juce::Button*>(c))found=found||b->getButtonText()=="STRETCHED";
         check(!found&&value("glideStretched")==1,"STRETCHED switch must be removed (glide is always stretched)");}
        {const int before=e.baseHeight;
         e.punchLayer.setToggleState(true,juce::sendNotificationSync);e.timerCallback();
         check(value("punchLayer")==1&&e.punch2Load.isVisible()&&e.punch2Unload.isVisible()&&e.punch2Name.isVisible(),"Ticking Punch layer did not show Punch 2");
         check(e.punch2Offset.isVisible()&&e.punch2Offset.getMinimum()==-25&&e.punch2Offset.getMaximum()==25,"Punch 2 offset slider missing or wrong range");
         e.punch2Offset.setValue(7.5,juce::sendNotificationSync);check(std::abs(value("punch2Offset")-7.5)<.02,"Punch 2 offset not host-bound");
         // v0.4.67 Punch 2 TUNE: -12..+12 st, host-bound, double-click resets.
         check(e.punch2Pitch.isVisible()&&e.punch2PitchLabel.isVisible()&&e.punch2Pitch.getMinimum()==-12&&e.punch2Pitch.getMaximum()==12&&value("punch2Pitch")==0,"Punch 2 tune slider missing or wrong range");
         e.punch2Pitch.setValue(-3.25,juce::sendNotificationSync);check(std::abs(value("punch2Pitch")+3.25)<.006,"Punch 2 tune not host-bound");
         check(e.punch2Pitch.getTextFromValue(e.punch2Pitch.getValue()).contains("st"),"Punch 2 tune shows no semitone unit");
         e.punch2Pitch.setValue(0,juce::sendNotificationSync);
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.punch2Name,&e.punch2OffsetLabel,&e.punch2Offset,&e.punch2PitchLabel,&e.punch2Pitch,&e.punch2Load})
             for(juce::Component* d:std::initializer_list<juce::Component*>{&e.punch2Name,&e.punch2OffsetLabel,&e.punch2Offset,&e.punch2PitchLabel,&e.punch2Pitch,&e.punch2Load,&e.punch2Unload})
                 if(c!=d)check(!c->getBounds().intersects(d->getBounds()),"Punch 2 row controls overlap");
         check(e.punch2Panel.contains(e.punch2Pitch.getBounds())&&e.punch2Panel.contains(e.punch2Unload.getBounds()),"Punch 2 row controls outside the panel");
         // v0.4.69: the Punch 2 row is a drop target: routing, hover outline, a real dropped file loads into Punch 2 only.
         {const auto scaled=[&](juce::Point<int> q){return juce::Point<int>(juce::roundToInt(float(q.x)*e.uiScale),juce::roundToInt(float(q.y)*e.uiScale));};
          for(juce::Component* c:std::initializer_list<juce::Component*>{&e.punch2Title,&e.punch2Name,&e.punch2Offset,&e.punch2Pitch,&e.punch2Load,&e.punch2Unload})
              check(e.dropSlotAt(c->getBounds().getCentreX(),c->getBounds().getCentreY())==3,"Punch 2 row control is not a Punch 2 drop target");
          check(e.dropSlotAt(e.punch2Panel.getX()+3,e.punch2Panel.getY()+2)==3&&e.dropSlotAt(e.punch2Panel.getRight()-3,e.punch2Panel.getBottom()-2)==3,"Punch 2 row edges are not drop targets");
          for(int i=0;i<3;++i){const auto c=e.sourcePanels[size_t(i)].getCentre();
              check(!e.punch2Panel.contains(c)&&e.dropSlotAt(c.x,c.y)==i,"Punch/Tail/Reverb drop routing changed");}
          if(e.completedKick.isVisible())check(!e.completedKick.getBounds().intersects(e.punch2Panel)&&e.dropSlotAt(e.completedKick.getBounds().getCentreX(),e.completedKick.getBounds().getCentreY())!=3&&
              e.dropSlotAt(e.completedKick.getBounds().getCentreX(),e.completedKick.getBounds().getCentreY())<=2,"Final Kick drop must load an ordinary lane (v0.5.13: no AUTO-SPLIT)");
          const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tt-drop-punch2.wav");
          {juce::WavAudioFormat wav;auto stream=std::make_unique<juce::FileOutputStream>(file);stream->setPosition(0);stream->truncate();
           std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(stream.release(),44100,2,24,{},0));
           check(bool(w),"Cannot write Punch 2 drop WAV");juce::AudioBuffer<float> b(2,2205);
           for(int i=0;i<2205;++i){b.setSample(0,i,.3f*std::sin(float(i)*.1f));b.setSample(1,i,.3f*std::sin(float(i)*.1f));}
           w->writeFromAudioSampleBuffer(b,0,2205);}
          const juce::StringArray files{file.getFullPathName()};
          const auto mid=scaled(e.punch2Panel.getCentre()),tailMid=scaled(e.sourcePanels[1].getCentre());
          check(!e.punch2DropHover,"Punch 2 drop highlight on before any drag");
          e.fileDragEnter(files,mid.x,mid.y);check(e.punch2DropHover,"No drop highlight over the Punch 2 row");
          if(snapshot){e.selectLayer(0);e.timerCallback();{const auto here=scaled(e.punch2Panel.getCentre());e.fileDragEnter(files,here.x,here.y);check(e.punch2DropHover,"No drop highlight on the Punch page");}
              juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-punch2-drop.png")};
              out.setPosition(0);out.truncate();check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render Punch 2 drop snapshot");
              e.selectLayer(1);e.timerCallback();e.fileDragEnter(files,mid.x,mid.y);}
          e.fileDragMove(files,tailMid.x,tailMid.y);check(!e.punch2DropHover,"Drop highlight stayed on outside the Punch 2 row");
          e.fileDragMove(juce::StringArray{"not-audio.txt"},mid.x,mid.y);check(!e.punch2DropHover,"Non-audio drag highlighted Punch 2");
          e.fileDragMove(files,mid.x,mid.y);e.fileDragExit(files);check(!e.punch2DropHover,"Drop highlight stayed on after the drag left");
          std::array<uint64_t,4> generations{};for(int slot=0;slot<4;++slot)generations[size_t(slot)]=PunchTailPlaybackTest::requestGeneration(e.proc,slot);
          e.fileDragMove(files,mid.x,mid.y);e.filesDropped(files,mid.x,mid.y);check(!e.punch2DropHover,"Drop highlight stayed on after the drop");
          check(PunchTailPlaybackTest::pendingFile(e.proc,3)==file&&PunchTailPlaybackTest::requestGeneration(e.proc,3)==generations[3]+1,"File dropped on the Punch 2 row was not sent to Punch 2");
          for(int slot=0;slot<3;++slot)check(PunchTailPlaybackTest::requestGeneration(e.proc,slot)==generations[size_t(slot)],"Punch 2 drop also loaded another lane");
          PunchTailPlaybackTest::clearRequest(e.proc,3);
          // Several files dropped on the row: the first audio file goes to Punch 2, nothing else loads.
          e.filesDropped(juce::StringArray{"notes.txt",file.getFullPathName(),"other.wav"},mid.x,mid.y);
          check(PunchTailPlaybackTest::pendingFile(e.proc,3)==file,"Multi-file drop on Punch 2 did not take the first audio file");
          for(int slot=0;slot<3;++slot)check(PunchTailPlaybackTest::requestGeneration(e.proc,slot)==generations[size_t(slot)],"Multi-file drop on Punch 2 loaded other lanes");
          PunchTailPlaybackTest::clearRequest(e.proc,3);
          // End to end with a running worker: the dropped file becomes Punch 2 and the row shows it.
          {auto liveOwner=std::make_unique<PunchTailProcessor>();auto& live=*liveOwner;
           live.params.getParameter("punchLayer")->setValueNotifyingHost(1.0f);
           auto liveEditorOwner=std::make_unique<PunchTailEditor>(live);auto& le=*liveEditorOwner;le.timerCallback();le.resizeLayout();
           check(le.punch2Name.isVisible()&&le.punch2Name.getText()=="Drop Punch 2 here","Empty Punch 2 row does not invite a drop");
           const auto at=juce::Point<int>(juce::roundToInt(float(le.punch2Panel.getCentreX())*le.uiScale),juce::roundToInt(float(le.punch2Panel.getCentreY())*le.uiScale));
           le.filesDropped(files,at.x,at.y);
           std::shared_ptr<const PunchTailProcessor::Asset> dropped;
           {const auto deadline=juce::Time::getMillisecondCounter()+30000;
            while(!dropped&&juce::Time::getMillisecondCounter()<deadline){dropped=live.asset(3);juce::Thread::sleep(2);}}
           check(dropped&&dropped->sample.left.size()==2205,"File dropped on the Punch 2 row did not load into Punch 2");
           check(!live.asset(0)&&!live.asset(1)&&!live.asset(2),"Punch 2 drop loaded another lane");
           le.timerCallback();check(le.punch2Name.getText()==dropped->name&&le.punch2Unload.isEnabled(),"Punch 2 row does not show the dropped file");
           le.punch2Unload.onClick();check(!live.asset(3)&&le.punch2Name.getText()=="Drop Punch 2 here","Punch 2 unload after drop");}
          file.deleteFile();}
         if(snapshot){e.selectLayer(0);e.timerCallback();juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-punch2.png")};
             out.setPosition(0);out.truncate();check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render Punch 2 snapshot");e.selectLayer(1);e.timerCallback();}
         check(e.baseHeight>=before&&e.baseHeight<=before+40,"Punch 2 row changed height unexpectedly");
         check(e.keys.getBottom()+20==e.baseHeight,"Punch 2 row clips or leaves blank space");
         check(e.punch2Panel.getBottom()<e.punchDetectButton.getY()&&e.content.getLocalBounds().contains(e.punch2Load.getBounds()),"Punch 2 row overlaps detection row");
         e.punchLayer.setToggleState(false,juce::sendNotificationSync);e.timerCallback();
         check(value("punchLayer")==0&&!e.punch2Load.isVisible()&&!e.punch2Pitch.isVisible()&&e.baseHeight==before,"Unticking Punch layer did not hide Punch 2");
         check(e.dropSlotAt(e.punch2Panel.getCentreX(),e.punch2Panel.getCentreY())!=3,"Hidden Punch 2 row still takes drops");}
        // v0.5.1: Reverb TAIL/PUNCH SETTINGS page and the view choices are remembered:
        // reopening the window and reloading the project come back the same way.
        {check(!e.punchGeneratedPage&&e.storedView()==PunchTailEditor::ViewState{},"View state defaults");
         e.generatedPunchSettings.onClick();e.punchWave.setZoomIndex(2);e.tailWave.setZoomIndex(3);e.completedKick.setZoomIndex(1);
         e.completedKick.setJoinSpanIndex(3);e.completedKick.setJoinView(true);e.completedKick.setBitDepthIndex(2);e.timerCallback();
         const auto t=p.params.state;
         check(int(t.getProperty("uiReverbPage"))==1&&int(t.getProperty("uiPunchZoom"))==2&&int(t.getProperty("uiTailZoom"))==3&&int(t.getProperty("uiKickZoom"))==1&&
               int(t.getProperty("uiJoinView"))==1&&int(t.getProperty("uiJoinSpan"))==3&&int(t.getProperty("uiExportBits"))==2,"View choices not saved with the project");
         e.timerCallback();check(e.punchGeneratedPage&&e.punchWave.zoomIndex()==2,"Saved view state fought the user's change");
         {auto reopenedOwner=std::make_unique<PunchTailEditor>(p);auto& r=*reopenedOwner;r.timerCallback();
          check(r.punchGeneratedPage&&r.generatedPunchSettings.getToggleState()&&r.punchGeneratedControls[0].slider.isVisible()&&!r.generatedControls[0].slider.isVisible(),"Reopened editor forgot the Reverb PUNCH SETTINGS page");
          check(r.punchWave.zoomIndex()==2&&r.tailWave.zoomIndex()==3&&r.completedKick.zoomIndex()==1&&r.completedKick.joinViewShown()&&r.completedKick.joinSpanIndex()==3&&r.completedKick.exportBits()==32,"Reopened editor forgot the view choices");}
         // Project round trip into a new instance.
         {juce::MemoryBlock saved;p.getStateInformation(saved);auto qOwner=std::make_unique<PunchTailProcessor>();auto& q=*qOwner;q.setStateInformation(saved.getData(),int(saved.getSize()));
          auto qeOwner=std::make_unique<PunchTailEditor>(q);auto& qe=*qeOwner;qe.timerCallback();
          check(qe.punchGeneratedPage&&qe.punchWave.zoomIndex()==2&&qe.completedKick.joinViewShown(),"Project did not restore the view choices");}
         // Loading another project while the window is open switches the view.
         {auto t2=p.params.state;t2.setProperty("uiReverbPage",0,nullptr);t2.setProperty("uiPunchZoom",0,nullptr);e.timerCallback();
          check(!e.punchGeneratedPage&&e.generatedTailSettings.getToggleState()&&e.punchWave.zoomIndex()==0,"Open editor ignored a loaded project's view");}
         // Back to defaults for the rest of the test.
         e.applyView(PunchTailEditor::ViewState{});e.timerCallback();check(e.storedView()==PunchTailEditor::ViewState{}&&!e.punchGeneratedPage,"View reset");}
        // v0.5 MONO BASS panel: right column under Pitch Drop, bound, ranges, enable state, no overlaps.
        {e.resizeLayout();
         check(e.bassMonoPanel.getX()==e.pitchDropPanel.getX()&&e.bassMonoPanel.getWidth()==e.pitchDropPanel.getWidth()&&e.bassMonoPanel.getY()==e.pitchDropPanel.getBottom()+12,"Mono bass panel not under Pitch Drop");
         const std::initializer_list<juce::Component*> parts{&e.bassMonoTitle,&e.bassMonoEnable,&e.bassMonoLinear,&e.bassMonoHzLabel,&e.bassMonoHz,&e.bassMonoMixLabel,&e.bassMonoMix,&e.bassMonoNote};
         for(auto* c:parts){check(c->isVisible()&&e.bassMonoPanel.contains(c->getBounds()),"Mono bass control hidden or outside its panel");
             for(auto* d:parts)if(c!=d)check(!c->getBounds().intersects(d->getBounds()),"Mono bass controls overlap");}
         check(e.content.getLocalBounds().contains(e.bassMonoPanel)&&e.statusLabel.getY()>=e.bassMonoPanel.getBottom()+8&&!e.bassMonoPanel.intersects(e.keys.getBounds()),"Mono bass panel clips into the status line / keyboard");
         check(e.bassMonoHz.getMinimum()==90&&e.bassMonoHz.getMaximum()==300&&e.bassMonoMix.getMinimum()==0&&e.bassMonoMix.getMaximum()==100,"Mono bass slider ranges");
         check(e.bassMonoHz.hasStepButtons()&&e.bassMonoMix.hasStepButtons()&&e.bassMonoHz.getTextFromValue(150).contains("Hz")&&e.bassMonoMix.getTextFromValue(50).contains("%"),"Mono bass sliders lack -/+ buttons or units");
         check(!e.bassMonoEnable.getToggleState()&&value("bassMono")==0,"Mono bass must start OFF");
         e.timerCallback();check(!e.bassMonoHz.isEnabled()&&!e.bassMonoMix.isEnabled(),"Mono bass sliders active while OFF");
         // v0.5.1 LINEAR PHASE: fresh ON, bound; switches live, so never shows RESTART.
         check(e.bassMonoLinear.getToggleState()&&value("bassMonoLinear")==1&&e.bassMonoLinear.getButtonText()=="LINEAR PHASE","Mono bass LINEAR PHASE default/binding");
         e.bassMonoEnable.setToggleState(true,juce::sendNotificationSync);e.timerCallback();
         check(value("bassMono")==1&&e.bassMonoHz.isEnabled()&&e.bassMonoMix.isEnabled(),"Mono bass ENABLE not bound");
         check(e.bassMonoLinear.getButtonText()=="LINEAR PHASE","Mono bass LINEAR PHASE must not ask for a restart (it switches live)");
         e.bassMonoLinear.setToggleState(false,juce::sendNotificationSync);e.timerCallback();
         check(value("bassMonoLinear")==0&&e.bassMonoLinear.getButtonText()=="LINEAR PHASE","Mono bass LINEAR PHASE switch not bound");
         e.bassMonoLinear.setToggleState(true,juce::sendNotificationSync);
         e.bassMonoHz.setValue(210,juce::sendNotificationSync);e.bassMonoMix.setValue(40,juce::sendNotificationSync);
         check(value("bassMonoHz")==210&&std::abs(value("bassMonoMix")-40)<.01,"Mono bass sliders not host-bound");
         e.bassMonoEnable.setToggleState(false,juce::sendNotificationSync);e.bassMonoHz.setValue(120,juce::sendNotificationSync);e.bassMonoMix.setValue(100,juce::sendNotificationSync);e.timerCallback();}
        // v0.4.67 PUNCH FOLLOWS TAIL PITCH: Punch page only, host-bound, clear of its neighbours.
        {e.selectLayer(0);e.timerCallback();
         check(e.punchFollowTail.isVisible()&&!e.punchFollowTail.getToggleState()&&value("punchFollowTail")==0,"PUNCH FOLLOWS TAIL toggle missing or not OFF");
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.punchPitchMode,&e.punchPitchModeLabel,&e.punchInvert,&e.controls[3].slider,&e.controls[6].slider,&e.controls[13].slider,&e.controls[8].slider,&e.completedKick})
             check(!e.punchFollowTail.getBounds().intersects(c->getBounds()),"PUNCH FOLLOWS TAIL toggle overlaps a Punch control");
         check(e.content.getLocalBounds().contains(e.punchFollowTail.getBounds()),"PUNCH FOLLOWS TAIL toggle clipped");
         e.punchFollowTail.setToggleState(true,juce::sendNotificationSync);check(value("punchFollowTail")==1,"PUNCH FOLLOWS TAIL not host-bound");
         e.punchFollowTail.setToggleState(false,juce::sendNotificationSync);check(value("punchFollowTail")==0,"PUNCH FOLLOWS TAIL did not switch off");
         // v0.5.4 ONLY PUNCH 2 FOLLOWS: Punch page, fresh ON, bound, enabled only with PUNCH LAYER + FOLLOW, clear of its neighbours.
         {check(e.punchFollowDecayOnly.isVisible()&&e.punchFollowDecayOnly.getToggleState()&&value("punchFollowDecayOnly")==1,"ONLY PUNCH 2 FOLLOWS missing or not ON");
          for(auto* c:e.content.getChildren())if(c!=&e.punchFollowDecayOnly&&c->isVisible()&&c!=&e.aboutPage)check(!e.punchFollowDecayOnly.getBounds().intersects(c->getBounds()),"ONLY PUNCH 2 FOLLOWS overlaps another control");
          check(e.content.getLocalBounds().contains(e.punchFollowDecayOnly.getBounds()),"ONLY PUNCH 2 FOLLOWS clipped");
          e.timerCallback();check(!e.punchFollowDecayOnly.isEnabled(),"ONLY PUNCH 2 FOLLOWS enabled without layer/follow");
          e.punchLayer.setToggleState(true,juce::sendNotificationSync);e.punchFollowTail.setToggleState(true,juce::sendNotificationSync);e.timerCallback();
          check(e.punchFollowDecayOnly.isEnabled(),"ONLY PUNCH 2 FOLLOWS not enabled with layer + follow");
          // v0.5.8: the arrow between the two switches needs a gap of at least 24 px.
          check(e.punchFollowDecayOnly.getX()-e.punchFollowTail.getRight()>=90,"The arrow between PUNCH FOLLOWS TAIL PITCH and ONLY PUNCH 2 FOLLOWS must be long (>= 90 px)");
          e.punchFollowDecayOnly.setToggleState(false,juce::sendNotificationSync);check(value("punchFollowDecayOnly")==0,"ONLY PUNCH 2 FOLLOWS not host-bound");
          e.punchFollowDecayOnly.setToggleState(true,juce::sendNotificationSync);
          e.punchFollowTail.setToggleState(false,juce::sendNotificationSync);e.punchLayer.setToggleState(false,juce::sendNotificationSync);e.timerCallback();}
         // v0.5.5 DECAY NOTE row: Punch page only, under the mode row, above the Final Kick waveform, button follows validity.
         {e.selectLayer(0);e.timerCallback();
          check(e.decayNoteLabel.isVisible()&&e.decayNoteButton.isVisible()&&e.decayNoteLabel.getY()>=e.punchFollowTail.getBottom()&&e.decayNoteButton.getBottom()<=e.completedKick.getY(),"Decay note row misplaced");
          check(!e.decayNoteLabel.getBounds().intersects(e.decayNoteButton.getBounds())&&e.content.getLocalBounds().contains(e.decayNoteButton.getBounds()),"Decay note row overlaps or clips");
          check(e.decayNoteLabel.getText().startsWith("DECAY NOTE:"),"Decay note label text");
          check(e.decayNoteButton.isEnabled()==e.proc.punchDecayNote().valid,"USE DECAY NOTE enabled state does not follow the measurement");
          check(PunchTailEditor::decayNoteText({},false)=="DECAY NOTE: load a punch","No-punch text");
          pt::decaynote::Result r;r.valid=true;r.midHz=110;r.startHz=180;r.endHz=60;
          const auto text=PunchTailEditor::decayNoteText(r,true);check(text.contains("A3")&&text.contains("110.0 Hz")&&text.contains("->"),"Decay note text wrong");
          e.selectLayer(1);e.timerCallback();check(!e.decayNoteLabel.isVisible()&&!e.decayNoteButton.isVisible(),"Decay note row shown on the Tail page");e.selectLayer(0);e.timerCallback();}
         // v0.5.10 PUNCH HARMONY row: below the decay note, above the waveform, host-bound, key/scale only for the third.
         {e.timerCallback();
          check(e.punchHarmony.isVisible()&&e.punchHarmonyKey.isVisible()&&e.punchHarmonyScale.isVisible()&&e.punchHarmonyLabel.isVisible(),"Punch harmony row missing on the Punch page");
          check(e.punchHarmony.getY()>=e.decayNoteLabel.getBottom()&&e.punchHarmonyScale.getBottom()<=e.completedKick.getY(),"Punch harmony row misplaced");
          for(auto* a:std::initializer_list<juce::Component*>{&e.punchHarmonyLabel,&e.punchHarmony,&e.punchHarmonyKeyLabel,&e.punchHarmonyKey,&e.punchHarmonyScaleLabel,&e.punchHarmonyScale})
              for(auto* b:std::initializer_list<juce::Component*>{&e.punchHarmonyLabel,&e.punchHarmony,&e.punchHarmonyKeyLabel,&e.punchHarmonyKey,&e.punchHarmonyScaleLabel,&e.punchHarmonyScale})
                  check(a==b||!a->getBounds().intersects(b->getBounds()),"Punch harmony row overlaps itself");
          check(e.content.getLocalBounds().contains(e.punchHarmonyScale.getBounds()),"Punch harmony row clipped");
          check(e.punchHarmony.getSelectedId()==1&&value("punchHarmony")==0&&!e.punchHarmonyKey.isEnabled()&&!e.punchHarmonyScale.isEnabled(),"Punch harmony must start Off with key/scale disabled");
          check(e.punchHarmony.getNumItems()==3&&e.punchHarmonyKey.getNumItems()==12&&e.punchHarmonyScale.getNumItems()==8,"Punch harmony menus");
          e.punchHarmony.setSelectedId(2,juce::sendNotificationSync);e.timerCallback();
          check(value("punchHarmony")==1&&e.punchHarmonyKey.isEnabled()&&e.punchHarmonyScale.isEnabled(),"Diatonic Third not host-bound or key/scale not enabled");
          e.punchHarmonyKey.setSelectedId(10,juce::sendNotificationSync);e.punchHarmonyScale.setSelectedId(2,juce::sendNotificationSync);
          check(value("punchHarmonyKey")==9&&value("punchHarmonyScale")==1,"Key / scale not host-bound (A natural minor)");
          e.punchHarmony.setSelectedId(3,juce::sendNotificationSync);e.timerCallback();
          check(value("punchHarmony")==2&&!e.punchHarmonyKey.isEnabled(),"Perfect Fifth must ignore key/scale");
          e.punchHarmony.setSelectedId(1,juce::sendNotificationSync);e.punchHarmonyKey.setSelectedId(1,juce::sendNotificationSync);e.punchHarmonyScale.setSelectedId(1,juce::sendNotificationSync);e.timerCallback();
          check(e.baseHeight==1173&&e.keys.getBottom()+20==e.baseHeight,"Punch page height with the harmony row");
          e.selectLayer(1);e.timerCallback();check(!e.punchHarmony.isVisible()&&!e.punchHarmonyKey.isVisible(),"Punch harmony row shown on the Tail page");e.selectLayer(0);e.timerCallback();}
         e.selectLayer(1);e.timerCallback();check(!e.punchFollowTail.isVisible(),"PUNCH FOLLOWS TAIL visible on the Tail page");}
        e.glide.setToggleState(true,juce::sendNotificationSync);e.timerCallback();
        check(value("glide")==1&&e.glideTime.isEnabled(),"Glide not host-bound");
        e.glideTime.setValue(150,juce::sendNotificationSync);check(std::abs(value("glideTime")-150)<.2,"Glide time not host-bound");
        e.glide.setToggleState(false,juce::sendNotificationSync);e.timerCallback();check(!e.glideTime.isEnabled(),"Glide time active while Glide is off");
        for(juce::Component* c:std::initializer_list<juce::Component*>{&e.punchLayer,&e.glide,&e.glideTime})
            check(c->getRight()<=e.layerMute[0].getX()&&c->getY()<60,"Header options overlap the mute switches");
        {const juce::Component* row[]={&e.punchLayer,&e.glide,&e.glideTime};
         for(int i=1;i<3;++i)check(row[i]->getX()>=row[i-1]->getRight(),"Header glide options overlap each other");
         const float title=85.f+juce::Font(27.0f,juce::Font::bold).getStringWidthFloat("KICKTONE v"+PunchTailProcessor::versionString());
         check(float(e.punchLayer.getX())>title+12.f,"Header options overlap the title");}
        // Highest Fidelity v4: the default and, with Adaptive Stretch v4, the only engine.
        e.qualityMode.setSelectedId(5,juce::sendNotificationSync);e.timerCallback();
        check(value("pitchAdaptive")==0&&value("pitchAdaptive")==0,"Highest Fidelity v4 selection not host-bound");
        check(e.qualityMode.getSelectedId()==5&&e.qualityMode.getText()=="Highest Fidelity v4"&&!e.adaptiveAmount.isEnabled(),"Highest Fidelity v4 selector state wrong");
        e.qualityMode.setSelectedId(4,juce::sendNotificationSync);e.timerCallback();
        check(value("pitchAdaptive")==1&&value("pitchAdaptive")==1&&e.adaptiveAmount.isEnabled(),"Adaptive Stretch v4 selection not host-bound");
        e.qualityMode.setSelectedId(5,juce::sendNotificationSync);e.timerCallback();
        check(value("pitchAdaptive")==0,"Leaving Adaptive did not clear override");
        e.selectLayer(0);
        check(e.punchPitchMode.isVisible(),"Punch mode selector hidden");
        e.punchPitchMode.setSelectedId(2,juce::sendNotificationSync);
        check(value("punchPitchMode")==1,"Punch resample mode UI not attached");
        e.punchPitchMode.setSelectedId(1,juce::sendNotificationSync);
        check(value("punchPitchMode")==0,"Punch aligned mode UI not attached");
        // v0.4.63 third punch mode: Highest Fidelity (appended punchPitchFidelity override).
        check(e.punchPitchMode.getNumItems()==3 && e.punchPitchMode.getItemText(2)=="Highest Fidelity","Punch Highest Fidelity mode missing");
        check(value("punchPitchFidelity")==0 && p.punchPitchModeValue()==0,"Punch Highest Fidelity must be off by default");
        e.punchPitchMode.setSelectedId(3,juce::sendNotificationSync);
        check(value("punchPitchFidelity")==1 && p.punchPitchModeValue()==2,"Punch Highest Fidelity UI not host-bound");
        e.timerCallback();check(e.punchPitchMode.getSelectedId()==3,"Punch Highest Fidelity selection not shown");
        e.punchPitchMode.setSelectedId(2,juce::sendNotificationSync);
        check(value("punchPitchFidelity")==0 && value("punchPitchMode")==1 && p.punchPitchModeValue()==1,"Leaving Punch Highest Fidelity did not restore Resample");
        p.params.getParameter("punchPitchFidelity")->setValueNotifyingHost(1.f);e.timerCallback();
        check(e.punchPitchMode.getSelectedId()==3,"Host automation of Punch Highest Fidelity not shown");
        p.params.getParameter("punchPitchFidelity")->setValueNotifyingHost(0.f);e.timerCallback();
        check(e.punchPitchMode.getSelectedId()==2,"Punch mode selector did not follow the host");
        e.punchPitchMode.setSelectedId(1,juce::sendNotificationSync);
        // v0.4.63: the first EQ page is named TAIL EQ (it is the pitched tail bus).
        check(e.eqSectionBox.getItemText(0)=="TAIL EQ","EQ page not renamed to TAIL EQ");
        for(int i=0;i<e.eqSectionBox.getNumItems();++i)check(!e.eqSectionBox.getItemText(i).contains("KICK"),"KICK EQ name still shown");
        // v0.4.64: Complete Waveform Envelope + Pitch Drop/Attack Layer form a new
        // column to the right of the effects column, starting level with Distortion.
        check(e.masterEnvelopePanel.getX()>=e.effectPanels[0].getRight()+8 && e.masterEnvelopePanel.getY()==e.effectPanels[0].getY(),"Envelope panel not beside Distortion");
        check(e.pitchDropPanel.getX()==e.masterEnvelopePanel.getX() && e.pitchDropPanel.getY()>=e.masterEnvelopePanel.getBottom()+8,"Pitch Drop not stacked under the envelope");
        check(e.masterEnvelopePanel.getRight()<=e.content.getWidth()-20 && e.pitchDropPanel.getRight()<=e.content.getWidth()-20,"New column clipped");
        check(e.pitchDropPanel.getBottom()<e.statusLabel.getY() && e.punchRegionPanel.getBottom()<e.statusLabel.getY(),"Panels overlap the status line");
        check(e.aboutButton.getRight()<=e.content.getWidth()-20 && !e.uiScaleBox.getBounds().intersects(e.aboutButton.getBounds()),"Header right end misplaced");
        for(juce::Component* c:std::initializer_list<juce::Component*>{&e.masterEnvelopeGraph,&e.pitchDropTarget,&e.attackNote,&e.pitchDropEnable,&e.attackEnable})
            check(e.pitchDropPanel.contains(c->getBounds().getCentre())||e.masterEnvelopePanel.contains(c->getBounds().getCentre()),"Envelope/Pitch Drop control outside its panel");
        if(snapshot){   // v0.4.63: Punch page with Highest Fidelity selected and the TAIL EQ page.
            const int priorEq=e.eqSectionBox.getSelectedId();
            e.punchPitchMode.setSelectedId(3,juce::sendNotificationSync);e.eqSectionBox.setSelectedId(1,juce::sendNotificationSync);e.timerCallback();
            const juce::File file=juce::File(juce::String::fromUTF8(snapshot));
            juce::FileOutputStream out{file.getSiblingFile(file.getFileNameWithoutExtension()+"-punch-hf.png")};
            out.setPosition(0);out.truncate();check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render punch mode snapshot");
            e.punchPitchMode.setSelectedId(1,juce::sendNotificationSync);if(priorEq>0)e.eqSectionBox.setSelectedId(priorEq,juce::sendNotificationSync);e.timerCallback();
        }
        check(e.controls[13].slider.isVisible(),"Punch pitch control hidden");
        e.controls[13].slider.setValue(-12,juce::sendNotificationSync);
        check(value("punchPitch")==-12,"Punch pitch UI not attached");
        e.controls[13].slider.setValue(0,juce::sendNotificationSync);
        auto punchTrim=[&]{return value("punchOffset")+value("punchExtra");};
        // Click on the original punch waveform to cut off much more than 600 ms.
        e.punchWave.mouseDown(event(e.punchWave,{26,55}));e.punchWave.mouseUp(event(e.punchWave,{26,55}));
        check(punchTrim()>2800,"Punch waveform did not trim past 600 ms");
        refresh(e);check(std::abs(e.controls[3].slider.getValue()-punchTrim())<.11,"Punch knob did not follow waveform");
        e.punchWave.mouseDown(event(e.punchWave,{12,55}));e.punchWave.mouseUp(event(e.punchWave,{12,55}));
        check(punchTrim()>=p.asset(0)->sample.duration()*1000.0,"Full punch trim unavailable at sub-ms file length");
        e.setTrim(true,0);refresh(e);
        e.punchDetectButton.onClick();
        check(punchTrim()>2910 && punchTrim()<2975,"Detect Punch End did not apply cached boundary");
        // Tail waveform gets the same full-file range.
        e.tailWave.mouseDown(event(e.tailWave,{float(e.tailWave.getWidth())/2,55}));e.tailWave.mouseUp(event(e.tailWave,{float(e.tailWave.getWidth())/2,55}));
        check(std::abs(value("trim")+value("tailExtra")-1000)<.11,"Tail waveform start drag failed");
        e.tailWave.mouseDown(event(e.tailWave,{float(e.tailWave.getWidth())/2,55},true));
        e.tailWave.mouseDrag(event(e.tailWave,{float(e.tailWave.getWidth())/2+10,55},true));
        check(std::abs(value("trim")+value("tailExtra")-(1000+2000.0/(e.tailWave.getWidth()-24)))<.2,"Shift-drag precision failed");
        // 10 px fine is 1 effective pixel; the next 10 px coarse is 10
        // additional pixels (11 total, not the old incorrect 6-pixel oracle).
        const double afterFine=value("trim")+value("tailExtra");
        e.tailWave.mouseDrag(event(e.tailWave,{float(e.tailWave.getWidth())/2+10,55},false));
        check(std::abs(value("trim")+value("tailExtra")-afterFine)<.01,
              "Releasing Shift without moving jumped the trim");
        e.tailWave.mouseDrag(event(e.tailWave,{float(e.tailWave.getWidth())/2+20,55},false));
        check(std::abs(value("trim")+value("tailExtra")-(1000+22000.0/(e.tailWave.getWidth()-24)))<.2,
              "Changing Shift mid-drag jumped the trim");
        const double afterCoarse=value("trim")+value("tailExtra");
        e.tailWave.mouseDrag(event(e.tailWave,{float(e.tailWave.getWidth())/2+20,55},true));
        check(std::abs(value("trim")+value("tailExtra")-afterCoarse)<.01,
              "Pressing Shift without moving jumped the trim");
        e.tailWave.mouseDrag(event(e.tailWave,{float(e.tailWave.getWidth())/2+10,55},true));
        check(std::abs(value("trim")+value("tailExtra")-(1000+20000.0/(e.tailWave.getWidth()-24)))<.2,
              "Coarse-to-fine switch changed the previous drag movement");
        e.tailWave.mouseDrag(event(e.tailWave,{float(e.tailWave.getWidth())/2,55},false));
        check(std::abs(value("trim")+value("tailExtra")-1000)<.2,
              "Alternating Shift changes did not cancel the drag symmetrically");
        e.tailWave.mouseUp(event(e.tailWave,{float(e.tailWave.getWidth())/2,55}));
        // v0.4.57: END marker (dry Tail end cut) sits at the right edge; grabbing it
        // drags tailEndTrim from the end and never moves the START trim.
        {const double tailMs=p.asset(1)->sample.duration()*1000.0;const float right=float(e.tailWave.getWidth()-12),plotWidth=float(e.tailWave.getWidth()-24);
            check(value("tailEndTrim")==0,"Tail end not off by default");
            e.tailWave.mouseDown(event(e.tailWave,{right,90}));check(e.tailWave.draggingEnd&&!e.tailWave.dragging,"END marker not grabbed");
            e.tailWave.mouseDrag(event(e.tailWave,{right-plotWidth/4,90}));
            check(std::abs(value("tailEndTrim")-tailMs/4)<.3,"END marker drag did not set the Tail end trim");
            check(std::abs(value("trim")+value("tailExtra")-1000)<.2,"END marker drag moved the START trim");
            e.tailWave.mouseDrag(event(e.tailWave,{12,90}));
            check(std::abs(value("tailEndTrim")-(tailMs-1000))<.3,"END marker crossed the START marker");
            e.tailWave.mouseUp(event(e.tailWave,{12,90}));check(!e.tailWave.draggingEnd,"END drag did not finish");
            refresh(e);check(std::abs(e.tailEndControls[0].slider.getValue()-value("tailEndTrim"))<.11,"TAIL END knob did not follow the marker");
            e.tailEndControls[0].slider.setValue(0,juce::sendNotificationSync);check(value("tailEndTrim")==0,"TAIL END knob not host-bound");
            e.tailEndControls[1].slider.setValue(120,juce::sendNotificationSync);check(std::abs(value("tailEndFade")-120)<.06,"END FADE knob not host-bound");
            e.tailEndControls[1].slider.setValue(10,juce::sendNotificationSync);}
        // Third sample has the same full-length, draggable start-trim behavior.
        e.selectLayer(2);
        e.reverbWave.mouseDown(event(e.reverbWave,{float(e.tailWave.getWidth())/2,55}));
        e.reverbWave.mouseUp(event(e.reverbWave,{float(e.tailWave.getWidth())/2,55}));
        refresh(e);
        check(std::abs(value("reverbOffset")+value("reverbExtra")-1000)<.11,"Reverb waveform start trim failed");
        check(std::abs(e.controls[10].slider.getValue()-1000)<.11,"Reverb offset knob failed to synchronize");
        e.reverbWave.mouseDown(event(e.reverbWave,{float(e.tailWave.getWidth())/2,55},true));
        e.reverbWave.mouseDrag(event(e.reverbWave,{float(e.tailWave.getWidth())/2+10,55},true));
        e.reverbWave.mouseUp(event(e.reverbWave,{float(e.tailWave.getWidth())/2+10,55}));
        check(value("reverbOffset")+value("reverbExtra")>1006,"Reverb fine dragging failed");
        refresh(e);
        e.completedKick.joinControls[1].setValue(16.1,juce::sendNotificationSync);refresh(e);
        check(std::abs(value("crossfade")-16.1)<.11,"Final crossfade control lost host binding");
        e.completedKick.joinControls[0].setValue(13.4,juce::sendNotificationSync);refresh(e);
        check(std::abs(value("join")-13.4)<.11,"Final join control lost host binding");
        // The new parameter is a separate host-automatable rotary knob:
        // do not confuse delaying playback with trimming reverb input.
        e.controls[11].slider.setValue(40.0,juce::sendNotificationSync);
        check(std::abs(value("reverbDelay")-40)<.01,"Reverb delay knob did not update host parameter");
        const auto reverbTrimBefore=value("reverbOffset")+value("reverbExtra");
        refresh(e);
        check(std::abs(e.lastPreviewParams[14]-40)<.01,"Waveform preview ignored reverb delay");
        check(std::abs(value("reverbOffset")+value("reverbExtra")-reverbTrimBefore)<.01,
              "Reverb delay knob accidentally trimmed reverb sample");
        e.controls[12].slider.setValue(-12.0,juce::sendNotificationSync);
        check(std::abs(value("reverbLevel")+12)<.01,"Reverb level knob did not update independent host parameter");
        refresh(e);
        check(std::abs(e.lastPreviewParams[15]+12)<.01,"Complete kick preview did not follow reverb level");
        check(std::abs(value("reverbDelay")-40)<.01,"Reverb level changed reverb timing");
        check(std::abs(value("reverbOffset")+value("reverbExtra")-reverbTrimBefore)<.01,
              "Reverb level changed reverb trim");
        check(!e.reverbLowCut.isVisible() && !e.reverbLowCutLabel.isVisible(),"Redundant low-cut control is visible");
        e.setParameter("reverbLowCut",3); // Legacy host state remains compatible.
        check(std::abs(value("reverbLowCut")-3)<.01,"Reverb low-cut selector did not set 48 dB/oct");
        refresh(e);
        check(std::abs(e.lastPreviewParams[16]-3)<.01,"Combined preview ignored low-cut slope");
        check(value("gate")==1,"Follow MIDI note length was not enabled by default");
        check(value("punchDistOn")==0 && value("tailDistOn")==0,"Distortion should default OFF");
        e.distortion[0].enabled.setToggleState(true,juce::sendNotificationSync);
        e.distortion[1].enabled.setToggleState(true,juce::sendNotificationSync);
        e.distortion[0].algorithm.setSelectedId(2,juce::sendNotificationSync);
        e.distortion[1].algorithm.setSelectedId(3,juce::sendNotificationSync);
        e.distortion[0].knobs[0].slider.setValue(17.0,juce::sendNotificationSync);
        e.distortion[1].knobs[2].slider.setValue(60.0,juce::sendNotificationSync);
        check(value("punchDistOn")==1 && value("tailDistOn")==1,"Distortion enable buttons did not update host");
        check(value("punchDistMode")==1 && value("tailDistMode")==2,"Distortion mode selector did not update host");
        check(std::abs(value("punchDistDrive")-17)<.01 && std::abs(value("tailDistMix")-60)<.01,
              "Distortion rotary controls were not bound to host parameters");
        // v0.4.17: left-side distortion tabs preserve BOTH active engines.
        check(e.distortionPage==0 && e.distortion[0].enabled.isVisible() && !e.distortion[1].enabled.isVisible(),
              "Initial compact distortion page is not Punch");
        e.distortionTabs[1].onClick();
        check(e.distortionPage==1 && e.distortion[1].enabled.isVisible() && !e.distortion[0].enabled.isVisible(),
              "Tail/reverb distortion did not switch to left compact page");
        check(value("punchDistOn")==1 && value("tailDistOn")==1,
              "Switching tabs bypassed or reset one of the distortion units");
        e.distortionTabs[0].onClick();
        check(e.distortionPage==0 && e.distortion[0].enabled.isVisible(),
              "Punch distortion tab failed to return");
        check(value("transientOn")==0,"Punch shaper must default OFF");
        e.transientEnable.setToggleState(true,juce::sendNotificationSync);
        e.transientControls[0].slider.setValue(66.0,juce::sendNotificationSync);
        e.transientControls[1].slider.setValue(-22.0,juce::sendNotificationSync);
        e.transientControls[5].slider.setValue(-42.0,juce::sendNotificationSync);
        check(value("transientOn")==1 && std::abs(value("transientAttack")-66)<0.1 &&
              std::abs(value("transientSustain")+22)<0.1 &&
              std::abs(value("transientSensitivity")+42)<0.1,
              "Punch shaper controls not attached to host-automatable state");
        check(e.effectPanels[0].getBottom() <= e.effectPanels[1].getY() &&
              e.effectPanels[0].contains(e.distortion[0].knobs[3].slider.getBounds()) &&
              e.effectPanels[1].contains(e.transientControls[0].slider.getBounds()),
              "Distortion overlaps punch transient panel");
        check(e.transientControls[7].slider.getRight() <= e.content.getWidth(),
              "Punch transient control is clipped by editor bounds");
        // v0.4.17: versioned identity, graphical EQ, editable extra bands,
        // type menu, slope and UI resizing are all host state-backed.
        check(e.jeiLogo.isValid() && e.jeiLogo.hasAlphaChannel() && e.jeiLogo.getPixelAt(0,0).getAlpha()==0,"Transparent JEI header logo missing");
        check(p.getName()=="Kicktone","Permanent DAW-visible name \"Kicktone\" missing");
        check(value("punchRegionOn")==0 && value("punchRegionSplit")==20 && value("punchRegionTransition")==2,"Punch region defaults changed");
        const auto regionBox=e.punchRegionWave.plot();
        e.punchRegionWave.mouseDown(event(e.punchRegionWave,{regionBox.getX()+regionBox.getWidth()*.2f,regionBox.getCentreY()}));
        check(!e.gestureParams.empty(),"Punch split did not open host automation gesture");
        e.punchRegionWave.mouseDrag(event(e.punchRegionWave,{regionBox.getX()+regionBox.getWidth()*.4f,regionBox.getCentreY()}));
        e.punchRegionWave.mouseDrag(event(e.punchRegionWave,{regionBox.getX()+regionBox.getWidth()*.5f,regionBox.getCentreY()},true));
        e.punchRegionWave.mouseUp(event(e.punchRegionWave,{regionBox.getX()+regionBox.getWidth()*.5f,regionBox.getCentreY()}));
        check(std::abs(value("punchRegionSplit")-41)<.11 && e.gestureParams.empty(),"Punch split drag/fine movement or gesture close failed");
        e.attackGain.setValue(6,juce::sendNotificationSync);e.decayGain.setValue(-9,juce::sendNotificationSync);
        check(value("punchAttackGain")==6 && value("punchDecayGain")==-9,"Independent regional gain sliders are not host-bound");
        e.attackGain.setValue(0,juce::sendNotificationSync);e.decayGain.setValue(0,juce::sendNotificationSync);
        e.regionTransition.setValue(6,juce::sendNotificationSync);e.punchRegionEnable.setToggleState(true,juce::sendNotificationSync);
        check(value("punchRegionTransition")==6 && value("punchRegionOn")==1,"Punch region enable/transition not host bound");
        const int priorRegionView=e.activeEQ;const auto overallPunchGain=value("punchEq1Gain");
        e.attackEQButton.onClick();check(e.activeEQ==5,"Attack EQ shortcut did not select its own page");
        e.bandGain.setValue(6,juce::sendNotificationSync);check(value("punchAttackEq1Gain")==6 && value("punchEq1Gain")==overallPunchGain,"Attack EQ modified overall Punch EQ");
        e.decayEQButton.onClick();e.bandGain.setValue(-3,juce::sendNotificationSync);
        check(e.activeEQ==6 && value("punchDecayEq1Gain")==-3 && value("punchAttackEq1Gain")==6,"Decay EQ did not remain independent");
        e.setParameter("punchAttackEq1Gain",0);e.setParameter("punchDecayEq1Gain",0);
        e.eqSectionBox.setSelectedId(priorRegionView+1,juce::sendNotificationSync);
        e.punchRegionEnable.setToggleState(false,juce::sendNotificationSync);e.regionSplit.setValue(20,juce::sendNotificationSync);e.regionTransition.setValue(2,juce::sendNotificationSync);

        check(value("mutePunch")==0 && value("muteTail")==0 && value("muteReverb")==1,
              "Fresh instances must leave Punch/Tail audible and Reverb muted");
        // Enabling the generator must not leave its only output bus silently
        // muted by the fresh-instance default. Explicit mute remains available.
        // triggerClick posts an asynchronous command. Exercise the same toggle
        // notification synchronously before asserting the attached parameters.
        e.generatedEnable.setToggleState(true,juce::sendNotificationSync);
        check(value("tailVerbOn")==1 && value("muteReverb")==0,
              "Enabling Experimental Reverb must unmute its output layer");
        e.layerMute[2].setToggleState(true,juce::sendNotificationSync);
        check(value("muteReverb")==1,"Reverb must remain manually muteable after enabling");
        e.generatedEnable.setToggleState(false,juce::sendNotificationSync);
        for(auto& mute:e.layerMute)mute.setToggleState(true,juce::sendNotificationSync);
        check(value("mutePunch")==1 && value("muteTail")==1 && value("muteReverb")==1,
              "Layer mute controls are not host bound");
        for(auto& mute:e.layerMute)mute.setToggleState(false,juce::sendNotificationSync);
        check(value("eqOn")==1 && value("eq0On")==1 && value("eq1On")==1 && value("eq2On")==1,
              "Three neutral default EQ bands were not enabled");
        e.addEQBand(2350,4.0);
        check(e.selectedBand==3 && value("eq3On")==1,"New filter point not activated");
        check(std::abs(value("eq3Freq")-2350)<1,"New EQ frequency not saved to parameter");
        e.bandType.setSelectedId(1,juce::sendNotificationSync); // Low Cut
        check(value("eq3Type")==tt::LowCut,"Extra point type dropdown not working");
        e.bandSlope.setSelectedId(3,juce::sendNotificationSync);
        check(value("eq3Slope")==2,"48 dB/oct cut slope not host-backed");
        e.bandType.setSelectedId(5,juce::sendNotificationSync); // High Shelf
        check(value("eq3Type")==tt::HighShelf,"Filter type not switchable");
        e.bandGain.setValue(7,juce::sendNotificationSync);
        check(std::abs(value("eq3Gain")-7)<.01,"EQ gain is not host-backed");
        // Three EQ pages are independently host-automatable, displayed one at a
        // time and switching never changes another page's band settings.
        check(value("punchEqOn")==1 && value("reverbEqOn")==1,"Independent EQs not neutral-enabled");
        const auto kickGain=value("eq3Gain");
        e.eqSectionBox.setSelectedId(2,juce::sendNotificationSync);
        check(e.activeEQ==1 && value("eqView")==1,"Punch EQ tab not selected/persisted");
        e.addEQBand(1700,-6);
        check(e.selectedBand==3 && std::abs(value("punchEq3Gain")+6)<.11,
              "Punch EQ did not save independent band");
        check(std::abs(value("eq3Gain")-kickGain)<.01,"Punch EQ modified kick EQ");
        e.eqSectionBox.setSelectedId(3,juce::sendNotificationSync);
        check(e.activeEQ==2 && value("eqView")==2,"Reverb EQ tab not selected/persisted");
        e.addEQBand(750,-7);
        check(std::abs(value("reverbEq3Gain")+7)<.11,"Reverb EQ did not save independent band");
        e.eqSectionBox.setSelectedId(1,juce::sendNotificationSync);
        check(e.activeEQ==0 && std::abs(value("eq3Gain")-kickGain)<.01,
              "Kick EQ page did not restore its independent settings");
        {   // v0.5.9: double-click near the LEFT edge of the EQ plot = LOW SHELF,
            // near the RIGHT edge = HIGH SHELF, in the middle = bell (as before).
            auto dblClick=[&](float x,float y){auto ev=event(e.eqGraph,{x,y});
                e.eqGraph.mouseDown({ev.source,ev.position,ev.mods,1,0,0,0,0,&e.eqGraph,&e.eqGraph,ev.eventTime,ev.position,ev.eventTime,2,false});};
            auto plot=e.eqGraph.getLocalBounds().toFloat().reduced(12,12);plot.removeFromBottom(21);plot.removeFromLeft(30);
            const float midY=plot.getCentreY();
            check(TailtoneEQGraph::typeForAdd(plot.getX()+plot.getWidth()*.05f,plot)==tt::LowShelf
                  &&TailtoneEQGraph::typeForAdd(plot.getX()+plot.getWidth()*.5f,plot)==tt::Bell
                  &&TailtoneEQGraph::typeForAdd(plot.getX()+plot.getWidth()*.95f,plot)==tt::HighShelf,"EQ edge zones do not pick the shelf types");
            check(TailtoneEQGraph::freqForAdd(20,tt::LowShelf)==40&&TailtoneEQGraph::freqForAdd(20000,tt::HighShelf)==12000&&TailtoneEQGraph::freqForAdd(20,tt::Bell)==20,
                  "Shelf corner frequency is not kept inside the audible edge zone");
            dblClick(plot.getX()+3,midY);   // far left
            check(e.selectedBand==4&&value("eq4On")==1&&value("eq4Type")==tt::LowShelf&&std::abs(value("eq4Freq")-40)<1,
                  "Double-click at the left edge of the EQ must add a LOW SHELF");
            dblClick(plot.getRight()-3,midY);   // far right
            check(e.selectedBand==5&&value("eq5On")==1&&value("eq5Type")==tt::HighShelf&&std::abs(value("eq5Freq")-12000)<1,
                  "Double-click at the right edge of the EQ must add a HIGH SHELF");
            dblClick(plot.getX()+plot.getWidth()*.5f,plot.getY()+plot.getHeight()*.25f);   // middle: bell at ~632 Hz (above the 750 Hz default node)
            check(e.selectedBand==6&&value("eq6On")==1&&value("eq6Type")==tt::Bell&&std::abs(value("eq6Freq")-632.5)<5&&std::abs(value("eq6Gain")-4.5)<.2,
                  "Double-click in the middle of the EQ must still add a bell");
            dblClick(plot.getX()+plot.getWidth()*.1f,plot.getY()+plot.getHeight()*.1f);   // inside the left zone, well above band 4's node
            check(e.selectedBand==7&&value("eq7Type")==tt::LowShelf&&std::abs(value("eq7Freq")-39.9)<2.5&&std::abs(value("eq7Gain")-7.2)<.2,
                  "A left-zone double-click must place a low shelf at the click frequency and half the click gain");
            for(int i=4;i<8;++i)e.setBandParameter(i,"On",0);
            e.selectBand(3);
        }
        // Every view element shares one logical coordinate system and receives
        // only its parent canvas transform; the editor's logical extent fits.
        check(e.content.getWidth()==PunchTailEditor::baseWidth && e.content.getHeight()==e.baseHeight && (e.baseHeight==1139||e.baseHeight==1173||e.baseHeight==1244)   /* v0.5.10: Punch page 1173 */,
              "Logical canvas changed unexpectedly");
        check(e.keys.getBottom()<=e.content.getHeight(),"Keyboard extends below window");
        check(e.punchWave.getRight()<=e.content.getWidth(),"Waveform extends beyond canvas");
        e.setUIScale(0.5);check(e.getWidth()==juce::roundToInt(PunchTailEditor::baseWidth*.5) && e.getHeight()==juce::roundToInt(e.baseHeight*.5) && std::abs(value("uiScale")-50)<1.1,
              "50% UI scale not applied or persisted");
        check(std::abs(e.content.getTransform().mat00-0.5f)<.001f,
              "50% view does not transform single canvas");
        e.setUIScale(1.0);check(e.getWidth()==PunchTailEditor::baseWidth && std::abs(value("uiScale")-100)<1.1,
              "UI did not restore to 100%");
        // Regression: every supported zoom preset must scale the entire canvas,
        // not double-scale the widget locations or clip the lower controls.
        for(int pct:{50,75,100,125,150,200}) {
            e.setUIScale(pct/100.0);
            checkEffectLayout(e);
            check(e.getWidth()==juce::roundToInt(PunchTailEditor::baseWidth*pct/100.0) && e.getHeight()==juce::roundToInt(e.baseHeight*pct/100.0),
                  "Host editor bounds do not match the UI scale preset");
            check(std::abs(e.content.getTransform().mat00-pct/100.0f)<.001f,
                  "Logical content did not scale exactly once");
            check(e.punchWave.getBottom() < e.pitchLabel.getY(),
                  "Source waveform overlaps note-detection panel");
            check(!e.eqGraph.getBounds().intersects(e.bandType.getBounds()),
                  "EQ graph overlaps parameter inspector");
            check(e.keys.getBottom()<=e.content.getHeight(),
                  "Keyboard is outside the scaled editor");
        }
        // Gain editing is half as sensitive, while all three buses still store
        // true dB and graph positions retain their original response scale.
        for(int page=0;page<3;++page) {
            e.setEQView(page); e.selectBand(1);
            e.setBandParameter(1,"Freq",1000); e.setBandParameter(1,"Gain",0);
            const float plotHeight=float(e.eqGraph.getHeight()-45);
            const float centreY=12.0f+plotHeight*.5f;
            const float nodeX=42.0f+float(e.eqGraph.getWidth()-54)*float(std::log(50.0)/std::log(1000.0));
            auto& graph=e.eqGraph;
            graph.mouseDown(event(graph,{nodeX,centreY}));
            graph.mouseDrag(event(graph,{nodeX,centreY-plotHeight/6}));
            graph.mouseUp(event(graph,{nodeX,centreY-plotHeight/6}));
            check(std::abs(p.eqSettings(page)[1].gain-3.0)<.11,
                  "Graph boost is not half sensitivity on every EQ page");
            // Grab slightly off-centre: zero movement must not jump the gain.
            const float gainY=centreY-plotHeight/12;
            graph.mouseDown(event(graph,{nodeX,gainY+2}));
            graph.mouseDrag(event(graph,{nodeX,gainY+2}));
            check(std::abs(p.eqSettings(page)[1].gain-3.0)<.11,"Grabbing EQ node jumped gain");
            graph.mouseDrag(event(graph,{nodeX,gainY+2+plotHeight/3}));
            graph.mouseUp(event(graph,{nodeX,gainY+2+plotHeight/3}));
            check(std::abs(p.eqSettings(page)[1].gain+3.0)<.11,"EQ cut is not half sensitivity");
        }
        // v0.4.60: Ctrl-drag up/down on an EQ node changes only its width (Q).
        {e.setEQView(0);e.selectBand(1);e.setBandParameter(1,"Freq",1000);e.setBandParameter(1,"Gain",0);e.setBandParameter(1,"Q",1.0);
         auto& graph=e.eqGraph;const float plotHeight=float(graph.getHeight()-45),centreY=12.0f+plotHeight*.5f;
         const float nodeX=42.0f+float(graph.getWidth()-54)*float(std::log(50.0)/std::log(1000.0));
         auto ctrlEvent=[&](juce::Point<float> pt,bool shift){return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(),pt,
             juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::ctrlModifier|(shift?juce::ModifierKeys::shiftModifier:0)),
             1,0,0,0,0,&graph,&graph,juce::Time::getCurrentTime(),pt,juce::Time::getCurrentTime(),1,true};};
         graph.mouseDown(event(graph,{nodeX,centreY}));
         graph.mouseDrag(ctrlEvent({nodeX+30,centreY-40},false));
         check(std::abs(value("eq1Q")-2.0)<.011&&std::abs(value("eq1Freq")-1000)<.5&&value("eq1Gain")==0,"Ctrl-drag up did not narrow Q (x2) or moved freq/gain");
         check(std::abs(e.bandQ.getValue()-2.0)<.011,"Q inspector did not follow Ctrl-drag");
         graph.mouseDrag(ctrlEvent({nodeX+30,centreY+40},false));
         check(std::abs(value("eq1Q")-.5)<.011,"Ctrl-drag down did not widen Q");
         graph.mouseDrag(ctrlEvent({nodeX+30,centreY+440},false));check(std::abs(value("eq1Q")-.2)<.011,"Q not clamped at 0.2");
         graph.mouseDrag(ctrlEvent({nodeX+30,centreY+40},true));check(std::abs(value("eq1Q")-.2)<.011,"Fine Ctrl-drag misbehaved at the clamp");
         graph.mouseUp(event(graph,{nodeX+30,centreY+40}));
         graph.mouseDown(event(graph,{nodeX,centreY}));graph.mouseDrag(ctrlEvent({nodeX,centreY-200},false));
         graph.mouseDrag(event(graph,{nodeX,centreY-200}));
         check(std::abs(value("eq1Gain"))<.01,"Releasing Ctrl mid-drag jumped the gain");
         graph.mouseUp(event(graph,{nodeX,centreY-200}));
         e.setBandParameter(1,"Q",.71);e.setBandParameter(1,"Gain",0);}
        // v0.4.69: the node right-click menu ends with "Turn off band N"; choosing it switches that band off
        // on the EQ being shown, leaves the others alone and keeps it selected so BAND ON brings it back.
        {for(int band=0;band<tt::kBands;++band){
             const auto menu=TailtoneEQGraph::nodeMenu(band,tt::Bell);int types=0,ticked=-1;bool turnOff=false;
             for(juce::PopupMenu::MenuItemIterator it(menu);it.next();){const auto& item=it.getItem();
                 if(item.itemID>=1&&item.itemID<=5){++types;if(item.isTicked)ticked=item.itemID-1;}
                 if(item.itemID==TailtoneEQGraph::turnOffItemId)turnOff=item.isEnabled&&item.text=="Turn off band "+juce::String(band+1);}
             check(types==5&&ticked==tt::Bell&&turnOff,"EQ right-click menu lacks the filter types or Turn off band");}
         check(e.eqGraph.getTooltip().contains("turn the band off"),"EQ tooltip does not mention Turn off band");
         check(bool(e.eqGraph.onTurnOff),"EQ graph Turn off band not wired");
         for(int page=0;page<3;++page){
             e.setEQView(page);e.selectBand(0);
             for(int band=0;band<3;++band)e.setBandParameter(band,"On",1);
             const auto id=e.eqPrefix()+"2On";
             std::array<bool,3> othersBefore{};for(int other=0;other<3;++other)othersBefore[size_t(other)]=p.eqSettings(other)[2].enabled;
             e.eqGraph.onTurnOff(2);
             check(value(id.toRawUTF8())==0&&!p.eqSettings(page)[2].enabled,"Turn off band did not switch the band off");
             check(p.eqSettings(page)[0].enabled&&p.eqSettings(page)[1].enabled,"Turn off band touched another band");
             for(int other=0;other<3;++other)if(other!=page)check(p.eqSettings(other)[2].enabled==othersBefore[size_t(other)],"Turn off band changed another EQ");
             check(e.selectedBand==2&&e.eqBandLabel.getText()=="BAND 3"&&!e.bandEnable.getToggleState(),"Turned-off band not kept selected with BAND ON unticked");
             e.bandEnable.setToggleState(true,juce::sendNotificationSync);
             check(value(id.toRawUTF8())==1&&p.eqSettings(page)[2].enabled,"BAND ON did not bring the band back");}
         e.setEQView(0);e.selectBand(1);}
        const int gainTrack=e.bandGain.getLookAndFeel().getSliderLayout(e.bandGain).sliderBounds.getWidth();
        check(!e.bandGain.getSliderSnapsToMousePosition() && e.bandGain.getMouseDragSensitivity()==2*gainTrack,
              "Gain slider did not retain half-sensitivity relative dragging");
        e.setEQView(0);
        e.selectLayer(0);check(e.controls[3].slider.isVisible()&&!e.controls[0].slider.isVisible(),"Punch controls not grouped");
        e.selectLayer(1);check(e.controls[0].slider.isVisible()&&!e.controls[3].slider.isVisible(),"Tail controls not grouped");
        for(auto& c:e.tailEndControls){check(c.slider.isVisible()&&c.label.isVisible(),"TAIL END / END FADE missing on Tail page");
            check(c.slider.getRight()<=e.controls[8].slider.getX()&&!c.slider.getBounds().intersects(e.controls[9].slider.getBounds())&&c.slider.getX()>=e.controls[9].slider.getRight(),"Tail end knobs overlap neighbours");}
        check(!e.tailEndControls[0].slider.getBounds().intersects(e.tailEndControls[1].slider.getBounds()),"Tail end knobs overlap each other");
        e.selectLayer(0);check(!e.tailEndControls[0].slider.isVisible()&&!e.tailEndControls[1].slider.isVisible(),"Tail end knobs leaked onto Punch page");e.selectLayer(1);
        // v0.4.60: JOIN SHIFT / CROSSFADE knobs removed; the final-waveform sliders remain.
        for(int page=0;page<3;++page){e.selectLayer(page);check(!e.controls[4].slider.isVisible()&&!e.controls[5].slider.isVisible()&&!e.controls[4].label.isVisible()&&!e.controls[5].label.isVisible(),"Duplicate JOIN SHIFT / CROSSFADE knobs still shown");
            check(e.controls[8].slider.isVisible()&&e.completedKick.joinControls[0].isVisible()&&e.completedKick.joinControls[1].isVisible(),"OUTPUT or final-waveform join controls missing");}
        e.selectLayer(1);
        // v0.4.60: every linear slider has -/+ buttons except Experimental Reverb, Glide (and its mono-sub Hz).
        {std::vector<TailtoneSlider*> stepped{&e.phaseFine,&e.formantAmount,&e.crunchLowCut,&e.punch2Offset,&e.bandFreq,&e.bandGain,&e.bandQ,&e.regionSplit,&e.regionTransition,&e.attackGain,&e.decayGain};
         for(auto& c:e.transientControls)stepped.push_back(&c.slider);
         for(auto* s:stepped){check(s->hasStepButtons()&&s->getSliderStyle()==juce::Slider::LinearHorizontal,"Linear slider without -/+ buttons");
             const auto layout=s->getLookAndFeel().getSliderLayout(*s);
             check(layout.sliderBounds.getX()>=s->plusButton()->getRight()&&layout.sliderBounds.getWidth()>=30&&!layout.textBoxBounds.intersects(s->plusButton()->getBounds())&&s->getLocalBounds().contains(s->plusButton()->getBounds()),"-/+ buttons overlap the track/value or squeeze it");}
         for(auto* s:std::initializer_list<TailtoneSlider*>{&e.glideTime,&e.monoSubHz})check(!s->hasStepButtons(),"Excluded slider got -/+ buttons");
         for(auto& c:e.generatedControls)check(!c.slider.hasStepButtons(),"Experimental Reverb slider got -/+ buttons");
         for(auto& c:e.punchGeneratedControls)check(!c.slider.hasStepButtons(),"Experimental Reverb slider got -/+ buttons");
         // Clicks: one step, Shift = a tenth, clamp at range ends, host-bound.
         e.phaseFine.setValue(0,juce::sendNotificationSync);e.phaseFine.plusButton()->onClick();check(std::abs(value("phaseFine")-.01)<.001,"Phase + step");
         e.phaseFine.minusButton()->onClick();e.phaseFine.minusButton()->onClick();check(std::abs(value("phaseFine")+.01)<.001,"Phase - step");
         e.phaseFine.setValue(0,juce::sendNotificationSync);
         e.transientControls[0].slider.setValue(30,juce::sendNotificationSync);e.transientControls[0].slider.stepBy(1,false);check(std::abs(value("transientAttack")-31)<.01,"Transient + step");
         e.transientControls[0].slider.stepBy(1,true);check(std::abs(value("transientAttack")-31.1)<.01,"Shift fine step");
         e.transientControls[0].slider.setValue(100,juce::sendNotificationSync);e.transientControls[0].slider.stepBy(1,false);check(value("transientAttack")==100,"Step passed the range end");
         e.transientControls[0].slider.setValue(66,juce::sendNotificationSync); // restore the earlier round-trip value
         e.crunchLowCut.stepBy(1,false);check(std::abs(value("crunchLowCut")-2100)<2,"Crunch low cut step");e.crunchLowCut.setValue(2000,juce::sendNotificationSync);
         e.attackGain.stepBy(-1,false);check(std::abs(value("punchAttackGain")+.1)<.01,"Attack gain step");e.attackGain.setValue(0,juce::sendNotificationSync);
         e.setEQView(0);e.selectBand(1);e.setBandParameter(1,"Freq",1000);
         e.bandFreq.stepBy(1,false);check(value("eq1Freq")>1010&&value("eq1Freq")<1060,"EQ frequency step is not a small proportional move");
         e.setBandParameter(1,"Freq",1000);e.syncEQControls();
         check(e.gestureParams.empty(),"Step buttons left a host gesture open");}
        // v0.4.60 output limiter switch: fresh ON, host-bound, beside the OUTPUT meter.
        check(value("outputLimiter")==1&&e.limiterToggle.getToggleState()&&e.limiterToggle.isVisible(),"Output limiter must be ticked by default");
        e.limiterToggle.setToggleState(false,juce::sendNotificationSync);check(value("outputLimiter")==0,"Limiter toggle not host-bound");
        e.limiterToggle.setToggleState(true,juce::sendNotificationSync);
        for(juce::Component* c:std::initializer_list<juce::Component*>{&e.gate,&e.clipButton,&e.previewLabel,&e.lowKeyPunch,&e.normalizeButtons[0],&e.normalizeButtons[1],&e.completedKick})
            check(!c->getBounds().intersects(e.limiterToggle.getBounds()),"Limiter toggle overlaps the output column");
        check(e.normalizeButtons[1].getBottom()<=e.completedKick.getBottom(),"Output column grew below the waveform");
        for(int i=0;i<4;++i){juce::Component* labels[]={&e.regionSplitLabel,&e.regionTransitionLabel,&e.attackGainLabel,&e.decayGainLabel};TailtoneSlider* sl[]={&e.regionSplit,&e.regionTransition,&e.attackGain,&e.decayGain};
            for(int j=0;j<4;++j)if(i!=j)check(!sl[i]->getBounds().intersects(sl[j]->getBounds())&&!labels[i]->getBounds().intersects(sl[j]->getBounds()),"Punch region rows overlap");}
        for(size_t i=0;i<8;++i)for(size_t j=0;j<8;++j)if(i!=j)check(!e.transientControls[i].slider.getBounds().intersects(e.transientControls[j].slider.getBounds())&&!e.transientControls[i].label.getBounds().intersects(e.transientControls[j].slider.getBounds()),"Transient rows overlap");
        check(e.baseHeight==1244 && e.phaseAuto.isVisible() && e.phaseInvert.isVisible(),
              "Tail phase panel not visible or UI height not expanded");
        // v0.5.22 (user request): FINE PHASE SHIFT is no longer shown; the parameter stays bound.
        check(!e.phaseFine.isVisible() && !e.phaseFineLabel.isVisible(),"FINE PHASE SHIFT is still shown");
        check(e.keys.getBottom()+20==e.baseHeight,"Phase settings made the Tail page clip or leave blank space");
        check(value("phaseAuto")==1 && value("phaseFine")==0 && value("phaseInvert")==0,
              "New phase controls must default to Auto Match with neutral fine/invert");
        e.phaseFine.setValue(0.25,juce::sendNotificationSync);
        e.phaseInvert.setToggleState(true,juce::sendNotificationSync);
        e.phaseAuto.setToggleState(true,juce::sendNotificationSync);
        check(std::abs(value("phaseFine")-0.25)<.011 && value("phaseInvert")==1 && value("phaseAuto")==1,
              "Phase UI controls are not host-automatable");
        refresh(e);
        check(std::abs(e.lastPreviewParams[18]-0.25)<.011 && e.lastPreviewParams[17]==1 && e.lastPreviewParams[19]==1,
              "Complete waveform did not receive the phase controls");
        check(e.phaseFine.getRight()<=e.content.getWidth() && e.phaseScore.getRight()<=e.content.getWidth(),
              "Phase controls overlap or leave logical editor width");
        e.selectLayer(2);check(e.controls[10].slider.isVisible()&&e.controls[8].slider.isVisible(),"Reverb/shared controls missing");
        check(!e.phaseFine.isVisible() && !e.phaseAuto.isVisible(),"Phase controls leaked outside the Tail page");
        const float effectBefore=value("transientOn");
        for(int mask=0;mask<8;++mask)for(int focus=-1;focus<3;++focus) {
            for(int i=0;i<3;++i)e.collapsed[size_t(i)]=(mask&(1<<i))!=0;
            e.waveFocus=focus;e.saveLayout();e.resizeLayout();
            checkEffectLayout(e);
            check(e.keys.getBottom()+20==e.baseHeight,"Collapsed layout leaves clipping or blank bottom");
            check(e.eqGraph.isVisible()==!e.collapsed[2],"EQ fold did not hide inspector");
            check(value("transientOn")==effectBefore,"Folding changed the audio effect");
            if(focus>=0){juce::Component* waves[]={&e.punchWave,&e.tailWave,&e.reverbWave};check(waves[focus]->getWidth()>500,"Focused waveform did not expand");}
        }
        e.waveFocus=1;e.collapsed={true,false,true};e.showDistortionPage(1);e.saveLayout();e.resizeLayout();
        { auto reopenedOwner=std::make_unique<PunchTailEditor>(p);auto& reopened=*reopenedOwner;
          check(reopened.selectedLayer==2&&reopened.waveFocus==1&&reopened.collapsed[0]&&reopened.collapsed[2]&&reopened.distortionPage==1,
                "Closing/reopening did not preserve layout"); }
        e.waveFocus=-1;e.collapsed={false,false,false};e.saveLayout();e.resizeLayout();
        // Recreate the editor like a host closing/reopening the plugin window.
        for(int pct:{50,75,125,150,200}) {
            { auto closingOwner=std::make_unique<PunchTailEditor>(p);auto& closing=*closingOwner; closing.setUIScale(pct/100.0); }
            { auto reopenedOwner=std::make_unique<PunchTailEditor>(p);auto& reopened=*reopenedOwner;
              check(std::abs(value("uiScale")-pct)<.01,"Opening editor overwrote saved UI scale");
              check(reopened.getWidth()==juce::roundToInt(PunchTailEditor::baseWidth*pct/100.0) && reopened.getHeight()==juce::roundToInt(reopened.baseHeight*pct/100.0),
                    "Reopened window did not restore saved scale"); }
        }
        e.setUIScale(.75);
        juce::MemoryBlock scaledState; p.getStateInformation(scaledState);
        { auto restoredOwner=std::make_unique<PunchTailProcessor>();auto& restored=*restoredOwner;
          restored.setStateInformation(scaledState.getData(),int(scaledState.getSize()));
          auto reopenedOwner=std::make_unique<PunchTailEditor>(restored);auto& reopened=*reopenedOwner;
          std::cout << "Restored editor size " << reopened.getWidth() << "x" << reopened.getHeight() << "; expected " << juce::roundToInt(PunchTailEditor::baseWidth*.75) << "x" << juce::roundToInt(reopened.baseHeight*.75) << "\n";
          check(reopened.getWidth()==juce::roundToInt(PunchTailEditor::baseWidth*.75) && reopened.getHeight()==juce::roundToInt(reopened.baseHeight*.75) && (reopened.baseHeight==1139||reopened.baseHeight==1173||reopened.baseHeight==1244),
                "Project state did not restore window scale"); }
        check(e.eqGraph.getHeight()==123,"EQ graph height reduction lost");
        check(e.completedKick.getWidth()==810 && e.completedKick.getHeight()==225,
              "Complete waveform did not become shorter and taller");
        e.setUIScale(1.0);
        check(e.gestureParams.empty(),"Host automation gesture left open");
        check(p.params.getParameter("punchOffset")->convertTo0to1(300)==.5f,"Legacy automation mapping changed");
        e.selectLayer(0);check(value("punchInvert")==0 && e.punchInvert.isVisible() && e.punchInvert.getBounds().getRight()<=741,"Punch polarity layout/default missing");
        e.punchInvert.setToggleState(true,juce::sendNotificationSync);check(value("punchInvert")==1,"Punch polarity not host bound");e.punchInvert.setToggleState(false,juce::sendNotificationSync);
        check(value("tailVerbKeepDry")==1,"Keep Dry must default on");
        check(value("tailVerbDistortWet")==0 && value("tailVerbPunchSend")==0 && value("tailVerbTailSend")==1,"New wet routing options must preserve the tail-only default");
        e.generatedDistortWet.setToggleState(true,juce::sendNotificationSync);check(value("tailVerbDistortWet")==1,"Distort Wet toggle is not host bound");
        e.generatedDistortWet.setToggleState(false,juce::sendNotificationSync);
        e.generatedPunchSend.setToggleState(true,juce::sendNotificationSync);check(value("tailVerbPunchSend")==1,"Punch reverb toggle is not host bound");
        e.generatedTailSend.setToggleState(false,juce::sendNotificationSync);check(value("tailVerbTailSend")==0,"Tail reverb source toggle is not host bound");
        e.generatedPunchSend.setToggleState(false,juce::sendNotificationSync);
        e.completedKick.onOutputGesture(false,true);e.completedKick.onOutputEdit(false,4.5);e.completedKick.onOutputGesture(false,false);
        check(std::abs(value("punchNormalize")-4.5)<.01 && e.gestureParams.empty(),"Punch waveform output trim is not host-bound or left its gesture open");
        e.completedKick.onOutputGesture(true,true);e.completedKick.onOutputEdit(true,-3.5);e.completedKick.onOutputGesture(true,false);
        check(std::abs(value("tailNormalize")+3.5)<.01 && e.gestureParams.empty(),"Tail waveform output trim is not host-bound or left its gesture open");
        p.params.getParameter("punchNormalize")->setValueNotifyingHost(p.params.getParameter("punchNormalize")->convertTo0to1(0));
        p.params.getParameter("tailNormalize")->setValueNotifyingHost(p.params.getParameter("tailNormalize")->convertTo0to1(0));
        e.generatedKeepDry.setToggleState(false,juce::sendNotificationSync);check(value("tailVerbKeepDry")==0,"Keep Dry not host bound");
        e.generatedKeepDry.setToggleState(true,juce::sendNotificationSync);
        check(value("tailVerbOn")==0 && value("eqGeneratedView")==0,"Experimental effect must default to bypass");
        check(value("tailVerbRootPitch")==0 && value("tailVerbOffset")==0 && e.generatedRootPitch.isVisible(),"Tail root-render option or offset default missing");
        e.generatedRootPitch.setToggleState(true,juce::sendNotificationSync);
        e.generatedControls[10].slider.setValue(137,juce::sendNotificationSync);
        check(value("tailVerbRootPitch")==1 && value("tailVerbOffset")==137,"Root-render and Tail reverb offset controls are not host-bound");
        e.generatedControls[10].slider.setValue(-137,juce::sendNotificationSync);
        check(value("tailVerbOffset")==-137 && e.generatedControls[10].slider.getMinimum()==-300,"Negative Tail reverb offset is not host bound");
        e.generatedRootPitch.setToggleState(false,juce::sendNotificationSync);e.generatedControls[10].slider.setValue(0,juce::sendNotificationSync);
        e.generatedControls[0].slider.setValue(40,juce::sendNotificationSync);check(value("tailVerbMix")==40,"Generated mix is not host bound");
        e.generatedControls[1].slider.setValue(700,juce::sendNotificationSync);check(value("tailVerbSpace")==700,"Generated space is not host bound");
        check(e.generatedControls[1].slider.getMaximum()==3000 && value("tailVerbDuck")==85,"Long Space or new duck default missing");
        e.generatedControls[4].slider.setValue(70,juce::sendNotificationSync);check(value("tailVerbDuck")==70,"Sub duck control is not host bound");
        e.generatedControls[5].slider.setValue(160,juce::sendNotificationSync);check(value("tailVerbSubHz")==160,"Sub range control is not host bound");
        e.generatedControls[6].slider.setValue(40,juce::sendNotificationSync);check(value("tailVerbRecovery")==40,"Recovery control is not host bound");
        const auto mixBefore=value("tailVerbMix");
        e.generatedControls[7].slider.setValue(-6,juce::sendNotificationSync);
        check(value("tailVerbLevel")==-6 && value("tailVerbMix")==mixBefore && e.generatedControls[7].slider.getSliderStyle()==juce::Slider::LinearHorizontal,"Reverb volume slider must be independent of Mix");
        e.generatedControls[8].slider.setValue(125,juce::sendNotificationSync);check(value("tailVerbPunchPreDelay")==125,"Punch reverb pre-delay control is not host bound");
        e.generatedPunchSettings.onClick();
        check(!e.generatedRootPitch.isVisible() && !e.generatedControls[10].slider.isVisible(),"Tail-only root pitch/offset controls appeared on Punch settings page");
        check(!e.generatedControls[11].slider.isVisible() && e.generatedControls[9].slider.isVisible(),"Tail fade-in must be on the Tail page only (Punch keeps its own fade)");
        check(e.generatedEngine.isVisible(),"Reverb engine selector must be visible on the Punch page too (shared engine)");
        e.punchGeneratedControls[0].slider.setValue(42,juce::sendNotificationSync);check(value("punchVerbMix")==42 && value("tailVerbMix")==mixBefore,"Punch Mix is not independently host bound");
        e.punchGeneratedControls[1].slider.setValue(1800,juce::sendNotificationSync);check(value("punchVerbSpace")==1800 && value("tailVerbSpace")==700,"Punch Space changed Tail Space");
        // v0.4.59: user asked for a louder Punch reverb top end; Tail stays +12 dB.
        check(e.generatedControls[7].slider.getMaximum()==12&&e.punchGeneratedControls[7].slider.getMaximum()==24,"Tail level must reach +12 dB and Punch level +24 dB");
        e.punchGeneratedControls[7].slider.setValue(24,juce::sendNotificationSync);check(value("punchVerbLevel")==24 && value("tailVerbLevel")!=24,"Punch wet level +24 dB not host-bound");
        e.generatedControls[7].slider.setValue(12,juce::sendNotificationSync);check(value("tailVerbLevel")==12,"Tail wet level +12 dB not host-bound");
        e.generatedControls[7].slider.setValue(-6,juce::sendNotificationSync);
        e.punchGeneratedControls[7].slider.setValue(-9,juce::sendNotificationSync);check(value("punchVerbLevel")==-9 && value("tailVerbLevel")==-6,"Punch wet level changed Tail wet level");
        e.generatedControls[8].slider.setValue(125,juce::sendNotificationSync);check(value("tailVerbPunchPreDelay")==125,"Punch reverb pre-delay control is not host bound");
        e.generatedControls[9].slider.setValue(240,juce::sendNotificationSync);check(value("tailVerbPunchFade")==240,"Punch reverb fade control is not host bound");
        check(e.generatedControls[8].slider.getSliderStyle()==juce::Slider::LinearHorizontal && e.generatedControls[9].slider.getSliderStyle()==juce::Slider::LinearHorizontal,"Punch reverb timing controls should be horizontal faders");
        const auto kickBefore=value("eq1Gain");
        e.collapsed[2]=true;e.saveLayout();e.resizeLayout();check(!e.eqGraph.isVisible(),"EQ fold precondition");
        e.generatedTailSettings.onClick();e.generatedEQButton.onClick();check(!e.collapsed[2] && e.eqGraph.isVisible() && e.effectPanels[2].contains(e.eqGraph.getBounds()),"Edit Wet EQ must reveal the inspector");check(e.activeEQ==3 && value("eqGeneratedView")==1,"Tail generated wet EQ page missing");
        check(e.generatedRootPitch.isVisible() && e.generatedControls[10].slider.isVisible(),"Tail reverb controls did not return on Tail settings page");
        check(e.generatedControls[11].slider.isVisible() && !e.generatedControls[9].slider.isVisible(),"Tail fade-in missing from Tail settings page");
        check(value("tailVerbTailFade")==0 && e.generatedControls[11].slider.getMaximum()==1000 && e.generatedControls[11].slider.getSliderStyle()==juce::Slider::LinearHorizontal,"Tail fade-in default/range/style wrong");
        e.generatedControls[11].slider.setValue(330,juce::sendNotificationSync);check(value("tailVerbTailFade")==330 && value("tailVerbPunchFade")==240,"Tail fade-in is not independently host bound");
        check(!e.generatedControls[11].slider.getBounds().intersects(e.generatedControls[10].slider.getBounds()) && e.generatedPanel.contains(e.generatedControls[11].slider.getBounds()),"Tail fade-in overlaps Tail offset or escapes the panel");
        check(value("tailVerbEngine")==1 && e.generatedEngine.getSelectedItemIndex()==1 && e.generatedEngine.getNumItems()==2,"Fresh reverb engine must be V2 with Legacy/V2 choices");
        e.generatedEngine.setSelectedItemIndex(0,juce::sendNotificationSync);check(value("tailVerbEngine")==0,"Reverb engine selector is not host bound");
        e.generatedEngine.setSelectedItemIndex(1,juce::sendNotificationSync);
        check(e.generatedPanel.contains(e.generatedEngine.getBounds()) && !e.generatedEngine.getBounds().intersects(e.generatedPunchSettings.getBounds()) && !e.generatedEngine.getBounds().intersects(e.generatedControls[0].label.getBounds()),"Reverb engine selector overlaps the panel controls");
        e.bandGain.setValue(-5,juce::sendNotificationSync);
        check(value("tailVerbEq1Gain")==-5 && value("eq1Gain")==kickBefore,"Generated EQ is not isolated from kick EQ");
        e.generatedPunchSettings.onClick();e.generatedEQButton.onClick();check(e.activeEQ==4 && value("eqPunchGeneratedView")==1,"Punch generated wet EQ page missing");
        e.bandGain.setValue(7,juce::sendNotificationSync);check(value("punchVerbEq1Gain")==7 && value("tailVerbEq1Gain")==-5,"Punch generated EQ is not independent of Tail EQ");
        e.eqSectionBox.setSelectedId(1,juce::sendNotificationSync);check(value("eqGeneratedView")==0 && value("eqView")==0,"Original EQ pages failed to restore");
        refresh(e);
        for(int i=0;i<2;++i) {
            const auto other=value(i==0?"tailNormalize":"punchNormalize");
            const float raw=e.processedPeaks[size_t(i)];check(raw>1.e-7f,"Processed bus analysis missing");
            e.normalizeButtons[size_t(i)].onClick();refresh(e);
            const float gain=juce::Decibels::decibelsToGain(value(i==0?"punchNormalize":"tailNormalize"));
            check(std::abs(juce::Decibels::gainToDecibels(raw*gain)+1)<.015,"Normalization did not target processed -1 dBFS peak");
            check(value(i==0?"tailNormalize":"punchNormalize")==other,"Normalize changed the other bus");
            e.normalizeReset[size_t(i)].onClick();refresh(e);check(value(i==0?"punchNormalize":"tailNormalize")==0,"Normalization reset failed");
        }
        // v0.5.28 instant waveform volume: a drag on the Final Kick redraws from the last
        // render's buses at once, starts no render while the mouse is held, and the render
        // after mouse-up equals what the drag showed; export waits for that render.
        {auto& d=e.completedKick;d.zoomBox.setSelectedId(1,juce::sendNotificationSync);refresh(e);
         check(d.buses!=nullptr && d.shownGains==d.renderedGains && d.exportButton.isEnabled(),"Final Kick render did not keep its output buses");
         const auto before=d.kick.envelope;const auto generation=e.previewGeneration;
         const float x=d.plotBounds().getX()+2;
         check(x<d.timeX(d.kick.tailStartMs),"Volume drag test must start on the Punch");
         d.mouseDown(event(d,{x,60}));d.mouseDrag(event(d,{x,90}));   // 30 px down = -6 dB
         check(d.outputDragActive() && !d.outputDragTail && std::abs(value("punchNormalize")+6)<.011,"Waveform drag did not set the Punch volume");
         d.applyRemix();
         bool changed=false;
         for(size_t i=0;i<before.bins.size();++i)changed=changed||before.bins[i].minimum!=d.kick.envelope.bins[i].minimum||before.bins[i].maximum!=d.kick.envelope.bins[i].maximum;
         check(changed && d.kick.envelope.bins.size()==before.bins.size(),"Final Kick waveform did not follow the volume drag at once");
         check(!d.exportButton.isEnabled() && !d.renderedAudio(),"Export must wait for the render at the new volume");
         e.timerCallback();e.timerCallback();
         check(e.previewGeneration==generation,"A render restarted while the waveform volume drag was held");
         d.mouseDrag(event(d,{x,85}));d.mouseUp(event(d,{x,85}));
         check(std::abs(value("punchNormalize")+5)<.011 && e.gestureParams.empty(),"Volume drag did not end at -5 dB or left its gesture open");
         e.timerCallback();check(e.previewGeneration!=generation,"Mouse-up did not start the render at the new volume");
         d.applyRemix();const auto shown=d.kick.envelope;
         refresh(e);
         check(d.shownGains==d.renderedGains && d.exportButton.isEnabled() && d.renderedAudio(),"The render at the new volume did not take over");
         bool same=shown.bins.size()==d.kick.envelope.bins.size() && shown.peak==d.kick.envelope.peak;
         for(size_t i=0;same && i<shown.bins.size();++i)same=shown.bins[i].minimum==d.kick.envelope.bins[i].minimum&&shown.bins[i].maximum==d.kick.envelope.bins[i].maximum;
         check(same,"The render after the drag differs from what the drag showed");
         e.setParameter("punchNormalize",0);e.timerCallback();d.applyRemix();
         same=true;for(size_t i=0;same && i<before.bins.size();++i)same=before.bins[i].minimum==d.kick.envelope.bins[i].minimum&&before.bins[i].maximum==d.kick.envelope.bins[i].maximum;
         check(same && std::abs(d.outputDb[0])<1e-9,"Waveform did not follow a volume change from outside the drag at once");
         refresh(e);}
        // v0.5.21 ATTACK / DECAY NORMALIZE: each part on its own, the final peak (with the
        // whole-Punch trim on top) at -1 dBFS; the other part and the Tail are untouched.
        {check(e.splitNormalizeButtons[0].isVisible()&&e.splitNormalizeButtons[1].isVisible()&&e.splitNormalizeReset.isVisible(),"Attack / decay normalize buttons missing");
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.splitNormalizeButtons[0],&e.splitNormalizeButtons[1],&e.splitNormalizeReset}){
             check(c->getBottom()<=e.completedKick.getBottom()&&c->getY()>=e.lowKeyPunch.getBottom(),"Attack / decay normalize row left the output column");
             for(juce::Component* o:std::initializer_list<juce::Component*>{&e.normalizeButtons[0],&e.normalizeButtons[1],&e.normalizeReset[0],&e.normalizeReset[1],&e.limiterToggle,&e.lowKeyPunch})
                 check(!c->getBounds().intersects(o->getBounds()),"Attack / decay normalize row overlaps the output column");}
         check(!e.splitNormalizeButtons[0].getBounds().intersects(e.splitNormalizeButtons[1].getBounds())&&!e.splitNormalizeButtons[1].getBounds().intersects(e.splitNormalizeReset.getBounds()),"Attack / decay normalize buttons overlap");
         e.setParameter("punchNormalize",-4);refresh(e);
         for(int part=0;part<2;++part){
             const float raw=e.processedPeaks[size_t(2+part)];check(raw>1.e-7f,"Attack / decay peak analysis missing");
             const auto tail=value("tailNormalize"),whole=value("punchNormalize"),other=value(part==0?"punchDecayNormalize":"punchAttackNormalize");
             e.splitNormalizeButtons[size_t(part)].onClick();refresh(e);
             const float trim=value(part==0?"punchAttackNormalize":"punchDecayNormalize");
             check(std::abs(juce::Decibels::gainToDecibels(raw)+trim+whole+1)<.015f,"Attack / decay normalize did not target a final -1 dBFS peak");
             check(value(part==0?"punchDecayNormalize":"punchAttackNormalize")==other&&value("tailNormalize")==tail&&value("punchNormalize")==whole,"Attack / decay normalize changed another trim");
             check(e.processedPeaks[size_t(2+part)]==raw,"Attack / decay analysis must be measured before the trims");
             check(e.gestureParams.empty(),"Attack / decay normalize left a gesture open");}
         check(value("punchAttackNormalize")!=0&&value("punchDecayNormalize")!=0,"Attack / decay normalize did nothing");
         e.splitNormalizeReset.onClick();refresh(e);
         check(value("punchAttackNormalize")==0&&value("punchDecayNormalize")==0&&value("punchNormalize")==-4&&e.gestureParams.empty(),"Attack / decay reset failed");
         e.setParameter("punchNormalize",0);refresh(e);}
        e.setEQView(3);e.collapsed[2]=false;e.resizeLayout();
        check(e.monoSub.isVisible(),"Mono sub control is hidden on Tail wet EQ");
        e.monoSub.setToggleState(true,juce::sendNotificationSync);check(value("tailVerbSubMono")==1,"Mono sub UI not host-bound");
        check(e.monoSubHz.isVisible()&&e.monoSubHz.getMinimum()==90&&e.monoSubHz.getMaximum()==300&&e.monoSubHz.getValue()==90,"Mono sub frequency slider missing or wrong range");
        e.monoSubHz.setValue(180,juce::sendNotificationSync);check(value("tailVerbSubMonoHz")==180,"Mono sub frequency UI not host-bound");
        check(!e.monoSubHz.getBounds().intersects(e.monoSub.getBounds())&&!e.monoSub.getBounds().intersects(e.removeBand.getBounds()),"Mono sub row controls overlap");
        // v0.5.7 CRUNCH LOST ONLY: Tail page, next to LOW CUT, bound, fresh OFF, no overlap.
        {e.selectLayer(1);e.timerCallback();
         check(e.crunchLostOnly.isVisible()&&!e.crunchLostOnly.getToggleState()&&value("crunchLostOnly")==0,"LOST ONLY missing or not OFF");
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.crunchLowCut,&e.crunchLowCutLabel,&e.crunchCharacter,&e.formantAmount,&e.adaptiveAmount,&e.adaptiveLabel,&e.completedKick})
             check(!e.crunchLostOnly.getBounds().intersects(c->getBounds()),"LOST ONLY overlaps a control");
         e.crunchLostOnly.setToggleState(true,juce::sendNotificationSync);check(value("crunchLostOnly")==1,"LOST ONLY not host-bound");
         e.crunchLostOnly.setToggleState(false,juce::sendNotificationSync);
         e.selectLayer(0);e.timerCallback();check(!e.crunchLostOnly.isVisible(),"LOST ONLY shown on the Punch page");e.selectLayer(1);e.timerCallback();}
        e.monoSubHz.setValue(90,juce::sendNotificationSync);
        e.setEQView(4);check(!e.monoSub.isVisible()&&!e.monoSubHz.isVisible(),"Mono sub leaked onto Punch wet EQ");e.setEQView(3);
        // v0.5.20 EQ POSITION: TAIL EQ / PUNCH EQ pages only, each bound to its own parameter, fresh OFF, no overlap.
        for(int page:{0,1}){e.setEQView(page);e.collapsed[2]=false;e.resizeLayout();
            const char* id=page==0?"tailEqPost":"punchEqPost";const char* other=page==0?"punchEqPost":"tailEqPost";
            check(e.eqPostButton.isVisible()&&!e.eqPostButton.getToggleState()&&value(id)==0,"EQ AFTER DISTORTION missing or not OFF");
            check(e.effectPanels[2].contains(e.eqPostButton.getBounds()),"EQ AFTER DISTORTION outside the EQ panel");
            for(juce::Component* c:std::initializer_list<juce::Component*>{&e.addBand,&e.removeBand,&e.eqGraph,&e.bandType,&e.bandEnable,&e.eqEnable,&e.eqSectionBox,&e.eqBandLabel})
                check(!e.eqPostButton.getBounds().intersects(c->getBounds()),"EQ AFTER DISTORTION overlaps an EQ control");
            check(e.addBand.getWidth()>=150&&e.removeBand.getWidth()>=150,"ADD/REMOVE BAND squeezed by EQ AFTER DISTORTION");
            e.eqPostButton.setToggleState(true,juce::sendNotificationSync);check(value(id)==1&&value(other)==0,"EQ AFTER DISTORTION not bound to its own EQ");
            e.eqPostButton.setToggleState(false,juce::sendNotificationSync);check(value(id)==0,"EQ AFTER DISTORTION did not switch back");
            auto* param=p.params.getParameter(id);param->setValueNotifyingHost(1.f);check(e.eqPostButton.getToggleState(),"Host change of the EQ position not shown");
            param->setValueNotifyingHost(0.f);check(!e.eqPostButton.getToggleState(),"Host change of the EQ position not shown");
            e.collapsed[2]=true;e.resizeLayout();check(!e.eqPostButton.isVisible(),"EQ AFTER DISTORTION shown while the EQ is folded");
            e.collapsed[2]=false;e.resizeLayout();}
        for(int page:{2,3,4,5,6}){e.setEQView(page);check(!e.eqPostButton.isVisible(),"EQ AFTER DISTORTION shown on a page it does not belong to");}
        e.setEQView(3);
        const std::array<const char*,4> masterIDs{{"masterAttack","masterDecay","masterSustain","masterRelease"}};
        const std::array<float,4> masterDefaults{{0,0,100,1}};
        for(size_t i=0;i<4;++i){check(value(masterIDs[i])==masterDefaults[i],"ADSR default mismatch");
            e.masterEnvelopeSliders[i].setValue(i==2?63:17,juce::sendNotificationSync);
            check(value(masterIDs[i])==(i==2?63:17),"ADSR UI not attached to host");
            e.masterEnvelopeSliders[i].setValue(masterDefaults[i],juce::sendNotificationSync);}
        refresh(e);
        if(snapshot){
            e.generatedTailSettings.onClick();e.generatedEnable.setToggleState(true,juce::sendNotificationSync);e.generatedEQButton.onClick();refresh(e);
            e.reverbMinimized=true;e.waveFocus=-1;e.selectLayer(1);e.qualityMode.setSelectedId(5,juce::sendNotificationSync);refresh(e);e.saveLayout();e.resizeLayout();
            check(e.linearPhase.isVisible()&&e.linearPhase.getToggleState(),"Tail wet linear phase control missing");
            check(e.monoSub.isVisible(),"Tail wet mono sub control missing");
            e.monoSub.setToggleState(true,juce::sendNotificationSync);check(value("tailVerbSubMono")==1,"Mono sub UI not host-bound");
            e.punchRegionEnable.setToggleState(true,juce::sendNotificationSync);e.regionSplit.setValue(20,juce::sendNotificationSync);
            e.attackEQButton.onClick();e.bandFreq.setValue(4000,juce::sendNotificationSync);e.bandGain.setValue(6,juce::sendNotificationSync);refresh(e);
            checkEffectLayout(e);
            juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot))};
            check(out.openedOk(),"Cannot open snapshot");
            out.setPosition(0);check(out.truncate().wasOk(),"Cannot reset snapshot");
            check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render editor snapshot");
        }
        // v0.4.61: zoom menu + scrollbar on the Punch and Tail waveforms.
        {auto& w=e.tailWave;
         check(w.zoomBox.isVisible()&&e.punchWave.zoomBox.isVisible()&&!e.reverbWave.zoomBox.isVisible(),"Zoom menu must be on Punch and Tail only");
         check(!w.scroll.isVisible()&&w.zoomFactor()==1&&w.viewStartMs()==0,"Waveforms must start at 1x showing the whole file");
         check(w.zoomBox.getNumItems()==7&&w.zoomBox.getItemText(6)=="64x","Zoom steps must be 1x..64x");
         const double dur=p.asset(1)->sample.duration()*1000.0;
         w.setZoomIndex(3);
         check(w.zoomFactor()==8&&w.scroll.isVisible()&&std::abs(w.viewLengthMs()-dur/8)<1e-6,"8x zoom did not show 1/8 of the tail with a scrollbar");
         const double marker=w.markerMs();
         check(marker>=w.viewStartMs()-1e-6&&marker<=w.viewStartMs()+w.viewLengthMs()+1e-6,"Zoom did not frame the START marker");
         check(w.scroll.getCurrentRangeStart()==w.viewStartMs()&&std::abs(w.scroll.getCurrentRangeSize()-w.viewLengthMs())<1e-6,"Scrollbar does not match the zoomed view");
         check(!w.zoomBox.getBounds().intersects(w.plotArea())&&w.getLocalBounds().contains(w.scroll.getBounds())&&w.getLocalBounds().contains(w.zoomBox.getBounds()),"Zoom controls overlap the waveform plot");
         const double before=value("trim")+value("tailExtra");
         // A click maps through the zoomed view (a quarter across = a quarter of the view).
         const auto plot=w.plotArea().toFloat();const float clickX=plot.getX()+plot.getWidth()*.25f;
         const double expected=w.viewStartMs()+w.viewLengthMs()*.25;
         w.mouseDown(event(w,{clickX,55}));w.mouseUp(event(w,{clickX,55}));
         check(std::abs(value("trim")+value("tailExtra")-expected)<.25,"Zoomed click did not map to the visible time");
         // A drag moves the marker zoom times more finely.
         const double start=value("trim")+value("tailExtra");
         w.mouseDown(event(w,{clickX,55}));w.mouseDrag(event(w,{clickX+10,55}));w.mouseUp(event(w,{clickX+10,55}));
         check(std::abs(value("trim")+value("tailExtra")-(start+10*dur/(plot.getWidth()*8)))<.2,"Zoomed drag is not 8x finer");
         // The scrollbar moves the view; the marker data is untouched.
         w.scroll.setCurrentRangeStart(0,juce::sendNotificationSync);check(w.viewStartMs()==0,"Scrollbar did not move the view");
         w.setZoomIndex(0);check(!w.scroll.isVisible()&&w.viewStartMs()==0&&w.zoomFactor()==1,"1x did not restore the full view");
         e.setTrim(false,before);
         auto& pw=e.punchWave;pw.setZoomIndex(5);
         check(pw.zoomFactor()==32&&pw.markerMs()>=pw.viewStartMs()-1e-6&&pw.markerMs()<=pw.viewStartMs()+pw.viewLengthMs()+1e-6,"Punch zoom did not frame the END marker");
         if(snapshot){juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-zoom.png")};
             out.setPosition(0);out.truncate();w.setZoomIndex(3);
             check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render zoom snapshot");w.setZoomIndex(0);}
         pw.setZoomIndex(0);}
        // v0.4.65: mouse wheel zooms the Punch/Tail waveforms around the pointer,
        // Shift + wheel scrolls, double-click returns to 1x; the Reverb view ignores it.
        {auto& w=e.tailWave;const double dur=p.asset(1)->sample.duration()*1000.0;
         const auto plot=w.plotArea().toFloat();const float x=plot.getX()+plot.getWidth()*.6f;
         const double anchor=w.msAtX(x);check(std::abs(anchor-dur*.6)<.01,"msAtX does not map the 1x plot");
         juce::MouseWheelDetails up{0.f,1.f,false,false,false},down{0.f,-1.f,false,false,false};
         w.mouseWheelMove(event(w,{x,55}),up);
         check(w.zoomFactor()==2&&w.zoomIndex()==1&&w.zoomBox.getSelectedId()==2,"wheel up did not zoom to 2x / update the menu");
         check(std::abs(w.msAtX(x)-anchor)<.01,"wheel zoom moved the time under the pointer");
         for(int i=0;i<3;++i)w.mouseWheelMove(event(w,{x,55}),up);
         check(w.zoomFactor()==16&&std::abs(w.msAtX(x)-anchor)<.01&&w.scroll.isVisible(),"repeated wheel zoom lost the anchor");
         check(w.viewStartMs()>=0&&w.viewStartMs()+w.viewLengthMs()<=dur+1e-6,"wheel zoom scrolled outside the file");
         const double before=w.viewStartMs();
         const juce::MouseEvent shifted(juce::Desktop::getInstance().getMainMouseSource(),{x,55},juce::ModifierKeys(juce::ModifierKeys::shiftModifier),1,0,0,0,0,&w,&w,juce::Time::getCurrentTime(),{x,55},juce::Time::getCurrentTime(),1,false);
         w.mouseWheelMove(shifted,down);
         check(w.viewStartMs()>before&&w.zoomFactor()==16,"Shift + wheel did not scroll the view (or changed the zoom)");
         for(int i=0;i<10;++i)w.mouseWheelMove(event(w,{x,55}),up);
         check(w.zoomFactor()==64,"wheel zoom exceeded the 64x menu range");
         w.mouseDoubleClick(event(w,{x,55}));check(w.zoomFactor()==1&&w.viewStartMs()==0&&w.zoomBox.getSelectedId()==1,"double-click did not return to 1x");
         w.mouseWheelMove(event(w,{x,55}),down);check(w.zoomFactor()==1,"wheel down at 1x must stay at 1x");
         auto& r=e.reverbWave;r.mouseWheelMove(event(r,{60,55}),up);check(r.zoomFactor()==1,"the Reverb waveform must not zoom");
         e.punchWave.mouseWheelMove(event(e.punchWave,{x,55}),up);check(e.punchWave.zoomFactor()==2,"Punch waveform wheel zoom");e.punchWave.setZoomIndex(0);}
        // v0.5.13: AUTO-SPLIT KICK removed (user request). The detection row is
        // Detect Punch End | pitch label | LOCK SPLIT | Use detected note, and a
        // single file dropped on the Final Kick Waveform no longer auto-splits.
        {check(e.pitchLabel.getY()==e.punchDetectButton.getY()&&e.pitchLabel.getX()>e.punchDetectButton.getRight()&&e.pitchLabel.getRight()<e.lockSplitButton.getX()&&
               e.lockSplitButton.getRight()<e.detectButton.getX(),"Detection row order / overlap");
         check(e.content.getLocalBounds().contains(e.detectButton.getBounds())&&e.content.getLocalBounds().contains(e.pitchLabel.getBounds()),"Detection row clipped");
         for(auto* c:e.content.getChildren())if(auto* b=dynamic_cast<juce::Button*>(c))check(!b->getButtonText().containsIgnoreCase("AUTO-SPLIT"),"An AUTO-SPLIT button is still in the editor");
         const auto k=e.completedKick.getBounds().getCentre();check(e.dropSlotAt(k.x,k.y)>=0&&e.dropSlotAt(k.x,k.y)<=2,"A drop on the Final Kick Waveform must load an ordinary lane");}
        // v0.4.65: spectral join view on the Final Kick Waveform.
        {auto& k=e.completedKick;
         check(k.joinButton.isVisible()&&!k.joinSpanBox.isVisible()&&!k.joinViewShown(),"join view must start hidden with its span menu");
         for(juce::Component* c:std::initializer_list<juce::Component*>{&k.exportButton,&k.bitDepthBox,&k.dragButton,&k.zoomBox})
             check(!k.joinButton.getBounds().intersects(c->getBounds())&&!k.joinSpanBox.getBounds().intersects(c->getBounds()),"join controls overlap the header controls");
         check(!k.joinButton.getBounds().intersects(k.plotBounds().toNearestInt()),"join button over the plot");
         refresh(e);check(k.renderedAudio()!=nullptr,"no rendered kick for the join view");
         k.joinButton.setToggleState(true,juce::sendNotificationSync);k.joinButton.onClick();
         check(k.joinViewShown()&&k.joinSpanBox.isVisible(),"SPECTRAL JOIN did not switch the view on");
         const auto& a=k.joinAnalysis();
         check(a.valid&&!k.joinImage.isNull()&&a.columns>0&&a.rows>0&&k.joinImage.getWidth()==a.columns&&k.joinImage.getHeight()==a.rows,"join analysis/image not built from the rendered kick");
         check(std::abs(a.joinMs-k.kick.tailStartMs)<1e-9&&std::abs(a.endMs-a.startMs-100.0)<1e-9&&std::abs(k.joinSpanMs()-50.0)<1e-9,"default span is not +-50 ms around the join");
         check(std::isfinite(a.seamRatio())&&a.seamRatio()>=0,"seam ratio not finite");
         k.setJoinSpanIndex(3);check(std::abs(k.joinSpanMs()-200.0)<1e-9&&std::abs(k.joinAnalysis().endMs-k.joinAnalysis().startMs-400.0)<1e-9,"span menu did not re-analyse");
         // Follows the next render.
         e.setParameter("join",3.0);refresh(e);check(k.joinAnalysis().valid&&std::abs(k.joinAnalysis().joinMs-k.kick.tailStartMs)<1e-9,"join view did not follow the new render");
         if(snapshot){juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-join.png")};
             out.setPosition(0);out.truncate();check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render join snapshot");}
         e.setParameter("join",0.0);
         k.setJoinSpanIndex(1);k.setJoinView(false);
         check(!k.joinViewShown()&&!k.joinSpanBox.isVisible()&&!k.joinButton.getToggleState(),"join view did not switch off");
         // Palette-only colour map, monotonic from background to text.
         check(PunchTailKickDisplay::spectrogramColour(-90.f)==tt::ui::pal().bg&&PunchTailKickDisplay::spectrogramColour(0.f)==tt::ui::pal().text,"spectrogram colour map ends");}
        // v0.4.61 regression: the CRUNCH row belongs to the Tail page only (it was
        // drawn over the Final Kick Waveform on the Punch page).
        {e.selectLayer(0);e.timerCallback();
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.formantLabel,&e.formantAmount,&e.crunchCharacter,&e.crunchLowCut,&e.crunchLowCutLabel})
             check(!c->isVisible(),"CRUNCH row visible on the Punch page");
         e.selectLayer(1);e.timerCallback();
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.formantLabel,&e.formantAmount,&e.crunchCharacter,&e.crunchLowCut,&e.crunchLowCutLabel})
             check(c->isVisible()&&!c->getBounds().intersects(e.completedKick.getBounds()),"CRUNCH row missing or over the waveform on the Tail page");}
        // v0.4.61: ABOUT button switches to the About page; BACK returns.
        {check(e.aboutButton.isVisible()&&!e.aboutPage.isVisible(),"About page must start hidden");
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.punchLayer,&e.glide,&e.glideTime,&e.uiScaleBox,&e.uiScaleLabel,&e.layerMute[0],&e.layerMute[1],&e.layerMute[2]})
             check(!e.aboutButton.getBounds().intersects(c->getBounds()),"ABOUT button overlaps the header controls");
         check(e.aboutButton.getX()>1500&&e.aboutButton.getRight()<=e.content.getWidth()-20,"ABOUT button must sit at the right end of the header, away from the title");
         e.aboutButton.onClick();
         check(e.aboutPage.isVisible()&&e.aboutPage.getBounds()==e.content.getLocalBounds()&&
               e.content.getChildComponent(e.content.getNumChildComponents()-1)==&e.aboutPage,"About page must cover the whole editor");
         const auto text=e.aboutPage.allText();
         check(text.contains("Created with different AI models")&&text.contains("Created and distributed by Jei Audio aka Jeipster"),"About page lost the transparency statement");
         check(text.containsIgnoreCase("beta build")&&text.contains("JUCE")&&text.contains("Aleksey Vaneev")&&text.contains("GNU"),"About page lost the beta notice or credits");
         check(text.contains("wrong to Jei Audio:\nJeiaudio.project@gmail.com"),"BETA BUILD notice must give the contact e-mail (v0.5.14)");
         check(!text.contains("installs separately"),"BETA BUILD notice still describes the pre-v0.5 side-by-side installs");
         // v0.4.68: "Extreme v5" is a current engine; the removed Extreme (no version) and Legacy must not appear.
         check(!text.containsIgnoreCase("legacy")&&!text.replace("Extreme v5","").containsIgnoreCase("extreme"),"Removed engines must not be named on the About page");
         check(e.aboutPage.buildInfo().size()>=8&&e.aboutPage.buildInfo()[0].contains(p.getName()),"About page build info missing");
         check(e.aboutPage.backButton.isVisible()&&e.aboutPage.getLocalBounds().contains(e.aboutPage.backButton.getBounds()),"BACK button missing");
         if(snapshot){juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-about.png")};
             out.setPosition(0);out.truncate();
             check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render About snapshot");}
         // v0.5.3 DEFAULT SETTINGS (custom start values for NEW instances; factory defaults kept).
         {tt::defaults::clear();auto& a=e.aboutPage;auto& d=a.defaultsPage;
          check(a.defaultsButton.isVisible()&&a.getLocalBounds().contains(a.defaultsButton.getBounds())&&!a.defaultsButton.getBounds().intersects(a.backButton.getBounds()),"DEFAULT SETTINGS button missing on the About page");
          a.defaultsButton.onClick();
          check(d.isVisible()&&d.getBounds()==a.getLocalBounds(),"Default settings page must cover the About page");
          check(d.toggles.size()==26&&d.statusText().startsWith("Factory defaults"),"Default settings list / status");   // v0.5.25: Stretched glide switch removed
          std::vector<juce::Component*> parts;for(auto& t:d.toggles)parts.push_back(t.button.get());
          for(auto* c:{static_cast<juce::Component*>(&d.engineBox),static_cast<juce::Component*>(&d.punchModeBox),static_cast<juce::Component*>(&d.reverbEngineBox),static_cast<juce::Component*>(&d.uiSizeBox),
                       static_cast<juce::Component*>(&d.engineLabel),static_cast<juce::Component*>(&d.punchModeLabel),static_cast<juce::Component*>(&d.reverbEngineLabel),static_cast<juce::Component*>(&d.uiSizeLabel),
                       static_cast<juce::Component*>(&d.saveCurrentButton),static_cast<juce::Component*>(&d.factoryButton),static_cast<juce::Component*>(&d.statusLabel),static_cast<juce::Component*>(&d.backButton)})parts.push_back(c);
          for(auto* c:parts){check(c->isVisible()&&d.getLocalBounds().contains(c->getBounds()),"Default settings control hidden or outside the page");
              for(auto* o:parts)if(o!=c)check(!c->getBounds().intersects(o->getBounds()),"Default settings controls overlap");}
          for(auto& t:d.toggles)check(d.switchesCard().contains(t.button->getBounds()),"Default switch outside its card");
          check(d.allCard().getBottom()<=d.getHeight(),"Default settings page clipped");
          auto toggle=[&](const char* id)->juce::ToggleButton&{for(auto& t:d.toggles)if(t.id==id)return *t.button;throw std::runtime_error(std::string("Missing default switch ")+id);};
          for(auto& t:d.toggles)check(t.button->getToggleState()==(p.params.getParameter(t.id)->getDefaultValue()>.5f),"Default switch does not show the factory value");
          check(d.engineBox.getSelectedId()==1&&d.punchModeBox.getSelectedId()==2&&d.reverbEngineBox.getSelectedId()==2&&d.uiSizeBox.getSelectedId()==75&&d.uiSizeBox.getText()=="100%","Default engines / size do not show the factory values");
          const float layerNow=value("punchLayer");
          toggle("punchLayer").setToggleState(true,juce::sendNotificationSync);toggle("bassMonoLinear").setToggleState(false,juce::sendNotificationSync);
          toggle("bassMono").setToggleState(true,juce::sendNotificationSync);
          d.engineBox.setSelectedId(3,juce::sendNotificationSync);d.punchModeBox.setSelectedId(3,juce::sendNotificationSync);d.uiSizeBox.setSelectedId(100,juce::sendNotificationSync);
          {const auto m=tt::defaults::load();
           check(m.at("punchLayer")==1&&m.at("bassMonoLinear")==0&&m.at("bassMono")==1&&m.at("pitchExtremeV5")==1&&!m.count("pitchAdaptive")&&m.at("punchPitchFidelity")==1&&!m.count("punchPitchMode")&&m.at("uiScale")==100&&m.size()==6,"Default settings not stored (or factory values stored)");
           check(d.statusText().startsWith("6 settings"),"Default settings status does not count the changes");}
          check(value("punchLayer")==layerNow,"Changing a default changed the open instance");
          // A new instance in a host starts from them; projects opened afterwards win; old projects keep their legacy values.
          {auto freshOwner=std::make_unique<PunchTailProcessor>();auto& fresh=*freshOwner;
           check(fresh.params.getRawParameterValue("punchLayer")->load()==0,"A test-constructed processor (no host) must keep factory values");
           check(fresh.applyUserDefaults()==6,"Custom defaults not applied");
           auto v=[&](const char* id){return fresh.params.getRawParameterValue(id)->load();};
           check(v("punchLayer")==1&&v("bassMonoLinear")==0&&v("bassMono")==1&&fresh.tailEngine()==5&&v("punchPitchFidelity")==1&&v("uiScale")==100,"New instance did not start from the custom defaults");
           auto factoryOwner=std::make_unique<PunchTailProcessor>();juce::MemoryBlock saved;factoryOwner->getStateInformation(saved);
           fresh.setStateInformation(saved.getData(),int(saved.getSize()));
           check(v("punchLayer")==0&&v("bassMono")==0&&fresh.tailEngine()==4,"An opened project must override the custom defaults");
           auto old=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
           for(int i=old.getNumChildren()-1;i>=0;--i)if(old.getChild(i).getProperty("id").toString().startsWith("bassMono"))old.removeChild(i,nullptr);
           juce::MemoryBlock oldBytes;{juce::MemoryOutputStream o(oldBytes,false);old.writeToStream(o);}
           fresh.applyUserDefaults();fresh.setStateInformation(oldBytes.getData(),int(oldBytes.getSize()));
           check(v("bassMono")==0&&v("bassMonoLinear")==0,"An older project must get its legacy values, not the custom defaults");}
          // Back to the factory value removes the entry; SAVE CURRENT stores this instance (not sample-specific values).
          toggle("bassMono").setToggleState(false,juce::sendNotificationSync);check(!tt::defaults::load().count("bassMono"),"Factory value kept in the store");
          {auto* g=p.params.getParameter("glide");g->setValueNotifyingHost(1.f);auto* r=p.params.getParameter("root");const float rootBefore=r->getValue();r->setValueNotifyingHost(r->convertTo0to1(30));
           d.saveCurrentButton.onClick();const auto m=tt::defaults::load();
           check(m.count("glide")&&m.at("glide")==1&&!m.count("root")&&!m.count("trim")&&!m.count("punchNormalize")&&!m.count("pitchCharacter"),"SAVE CURRENT stored the wrong settings");
           for(const auto& [id,val]:m)check(std::abs(val-tt::defaults::factory(p.params,id))>1e-6f,"SAVE CURRENT stored a factory value");
           check(d.toggles.size()>0&&toggle("glide").getToggleState(),"Page not refreshed after SAVE CURRENT");
           g->setValueNotifyingHost(0.f);r->setValueNotifyingHost(rootBefore);}
          // v0.5.30: every EQ is neutral at factory and never a custom default.
          {using tt::defaults::isEq;
           for(const char* id:{"eqOn","eq2Gain","eq7Slope","punchEqOn","punchEq0Freq","reverbEq3Q","tailVerbEqOn","tailVerbEq1Gain","punchVerbEq4Type","punchAttackEqOn","punchAttackEq3On","punchDecayEq4Gain"})
               check(isEq(id)&&!tt::defaults::storable(id),"EQ parameter would become a custom default");
           for(const char* id:{"eqView","eqGeneratedView","eqPunchGeneratedView","punchEqPost","tailEqPost","reverbExtra","punchRegionOn","punchAttackGain","punchDecayGain","tailVerbSubMono","punchRegionSplit"})
               check(!isEq(id),"Non-EQ parameter treated as EQ");
           int eqGains=0;
           for(auto* parameter:p.getParameters())if(auto* rp=dynamic_cast<juce::RangedAudioParameter*>(parameter))
               if(isEq(rp->paramID)&&rp->paramID.endsWith("Gain")){++eqGains;check(rp->convertFrom0to1(rp->getDefaultValue())==0.f,"An EQ band does not start at 0 dB");}
           check(eqGains==7*tt::kBands,"Expected 7 EQs of 8 bands");
           std::vector<std::pair<juce::RangedAudioParameter*,float>> before;
           for(const char* id:{"eq2Gain","punchEq1Gain","punchAttackEq3On","tailVerbEq0Gain","punchDecayEqOn"}){auto* q=p.params.getParameter(id);before.push_back({q,q->getValue()});}
           p.params.getParameter("eq2Gain")->setValueNotifyingHost(p.params.getParameter("eq2Gain")->convertTo0to1(8.5f));
           p.params.getParameter("punchEq1Gain")->setValueNotifyingHost(p.params.getParameter("punchEq1Gain")->convertTo0to1(-6.f));
           p.params.getParameter("punchAttackEq3On")->setValueNotifyingHost(1.f);p.params.getParameter("tailVerbEq0Gain")->setValueNotifyingHost(p.params.getParameter("tailVerbEq0Gain")->convertTo0to1(4.f));
           p.params.getParameter("punchDecayEqOn")->setValueNotifyingHost(0.f);
           d.saveCurrentButton.onClick();
           for(const auto& [id,val]:tt::defaults::load())check(!isEq(id),"SAVE CURRENT stored an EQ setting");
           for(auto& [q,v]:before)q->setValueNotifyingHost(v);
           // EQ entries saved by an older version are ignored; the rest still apply.
           tt::defaults::memoryStore()={{"eq2Gain",8.5f},{"eq3On",1.f},{"punchDecayEq3On",1.f},{"punchAttackEq5Gain",6.5f},{"glide",1.f}};
           check(tt::defaults::load().size()==1&&tt::defaults::load().count("glide"),"Stored EQ entries were not ignored");
           auto freshOwner=std::make_unique<PunchTailProcessor>();auto& fresh=*freshOwner;
           check(fresh.applyUserDefaults()==1,"Only the non-EQ default may apply");
           auto v=[&](const char* id){return fresh.params.getRawParameterValue(id)->load();};
           check(v("glide")==1&&v("eq2Gain")==0&&v("eq3On")==0&&v("punchDecayEq3On")==0&&v("punchAttackEq5Gain")==0,"A new instance did not start with neutral EQs");
           tt::defaults::clear();d.refresh();}
          if(snapshot){juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-defaults.png")};
              out.setPosition(0);out.truncate();check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render defaults snapshot");}
          d.factoryButton.onClick();check(tt::defaults::load().empty()&&d.statusText().startsWith("Factory defaults")&&!toggle("punchLayer").getToggleState()&&d.engineBox.getSelectedId()==1,"FACTORY DEFAULTS did not reset");
          d.backButton.onClick();check(!d.isVisible()&&a.isVisible(),"Default settings BACK did not return to the About page");}
         e.aboutPage.backButton.onClick();check(!e.aboutPage.isVisible()&&!e.aboutButton.getToggleState(),"BACK did not return to the controls");}
        // v0.4.62 ATTACK LAYER row (third row of the Pitch Drop panel).
        {check(e.attackEnable.isVisible()&&e.pitchDropPanel.contains(e.attackEnable.getBounds())&&e.pitchDropPanel.contains(e.attackNote.getBounds()),"Attack layer row missing");
         for(size_t i=0;i<4;++i){check(e.pitchDropPanel.contains(e.attackSliders[i].getBounds())&&e.pitchDropPanel.contains(e.attackLabels[i].getBounds()),"Attack control outside the Pitch Drop panel");
            check(e.attackSliders[i].getY()>e.pitchDropNote.getBottom(),"Attack layer is not a third row under Pitch Drop");
            for(juce::Component* c:std::initializer_list<juce::Component*>{&e.pitchDropSliders[0],&e.pitchDropSliders[1],&e.pitchDropSliders[2],&e.pitchDropTarget,&e.pitchDropNote,&e.attackNote})
                check(!c->getBounds().intersects(e.attackSliders[i].getBounds()),"Attack layer overlaps Pitch Drop controls");}
         check(e.keys.getY()>e.pitchDropPanel.getBottom()&&e.statusLabel.getY()>e.pitchDropPanel.getBottom(),"Pitch Drop panel overlaps the status/keyboard");
         e.attackEnable.setToggleState(true,juce::sendNotificationSync);check(value("attackLayerOn")==1,"Attack ENABLE not host-bound");
         e.attackSliders[0].setValue(4.5,juce::sendNotificationSync);check(std::abs(value("attackLayerLength")-4.5)<.02,"Attack LENGTH not host-bound");
         e.attackSliders[1].setValue(9000,juce::sendNotificationSync);check(std::abs(value("attackLayerStart")-9000)<2,"Attack START not host-bound");
         e.attackSliders[2].setValue(-12,juce::sendNotificationSync);check(std::abs(value("attackLayerLevel")+12)<.11,"Attack LEVEL not host-bound");
         e.attackSliders[0].setValue(30,juce::sendNotificationSync);check(std::abs(value("attackLayerLength")-30)<.02,"Attack LENGTH does not reach 30 ms (v0.5.1: 1-50 ms)");
         e.attackSliders[0].setValue(90,juce::sendNotificationSync);check(value("attackLayerLength")<=50.0f,"Attack LENGTH exceeded 50 ms");
         // v0.5.13 END knob: KEY by default, host-bound, typed values, no overlap with its neighbours.
         check(e.attackLabels[3].getText()=="END (Hz)"&&e.attackSliders[3].getValue()==19&&e.attackSliders[3].getTextFromValue(19)=="Key","Attack END must start at KEY");
         for(size_t i=0;i<3;++i)check(!e.attackSliders[3].getBounds().intersects(e.attackSliders[i].getBounds())&&!e.attackLabels[3].getBounds().intersects(e.attackLabels[i].getBounds()),"Attack END overlaps a knob");
         check(!e.attackNote.getBounds().intersects(e.attackSliders[3].getBounds())&&!e.attackNote.getBounds().intersects(e.attackEnable.getBounds()),"Attack note overlaps END / ENABLE");
         e.attackSliders[3].setValue(1500,juce::sendNotificationSync);check(std::abs(value("attackLayerEnd")-1500)<1,"Attack END not host-bound");
         check(e.attackSliders[3].getValueFromText("k")==19&&std::abs(e.attackSliders[3].getValueFromText("250")-250)<1,"Attack END typed values");
         e.attackSliders[3].setValue(19,juce::sendNotificationSync);check(value("attackLayerEnd")==19,"Attack END did not return to KEY");
         e.attackEnable.setToggleState(false,juce::sendNotificationSync);}
        // v0.4.62 EXPORT WAV / DRAG: the file holds exactly the rendered kick.
        {refresh(e);
         check(e.completedKick.exportButton.isVisible()&&e.completedKick.dragButton.isVisible()&&e.completedKick.bitDepthBox.getSelectedId()==2,"Export controls missing or not 24-bit by default");
         for(juce::Component* c:std::initializer_list<juce::Component*>{&e.completedKick.exportButton,&e.completedKick.bitDepthBox,&e.completedKick.dragButton})
             check(!c->getBounds().intersects(e.completedKick.zoomBox.getBounds())&&!c->getBounds().toFloat().intersects(e.completedKick.plotBounds())&&c->getX()>170,"Export controls overlap the waveform header");
         const auto audio=e.completedKick.renderedAudio();
         check(audio&&audio->left.size()>1000&&e.completedKick.exportButton.isEnabled(),"No rendered kick to export");
         const auto file=e.writeDragFile();
         check(file.existsAsFile()&&file.getFileName().startsWith("Kicktone Kick ")&&file.hasFileExtension("wav"),"Drag WAV was not written");
         juce::AudioFormatManager formats;formats.registerBasicFormats();
         {std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
          check(reader&&reader->numChannels==2&&reader->bitsPerSample==24&&reader->sampleRate==audio->rate&&reader->lengthInSamples==juce::int64(audio->left.size()),"Exported WAV format/length wrong");
          juce::AudioBuffer<float> back(2,int(reader->lengthInSamples));reader->read(&back,0,back.getNumSamples(),0,true,true);
          float worst=0;for(int i=0;i<back.getNumSamples();++i){worst=(std::max)(worst,std::abs(back.getSample(0,i)-std::clamp(audio->left[size_t(i)],-1.f,1.f)));worst=(std::max)(worst,std::abs(back.getSample(1,i)-std::clamp(audio->right[size_t(i)],-1.f,1.f)));}
          check(worst<2.e-6f,"Exported 24-bit WAV differs from the rendered kick");}
         e.completedKick.bitDepthBox.setSelectedId(3,juce::sendNotificationSync);
         {const auto f32=e.writeDragFile();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f32));
          check(reader&&reader->bitsPerSample==32&&reader->usesFloatingPointData,"32-bit float export not float");
          juce::AudioBuffer<float> back(2,int(reader->lengthInSamples));reader->read(&back,0,back.getNumSamples(),0,true,true);
          for(int i=0;i<back.getNumSamples();++i)check(back.getSample(0,i)==audio->left[size_t(i)]&&back.getSample(1,i)==audio->right[size_t(i)],"32-bit float export is not bit-exact");
          f32.getParentDirectory().deleteRecursively();}
         e.completedKick.bitDepthBox.setSelectedId(1,juce::sendNotificationSync);
         {const auto f16=e.writeDragFile();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f16));check(reader&&reader->bitsPerSample==16,"16-bit export");f16.getParentDirectory().deleteRecursively();}
         e.completedKick.bitDepthBox.setSelectedId(2,juce::sendNotificationSync);
         file.getParentDirectory().deleteRecursively();
         check(!tt::exporter::writeWav(pt::Sample{},juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tt-empty.wav"),24),"Empty audio must not export");
         check(tt::exporter::defaultFileName(36)=="Kicktone Kick C3.wav","Export file name must use FL Studio note names");
         e.completedKick.updating=true;e.completedKick.refreshExportState();
         check(!e.completedKick.exportButton.isEnabled()&&!e.completedKick.renderedAudio(),"Export must wait while the waveform is updating");
         e.completedKick.updating=false;e.completedKick.refreshExportState();check(e.completedKick.exportButton.isEnabled(),"Export did not re-enable after rendering");}
        // v0.5.10 PREPARATION CPU selector on the About page, applied live; Quiet (1 core) by default since v0.5.12.
        // v0.5.18: shown as "Background rendering" with Eco / Balanced / Fast (same stored values).
        {check(e.aboutPage.prepBox.getNumItems()==3&&e.aboutPage.prepBox.getSelectedId()==1&&tt::ui::prepEffort()==0&&pt::parallel::effort().load()==0&&pt::parallel::defaultThreads()==1,"Background rendering must default to Eco (1 core)");
         check(e.aboutPage.prepBox.getItemText(0)=="Eco (1 core)"&&e.aboutPage.prepBox.getItemText(1)=="Balanced (half the cores)"&&e.aboutPage.prepBox.getItemText(2)=="Fast (all cores but one)","Background rendering choices renamed wrongly");
         check(e.aboutPage.prepBox.getTooltip()=="How many CPU cores Kicktone uses to prepare notes in the background. Playing and exporting are never slowed down.","Background rendering tooltip wrong");
         check(juce::Font(14.0f,juce::Font::bold).getStringWidth("Background rendering")<=166&&e.aboutPage.prepBox.getX()>=28+24+166,"Background rendering label does not fit before its box");
         check(e.aboutPage.prepBox.getY()>e.aboutPage.themeBox.getBottom()&&e.aboutPage.prepBox.getX()==e.aboutPage.themeBox.getX(),"Background rendering box misplaced");
         e.aboutPage.prepBox.setSelectedId(2,juce::sendNotificationSync);
         check(tt::ui::prepEffort()==1&&pt::parallel::effort().load()==1,"Balanced did not apply");
         e.aboutPage.prepBox.setSelectedId(3,juce::sendNotificationSync);
         check(tt::ui::prepEffort()==2&&pt::parallel::effort().load()==2,"Fast did not apply");
         e.aboutPage.prepBox.setSelectedId(1,juce::sendNotificationSync);
         check(tt::ui::prepEffort()==0&&pt::parallel::effort().load()==0&&pt::parallel::defaultThreads()==1,"Eco did not restore");}
        // v0.5.19 preparation progress bar: right end of the status line, palette colours, readable texts.
        {auto& bar=e.prepBar;
         check(bar.getParentComponent()==&e.content&&bar.isVisible(),"Preparation bar not on the canvas");
         check(bar.getY()==e.statusLabel.getY()&&bar.getHeight()==e.statusLabel.getHeight()&&bar.getRight()==e.baseWidth-28&&bar.getWidth()==e.prepBarWidth,"Preparation bar misplaced");
         check(!bar.getBounds().intersects(e.statusLabel.getBounds())&&e.statusLabel.getX()==28&&e.statusLabel.getRight()+12==bar.getX(),"Preparation bar overlaps the status text");
         check(!bar.getBounds().intersects(e.keys.getBounds())&&e.content.getLocalBounds().contains(bar.getBounds()),"Preparation bar clips");
         check(bar.getTooltip().contains("37 pitched tail keys")&&bar.getTooltip().contains("Background rendering"),"Preparation bar tooltip");
         pt::progress::Snapshot s;s.phase=pt::progress::Phase::Rendering;s.ready=14;s.secondsLeft=24.2;
         bar.setSnapshot(s);check(bar.getText()=="14/37 keys | about 25 s left"&&bar.fillColour()==tt::ui::pal().amber,("Rendering text/colour: "+bar.getText().toStdString()).c_str());
         if(snapshot){const auto file=juce::File(juce::String::fromUTF8(snapshot));
             juce::FileOutputStream out{file.getSiblingFile(file.getFileNameWithoutExtension()+"-prepbar.png")};
             check(out.openedOk()&&out.truncate().wasOk(),"Cannot open the preparation bar snapshot");
             check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render the preparation bar snapshot");}
         s.phase=pt::progress::Phase::Paused;s.startsIn=4.3;s.secondsLeft=84;
         bar.setSnapshot(s);check(bar.getText()=="14/37 keys | the other keys start in 5 s | about 1 min 25 s left"&&bar.fillColour()==tt::ui::pal().muted,("Paused text/colour: "+bar.getText().toStdString()).c_str());
         check(juce::Font(13.0f).getStringWidth(bar.getText())<=e.prepBarWidth-16,"Longest preparation text does not fit the bar");
         s.waitingTurn=true;bar.setSnapshot(s);check(bar.getText()=="14/37 keys | waiting for another Kicktone to finish","Waiting text");
         s={};s.phase=pt::progress::Phase::Ready;s.ready=37;s.secondsLeft=0;
         bar.setSnapshot(s);check(bar.getText()=="All 37 keys ready"&&bar.fillColour()==tt::ui::pal().ok,"Complete text/colour");
         s={};bar.setSnapshot(s);check(bar.getText()=="Load a tail to prepare its 37 keys","No-tail text");
         bar.setSnapshot(p.preparationProgress());}
        // v0.4.62 themes: Charcoal default, live switch from the About page, exact round trip.
        {check(tt::ui::themeIndex()==0&&e.aboutPage.themeBox.getSelectedId()==1&&e.aboutPage.themeBox.getNumItems()==5,"Theme selector/default wrong");
         check(e.theme.findColour(juce::Slider::rotarySliderFillColourId)==juce::Colour(0xfff5f5f5)&&e.qualityLabel.findColour(juce::Label::textColourId)==juce::Colour(0xfff5f5f5)&&
               e.theme.findColour(juce::TextButton::buttonColourId)==juce::Colour(0xff4a4a4a),"Charcoal must keep the original colours");
         e.aboutButton.onClick();
         for(int t=1;t<5;++t){
             e.aboutPage.themeBox.setSelectedId(t+1,juce::sendNotificationSync);
             const auto& p=tt::ui::palettes()[size_t(t)];
             check(tt::ui::themeIndex()==t&&e.theme.findColour(juce::Slider::rotarySliderFillColourId)==p.text&&e.qualityLabel.findColour(juce::Label::textColourId)==p.text&&
                   e.transientHeading.findColour(juce::Label::textColourId)==p.amber&&e.theme.findColour(juce::TextButton::buttonColourId)==p.button,"Theme did not reach every control");
             if(snapshot){e.aboutPage.setVisible(false);
                 juce::FileOutputStream out{juce::File(juce::String::fromUTF8(snapshot)).getSiblingFile(juce::File(juce::String::fromUTF8(snapshot)).getFileNameWithoutExtension()+"-theme"+juce::String(t)+".png")};
                 out.setPosition(0);out.truncate();check(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"Cannot render theme snapshot");
                 e.aboutPage.setVisible(true);}
         }
         // A second open window follows the switch (shared preference).
         {auto otherOwner=std::make_unique<PunchTailEditor>(p);auto& other=*otherOwner;check(other.appliedTheme==4,"New window did not open in the current theme");
          tt::ui::setTheme(3);other.timerCallback();check(other.appliedTheme==3&&other.qualityLabel.findColour(juce::Label::textColourId)==tt::ui::palettes()[3].text,"Other window did not follow the theme");
          e.timerCallback();check(e.appliedTheme==3,"Editor did not follow a theme change from another window");}
         e.aboutPage.themeBox.setSelectedId(1,juce::sendNotificationSync);
         check(e.qualityLabel.findColour(juce::Label::textColourId)==juce::Colour(0xfff5f5f5)&&e.transientHeading.findColour(juce::Label::textColourId)==juce::Colour(0xffe1b477)&&
               e.theme.findColour(juce::Slider::rotarySliderFillColourId)==juce::Colour(0xfff5f5f5),"Switching back to Charcoal did not restore the original colours");
         e.aboutPage.backButton.onClick();}
        e.punchInvert.setToggleState(true,juce::sendNotificationSync);
        e.generatedDistortWet.setToggleState(true,juce::sendNotificationSync);e.generatedPunchSend.setToggleState(true,juce::sendNotificationSync);
        e.reverbMinimized=false;e.saveLayout();e.resizeLayout();
        const auto extra=value("punchExtra"),reverbExtra=value("reverbExtra");
        for(int i=0;i<3;++i){auto* muteParam=p.params.getParameter(i==0?"mutePunch":i==1?"muteTail":"muteReverb");muteParam->setValueNotifyingHost(muteParam->convertTo0to1(1));}
        juce::MemoryBlock saved;p.getStateInformation(saved);
        auto tree=juce::ValueTree::readFromData(saved.getData(),int(saved.getSize()));
        check(tree.getChildWithName("Reverb").isValid(),"Reverb slot missing from saved project");
        e.setTrim(true,0);p.setStateInformation(saved.getData(),int(saved.getSize()));
        check(value("punchExtra")==extra,"Extended trim did not round trip");
        check(value("reverbExtra")==reverbExtra,"Reverb trim did not round trip");
        check(std::abs(value("reverbDelay")-40)<.01,"Reverb delay did not round trip in DAW state");
        check(std::abs(value("reverbLevel")+12)<.01,"Reverb level did not round trip in DAW state");
        check(value("reverbLowCut")==3,"Reverb 48 dB/oct setting did not round trip");
        check(value("punchInvert")==1 && value("tailVerbKeepDry")==1 && value("tailVerbDistortWet")==1 && value("tailVerbPunchSend")==1 && value("tailVerbTailSend")==0 && value("tailVerbPunchPreDelay")==125 && value("tailVerbPunchFade")==240 && value("phaseAuto")==1 && value("phaseInvert")==1 && std::abs(value("phaseFine")-.25)<.011,
              "New phase state did not round trip in the project");
        check(value("mutePunch")==1 && value("muteTail")==1 && value("muteReverb")==1,
              "Layer mute settings did not round trip in host state");
        check(value("punchDistOn")==1 && value("tailDistOn")==1,"Distortion enabled state did not round trip");
        check(value("punchDistMode")==1 && value("tailDistMode")==2,"Distortion algorithm did not round trip");
        check(value("transientOn")==1 && std::abs(value("transientAttack")-66)<.1 &&
              std::abs(value("transientSustain")+22)<.1 && std::abs(value("transientSensitivity")+42)<.1,
              "Transient settings did not round trip in host state");
        check(std::abs(value("punchDistDrive")-17)<.01 && std::abs(value("tailDistMix")-60)<.01,
              "Distortion settings did not round trip");
        auto legacy=p.params.copyState();
        for(int i=legacy.getNumChildren()-1;i>=0;--i){auto id=legacy.getChild(i).getProperty("id").toString();if(id=="punchExtra" || id=="tailExtra" || id=="reverbOffset" || id=="reverbExtra" || id=="reverbDelay" || id=="reverbLevel" || id=="reverbLowCut" || id.startsWith("punchDist") || id.startsWith("tailDist") || id.startsWith("eq") || id.startsWith("punchEq") || id.startsWith("reverbEq") || id=="eqView" || id=="uiScale" || id.startsWith("transient") || id.startsWith("phase") || id=="punchInvert" || id=="tailVerbKeepDry" || id=="tailVerbDistortWet" || id=="tailVerbPunchSend" || id=="tailVerbTailSend" || id=="tailVerbPunchPreDelay" || id=="tailVerbPunchFade" || id.startsWith("punchVerb") || id=="mutePunch" || id=="muteTail" || id=="muteReverb" || id=="outputLimiter")legacy.removeChild(i,nullptr);}
        juce::MemoryBlock bytes;{juce::MemoryOutputStream stream(bytes,false);legacy.writeToStream(stream);}
        p.setStateInformation(bytes.getData(),int(bytes.getSize()));
        check(value("punchExtra")==0 && value("tailExtra")==0,"Legacy state retained extended trim");
        check(value("reverbOffset")==0 && value("reverbExtra")==0,"Legacy state retained reverb trim");
        check(value("reverbDelay")==0,"Legacy project did not default reverb playback delay to 0");
        check(value("reverbLevel")==0,"Legacy project did not default reverb level to unity (0 dB)");
        check(value("reverbLowCut")==0,"Legacy project enabled the new low-cut filter");
        check(value("punchDistOn")==0 && value("tailDistOn")==0,"Legacy project enabled the new distortion effects");
        check(value("transientOn")==0 && value("transientAttack")==30 && value("transientMix")==100,
              "Legacy project did not restore neutral, bypassed transient defaults");
        check(value("eqOn")==0,"Older state unexpectedly enabled legacy EQ");
        check(value("outputLimiter")==0,"Older project must load with the output limiter OFF");
        check(value("uiScale")==100,"Older state did not default interface to 100%");
        check(value("punchInvert")==0 && value("tailVerbKeepDry")==0 && value("tailVerbDistortWet")==0 && value("tailVerbPunchSend")==0 && value("tailVerbTailSend")==1 && value("tailVerbPunchPreDelay")==0 && value("tailVerbPunchFade")==0 && value("phaseAuto")==0 && value("phaseInvert")==0 && value("phaseFine")==0,
              "Older project unexpectedly activated the phase controls");
        check(value("mutePunch")==0 && value("muteTail")==0 && value("muteReverb")==0,
              "Older project must restore all three layers unmuted");
        check(value("punchEqOn")==1 && value("reverbEqOn")==1 && value("eqView")==0,
              "Older preset did not restore neutral independent EQs and Kick tab");
        // Each lane has a distinct accessible Unload control. A cleared sample
        // must vanish from the individual and composite waveform previews.
        check(e.theme.findColour(juce::Slider::rotarySliderFillColourId)==juce::Colour(0xfff5f5f5),
              "Rotary knobs lost the white accent theme");
        check(e.sourcePanels[0].contains(e.unloadButtons[0].getBounds()),
              "Punch Unload button is outside the source panel");
        PunchTailPlaybackTest::setup(p);
        refresh(e);
        for(int slot=0;slot<3;++slot) {
            check(e.unloadButtons[size_t(slot)].isEnabled(),"Loaded sample did not enable Unload");
            if(slot<2)check(e.trimButtons[size_t(slot)].isEnabled(),"Loaded sample did not enable silence trim");
            e.unloadButtons[size_t(slot)].onClick();
            check(!p.asset(slot) && !e.displayed[size_t(slot)],"Unload did not clear waveform/source");
            check(!e.unloadButtons[size_t(slot)].isEnabled(),"Unload remained enabled on an empty slot");
            if(slot<2)check(!e.trimButtons[size_t(slot)].isEnabled(),"Empty sample did not disable silence trim");
            for(int other=slot+1;other<3;++other)
                check(bool(p.asset(other)),"Unloading one waveform cleared another lane");
        }
        check(!p.previewBank(),"Unloading tail did not reset the composite pitch preview");
        check(e.punchButton.isEnabled() && e.tailButton.isEnabled() && e.reverbButton.isEnabled(),
              "Unload disabled Load buttons");
        std::cout<<"PASS actual mouse callbacks on punch, tail and reverb waveforms, detection button, knob sync, host gestures, long trim state and legacy automation\n";
    }
};
int main(int argc,char** argv)try{juce::ScopedJuceInitialiser_GUI init;tt::ui::themeStorageDisabled()=true;tt::defaults::storageDisabled()=true;std::cout<<"Fixture sizes: processor "<<sizeof(PunchTailProcessor)<<" bytes, editor "<<sizeof(PunchTailEditor)<<" bytes (heap owned)"<<std::endl;PunchTailEditorTest::run(argc>1?argv[1]:nullptr);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
