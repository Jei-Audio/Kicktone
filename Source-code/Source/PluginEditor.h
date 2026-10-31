#pragma once
#include "PluginProcessor.h"
#include "SpectralJoin.h"
#include "WaveformEnvelope.h"
#include "KickOverview.h"
#include "WaveformEdit.h"
#include <functional>
#include <algorithm>
#include <cmath>
#include <array>
#include <memory>
#include <vector>
#include "EQGraph.h"
#include "PunchRegionWaveform.h"
#include "ProcessedPreview.h"
#include "Theme.h"

class MasterEnvelopeGraph : public juce::Component {
public:
    tt::MasterEnvelopeSettings settings;
    void paint(juce::Graphics& g) override {
        const auto& pal=tt::ui::pal();
        const auto box=getLocalBounds().toFloat();g.setColour(pal.envBg);g.fillRoundedRectangle(box,5);
        const auto b=box.reduced(9);g.setColour(pal.envGrid);
        for(int i=0;i<4;++i){const float y=b.getY()+b.getHeight()*float(i)/3;g.drawHorizontalLine(int(y),b.getX(),b.getRight());}
        const double hold=(std::max)(100.0,settings.attackMs+settings.decayMs+settings.releaseMs);
        const double total=settings.attackMs+settings.decayMs+settings.releaseMs+hold;
        const auto x=[&](double t){return b.getX()+float(t/total)*b.getWidth();};
        const auto y=[&](double level){return b.getBottom()-float(level)*b.getHeight();};
        const double sustain=settings.sustainPercent*.01;
        juce::Path curve;curve.startNewSubPath(x(0),y(settings.attackMs>0?0:1));
        curve.lineTo(x(settings.attackMs),y(1));curve.lineTo(x(settings.attackMs+settings.decayMs),y(sustain));
        curve.lineTo(x(total-settings.releaseMs),y(sustain));curve.lineTo(x(total),y(0));
        g.setColour(pal.envCurve);g.strokePath(curve,juce::PathStrokeType(2));
    }
};

// Shared editing convention for every numeric control.
class TailtoneSlider : public juce::Slider {
public:
    void mouseDown(const juce::MouseEvent& e) override {
        last=e.position;fine=e.position;
        if(e.mods.isAltDown()){juce::Slider::mouseDoubleClick(e);return;}
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        fine+=(e.position-last)*(e.mods.isShiftDown()?0.1f:1.0f);last=e.position;
        juce::Slider::mouseDrag(e.withNewPosition(fine));
    }
    void mouseDoubleClick(const juce::MouseEvent&) override {showTextBox();}
    // v0.4.60: -/+ step buttons at the left edge of a linear slider. They are
    // children of the slider, so they show/hide/enable with it; the shared look
    // and feel moves the track right by stepArea. One click = `step` in the
    // slider's own units (or, if proportional, that fraction of the track, for
    // skewed ranges such as Hz). Shift-click = a tenth of a step. Hold to repeat.
    // Each click is a complete host gesture, like typing a value.
    static constexpr int stepButtonWidth=20, stepArea=46;
    void enableStepButtons(double stepSize,bool proportionalStep=false) {
        step=stepSize;proportional=proportionalStep;
        if(!minus) {
            minus=std::make_unique<juce::TextButton>("-");plus=std::make_unique<juce::TextButton>("+");
            for(int i=0;i<2;++i){auto& b=i==0?*minus:*plus;
                b.setRepeatSpeed(400,60);b.setWantsKeyboardFocus(false);
                b.setTooltip(i==0?"One step down (Shift-click for a smaller step). Hold to repeat.":"One step up (Shift-click for a smaller step). Hold to repeat.");
                b.onClick=[this,i]{stepBy(i==0?-1:1,juce::ModifierKeys::currentModifiers.isShiftDown());};
                addAndMakeVisible(b);}
        }
        resized();
    }
    bool hasStepButtons() const { return minus!=nullptr; }
    double stepSize() const { return step; }
    void stepBy(int direction,bool fineStep) {
        if(!isEnabled()||direction==0)return;
        const double interval=getInterval();
        double next;
        if(proportional) {
            const double amount=step*(fineStep?.1:1.);
            next=proportionOfLengthToValue(std::clamp(valueToProportionOfLength(getValue())+direction*amount,0.0,1.0));
        } else {
            const double amount=(std::max)(step*(fineStep?.1:1.),interval);
            // Land on whole multiples of the step so repeated clicks stay tidy.
            double base=getValue()/amount;
            if(std::abs(base-std::round(base))<1e-3)base=std::round(base); // float parameter intervals
            const double snapped=direction>0?std::floor(base)+1:std::ceil(base)-1;
            next=snapped*amount;
        }
        next=std::clamp(next,getMinimum(),getMaximum());
        if(next==getValue())return;
        ScopedDragNotification gesture(*this);
        setValue(next,juce::sendNotificationSync);
    }
    void resized() override {
        juce::Slider::resized();
        if(!minus)return;
        const int h=(std::min)(getHeight(),26),y=(getHeight()-h)/2;
        minus->setBounds(0,y,stepButtonWidth,h);plus->setBounds(stepButtonWidth+2,y,stepButtonWidth,h);
    }
    juce::TextButton* minusButton() const { return minus.get(); }
    juce::TextButton* plusButton() const { return plus.get(); }
private:
    juce::Point<float> last,fine;
    std::unique_ptr<juce::TextButton> minus,plus;
    double step=0;bool proportional=false;
};

// Editor look and feel: charcoal V4 theme; reserves the -/+ area on stepped sliders.
class TailtoneLookAndFeel : public juce::LookAndFeel_V4 {
public:
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override {
        auto layout=juce::LookAndFeel_V4::getSliderLayout(slider);
        if(auto* stepped=dynamic_cast<TailtoneSlider*>(&slider); stepped && stepped->hasStepButtons())
            layout.sliderBounds.setLeft((std::min)(layout.sliderBounds.getRight(),layout.sliderBounds.getX()+TailtoneSlider::stepArea));
        return layout;
    }
};

// An editable waveform. Its min/max buckets are prepared on JUCE's message
// thread when the decoded sample changes; paint() never decodes or scans audio.
// No waveform work is performed in the realtime audio callback.
class PunchTailWaveform final : public juce::Component, public juce::SettableTooltipClient, private juce::ScrollBar::Listener {
public:
    PunchTailWaveform(bool punchView, bool reverbView = false);   // accent colour follows the theme
    void setSample(std::shared_ptr<const PunchTailProcessor::Asset> next);
    void setGuideMilliseconds(double ms);
    // v0.4.57 (Tail view only): END marker = dry Tail end cut from the file end,
    // with its fade-out drawn before the cut. Grab it within 9 px to drag.
    void setEndGuide(double trimFromEndMs,double fadeMs);
    // v0.4.66 LOCK SPLIT: non-empty = this view shows the whole split file and
    // its marker is the shared CUT (caption drawn top-left).
    void setLockCaption(const juce::String& caption);
    const juce::String& lockCaptionText() const { return lockCaption; }
    // v0.5.33 (Punch view): with the Punch layer on, Punch 2 is drawn in purple
    // where it plays - from ATTACK END (split % of the kept punch) on, moved by
    // PUNCH 2 OFFSET, untuned - and Punch 1 only up to ATTACK END (both inside the
    // TRANSITION, where both are heard). No layer = the plain Punch 1 view.
    void setLayer(std::shared_ptr<const PunchTailProcessor::Asset> layer,double splitPercent,double transitionMs,double offsetMs);
    bool showsLayer() const { return isPunch && layerSample && !layerEnvelope.bins.empty(); }
    double layerSplitMs() const;                       // ATTACK END in file time
    std::function<void(bool)> onEndGesture;
    std::function<void(double)> onEndTrim;
    std::function<void(bool)> onGesture;
    std::function<void()> onSelect;
    std::function<void(double)> onTrim;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    // v0.4.65: mouse wheel zooms around the pointer (Shift + wheel scrolls the
    // zoomed view), double-click returns to 1x. Reverb view: no zoom.
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // v0.4.61: zoom (Punch and Tail views). 1x shows the whole file; higher
    // zooms centre on the marker being edited and the scrollbar moves the view.
    static constexpr int kZoomSteps=7;                 // 1x .. 64x
    void setZoomIndex(int index);                      // 0 = 1x
    // v0.4.65: zoom step keeping the file time under `anchorX` (component px) fixed.
    void zoomAt(int index,float anchorX);
    int zoomIndex() const { return zoomBox.getSelectedId()-1; }
    double zoomFactor() const { return zoom; }
    double viewStartMs() const { return viewStart; }
    double viewLengthMs() const;
    double msAtX(float x) const;                       // file time under a component x (visible slice)
private:
    bool isPunch, isReverb;
    juce::ComboBox zoomBox;
    juce::ScrollBar scroll {false};
    double zoom=1.0, viewStart=0.0;
    bool zoomable() const { return !isReverb; }
    double sampleMs() const;
    juce::Rectangle<int> plotArea() const;
    float msToX(double ms,juce::Rectangle<float> plot) const;
    double markerMs() const;                           // edited marker position in file time
    void clampView();
    void syncScroll();
    void keepVisible(double ms);
    void scrollBarMoved(juce::ScrollBar*,double start) override {viewStart=start;clampView();repaint();}
    std::shared_ptr<const PunchTailProcessor::Asset> sample, layerSample;
    pt::WaveformEnvelope envelope, layerEnvelope;
    double layerSplitPercent=20, layerTransitionMs=2, layerOffsetMs=0;
    size_t envelopeBins() const { return zoomable()?size_t(1024)<<(kZoomSteps-1):size_t(1024); }
    double guideMs = 0.0, dragGuide=0, dragX=0, dragDuration=0;
    double endGuideMs = 0.0, endFadeMs = 10.0;
    juce::String lockCaption;
    bool dragging=false, draggingEnd=false;
    bool hasEndHandle() const { return !isPunch && !isReverb; }
    friend struct PunchTailEditorTest;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PunchTailWaveform)
};

// v0.4.62: a button that starts an external (OS) drag of the exported kick
// once the mouse moves a few pixels while pressed.
class TailtoneDragButton final : public juce::TextButton {
public:
    using juce::TextButton::TextButton;
    std::function<void()> onDragOut;
    void mouseDown(const juce::MouseEvent& e) override { started=false; juce::TextButton::mouseDown(e); }
    void mouseDrag(const juce::MouseEvent& e) override {
        juce::TextButton::mouseDrag(e);
        if(!started && e.getDistanceFromDragStart()>4){started=true;if(onDragOut)onDragOut();}
    }
private:
    bool started=false;
};

// The LARGE strip below the knobs: an actual composite waveform from the
// punch onset to the final tail sample, not two flat duration rectangles.
class PunchTailKickDisplay final : public juce::Component, public juce::SettableTooltipClient, private juce::ScrollBar::Listener {
public:
    PunchTailKickDisplay();
    void setAudio(const pt::Sample* punch, const pt::Sample* preparedTail,
                  pt::Settings settings, int rootNote, bool ready, const pt::Sample* reverb = nullptr);
    std::function<void(pt::KickHandle,bool)> onGesture;
    std::function<void(pt::KickHandle,double)> onEdit;
    std::function<void(bool,bool)> onOutputGesture;
    std::function<void(bool,double)> onOutputEdit;
    // v0.4.62 export: the latest complete render (null while nothing is loaded).
    std::function<void()> onExport, onDragOut;
    std::shared_ptr<const pt::Sample> renderedAudio() const { return updating||shownGains!=renderedGains?nullptr:rendered; }
    int renderedNote() const { return note; }
    int exportBits() const { return bitDepthBox.getSelectedId()==1?16:bitDepthBox.getSelectedId()==3?32:24; }
    void setExportMessage(const juce::String& text);
    void refreshExportState() { const bool ok=rendered!=nullptr && !updating && shownGains==renderedGains; exportButton.setEnabled(ok); dragButton.setEnabled(ok); }
    void paint(juce::Graphics&) override;
    void setRendered(const PreviewResult&);
    // v0.5.28: draw the kick for these Punch / Tail normalize gains at once, by
    // remixing the last render's buses (remixPreview) until a render for them arrives.
    void showOutputGains(std::array<float,2> gains);
    bool outputDragActive() const { return outputDragging; }
    std::shared_ptr<const PreviewBuses> renderedBuses() const { return buses; }   // v0.5.32: base of Punch-only renders
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool updating=false;
    // v0.4.65 spectral join view: a spectrogram of the rendered kick around the
    // punch-to-tail join with a seam figure (flux at the join vs. neighbours).
    // Analysed on the message thread from the latest finished render; paint()
    // only draws the prepared image.
    void setJoinView(bool on);
    bool joinViewShown() const { return joinView; }
    void setJoinSpanIndex(int index);                 // 0..3 = +-25/50/100/200 ms
    // v0.5.1: view settings remembered with the project.
    int joinSpanIndex() const { return juce::jlimit(0,3,joinSpanBox.getSelectedId()-1); }
    int zoomIndex() const { return juce::jlimit(0,3,zoomBox.getSelectedId()-1); }
    void setZoomIndex(int index) { zoomBox.setSelectedId(juce::jlimit(0,3,index)+1,juce::sendNotificationSync); }
    int bitDepthIndex() const { return juce::jlimit(0,2,bitDepthBox.getSelectedId()-1); }   // 0 16-bit, 1 24-bit, 2 32f
    void setBitDepthIndex(int index) { bitDepthBox.setSelectedId(juce::jlimit(0,2,index)+1,juce::dontSendNotification); }
    double joinSpanMs() const;
    const pt::spectral::JoinAnalysis& joinAnalysis() const { return join; }
    static juce::Colour spectrogramColour(float db);  // palette-derived colour map
private:
    friend struct PunchTailEditorTest;
    juce::TextButton joinButton {"SPECTRAL JOIN"};
    juce::ComboBox joinSpanBox;
    bool joinView=false;
    pt::spectral::JoinAnalysis join;
    juce::Image joinImage;
    void rebuildJoinAnalysis();
    void paintJoinView(juce::Graphics&,juce::Rectangle<float> plot);
    pt::KickOverview kick;
    std::array<TailtoneSlider,2> joinControls;
    std::array<juce::Label,2> joinLabels;
    std::array<juce::TextButton,2> minusButtons,plusButtons,resetButtons;
    juce::ComboBox zoomBox;
    juce::TextButton exportButton {"EXPORT WAV"};
    juce::ComboBox bitDepthBox;
    TailtoneDragButton dragButton {"DRAG"};
    std::shared_ptr<const pt::Sample> rendered;
    juce::String exportMessage;
    juce::uint32 exportMessageTime=0;
    juce::ScrollBar scroll {false};
    double zoom=1,viewStart=0;
    bool syncing=false;
    bool outputDragging=false, outputDragTail=false;
    double outputDragStartY=0, outputDragStartDb=0;
    std::array<double,2> outputDb{};
    std::shared_ptr<const PreviewBuses> buses;
    pt::WaveformEnvelope renderedEnvelope;
    std::array<float,2> renderedGains{{1,1}}, shownGains{{1,1}};
    bool remixPending=false;
    void applyRemix();   // in paint(): one remix per repaint, however many mouse moves came in
    juce::Rectangle<float> plotBounds() const;
    double viewDuration() const {return (std::max)(1.0,kick.totalMs)/zoom;}
    float timeX(double ms) const;
    void syncControls();
    void editJoin(int index,double value);
    void scrollBarMoved(juce::ScrollBar*,double start) override {viewStart=start;repaint();}
    pt::Settings currentSettings;
    double punchDuration=0;
    int note = 36;
    bool hasPunch = false, hasTail = false, pitchReady = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PunchTailKickDisplay)
};

// v0.4.61 ABOUT page: transparency about how Tailtone is made, who made it,
// what it is built on, build details and the editing shortcuts. It covers the
// whole editor canvas while shown; BACK returns to the controls.
// v0.5.3 DEFAULT SETTINGS page (opened from the About page): the values a NEW
// Tailtone instance starts with, stored per computer (Source/UserDefaults.h).
class TailtoneDefaultsPage final : public juce::Component {
public:
    TailtoneDefaultsPage();
    void setContext(juce::AudioProcessor* p,juce::AudioProcessorValueTreeState* s){processor=p;state=s;refresh();}
    std::function<void()> onBack;
    juce::TextButton backButton {"BACK"}, saveCurrentButton {"SAVE CURRENT SETTINGS AS DEFAULTS"}, factoryButton {"FACTORY DEFAULTS"};
    struct Toggle { juce::String id; std::unique_ptr<juce::ToggleButton> button; };
    std::vector<Toggle> toggles;
    juce::ComboBox engineBox, punchModeBox, reverbEngineBox, uiSizeBox;
    juce::Label engineLabel, punchModeLabel, reverbEngineLabel, uiSizeLabel, statusLabel;
    static const std::vector<std::pair<const char*,const char*>>& toggleList();   // {parameter ID, label}
    void refresh();                                   // show the effective start values
    void storeValue(const juce::String& id,float value);   // equal to factory = removed from the store
    juce::String statusText() const;
    juce::Rectangle<int> switchesCard() const, modesCard() const, allCard() const;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    juce::AudioProcessor* processor=nullptr;
    juce::AudioProcessorValueTreeState* state=nullptr;
    bool refreshing=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TailtoneDefaultsPage)
};

class TailtoneAboutPage final : public juce::Component {
public:
    TailtoneAboutPage();
    // v0.5.3: DEFAULT SETTINGS button + page (shown over the About page).
    juce::TextButton defaultsButton {"DEFAULT SETTINGS"};
    TailtoneDefaultsPage defaultsPage;
    void showDefaults(bool show);
    std::function<void()> onBack;
    std::function<void(int)> onTheme;   // v0.4.62 theme selector
    juce::TextButton backButton {"BACK"};
    juce::ComboBox themeBox;
    void setThemeIndex(int index) { themeBox.setSelectedId(index+1,juce::dontSendNotification); }
    // v0.5.10 PREPARATION CPU (Eco / Balanced / Fast; shown as BACKGROUND RENDERING since v0.5.18), saved on this computer.
    juce::ComboBox prepBox;
    std::function<void(int)> onPrepEffort;
    void setLogo(const juce::Image& image) { logo=image; repaint(); }
    void setProductName(const juce::String& name) { product=name; repaint(); }
    // Rows of "label|value" for the VERSION & BUILD card.
    void setBuildInfo(const juce::StringArray& rows);
    const juce::StringArray& buildInfo() const { return build; }
    // Every sentence shown on the page (used by the editor tests).
    juce::String allText() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    int appearanceY() const;
    static const char* const aiStatement;
    static const char* const creatorStatement;
private:
    juce::Image logo;
    juce::String product;
    juce::StringArray build;
    struct Card { juce::String title, body; juce::Colour accent; };
    static juce::StringArray shortcuts();
    static juce::String creditsText();
    static juce::String betaText();
    static const char* const kContactEmail;   // v0.5.14
    static juce::String aiDetailText();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TailtoneAboutPage)
};

// v0.5.19 PREPARATION PROGRESS: how many of the 37 tail keys are ready and the
// estimated time left (pt::progress). The editor timer passes a snapshot in;
// colours come from the palette at paint time.
class TailtonePreparationBar final : public juce::Component, public juce::SettableTooltipClient {
public:
    void setSnapshot(const pt::progress::Snapshot& s) {
        const juce::String t(pt::progress::describe(s));
        if(t==text && s.ready==shown.ready && s.total==shown.total && s.phase==shown.phase && s.waitingTurn==shown.waitingTurn)return;
        shown=s;text=t;repaint();
    }
    const juce::String& getText() const { return text; }
    const pt::progress::Snapshot& snapshot() const { return shown; }
    // amber while rendering, muted while paused / waiting, green when complete
    juce::Colour fillColour() const {
        const auto& pal=tt::ui::pal();
        if(shown.complete())return pal.ok;
        if(shown.waitingTurn || shown.phase==pt::progress::Phase::Paused || shown.phase==pt::progress::Phase::Settling)return pal.muted;
        return pal.amber;
    }
    void paint(juce::Graphics& g) override {
        const auto& pal=tt::ui::pal();
        const auto box=getLocalBounds().toFloat().reduced(.5f);
        g.setColour(pal.plot);g.fillRoundedRectangle(box,4);
        if(shown.phase!=pt::progress::Phase::NoTail && shown.fraction()>0) {
            g.setColour(fillColour().withAlpha(.45f));
            g.fillRoundedRectangle(box.withWidth((std::max)(8.0f,box.getWidth()*shown.fraction())),4);
        }
        g.setColour(pal.thinLine);g.drawRoundedRectangle(box,4,1);
        g.setColour(shown.phase==pt::progress::Phase::NoTail?pal.plotDim:pal.text);g.setFont(juce::Font(13.0f));
        g.drawFittedText(text,getLocalBounds().reduced(8,0),juce::Justification::centred,1);
    }
private:
    pt::progress::Snapshot shown;
    juce::String text {pt::progress::describe(pt::progress::Snapshot{})};
};

class PunchTailEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit PunchTailEditor(PunchTailProcessor&);
    ~PunchTailEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&,int,int) override;
    void fileDragEnter(const juce::StringArray&,int,int) override;   // v0.4.69: Punch 2 drop highlight
    void fileDragMove(const juce::StringArray&,int,int) override;
    void fileDragExit(const juce::StringArray&) override;
private:
    PunchTailProcessor& proc;
    juce::Component content; // Single logical canvas, scaled exactly once
    TailtoneLookAndFeel theme;
    juce::TooltipWindow tooltips {this, 700};
    juce::TextButton punchButton {"Load punch"}, tailButton {"Load tail"}, reverbButton {"Load reverb"}, detectButton {"Use detected note"};
    // v0.5.36: one detect button in each of the Punch and Tail windows, beside Trim.
    juce::TextButton punchDetectButton {"Detect End"}, tailDetectButton {"Detect Start"};
    // v0.4.66: Punch END and Tail START move together (one shared cut).
    juce::TextButton lockSplitButton {"LOCK SPLIT"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockSplitAttachment;
    std::array<juce::TextButton,2> trimButtons {juce::TextButton{"Trim Punch"},juce::TextButton{"Trim Tail"}};
    std::array<juce::TextButton,3> unloadButtons {juce::TextButton{"Unload"}, juce::TextButton{"Unload"}, juce::TextButton{"Unload"}};
    juce::ToggleButton gate {"Follow MIDI note length"};
    juce::ToggleButton lowKeyPunch {"D2-C#3: octave-down punch"};
    // v0.4.49 options, unticked by default.
    juce::ToggleButton punchLayer {"PUNCH LAYER"}, glide {"GLIDE"};
    TailtoneSlider glideTime;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> punchLayerAttachment, glideAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> glideTimeAttachment;
    juce::Label punch2Title, punch2Name;
    juce::TextButton punch2Load {"Load punch 2"}, punch2Unload {"Unload"};
    TailtoneSlider punch2Offset;juce::Label punch2OffsetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> punch2OffsetAttachment;
    // v0.4.67: Punch 2 tuning (semitones, cents via the 0.01 st step).
    TailtoneSlider punch2Pitch;juce::Label punch2PitchLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> punch2PitchAttachment;
    juce::Rectangle<int> punch2Panel;
    bool punchLayerShown() const;
    // v0.4.69: lane a file dropped at a logical canvas point goes to:
    // 0 Punch, 1 Tail, 2 Reverb, 3 Punch 2 (row shown). (v0.5.13: 4 AUTO-SPLIT removed.)
    int dropSlotAt(int logicalX,int logicalY) const;
    juce::Point<int> logicalPoint(int x,int y) const;
    void setPunch2DropHover(bool);
    bool punch2DropHover=false;
    // v0.4.52: Auto Crunch Match (replaces the v0.4.51 FORMANT test).
    TailtoneSlider formantAmount;juce::Label formantLabel; // CRUNCH amount (member names kept)
    juce::ComboBox crunchCharacter;
    juce::ToggleButton wetAutoMatch {"AUTO MATCH WET"}, wetPhaseLock {"LOW PHASE LOCK"};   // v0.4.54
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetAutoMatchAttachment, wetPhaseLockAttachment;
    TailtoneSlider crunchLowCut;juce::Label crunchLowCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crunchLowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> formantAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> crunchCharacterAttachment;
    void rebuildEngineItems();
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowKeyPunchAttachment;
    std::array<juce::ToggleButton,3> layerMute;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,3> layerMuteAttachments;
    juce::Label phaseHeading, phaseFineLabel, phaseScore, qualityLabel;
    juce::ComboBox qualityMode, punchPitchMode;
    juce::Slider adaptiveAmount;
    juce::Label adaptiveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> adaptiveAttachment;
    juce::Label punchPitchModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> punchPitchModeAttachment;

    juce::ToggleButton phaseAuto {"AUTO MATCH"}, phaseInvert {"INVERT TAIL"}, punchInvert {"INVERT PUNCH"};
    TailtoneSlider phaseFine;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> phaseAutoAttachment, phaseInvertAttachment, punchInvertAttachment;
    // v0.4.67: pitch the punch with the tail on every tail key.
    juce::ToggleButton punchFollowTail {"PUNCH FOLLOWS TAIL PITCH"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> punchFollowTailAttachment;
    juce::ToggleButton punchFollowDecayOnly {"ONLY PUNCH 2 FOLLOWS"};   // v0.5.4
    // v0.5.5 PUNCH DECAY NOTE row (Punch page): measured note of the decay + USE DECAY NOTE.
    juce::ToggleButton crunchLostOnly {"LOST ONLY"};   // v0.5.7
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> crunchLostOnlyAttachment;
    // v0.5.10 PUNCH HARMONY row (Punch page): mode, key and scale.
    juce::Label punchHarmonyLabel, punchHarmonyKeyLabel, punchHarmonyScaleLabel;
    juce::ComboBox punchHarmony, punchHarmonyKey, punchHarmonyScale;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> punchHarmonyAttachment, punchHarmonyKeyAttachment, punchHarmonyScaleAttachment;
    juce::Label decayNoteLabel;
    juce::TextButton decayNoteButton {"USE DECAY NOTE"};
    static juce::String decayNoteText(const pt::decaynote::Result&,bool punchLoaded);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> punchFollowDecayOnlyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> phaseFineAttachment;
    juce::Label statusLabel, pitchLabel;
    TailtonePreparationBar prepBar;              // v0.5.19, right end of the status line
    static constexpr int prepBarWidth=520;
    struct Control { TailtoneSlider slider; juce::Label label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment; };
    std::array<Control,14> controls;
    std::array<Control,2> tailEndControls; // v0.4.57: TAIL END (tailEndTrim), END FADE (tailEndFade)
    juce::ToggleButton generatedEnable {"ENABLE"}, generatedKeepDry {"KEEP DRY"}, generatedDistortWet {"DISTORT WET"}, generatedPunchSend {"PUNCH REVERB"}, generatedTailSend {"TAIL REVERB"}, generatedRootPitch {"ROOT RENDER + PITCH"};
    juce::TextButton generatedEQButton {"EDIT WET EQ"}, generatedTailSettings {"TAIL SETTINGS"}, generatedPunchSettings {"PUNCH SETTINGS"};
    juce::Label generatedTitle;
    std::array<Control,12> generatedControls; // [11] = v0.4.59 Tail fade-in
    std::array<Control,8> punchGeneratedControls;
    std::array<Control,2> verbDuckControls; // v0.4.51: PUNCH DUCK, DUCK RELEASE
    bool punchGeneratedPage=false;
    void showGeneratedSettings(bool punch);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> generatedEnableAttachment, generatedKeepDryAttachment, generatedDistortWetAttachment, generatedPunchSendAttachment, generatedTailSendAttachment, generatedRootPitchAttachment;
    juce::Rectangle<int> generatedPanel;
    // v0.4.47: taller Experimental Reverb panel gives its rotary knobs more room.
    static constexpr int generatedPanelHeight=298;
    // v0.5.13 (user request): the Experimental Reverb panel can be minimized to
    // its title row (+ ENABLE). Saved with the project (uiGeneratedMinimized),
    // like the effect folds; projects without it open expanded.
    static constexpr int generatedMinimizedHeight=36;
    juce::TextButton generatedFold;
    bool generatedMinimized=false;
    int generatedHeight() const { return generatedMinimized?generatedMinimizedHeight:generatedPanelHeight; }
    void updateGeneratedVisibility();
    juce::ComboBox reverbLowCut;
    juce::ComboBox generatedEngine; juce::Label generatedEngineLabel; // v0.4.59 Legacy / V2
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> generatedEngineAttachment;
    juce::Label reverbLowCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbLowCutAttachment;
    struct DistortionControls {
        juce::ToggleButton enabled {"ENABLE"};
        juce::ComboBox algorithm;
        juce::Label title;
        std::array<Control,4> knobs;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algorithmAttachment;
    };
    std::array<DistortionControls,2> distortion;
    std::array<juce::TextButton,2> distortionTabs {juce::TextButton{"PUNCH DIST."},juce::TextButton{"TAIL + REVERB"}};
    int distortionPage=0;
    void showDistortionPage(int);
    juce::ToggleButton transientEnable {"ON"};
    juce::Label transientHeading;
    std::array<Control,8> transientControls;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> transientEnableAttachment;
    TailtoneEQGraph eqGraph;
    juce::Label eqHeading, eqBandLabel, eqFreqLabel, eqGainLabel, eqQLabel, eqSlopeLabel, uiScaleLabel;
    juce::TextButton addBand {"+ ADD POINT"}, removeBand {"REMOVE POINT"};
    juce::ToggleButton monoSub {"MONO SUB"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoSubAttachment;
    TailtoneSlider monoSubHz; // v0.4.56: 90..300 Hz mono sub crossover
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> monoSubHzAttachment;
    juce::ToggleButton linearPhase {"LINEAR PHASE"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linearAttachment;
    juce::ToggleButton eqEnable {"EQ ON"}, bandEnable {"BAND ON"};
    juce::ComboBox bandType, bandSlope, uiScaleBox, eqSectionBox;
    TailtoneSlider bandFreq, bandGain, bandQ;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqEnableAttachment;
    // v0.5.20 EQ POSITION: shown on the TAIL EQ / PUNCH EQ pages, attached to
    // tailEqPost / punchEqPost (re-attached by setEQView like eqEnable).
    juce::ToggleButton eqPostButton {"EQ AFTER DISTORTION"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqPostAttachment;
    static const char* eqPostID(int view) { return view==1?"punchEqPost":view==0?"tailEqPost":nullptr; }
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> eqSectionAttachment;
    int selectedBand=1, activeEQ=0;
    std::array<int,7> selectedBandPerEQ {1,1,1,1,1,1,1};
    float uiScale=1.0f;
    bool resizingFromPreset=true, updatingEQControls=false;
    static constexpr int baseWidth=2329;   // v0.4.64 (was 1740): new Envelope/Pitch Drop column
    int baseHeight=1105;
    std::array<juce::TextButton,2> normalizeButtons, normalizeReset;
    std::array<juce::TextButton,2> splitNormalizeButtons;   // v0.5.21: 0 Normalize Attack, 1 Normalize Decay
    juce::TextButton splitNormalizeReset;                   // resets both
    void normalizeSplit(int part);
    std::array<float,4> processedPeaks{};   // see PreviewResult (2 attack, 3 decay)
    juce::TextButton reverbFold;
    bool reverbMinimized=true;
    int selectedLayer=0, waveFocus=-1;
    std::array<bool,3> collapsed{};
    std::array<juce::TextButton,3> layerTabs, expandWave, foldButtons;
    juce::TextButton clipButton {"OUTPUT"};
    juce::TextButton aboutButton {"ABOUT"};   // v0.4.61
    TailtoneAboutPage aboutPage;
    void showAbout(bool show);
    void applyTheme();                // v0.4.62
    int appliedTheme=-1, seenThemeEpoch=-1;
    void refreshAboutInfo();
    juce::ToggleButton limiterToggle {"OUTPUT LIMITER"}; // v0.4.60, fresh ON
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limiterAttachment;
    float limiterHold=1.0f;
    juce::Label layerHeading, editHelp, previewLabel;
    std::array<juce::Label,3> effectActivity;
    std::array<float,3> meterHold{};
    std::array<juce::Rectangle<int>,3> sourcePanels, effectPanels;
    std::array<juce::Label,3> sourceNames;
    PunchRegionWaveform punchRegionWave;
    juce::ToggleButton punchRegionEnable {"ENABLE"};
    juce::Label punchRegionTitle,regionSplitLabel,regionTransitionLabel,attackGainLabel,decayGainLabel;
    juce::Image jeiLogo;
    TailtoneSlider regionSplit,regionTransition,attackGain,decayGain;
    juce::TextButton attackEQButton {"ATTACK EQ"},decayEQButton {"DECAY EQ"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> regionEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> regionSplitAttachment,regionTransitionAttachment,attackGainAttachment,decayGainAttachment;
    juce::Rectangle<int> punchRegionPanel,masterEnvelopePanel;
    juce::Label masterEnvelopeTitle,masterEnvelopeNote;
    juce::Rectangle<int> pitchDropPanel;
    juce::Label pitchDropTitle,pitchDropNote;
    juce::ToggleButton pitchDropEnable{"ENABLE"};
    juce::ComboBox pitchDropTarget;
    std::array<juce::Label,3> pitchDropLabels;
    std::array<TailtoneSlider,3> pitchDropSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,3> pitchDropAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pitchDropEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pitchDropTargetAttachment;
    // v0.4.62 ATTACK LAYER: third row of the Pitch Drop panel.
    juce::Label attackTitle,attackNote;
    juce::ToggleButton attackEnable{"ENABLE"};
    std::array<juce::Label,4> attackLabels;
    std::array<TailtoneSlider,4> attackSliders;   // LENGTH, START, LEVEL, END (v0.5.13)
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,4> attackAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attackEnableAttachment;
    // v0.5 MONO BASS panel (right column, under Pitch Drop / Attack Layer).
    juce::Rectangle<int> bassMonoPanel;
    juce::Label bassMonoTitle,bassMonoNote,bassMonoHzLabel,bassMonoMixLabel;
    juce::ToggleButton bassMonoEnable{"ENABLE"};
    juce::ToggleButton bassMonoLinear{"LINEAR PHASE"};   // v0.5.1
    TailtoneSlider bassMonoHz,bassMonoMix;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bassMonoEnableAttachment,bassMonoLinearAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassMonoHzAttachment,bassMonoMixAttachment;
    static constexpr int fullHeight=1065;   // v0.4.64 (was 1515): Envelope + Pitch Drop moved to their own column
    MasterEnvelopeGraph masterEnvelopeGraph;
    std::array<juce::Label,4> masterEnvelopeLabels;
    std::array<TailtoneSlider,4> masterEnvelopeSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,4> masterEnvelopeAttachments;
    int selectedEQView() const;
    ProcessedPreview previewWorker;
    uint64_t previewGeneration=0;
    std::vector<float> lastEffectParams;
    double lastPreviewRate=0;
    std::array<float,2> lastNormalizeGains{};   // v0.5.28: see timerCallback
    // v0.5.32: Punch-chain parameters are tracked apart; while the shown render
    // comes from the latest full render, changing only them redoes just the Punch.
    std::vector<float> lastPunchParams;
    uint64_t fullPreviewGeneration=0, shownFullGeneration=0;
    void applyLayout();
    void resizeLayout();
    void saveLayout();
    void selectLayer(int);
    void restoreLayout();
    // v0.5.1: page/view choices that are not host parameters (Reverb TAIL/PUNCH
    // SETTINGS, waveform zooms, spectral join view, export bit depth) are kept in
    // the project state. The timer saves user changes and applies project loads.
    struct ViewState {
        int reverbPage=0,punchZoom=0,tailZoom=0,kickZoom=0,joinView=0,joinSpan=1,bitDepth=1;
        bool operator==(const ViewState& o) const { return reverbPage==o.reverbPage&&punchZoom==o.punchZoom&&tailZoom==o.tailZoom&&kickZoom==o.kickZoom&&joinView==o.joinView&&joinSpan==o.joinSpan&&bitDepth==o.bitDepth; }
        bool operator!=(const ViewState& o) const { return !(*this==o); }
    };
    ViewState currentView() const;
    ViewState storedView() const;
    void applyView(const ViewState&);
    void storeView(const ViewState&);
    void syncViewState();
    ViewState lastView;
    void selectBand(int);
    void setEQView(int);
    juce::String eqPrefix() const;
    juce::String eqOnID() const;
    void syncEQControls();
    void setBandParameter(int,const char*,double);
    void addEQBand(double freq=1500,double gain=0,int type=tt::Bell);   // v0.5.9: type (bell / low shelf / high shelf)
    void updateScale(bool writeParameter);
    void setUIScale(double scale);
    void showUiScale();                      // v0.5.24: preset id, or the size as text between presets
    // v0.5.24: the largest scale <= wanted at which a baseWidth x height window (plus
    // room for the host's window frame) fits `area`; never below 50 %. Empty area = wanted.
    static double screenFitScale(double wanted,int height,juce::Rectangle<int> area);
    double fitToScreen(double wanted) const; // screen under the mouse; tests (Undefined wrapper) keep wanted
    static constexpr int screenMarginX=24, screenMarginY=72;
    void place(juce::Component&,int,int,int,int);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAttachment;
    juce::MidiKeyboardComponent keys;
    std::unique_ptr<juce::FileChooser> chooser;
    void syncPunchPitchMode();               // v0.4.63 combo <- punchPitchMode/punchPitchFidelity
    void exportKick();                       // v0.4.62 Export WAV (save dialog)
    void dragKickOut();                      // v0.4.62 drag the kick into a DAW
    juce::File writeDragFile();              // temp WAV used for drag-and-drop
    juce::File lastExportFolder() const;
    std::array<std::shared_ptr<const PunchTailProcessor::Asset>,3> displayed;
    PunchTailWaveform punchWave, tailWave, reverbWave;
    PunchTailKickDisplay completedKick;
    std::shared_ptr<const PunchTailProcessor::Asset> displayedPreparedPunch;
    std::shared_ptr<const PunchTailProcessor::Bank> displayedBank;
    std::shared_ptr<const PunchTailProcessor::CrunchBank> displayedCrunch;
    std::shared_ptr<const pt::Sample> displayedTailWet;
    std::shared_ptr<const PunchTailProcessor::PhaseMap> displayedPhase;
    std::array<float, 21> lastPreviewParams {};
    std::vector<juce::RangedAudioParameter*> gestureParams;
    bool syncingTrimControls=false;
    void beginGesture(std::initializer_list<const char*> ids);
    void endGesture();
    void setParameter(const char* id,double value);
    void setTrim(bool punch,double ms);
    // v0.4.66 LOCK SPLIT: both lanes hold the same sample, and
    // while locked the Punch ends at one cut and the Tail starts CROSSFADE ms
    // before it (the crossfade then blends identical audio).
    bool lockedPair=false;
    std::array<const void*,2> lockedSeen{{nullptr,nullptr}};   // lane pointers last checked
    void updateLockedPair();
    bool lockedSplitActive() const { return lockedPair && proc.lockSplitOn(); }
    double clampLockedCut(double ms) const;     // >= 3 ms of punch, >= 40 ms of tail
    void trimGesture(bool punch);               // this lane's trims (+ the other lane's while locked)
    void syncLockedGuides();                    // CUT captions on both waveforms
    double lockedCutTarget(bool& changed) const; // Tail START for the current cut
    void setReverbTrim(double ms);
    void detectPunchEnd();
    void detectTailStart();   // v0.5.36
    void updateDetectButtons();   // v0.5.36: enable + tooltips of both detect buttons
    void browse(int);
    friend struct PunchTailEditorTest;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PunchTailEditor)
};
