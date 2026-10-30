#include "PluginEditor.h"
#include "BinaryData.h"
#include "KickExport.h"
#include "ParallelRender.h"   // v0.5.10 preparation CPU
#include <algorithm>
#include <cmath>

namespace {
// v0.4.62: the editor colours follow the selected theme (Theme.h). They are
// refreshed on the message thread whenever the theme changes; theme 0
// (Charcoal) keeps the original values: bg 242424, panel 353535, muted c3c3c3,
// whiteAccent f5f5f5, amber e1b477, thinLine 515151, violet b5a6df.
juce::Colour bg, panel, muted, whiteAccent, amber, thinLine, violet, titleColour;
void refreshThemeColours() {
    const auto& p=tt::ui::pal();
    bg=p.bg;panel=p.panel;muted=p.muted;whiteAccent=p.text;amber=p.amber;thinLine=p.thinLine;violet=p.violet;titleColour=p.title;
}
}

PunchTailWaveform::PunchTailWaveform(bool punchView, bool reverbView)
    : isPunch(punchView), isReverb(reverbView) {
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    setTooltip(punchView ? "Drag the END marker (or click) to choose where the punch stops. Hold Shift to move it slowly. Mouse wheel zooms in where you point, Shift + wheel scrolls, double-click zooms back out. With Punch 2 on, the purple part is Punch 2: it takes over from ATTACK END."
                         : reverbView ? "Drag START to skip the beginning of the reverb file. The reverb always starts where the punch ends. Hold Shift to move it slowly."
                                      : "Drag the START marker (or click) to skip the beginning of the tail. Drag the END marker (bottom right) to make the dry tail shorter; it fades out over END FADE and the reverb keeps ringing. Hold Shift to move slowly. Mouse wheel zooms in where you point, Shift + wheel scrolls, double-click zooms back out.");
    // v0.4.61: zoom menu + scrollbar (Punch and Tail only).
    for(int i=0;i<kZoomSteps;++i)zoomBox.addItem(juce::String(1<<i)+"x",i+1);
    zoomBox.setSelectedId(1,juce::dontSendNotification);
    zoomBox.setTooltip(punchView?"Zoom in around the END marker. 1x shows the whole punch. Use the scrollbar to move around."
                                :"Zoom in around the START marker. 1x shows the whole tail. Use the scrollbar to move around.");
    zoomBox.onChange=[this]{
        const int id=zoomBox.getSelectedId();if(id<1)return;
        zoom=double(1<<(id-1));
        viewStart=markerMs()-viewLengthMs()*.5;   // centre on the marker being edited
        clampView();repaint();
    };
    scroll.setAutoHide(false);scroll.addListener(this);
    scroll.setColour(juce::ScrollBar::thumbColourId,tt::ui::pal().scrollThumb);
    scroll.setColour(juce::ScrollBar::trackColourId,tt::ui::pal().bg);
    if(zoomable()){addAndMakeVisible(zoomBox);addChildComponent(scroll);}
}
double PunchTailWaveform::sampleMs() const { return sample ? sample->sample.duration()*1000.0 : 0.0; }
double PunchTailWaveform::viewLengthMs() const { return sampleMs()/zoom; }
juce::Rectangle<int> PunchTailWaveform::plotArea() const {
    return getLocalBounds().reduced(12).withTrimmedTop(18).withTrimmedBottom(17);
}
float PunchTailWaveform::msToX(double ms,juce::Rectangle<float> plot) const {
    const double view=viewLengthMs();
    return plot.getX()+float(view>0?(ms-viewStart)/view:0.0)*plot.getWidth();
}
double PunchTailWaveform::markerMs() const {
    const double d=sampleMs();
    return std::clamp(isPunch?d-guideMs:guideMs,0.0,d);
}
void PunchTailWaveform::setZoomIndex(int index) {
    zoomBox.setSelectedId(std::clamp(index,0,kZoomSteps-1)+1,juce::sendNotificationSync);
}
double PunchTailWaveform::msAtX(float x) const {
    const auto plot=plotArea().toFloat();
    if(plot.getWidth()<=0)return viewStart;
    return viewStart+double((x-plot.getX())/plot.getWidth())*viewLengthMs();
}
void PunchTailWaveform::zoomAt(int index,float anchorX) {
    if(!zoomable() || !sample)return;
    index=std::clamp(index,0,kZoomSteps-1);
    const double anchorMs=msAtX(anchorX);
    const auto plot=plotArea().toFloat();
    const double fraction=plot.getWidth()>0?std::clamp(double((anchorX-plot.getX())/plot.getWidth()),0.0,1.0):0.5;
    zoom=double(1<<index);
    // Keep the file time under the pointer where it is, instead of recentring
    // on the marker (the zoom menu's behaviour).
    viewStart=anchorMs-fraction*viewLengthMs();
    clampView();
    zoomBox.setSelectedId(index+1,juce::dontSendNotification);
    repaint();
}
void PunchTailWaveform::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w) {
    if(!zoomable() || !sample)return;
    if(e.mods.isShiftDown()) {
        // Scroll the zoomed view (same convention as the Final Kick Waveform).
        if(zoom<=1.0)return;
        viewStart=viewStart-double(w.deltaY+w.deltaX)*viewLengthMs()*.4;
        clampView();repaint();
        return;
    }
    const float delta=w.deltaY!=0.f?w.deltaY:w.deltaX;
    if(delta==0.f)return;
    zoomAt(zoomIndex()+(delta>0.f?1:-1),e.position.x);
}
void PunchTailWaveform::mouseDoubleClick(const juce::MouseEvent& e) {
    if(!zoomable() || zoom<=1.0)return;
    zoomAt(0,e.position.x);
}
void PunchTailWaveform::clampView() {
    const double d=sampleMs();
    viewStart=std::clamp(viewStart,0.0,(std::max)(0.0,d-viewLengthMs()));
    if(zoom<=1.0)viewStart=0;
    syncScroll();
}
void PunchTailWaveform::syncScroll() {
    const double d=sampleMs();
    scroll.setRangeLimits(0,(std::max)(1.0,d),juce::dontSendNotification);
    scroll.setCurrentRange(viewStart,(std::max)(1.e-6,viewLengthMs()),juce::dontSendNotification);
    scroll.setVisible(zoomable() && zoom>1.0 && d>0);
}
void PunchTailWaveform::keepVisible(double ms) {
    if(zoom<=1.0)return;
    const double view=viewLengthMs(),margin=view*.05;
    if(ms<viewStart+margin)viewStart=ms-margin;
    else if(ms>viewStart+view-margin)viewStart=ms-view+margin;
    else return;
    clampView();
}
void PunchTailWaveform::resized() {
    const int w=58;
    zoomBox.setBounds(std::clamp((getWidth()-w)/2,74,(std::max)(74,getWidth()-12-w)),2,w,17);
    const auto plot=plotArea();
    scroll.setBounds(plot.getX(),plot.getBottom()+1,plot.getWidth(),9);
}
void PunchTailWaveform::setEndGuide(double trimFromEndMs,double fadeMs) {
    if(std::abs(endGuideMs-trimFromEndMs)<0.001 && std::abs(endFadeMs-fadeMs)<0.001)return;
    endGuideMs=trimFromEndMs;endFadeMs=fadeMs;repaint();
}

void PunchTailWaveform::setSample(std::shared_ptr<const PunchTailProcessor::Asset> next) {
    if (sample == next) return;
    if(dragging){dragging=false;if(onGesture)onGesture(false);}
    if(draggingEnd){draggingEnd=false;if(onEndGesture)onEndGesture(false);}
    sample = std::move(next);
    // v0.4.61: enough peak buckets for the deepest zoom (64 x 1024; never more than
    // one bucket per frame). Built here on the message thread, never in paint().
    envelope = sample ? pt::buildWaveformEnvelope(sample->sample.left, sample->sample.right, envelopeBins())
                      : pt::WaveformEnvelope{};
    if(zoom>1.0)viewStart=markerMs()-viewLengthMs()*.5;
    clampView();
    repaint();
}

void PunchTailWaveform::setLayer(std::shared_ptr<const PunchTailProcessor::Asset> layer,double splitPercent,double transitionMs,double offsetMs) {
    if(!isPunch)layer.reset();
    if(layer==layerSample && splitPercent==layerSplitPercent && transitionMs==layerTransitionMs && offsetMs==layerOffsetMs)return;
    if(layer!=layerSample) {
        layerSample=std::move(layer);
        // Built here on the message thread, like the Punch 1 envelope; paint() only reads it.
        layerEnvelope=layerSample?pt::buildWaveformEnvelope(layerSample->sample.left,layerSample->sample.right,envelopeBins())
                                 :pt::WaveformEnvelope{};
    }
    layerSplitPercent=splitPercent;layerTransitionMs=transitionMs;layerOffsetMs=offsetMs;
    repaint();
}
double PunchTailWaveform::layerSplitMs() const {
    // Same point as the mix in PunchTailProcessor::rebuildPunch: split % of the
    // punch that is kept (the file minus the Punch END trim), counted from 0.
    const double kept=(std::max)(0.0,sampleMs()-(std::max)(0.0,guideMs));
    return std::clamp(layerSplitPercent*.01,0.0,1.0)*kept;
}

void PunchTailWaveform::setLockCaption(const juce::String& caption) {
    if(caption==lockCaption)return;
    lockCaption=caption;repaint();
}

void PunchTailWaveform::setGuideMilliseconds(double ms) {
    if (std::abs(guideMs - ms) < 0.001) return;
    guideMs = ms;
    repaint();
}

void PunchTailWaveform::mouseDown(const juce::MouseEvent& e) {
    if(onSelect)onSelect();
    if(!sample || !e.mods.isLeftButtonDown()) return;
    dragX=e.position.x;
    dragDuration=sample->sample.duration()*1000.0;
    const auto plot=plotArea();
    const auto plotF=plot.toFloat();
    // The visible window is a 1/zoom slice of a virtual full-file plot.
    const double fullWidth=plot.getWidth()*zoom;
    const double fullLeft=plot.getX()-(dragDuration>0?viewStart/dragDuration:0.0)*fullWidth;
    if(hasEndHandle() && dragDuration>0) {
        const double endMarker=msToX(dragDuration-std::clamp(endGuideMs,0.0,dragDuration),plotF);
        if(std::abs(endMarker-e.position.x)<=9) {
            draggingEnd=true;dragGuide=endGuideMs;
            if(onEndGesture)onEndGesture(true);
            return;
        }
    }
    dragging=true; dragGuide=guideMs;
    if(onGesture)onGesture(true);
    const double marker=msToX(isPunch?dragDuration-guideMs:guideMs,plotF);
    // Grabbing an existing handle must not jump it to the mouse's edge.
    if(!e.mods.isShiftDown() && std::abs(marker-e.position.x)>9) {
        dragGuide=pt::trimAtPosition(isPunch,e.position.x,fullLeft,fullWidth,dragDuration);
        setGuideMilliseconds(dragGuide); if(onTrim)onTrim(dragGuide);
    }
}
void PunchTailWaveform::mouseDrag(const juce::MouseEvent& e) {
    // Zoomed drags move the marker zoom times more finely (same pixel feel).
    const double plotWidth=double(getWidth()-24)*zoom;
    if(draggingEnd) {
        const double limit=(std::max)(0.0,dragDuration-guideMs);   // never past the START marker
        const double value=std::clamp(pt::dragWaveformTrim(true,dragGuide,dragX,e.position.x,
                                            dragDuration,plotWidth,e.mods.isShiftDown()),0.0,limit);
        dragGuide=value;dragX=e.position.x;
        setEndGuide(value,endFadeMs); if(onEndTrim)onEndTrim(value);
        keepVisible(dragDuration-value);
        return;
    }
    if(!dragging)return;
    const double value=pt::dragWaveformTrim(isPunch,dragGuide,dragX,e.position.x,
                                            dragDuration,plotWidth,e.mods.isShiftDown());
    dragGuide=value;dragX=e.position.x;
    setGuideMilliseconds(value); if(onTrim)onTrim(value);
    keepVisible(isPunch?dragDuration-value:value);
}
void PunchTailWaveform::mouseUp(const juce::MouseEvent&) {
    if(draggingEnd){draggingEnd=false;if(onEndGesture)onEndGesture(false);return;}
    if(!dragging)return; dragging=false;if(onGesture)onGesture(false);
}

void PunchTailWaveform::paint(juce::Graphics& g) {
    const juce::Colour accent=isPunch?amber:isReverb?violet:whiteAccent; // v0.4.62: follows the theme
    const auto rect = getLocalBounds().toFloat();
    g.setColour(panel.brighter(0.07f));
    g.fillRoundedRectangle(rect, 9.0f);
    g.setColour(thinLine.withAlpha(0.8f));
    g.drawRoundedRectangle(rect.reduced(0.5f), 9.0f, 1.0f);

    const auto plot = plotArea().toFloat();
    if (plot.isEmpty()) return;
    g.setColour(muted.withAlpha(0.30f));
    for (int i = 1; i < 5; ++i) {
        const float x = plot.getX() + plot.getWidth() * float(i) / 5.0f;
        g.drawVerticalLine(int(x), plot.getY(), plot.getBottom());
    }
    g.setColour(muted.withAlpha(0.22f));
    g.drawHorizontalLine(int(plot.getCentreY()), plot.getX(), plot.getRight());

    if (!sample || envelope.bins.empty()) {
        g.setColour(muted);
        g.setFont(juce::Font(15.0f));
        g.drawFittedText("Load or drop a WAV / MP3 sample", plot.toNearestInt(), juce::Justification::centred, 1);
        return;
    }
    const double durationMs = sample->sample.duration() * 1000.0;
    const double viewMs = viewLengthMs();
    // The waveform is scaled uniformly, retaining its relative dynamics. With
    // Punch 2 shown, both punches share one scale (they are mixed as they are).
    const bool layerShown=showsLayer();
    const float peak=layerShown?(std::max)(envelope.peak,layerEnvelope.peak):envelope.peak;
    const float gain = peak > 0.0000001f ? 1.0f / peak : 0.0f;
    const float amp = plot.getHeight() * 0.46f;
    const float centre = plot.getCentreY();
    // One min/max column per pixel over the visible slice of the bucket list.
    // `env` is read at file time - shiftMs, only for file times fromMs..toMs.
    const auto drawWave=[&](const pt::WaveformEnvelope& env,double envMs,double shiftMs,double fromMs,double toMs,juce::Colour colour) {
        const size_t count=env.bins.size();
        if(count==0 || envMs<=0)return;
        const int columns=(std::max)(2,int(std::ceil(plot.getWidth())));
        const double binsPerMs=double(count)/envMs;
        std::vector<float> tops,bottoms;std::vector<float> xs;
        for(int c=0;c<columns;++c) {
            const double ms0=viewStart+viewMs*double(c)/double(columns);
            const double ms1=viewStart+viewMs*double(c+1)/double(columns);
            if(ms1<=fromMs || ms0>=toMs || ms1-shiftMs<=0 || ms0-shiftMs>=envMs)continue;
            size_t first=size_t(std::clamp(std::floor((ms0-shiftMs)*binsPerMs),0.0,double(count-1)));
            size_t last=size_t(std::clamp(std::ceil((ms1-shiftMs)*binsPerMs),double(first+1),double(count)));
            float lo=env.bins[first].minimum,hi=env.bins[first].maximum;
            for(size_t i=first+1;i<last;++i){lo=(std::min)(lo,env.bins[i].minimum);hi=(std::max)(hi,env.bins[i].maximum);}
            xs.push_back(plot.getX()+plot.getWidth()*float(c)/float(columns-1));
            tops.push_back(centre-hi*gain*amp);bottoms.push_back(centre-lo*gain*amp);
        }
        if(xs.size()<2)return;
        auto clip=plot.toNearestInt().expanded(1,0);
        if(fromMs>-1.e300){const int x=juce::roundToInt(msToX(fromMs,plot));clip.setLeft((std::max)(clip.getX(),x));}
        if(toMs<1.e300){const int x=juce::roundToInt(msToX(toMs,plot));clip.setRight((std::min)(clip.getRight(),x));}
        if(clip.isEmpty())return;
        g.saveState();g.reduceClipRegion(clip);
        juce::Path shape;
        for(size_t c=0;c<xs.size();++c){if(c==0)shape.startNewSubPath(xs[0],tops[0]);else shape.lineTo(xs[c],tops[c]);}
        for(size_t c=xs.size();c-->0;)shape.lineTo(xs[c],bottoms[c]);
        shape.closeSubPath();
        g.setColour(colour.withAlpha(0.65f));
        g.fillPath(shape);
        g.setColour(colour.withAlpha(0.98f));
        g.strokePath(shape, juce::PathStrokeType(1.2f));
        g.restoreState();
    };
    constexpr double kAll=1.e301;
    if(!layerShown) drawWave(envelope,durationMs,0.0,-kAll,kAll,accent);
    else {
        // v0.5.33: what the punch plays with the Punch layer on (see rebuildPunch's
        // mix): Punch 1 until ATTACK END, Punch 2 after it; inside the TRANSITION
        // both are heard. Punch 1 after the transition is not heard: not drawn.
        const double splitMs=layerSplitMs();
        const double fadeMs=(std::max)(layerTransitionMs,1000.0/(std::max)(1.0,sample->sample.rate));
        drawWave(envelope,durationMs,0.0,-kAll,splitMs+fadeMs*.5,accent);
        drawWave(layerEnvelope,layerSample->sample.duration()*1000.0,layerOffsetMs,splitMs-fadeMs*.5,kAll,tt::ui::pal().punch2);
    }

    // Trim overlays and handles use the original full sample duration.
    if (durationMs > 0.0) {
        g.saveState();g.reduceClipRegion(plot.toNearestInt().expanded(5,0));
        const float marker = msToX(markerMs(), plot);
        const float left=plot.getX(),right=plot.getRight();
        const float clipped=std::clamp(marker,left,right);
        g.setColour(bg.withAlpha(isPunch ? 0.68f : 0.57f));
        if (isPunch) g.fillRect(clipped, plot.getY(), right - clipped, plot.getHeight());
        else g.fillRect(left, plot.getY(), clipped - left, plot.getHeight());
        const bool markerVisible=marker>=left-4 && marker<=right+4;
        if(markerVisible) {
            g.setColour(titleColour.withAlpha(0.80f));
            g.drawVerticalLine(int(marker), plot.getY(), plot.getBottom());
            g.setColour(accent);
            g.fillRoundedRectangle(marker-4,plot.getY(),8,18,3);
            const float labelX=std::clamp(marker-35.0f,plot.getX(),plot.getRight()-70.0f);
            g.setFont(juce::Font(10.0f,juce::Font::bold));
            g.drawText(isPunch?"END":"START",int(labelX),int(plot.getY()+20),70,14,juce::Justification::centred);
        }
        if(lockCaption.isNotEmpty()) {   // v0.4.66 LOCK SPLIT: shared cut, bottom-left pill
            const juce::Font font(10.0f,juce::Font::bold);g.setFont(font);
            const float w=(std::min)(plot.getWidth()-8.0f,float(font.getStringWidthFloat(lockCaption))+12.0f);
            const juce::Rectangle<float> pill(plot.getX()+4,plot.getBottom()-17,w,14);
            g.setColour(bg.withAlpha(0.85f));g.fillRoundedRectangle(pill,4);
            g.setColour(accent);g.drawText(lockCaption,pill.reduced(6,0),juce::Justification::centredLeft,true);
        }
        if(hasEndHandle()) {
            // Dry Tail END cut: dim after the cut, draw the fade-out ramp.
            const double cutMs=std::clamp(durationMs-endGuideMs,0.0,durationMs);
            const float endX=msToX(cutMs,plot);
            if(endGuideMs>0) {
                g.setColour(bg.withAlpha(0.68f));
                const float from=std::clamp(endX,left,right);
                g.fillRect(from,plot.getY(),right-from,plot.getHeight());
                const double fadeStartMs=(std::max)(cutMs-endFadeMs,guideMs);
                const float fadeX=msToX(fadeStartMs,plot);
                g.setColour(accent.withAlpha(0.9f));
                g.drawLine(fadeX,plot.getY()+2,endX,plot.getBottom()-2,1.4f);
            }
            if(endX>=left-4 && endX<=right+4) {
                g.setColour(titleColour.withAlpha(endGuideMs>0?0.80f:0.35f));
                g.drawVerticalLine(int(endX),plot.getY(),plot.getBottom());
                g.setColour(accent);
                g.fillRoundedRectangle(endX-4,plot.getBottom()-18,8,18,3);
                const float endLabelX=std::clamp(endX-35.0f,plot.getX(),plot.getRight()-70.0f);
                g.setFont(juce::Font(10.0f,juce::Font::bold));
                g.drawText("END",int(endLabelX),int(plot.getBottom()-34),70,14,juce::Justification::centred);
            }
        }
        g.restoreState();
    }

    g.setFont(juce::Font(10.0f));
    g.setColour(muted);
    // Zoomed: the bottom labels show the visible time range.
    const bool zoomed=zoom>1.0;
    g.drawText(zoomed?juce::String(viewStart,1)+" ms":juce::String("0 ms"), 12, getHeight() - 18, 75, 14, juce::Justification::centredLeft);
    g.drawText(juce::String(zoomed?viewStart+viewMs:durationMs, 1) + " ms", getWidth() - 115, getHeight() - 18, 103, 14, juce::Justification::centredRight);
    g.setColour(accent.withAlpha(0.85f));
    g.drawText(isPunch ? "DRAG END" : "DRAG START", 12, 2, 90, 16, juce::Justification::centredLeft);
    g.setColour(muted);
    const double selectedMs=isPunch ? std::clamp(durationMs-guideMs,0.0,durationMs) : std::clamp(guideMs,0.0,durationMs);
    const juce::String status=juce::String(isPunch?"KEEP ":"SKIP ")+juce::String(selectedMs,1)+" ms"+
        (hasEndHandle()&&endGuideMs>0?"  CUT "+juce::String(endGuideMs,1)+" ms":juce::String());
    g.drawText(status,getWidth()-212,2,200,16,juce::Justification::centredRight);
}

// ---------------------------------------------------------------------------
// v0.4.61 ABOUT page
const char* const TailtoneAboutPage::aiStatement="Created with different AI models";
const char* const TailtoneAboutPage::creatorStatement="Created and distributed by Jei Audio aka Jeipster";
TailtoneAboutPage::TailtoneAboutPage() {
    setOpaque(true);
    backButton.setTooltip("Back to the controls.");
    backButton.onClick=[this]{if(onBack)onBack();};
    addAndMakeVisible(backButton);
    // v0.4.62 theme selector (saved for all projects on this computer).
    for(size_t i=0;i<tt::ui::palettes().size();++i)themeBox.addItem(tt::ui::palettes()[i].name,int(i)+1);
    themeBox.setSelectedId(tt::ui::themeIndex()+1,juce::dontSendNotification);
    themeBox.setTooltip("Colours of the Kicktone window. Saved on this computer for all your projects.");
    themeBox.onChange=[this]{const int id=themeBox.getSelectedId();if(id>0&&onTheme)onTheme(id-1);};
    addAndMakeVisible(themeBox);
    // v0.5.10 PREPARATION CPU, shown as BACKGROUND RENDERING since v0.5.18 (Quiet is labelled Eco).
    prepBox.addItem("Eco (1 core)",1);prepBox.addItem("Balanced (half the cores)",2);prepBox.addItem("Fast (all cores but one)",3);
    prepBox.setSelectedId(tt::ui::prepEffort()+1,juce::dontSendNotification);
    prepBox.setTooltip("How many CPU cores Kicktone uses to prepare notes in the background. Playing and exporting are never slowed down.");
    prepBox.onChange=[this]{const int id=prepBox.getSelectedId();if(id>0&&onPrepEffort)onPrepEffort(id-1);};
    addAndMakeVisible(prepBox);
    // v0.5.3 custom defaults.
    defaultsButton.setTooltip("Choose the settings a new Kicktone starts with. Saved on this computer; projects you open keep their own settings.");
    defaultsButton.onClick=[this]{showDefaults(true);};
    addAndMakeVisible(defaultsButton);
    defaultsPage.onBack=[this]{showDefaults(false);};
    addChildComponent(defaultsPage);
}
void TailtoneAboutPage::showDefaults(bool show) {
    if(show)defaultsPage.refresh();
    defaultsPage.setVisible(show);
    if(show)defaultsPage.toFront(false);
}

// --- v0.5.3 DEFAULT SETTINGS page -----------------------------------------
const std::vector<std::pair<const char*,const char*>>& TailtoneDefaultsPage::toggleList() {
    static const std::vector<std::pair<const char*,const char*>> list{
        {"gate","Follow MIDI note length"},{"eqOn","Tail EQ on"},{"phaseAuto","Auto phase match (Punch / Tail)"},{"transientOn","Transient on"},
        {"punchLayer","Punch layer (Punch 2)"},{"glide","Glide"},{"lowKeyPunch","D2-C#3: octave-down punch"},{"punchFollowTail","Punch follows tail pitch"},
        {"lockSplit","Lock split"},{"punchFollowDecayOnly","Only Punch 2 follows (layer)"},{"crunchLostOnly","Crunch: lost frequencies only"},{"punchRegionOn","Punch Attack / Decay EQ"},{"attackLayerOn","Attack layer"},{"pitchDropOn","Pitch drop"},
        {"tailVerbOn","Experimental reverb on"},{"tailVerbKeepDry","Keep dry"},{"tailVerbTailSend","Tail reverb"},{"tailVerbPunchSend","Punch reverb"},
        {"tailVerbLinearPhase","Tail wet EQ linear phase"},{"tailVerbSubMono","Tail wet mono sub"},{"tailVerbAutoMatch","Root render auto match wet"},{"muteReverb","Reverb muted"},
        {"outputLimiter","Output limiter"},{"bassMono","Mono bass"},{"bassMonoLinear","Mono bass linear phase"},{"tailVerbPhaseLock","Root render low phase lock"}};
    return list;
}
TailtoneDefaultsPage::TailtoneDefaultsPage() {
    setOpaque(true);
    for(const auto& [id,label]:toggleList()){
        Toggle t{id,std::make_unique<juce::ToggleButton>(label)};
        const juce::String pid=id;
        t.button->onClick=[this,pid,b=t.button.get()]{if(!refreshing)storeValue(pid,b->getToggleState()?1.f:0.f);};
        addAndMakeVisible(*t.button);toggles.push_back(std::move(t));
    }
    engineBox.addItemList({"Highest Fidelity v4","Adaptive Stretch v4","Extreme v5"},1);
    punchModeBox.addItemList({"Stretch Aligned","Resample","Highest Fidelity"},1);
    reverbEngineBox.addItemList({"Legacy","V2"},1);
    for(int pct:{50,75,100,125,150,200})uiSizeBox.addItem(juce::String(PunchTailProcessor::displayedUiScale(float(pct)))+"%",pct);   // v0.5.22: 75 shows as 100%
    engineBox.onChange=[this]{if(refreshing||engineBox.getSelectedId()<1)return;const int id=engineBox.getSelectedId();
        storeValue("pitchAdaptive",id==2?1.f:0.f);storeValue("pitchExtremeV5",id==3?1.f:0.f);refresh();};
    punchModeBox.onChange=[this]{if(refreshing||punchModeBox.getSelectedId()<1)return;const int id=punchModeBox.getSelectedId();
        storeValue("punchPitchFidelity",id==3?1.f:0.f);storeValue("punchPitchMode",id==1?0.f:1.f);refresh();};
    reverbEngineBox.onChange=[this]{if(!refreshing&&reverbEngineBox.getSelectedId()>0){storeValue("tailVerbEngine",float(reverbEngineBox.getSelectedId()-1));refresh();}};
    uiSizeBox.onChange=[this]{if(!refreshing&&uiSizeBox.getSelectedId()>0){storeValue("uiScale",float(uiSizeBox.getSelectedId()));refresh();}};
    const std::array<std::pair<juce::Label*,const char*>,4> labels{{{&engineLabel,"Tail engine"},{&punchModeLabel,"Punch pitch mode"},{&reverbEngineLabel,"Reverb engine"},{&uiSizeLabel,"UI size"}}};
    for(auto [l,text]:labels){l->setText(text,juce::dontSendNotification);l->setFont(juce::Font(14.0f,juce::Font::bold));addAndMakeVisible(*l);}
    for(auto* c:{&engineBox,&punchModeBox,&reverbEngineBox,&uiSizeBox})addAndMakeVisible(*c);
    statusLabel.setFont(juce::Font(14.0f));addAndMakeVisible(statusLabel);
    saveCurrentButton.setTooltip("Use the settings of this Kicktone as the start for every new one. Settings that belong to one sample (tail root, trims, offsets, output trims) and EQ settings are not stored: every EQ starts neutral.");
    factoryButton.setTooltip("Forget your own defaults: new Kicktones start with the factory settings again.");
    saveCurrentButton.onClick=[this]{if(state&&processor){auto m=tt::defaults::capture(*state,*processor);
        for(auto it=m.begin();it!=m.end();)it=std::abs(it->second-tt::defaults::factory(*state,it->first))<1.e-6f?m.erase(it):std::next(it);
        tt::defaults::save(m);refresh();}};
    factoryButton.onClick=[this]{tt::defaults::clear();refresh();};
    backButton.onClick=[this]{if(onBack)onBack();};
    for(auto* b:{&saveCurrentButton,&factoryButton,&backButton})addAndMakeVisible(*b);
}
void TailtoneDefaultsPage::storeValue(const juce::String& id,float value) {
    if(!state)return;
    auto m=tt::defaults::load();
    if(std::abs(value-tt::defaults::factory(*state,id))<1.e-6f)m.erase(id);else m[id]=value;
    tt::defaults::save(m);
    statusLabel.setText(statusText(),juce::dontSendNotification);
}
juce::String TailtoneDefaultsPage::statusText() const {
    const auto n=tt::defaults::load().size();
    return n==0?juce::String("Factory defaults: nothing changed."):juce::String(int(n))+" setting"+(n==1?"":"s")+" differ from the factory defaults (saved on this computer).";
}
void TailtoneDefaultsPage::refresh() {
    if(!state)return;
    const juce::ScopedValueSetter<bool> guard(refreshing,true);
    const auto m=tt::defaults::load();
    const auto eff=[&](const char* id){return tt::defaults::effective(*state,m,id);};
    for(auto& t:toggles){t.button->setToggleState(eff(t.id.toRawUTF8())>.5f,juce::dontSendNotification);
        t.button->setTooltip(juce::String("Factory setting: ")+(tt::defaults::factory(*state,t.id)>.5f?"on":"off"));}
    engineBox.setSelectedId(eff("pitchExtremeV5")>.5f?3:eff("pitchAdaptive")>.5f?2:1,juce::dontSendNotification);
    punchModeBox.setSelectedId(eff("punchPitchFidelity")>.5f?3:juce::roundToInt(eff("punchPitchMode"))==0?1:2,juce::dontSendNotification);
    reverbEngineBox.setSelectedId(juce::roundToInt(eff("tailVerbEngine"))+1,juce::dontSendNotification);
    {int best=75;for(int pct:{50,75,100,125,150,200})if(std::abs(pct-eff("uiScale"))<std::abs(best-eff("uiScale")))best=pct;uiSizeBox.setSelectedId(best,juce::dontSendNotification);}
    statusLabel.setText(statusText(),juce::dontSendNotification);
}
juce::Rectangle<int> TailtoneDefaultsPage::switchesCard() const { const int rows=(int(toggleList().size())+3)/4;return {28,140,getWidth()-56,58+rows*38}; }
juce::Rectangle<int> TailtoneDefaultsPage::modesCard() const { const auto s=switchesCard();return {28,s.getBottom()+20,getWidth()-56,104}; }
juce::Rectangle<int> TailtoneDefaultsPage::allCard() const { const auto m=modesCard();return {28,m.getBottom()+20,getWidth()-56,116}; }
void TailtoneDefaultsPage::resized() {
    backButton.setBounds(getWidth()-28-120,18,120,32);
    const auto s=switchesCard();const int colW=(s.getWidth()-48)/4;
    const int rows=(int(toggles.size())+3)/4;
    for(size_t i=0;i<toggles.size();++i){const int col=int(i)/rows,row=int(i)%rows;toggles[i].button->setBounds(s.getX()+24+col*colW,s.getY()+48+row*38,colW-16,30);}
    const auto m=modesCard();const int w=(m.getWidth()-48)/4;
    const std::array<std::pair<juce::Label*,juce::ComboBox*>,4> modes{{{&engineLabel,&engineBox},{&punchModeLabel,&punchModeBox},{&reverbEngineLabel,&reverbEngineBox},{&uiSizeLabel,&uiSizeBox}}};
    for(size_t i=0;i<modes.size();++i){const int x=m.getX()+24+int(i)*w;modes[i].first->setBounds(x,m.getY()+44,150,30);modes[i].second->setBounds(x+154,m.getY()+44,(std::min)(260,w-174),30);}
    const auto a=allCard();
    saveCurrentButton.setBounds(a.getX()+24,a.getY()+48,360,34);factoryButton.setBounds(a.getX()+24+372,a.getY()+48,200,34);
    statusLabel.setBounds(a.getX()+24+372+212,a.getY()+48,a.getWidth()-24-372-212-24,34);
}
void TailtoneDefaultsPage::paint(juce::Graphics& g) {
    g.fillAll(bg);
    g.setColour(titleColour);g.setFont(juce::Font(27.0f,juce::Font::bold));
    g.drawText("DEFAULT SETTINGS",28,12,700,39,juce::Justification::centredLeft);
    g.setColour(thinLine);g.drawHorizontalLine(66,28,float(getWidth()-28));
    g.setColour(whiteAccent);g.setFont(juce::Font(16.0f));
    g.drawFittedText("The settings a NEW Kicktone starts with, saved on this computer for every new instance. Projects and presets you open keep their own settings. "
        "Until you change something here, the factory defaults are used. EQs always start neutral.",28,80,getWidth()-56,48,juce::Justification::topLeft,2);
    const auto card=[&](juce::Rectangle<int> r,const char* title,juce::Colour edge){
        g.setColour(panel);g.fillRoundedRectangle(r.toFloat(),9);g.setColour(edge);g.fillRoundedRectangle(r.withWidth(4).toFloat(),2);
        g.setColour(edge);g.setFont(juce::Font(13.0f,juce::Font::bold));g.drawText(title,r.getX()+24,r.getY()+16,r.getWidth()-48,18,juce::Justification::centredLeft);};
    card(switchesCard(),"SWITCHES (TICKED = ON IN A NEW INSTANCE)",amber);
    card(modesCard(),"ENGINES AND SIZE",whiteAccent);
    card(allCard(),"ALL SETTINGS",muted);
}
int TailtoneAboutPage::appearanceY() const {
    return 86+180+20+140+20+(58+(std::max)(8,build.size())*26+10)+20;
}
void TailtoneAboutPage::setBuildInfo(const juce::StringArray& rows) {
    if(rows==build)return;
    build=rows;resized();repaint();
}
juce::String TailtoneAboutPage::aiDetailText() {
    return "Kicktone's sound engines, interface and automated tests were written with the help of "
           "different AI models, directed and tested by Jeipster. "
           "It is stated here openly so you know how this instrument was made.";
}
// v0.5.14 (user request): contact address shown in the BETA BUILD notice.
const char* const TailtoneAboutPage::kContactEmail="Jeiaudio.project@gmail.com";
juce::String TailtoneAboutPage::betaText() {
    return "This is a beta build. Every release passes its automated test suite, but it has not been "
           "checked in every DAW and on every system. Save your projects often and keep a copy of "
           "the previous version: each new Kicktone version replaces the installed one. Please "
           "report crashes or anything that sounds wrong to Jei Audio:\n"
           + juce::String(kContactEmail);
}
juce::String TailtoneAboutPage::creditsText() {
    return "JUCE framework - Raw Material Software Limited, used under the GNU GPL v3.\n"
           "r8brain-free-src - sample rate converter designed by Aleksey Vaneev of Voxengo (MIT licence).\n"
           "VST is a registered trademark of Steinberg Media Technologies GmbH.\n"
           "Kicktone is free software under the GNU General Public License v3; the complete source "
           "code is distributed with every release (LICENSE.txt, THIRD-PARTY.md).";
}
juce::StringArray TailtoneAboutPage::shortcuts() {
    return {
        "Shift + drag|Fine adjustment on knobs, sliders, EQ points and waveforms",
        "Double-click a knob or slider|Type an exact value",
        "Alt + click a knob or slider|Reset it to its default value",
        "- / + buttons|One step; Shift-click for a tenth of a step; hold to repeat",
        "EQ: drag a point|Move frequency and gain",
        "EQ: Ctrl (Cmd) + drag up/down|Change the band width (Q)",
        "Punch / Tail waveform|Drag the END / START marker, or click to place it",
        "Punch / Tail zoom menu|Zoom in around the marker; use the scrollbar to move",
        "Punch / Tail waveform: mouse wheel|Zoom around the pointer; Shift + wheel scrolls; double-click resets",
        "Punch / Tail waveform: JOIN button|Show the frequencies where the punch meets the tail",
        "LOCK SPLIT|Same kick in Punch and Tail: drag the CUT, both markers follow",
        "Tail waveform END handle|Shorten the end of the dry tail (fades over END FADE)",
        "Final Kick Waveform: drag up/down|Change the volume of the Punch or the Tail + reverb",
        "Final Kick Waveform: mouse wheel|Zoom; Shift + wheel scrolls; double-click resets",
        "Drop files on the plugin|Load punch, tail or reverb (WAV / MP3)",
        "EXPORT WAV / DRAG|Save the kick as a WAV, or drag DRAG into your DAW",
        "Keys D#2 - D#5|Pitched tail notes (FL Studio note names)",
        "Keys D2 - C#3|Octave-down punch lane (when ticked)",
        "Legato with GLIDE ticked|Hold a key and press another to slide the tail"
    };
}
juce::String TailtoneAboutPage::allText() const {
    juce::String text=product+"\n"+aiStatement+"\n"+aiDetailText()+"\n"+creatorStatement+"\n"+betaText()+"\n"+creditsText()+"\n";
    for(const auto& row:shortcuts())text<<row.replace("|"," : ")<<"\n";
    for(const auto& row:build)text<<row.replace("|"," : ")<<"\n";
    return text;
}
void TailtoneAboutPage::resized() {
    backButton.setBounds(getWidth()-28-120,18,120,32);
    defaultsButton.setBounds(getWidth()-28-120-130-12-200,18,200,32);   // v0.5.3, left of the ABOUT title
    defaultsPage.setBounds(getLocalBounds());
    themeBox.setBounds(28+24+170,appearanceY()+40,260,28);
    prepBox.setBounds(28+24+170,appearanceY()+80,260,28);   // v0.5.10, label column 170 since v0.5.18
}
void TailtoneAboutPage::paint(juce::Graphics& g) {
    g.fillAll(bg);
    // Header: logo, product name, page title.
    g.setColour(titleColour);
    if(logo.isValid())g.drawImageWithin(logo,28,10,45,43,juce::RectanglePlacement::centred,true);
    g.setFont(juce::Font(27.0f,juce::Font::bold));
    g.drawText(product.replace("Kicktone","KICKTONE"),85,12,700,39,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(juce::Font(15.0f,juce::Font::bold));
    g.drawText("ABOUT",getWidth()-28-120-130,18,120,32,juce::Justification::centredRight);
    g.setColour(thinLine);g.drawHorizontalLine(66,28,float(getWidth()-28));

    const int gap=20,left=28,colW=(getWidth()-2*left-gap)/2,right=left+colW+gap;
    const auto card=[&](juce::Rectangle<int> r,juce::Colour edge){
        g.setColour(panel);g.fillRoundedRectangle(r.toFloat(),9);
        g.setColour(edge);g.fillRoundedRectangle(r.removeFromLeft(4).toFloat(),2);
    };
    const auto heading=[&](juce::String text,int x,int y,int w,juce::Colour c){
        g.setColour(c);g.setFont(juce::Font(13.0f,juce::Font::bold));
        g.drawText(text,x,y,w,18,juce::Justification::centredLeft);
    };
    const auto paragraph=[&](const juce::String& text,juce::Rectangle<int> r,float size,juce::Colour c){
        juce::AttributedString a;a.setText(text);a.setFont(juce::Font(size));a.setColour(c);
        a.setJustification(juce::Justification::topLeft);a.setWordWrap(juce::AttributedString::byWord);
        a.setLineSpacing(3.0f);a.draw(g,r.toFloat());
    };
    const auto pairs=[&](const juce::StringArray& rows,int x,int y,int w,int labelW,int rowH){
        for(const auto& row:rows){
            const auto label=row.upToFirstOccurrenceOf("|",false,false),value=row.fromFirstOccurrenceOf("|",false,false);
            g.setColour(whiteAccent);g.setFont(juce::Font(14.0f,juce::Font::bold));
            g.drawFittedText(label,x,y,labelW-12,rowH,juce::Justification::centredLeft,1,.85f);
            g.setColour(muted);g.setFont(juce::Font(14.0f));
            g.drawFittedText(value,x+labelW,y,w-labelW,rowH,juce::Justification::centredLeft,2,.85f);
            y+=rowH;
        }
        return y;
    };

    // LEFT 1: AI transparency + creator.
    int y=86;
    {const juce::Rectangle<int> r(left,y,colW,180);card(r,violet);
     heading("TRANSPARENCY",left+24,y+16,colW-48,violet);
     g.setColour(titleColour);g.setFont(juce::Font(26.0f,juce::Font::bold));
     g.drawText(aiStatement,left+24,y+40,colW-48,34,juce::Justification::centredLeft);
     g.setColour(whiteAccent);g.setFont(juce::Font(19.0f));
     g.drawText(creatorStatement,left+24,y+78,colW-48,28,juce::Justification::centredLeft);
     paragraph(aiDetailText(),{left+24,y+118,colW-48,52},15.0f,muted);
     y+=180+gap;}
    // LEFT 2: beta build.
    {const juce::Rectangle<int> r(left,y,colW,140);card(r,amber);
     heading("BETA BUILD",left+24,y+16,colW-48,amber);
     paragraph(betaText(),{left+24,y+42,colW-48,90},15.0f,whiteAccent);
     y+=140+gap;}
    // LEFT 3: version & build information.
    {const int rows=build.size(),h=58+rows*26+10;const juce::Rectangle<int> r(left,y,colW,h);card(r,whiteAccent);
     heading("VERSION & BUILD",left+24,y+16,colW-48,whiteAccent);
     pairs(build,left+24,y+44,colW-48,190,26);
     y+=h+gap;}
    // LEFT 4: appearance (theme selector, v0.4.62).
    y=appearanceY();
    {const juce::Rectangle<int> r(left,y,colW,136);card(r,whiteAccent);
     heading("APPEARANCE & PERFORMANCE",left+24,y+16,colW-48,whiteAccent);
     g.setColour(whiteAccent);g.setFont(juce::Font(14.0f,juce::Font::bold));
     g.drawText("Theme",left+24,y+40,120,28,juce::Justification::centredLeft);
     g.drawText("Background rendering",left+24,y+80,166,28,juce::Justification::centredLeft);   // v0.5.10 Preparation CPU
     g.setColour(muted);g.setFont(juce::Font(13.0f));
     g.drawText("Saved on this computer for all your projects.",left+24+170+272,y+40,colW-48-170-272,28,juce::Justification::centredLeft);
     g.drawText("Shared by every Kicktone on this computer.",left+24+170+272,y+80,colW-48-170-272,28,juce::Justification::centredLeft);}
    // RIGHT 1: mouse & keyboard.
    y=86;
    {const auto rows=shortcuts();const int h=58+rows.size()*32+8;const juce::Rectangle<int> r(right,y,colW,h);card(r,amber.brighter(.25f));
     heading("MOUSE & KEYBOARD",right+24,y+16,colW-48,amber.brighter(.25f));
     pairs(rows,right+24,y+44,colW-48,280,32);
     y+=h+gap;}
    // RIGHT 2: credits.
    {const juce::Rectangle<int> r(right,y,colW,140);card(r,muted);
     heading("BUILT WITH",right+24,y+16,colW-48,muted);
     paragraph(creditsText(),{right+24,y+42,colW-48,92},14.0f,muted);}
}

PunchTailKickDisplay::PunchTailKickDisplay() {
    setTooltip("The finished kick. Drag up or down on it to change the Punch or Tail volume; set the join below. Mouse wheel zooms, Shift + wheel scrolls, double-click zooms back out.");
    const char* names[]={"TAIL JOIN SHIFT (ms)","CROSSFADE (ms)"};
    for(int i=0;i<2;++i) {
        auto& slider=joinControls[size_t(i)];slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,73,25);
        slider.setRange(i==0?-30:0,30,0.1);
        slider.setTooltip(juce::String(i==0?"Moves the tail earlier (negative) or later (positive) where it meets the punch.":"How long the end of the punch and the start of the tail overlap and blend.")
            +" Type a value in ms or use - / + for 0.1 ms steps. Hold Shift while dragging to move slowly.");
        slider.onValueChange=[this,i]{if(!syncing)editJoin(i,joinControls[size_t(i)].getValue());};
        slider.onDragStart=[this,i]{if(onGesture)onGesture(static_cast<pt::KickHandle>(i+1),true);};
        slider.onDragEnd=[this,i]{if(onGesture)onGesture(static_cast<pt::KickHandle>(i+1),false);};
        addAndMakeVisible(slider);
        auto& label=joinLabels[size_t(i)];label.setText(names[i],juce::dontSendNotification);label.setFont(juce::Font(11.0f,juce::Font::bold));addAndMakeVisible(label);
        for(auto* button:{&minusButtons[size_t(i)],&plusButtons[size_t(i)],&resetButtons[size_t(i)]})addAndMakeVisible(*button);
        minusButtons[size_t(i)].setButtonText("-");plusButtons[size_t(i)].setButtonText("+");resetButtons[size_t(i)].setButtonText("Reset");
        minusButtons[size_t(i)].onClick=[this,i]{joinControls[size_t(i)].setValue(joinControls[size_t(i)].getValue()-0.1);};
        plusButtons[size_t(i)].onClick=[this,i]{joinControls[size_t(i)].setValue(joinControls[size_t(i)].getValue()+0.1);};
        resetButtons[size_t(i)].onClick=[this,i]{joinControls[size_t(i)].setValue(i==0?0:8);};
    }
    zoomBox.addItem("1x",1);zoomBox.addItem("2x",2);zoomBox.addItem("4x",3);zoomBox.addItem("8x",4);
    zoomBox.setSelectedId(1);zoomBox.setTooltip("Zoom in on the spot where the punch meets the tail. 1x shows the whole kick.");
    zoomBox.onChange=[this]{zoom=std::pow(2.0,zoomBox.getSelectedId()-1);viewStart=std::clamp(kick.tailStartMs-viewDuration()*.35,0.0,(std::max)(0.0,kick.totalMs-viewDuration()));syncControls();repaint();};
    addAndMakeVisible(zoomBox);addAndMakeVisible(scroll);
    // v0.4.62 export: the rendered kick (what this waveform shows) as a WAV file.
    exportButton.setTooltip("Save this kick as a WAV file: the root note with all effects, at the project's sample rate.");
    exportButton.onClick=[this]{if(onExport)onExport();};
    bitDepthBox.addItem("16-bit",1);bitDepthBox.addItem("24-bit",2);bitDepthBox.addItem("32-bit float",3);
    bitDepthBox.setSelectedId(2,juce::dontSendNotification);bitDepthBox.setTooltip("Bit depth of the WAV files you export or drag. 24-bit suits most projects.");
    dragButton.setTooltip("Drag this into your DAW (onto a track, sampler or drum rack) to drop the kick in as a WAV file.");
    dragButton.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    dragButton.onDragOut=[this]{if(onDragOut)onDragOut();};
    for(juce::Component* c:std::initializer_list<juce::Component*>{&exportButton,&bitDepthBox,&dragButton})addAndMakeVisible(c);
    // v0.4.65 spectral join view toggle and its time span.
    joinButton.setClickingTogglesState(true);
    joinButton.setTooltip("Show the frequencies around the spot where the punch meets the tail. SEAM tells you how smooth that join is: about 1x is seamless, 3x or more means you may hear a step there.");
    joinButton.onClick=[this]{setJoinView(joinButton.getToggleState());};
    joinSpanBox.addItem(juce::CharPointer_UTF8("\xc2\xb1 25"),1);joinSpanBox.addItem(juce::CharPointer_UTF8("\xc2\xb1 50"),2);
    joinSpanBox.addItem(juce::CharPointer_UTF8("\xc2\xb1 100"),3);joinSpanBox.addItem(juce::CharPointer_UTF8("\xc2\xb1 200"),4);
    joinSpanBox.setSelectedId(2,juce::dontSendNotification);
    joinSpanBox.setTooltip("How much time to show on each side of the join.");
    joinSpanBox.onChange=[this]{rebuildJoinAnalysis();repaint();};
    addAndMakeVisible(joinButton);addChildComponent(joinSpanBox);
    refreshExportState();
    scroll.setAutoHide(false);scroll.addListener(this);
    scroll.setColour(juce::ScrollBar::thumbColourId,tt::ui::pal().scrollThumb);
    scroll.setColour(juce::ScrollBar::trackColourId,tt::ui::pal().bg);
} 
juce::Rectangle<float> PunchTailKickDisplay::plotBounds() const {
    return getLocalBounds().reduced(13).withTrimmedTop(32).withTrimmedBottom(104).toFloat();
}
float PunchTailKickDisplay::timeX(double ms) const {
    const auto plot=plotBounds();return plot.getX()+float((ms-viewStart)/viewDuration())*plot.getWidth();
}
void PunchTailKickDisplay::editJoin(int index,double value) {
    const auto handle=static_cast<pt::KickHandle>(index+1);
    const bool ownGesture=!joinControls[size_t(index)].isMouseButtonDown();
    if(ownGesture && onGesture)onGesture(handle,true);
    if(onEdit)onEdit(handle,value);
    if(ownGesture && onGesture)onGesture(handle,false);
}
void PunchTailKickDisplay::syncControls() {
    syncing=true;
    const double values[]={currentSettings.joinMs,currentSettings.crossfadeMs};
    for(int i=0;i<2;++i) {
        if(!joinControls[size_t(i)].isMouseButtonDown())joinControls[size_t(i)].setValue(values[i],juce::dontSendNotification);
        const bool enabled=i==0?hasTail:hasPunch&&hasTail;
        joinControls[size_t(i)].setEnabled(enabled);
        for(auto* b:{&minusButtons[size_t(i)],&plusButtons[size_t(i)],&resetButtons[size_t(i)]})b->setEnabled(enabled);
    }
    viewStart=std::clamp(viewStart,0.0,(std::max)(0.0,kick.totalMs-viewDuration()));
    scroll.setRangeLimits(0,(std::max)(1.0,kick.totalMs));scroll.setCurrentRange(viewStart,viewDuration(),juce::dontSendNotification);
    scroll.setEnabled(zoom>1);syncing=false;
}
void PunchTailKickDisplay::setExportMessage(const juce::String& text) {
    exportMessage=text;exportMessageTime=juce::Time::getMillisecondCounter();repaint();
    juce::Component::SafePointer<PunchTailKickDisplay> safe(this);
    juce::Timer::callAfterDelay(4200,[safe]{if(safe)safe->repaint();});
}
void PunchTailKickDisplay::resized() {
    zoomBox.setBounds(getWidth()-84,5,70,24);
    exportButton.setBounds(178,5,96,24);bitDepthBox.setBounds(278,5,104,24);dragButton.setBounds(386,5,58,24);
    joinButton.setBounds(456,5,118,24);joinSpanBox.setBounds(578,5,66,24);   // v0.4.65
    const int width=(getWidth()-26)/2;
    for(int i=0;i<2;++i) {
        const int x=13+i*width,y=getHeight()-82;
        joinLabels[size_t(i)].setBounds(x,y,width-60,20);resetButtons[size_t(i)].setBounds(x+width-59,y,52,20);
        minusButtons[size_t(i)].setBounds(x,y+25,25,27);plusButtons[size_t(i)].setBounds(x+29,y+25,25,27);
        joinControls[size_t(i)].setBounds(x+59,y+23,width-65,31);
    }
    scroll.setBounds(13,getHeight()-20,getWidth()-26,13);
}
void PunchTailKickDisplay::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w) {
    if(e.mods.isShiftDown()) {
        viewStart=std::clamp(viewStart-double(w.deltaY+w.deltaX)*viewDuration()*.4,0.0,(std::max)(0.0,kick.totalMs-viewDuration()));syncControls();repaint();
    } else zoomBox.setSelectedId(juce::jlimit(1,4,zoomBox.getSelectedId()+(w.deltaY>0?1:-1)));
}
void PunchTailKickDisplay::mouseDoubleClick(const juce::MouseEvent&) {
    zoomBox.setSelectedId(1);
}
void PunchTailKickDisplay::mouseDown(const juce::MouseEvent& e) {
    if(!e.mods.isLeftButtonDown() || !plotBounds().contains(e.position))return;
    const double ms=std::clamp(viewStart+(e.position.x-plotBounds().getX())/plotBounds().getWidth()*viewDuration(),0.0,kick.totalMs);
    outputDragTail=hasTail && (!hasPunch || ms>=kick.tailStartMs);
    const int index=outputDragTail?1:0;
    outputDragStartDb=outputDb[size_t(index)];outputDragStartY=e.position.y;outputDragging=true;
    if(onOutputGesture)onOutputGesture(outputDragTail,true);
}
void PunchTailKickDisplay::mouseDrag(const juce::MouseEvent& e) {
    if(!outputDragging)return;
    const double sensitivity=e.mods.isShiftDown()?0.02:0.2;
    const double value=std::clamp(outputDragStartDb+(outputDragStartY-e.position.y)*sensitivity,-48.0,36.0);
    outputDb[outputDragTail?1:0]=value;
    if(onOutputEdit)onOutputEdit(outputDragTail,value);
    auto gains=shownGains;gains[outputDragTail?1:0]=juce::Decibels::decibelsToGain(float(value));
    showOutputGains(gains);   // v0.5.28: the waveform follows the mouse, no render wait
}
void PunchTailKickDisplay::mouseUp(const juce::MouseEvent&) {
    if(!outputDragging)return;
    outputDragging=false;if(onOutputGesture)onOutputGesture(outputDragTail,false);
}
void PunchTailKickDisplay::mouseMove(const juce::MouseEvent& e) {
    if(!plotBounds().contains(e.position) || kick.totalMs<=0){setMouseCursor(juce::MouseCursor::NormalCursor);return;}
    const double ms=std::clamp(viewStart+(e.position.x-plotBounds().getX())/plotBounds().getWidth()*viewDuration(),0.0,kick.totalMs);
    const bool tail=hasTail && (!hasPunch || ms>=kick.tailStartMs);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setTooltip(juce::String(tail?"Tail + reverb":"Punch")+" volume: "+juce::String(outputDb[tail?1:0],1)+" dB. Drag up or down to change it; hold Shift to move slowly.");
}

void PunchTailKickDisplay::setAudio(const pt::Sample* punch,
                                    const pt::Sample* preparedTail,
                                    pt::Settings settings, int rootNote, bool ready, const pt::Sample* reverb) {
    currentSettings=settings; punchDuration=punch?punch->duration()*1000.0/currentSettings.punchTimeScale:0.0;
    hasPunch = punch != nullptr;
    hasTail = preparedTail != nullptr;
    pitchReady = ready;
    note = rootNote;
    kick = pt::buildKickOverview(punch, preparedTail, settings, rootNote, 7200, reverb);
    syncControls();repaint();
}

void PunchTailKickDisplay::setRendered(const PreviewResult& result) {
    const auto& r=result.request;currentSettings=r.settings;
    if(!outputDragging)for(size_t i=0;i<2;++i)outputDb[i]=juce::Decibels::gainToDecibels(r.normalizeGains[i],-48.0f);
    punchDuration=r.samples[0]?r.samples[0]->duration()*1000.0/currentSettings.punchTimeScale:0;
    hasPunch=bool(r.samples[0]);hasTail=bool(r.samples[1]);pitchReady=r.ready;note=r.root;
    kick=result.kick;rendered=result.audio;updating=false;
    // v0.5.28: keep the buses for instant volume remixes; a drag still running
    // keeps showing its own gains on the new render.
    buses=result.buses;renderedEnvelope=result.kick.envelope;renderedGains=r.normalizeGains;
    if(!outputDragging)shownGains=renderedGains;
    remixPending=shownGains!=renderedGains;
    refreshExportState();syncControls();
    rebuildJoinAnalysis();   // v0.4.65: follows every finished render while the join view is shown
    repaint();
}

void PunchTailKickDisplay::showOutputGains(std::array<float,2> gains) {
    if(!outputDragging)for(size_t i=0;i<2;++i)outputDb[i]=juce::Decibels::gainToDecibels(gains[i],-48.0f);
    if(gains==shownGains)return;
    shownGains=gains;remixPending=gains!=renderedGains;
    if(!remixPending && buses)kick.envelope=renderedEnvelope;
    refreshExportState();repaint();
}
void PunchTailKickDisplay::applyRemix() {
    if(!remixPending)return;
    remixPending=false;
    if(buses)kick.envelope=remixPreview(*buses,shownGains);
}

// --- v0.4.65 spectral join view -------------------------------------------
void PunchTailKickDisplay::setJoinView(bool on) {
    if(joinView==on && joinButton.getToggleState()==on)return;
    joinView=on;
    joinButton.setToggleState(on,juce::dontSendNotification);
    joinSpanBox.setVisible(on);
    rebuildJoinAnalysis();
    repaint();
}
void PunchTailKickDisplay::setJoinSpanIndex(int index) {
    joinSpanBox.setSelectedId(std::clamp(index,0,3)+1,juce::sendNotificationSync);
}
double PunchTailKickDisplay::joinSpanMs() const {
    switch(joinSpanBox.getSelectedId()){case 1:return 25;case 3:return 100;case 4:return 200;default:return 50;}
}
juce::Colour PunchTailKickDisplay::spectrogramColour(float db) {
    // Palette-only map: background (silence) -> violet -> amber -> text (loudest).
    const auto& p=tt::ui::pal();
    const float t=std::clamp((db+90.f)/90.f,0.f,1.f);
    if(t>=1.f)return p.text;
    if(t<.45f)return p.bg.interpolatedWith(p.violet,t/.45f);
    if(t<.8f)return p.violet.interpolatedWith(p.amber,(t-.45f)/.35f);
    return p.amber.interpolatedWith(p.text,(t-.8f)/.2f);
}
void PunchTailKickDisplay::rebuildJoinAnalysis() {
    if(!joinView || !rendered || !hasTail || kick.totalMs<=0) { join={};joinImage=juce::Image(); return; }
    // The join on the output timeline is the tail start; the rendered audio
    // (PreviewResult::audio) starts at the punch onset, like the overview.
    join=pt::spectral::analyseJoin(*rendered,kick.tailStartMs,joinSpanMs());
    if(!join.valid){joinImage=juce::Image();return;}
    juce::Image image(juce::Image::RGB,join.columns,join.rows,false);
    {
        juce::Image::BitmapData data(image,juce::Image::BitmapData::writeOnly);
        for(int r=0;r<join.rows;++r)   // row 0 = lowest frequency at the bottom
            for(int c=0;c<join.columns;++c)
                data.setPixelColour(c,join.rows-1-r,spectrogramColour(join.at(r,c)));
    }
    joinImage=std::move(image);
}
void PunchTailKickDisplay::paintJoinView(juce::Graphics& g,juce::Rectangle<float> plot) {
    if(!join.valid || joinImage.isNull()) {
        g.setColour(muted);g.setFont(juce::Font(13.0f));
        g.drawFittedText(hasTail?"Spectral join view: waiting for the rendered kick...":"Load punch and tail samples to analyse the join",
                         plot.toNearestInt(),juce::Justification::centred,1);
        return;
    }
    // Leave room for the frequency scale on the left.
    const float scaleW=46.f;
    auto image=plot;image.removeFromLeft(scaleW);
    g.saveState();g.reduceClipRegion(image.toNearestInt());
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);   // crisp frames, no blur
    g.drawImage(joinImage,image,juce::RectanglePlacement::stretchToFit);
    g.restoreState();
    const auto xOf=[&](double ms){return image.getX()+float((ms-join.startMs)/(join.endMs-join.startMs))*image.getWidth();};
    const auto yOf=[&](double hz){return image.getBottom()-float(std::log(hz/join.minHz)/std::log(join.maxHz/join.minHz))*image.getHeight();};
    // Frequency scale (fewer labels when the plot is short).
    g.setFont(juce::Font(9.5f));
    const bool dense=image.getHeight()>=150.f;
    for(double hz:dense?std::vector<double>{50.,100.,200.,500.,1000.,2000.,5000.,10000.}:std::vector<double>{50.,200.,1000.,5000.}) {
        if(hz<join.minHz || hz>join.maxHz)continue;
        const float y=yOf(hz);
        g.setColour(muted.withAlpha(.25f));g.drawHorizontalLine(juce::roundToInt(y),image.getX(),image.getRight());
        g.setColour(muted);
        g.drawText(hz>=1000?juce::String(hz/1000.,0)+"k":juce::String(hz,0),int(plot.getX()),int(y-7),int(scaleW-6),14,juce::Justification::centredRight);
    }
    // Join marker and crossfade span (where the tail blends in).
    const float joinX=xOf(join.joinMs);
    if(kick.overlapMs>0) {
        g.setColour(whiteAccent.withAlpha(.18f));
        g.fillRect(joinX,image.getY(),(std::max)(1.f,xOf(join.joinMs+kick.overlapMs)-joinX),image.getHeight());
    }
    g.setColour(whiteAccent.withAlpha(.9f));g.drawVerticalLine(juce::roundToInt(joinX),image.getY(),image.getBottom());
    g.setFont(juce::Font(10.0f,juce::Font::bold));
    const auto label=[&](const juce::String& text,int x,int y,juce::Colour colour,juce::Justification just) {
        const int w=juce::roundToInt(g.getCurrentFont().getStringWidthFloat(text))+8;
        const int left=just==juce::Justification::centredRight?x-w:x;
        g.setColour(bg.withAlpha(.75f));g.fillRoundedRectangle(float(left),float(y),float(w),14.f,3.f);
        g.setColour(colour);g.drawText(text,left+4,y,w-8,14,juce::Justification::centredLeft);
    };
    label("JOIN",int(joinX)+3,int(image.getY())+2,whiteAccent,juce::Justification::centredLeft);
    // Punch end (when the punch ends before the tail starts or overlaps it).
    if(hasPunch && std::abs(kick.punchEndMs-join.joinMs)>.05) {
        const float px=xOf(kick.punchEndMs);
        if(px>=image.getX() && px<=image.getRight()) {
            g.setColour(amber.withAlpha(.9f));
            for(float y=image.getY();y<image.getBottom();y+=6.f)g.drawVerticalLine(juce::roundToInt(px),y,(std::min)(image.getBottom(),y+3.f));
            label("PUNCH END",int(px)-3,int(image.getBottom())-16,amber,juce::Justification::centredRight);
        }
    }
    // Time span and the seam figure go on the info line under the plot.
    const double ratio=join.seamRatio();
    const juce::String verdict=ratio<1.6?"seamless":ratio<3.0?"small step":"audible step / click";
    g.setColour(muted);g.setFont(juce::Font(10.0f));
    const juce::String span=juce::String(join.startMs,1)+" to "+juce::String(join.endMs,1)+" ms   |   "+juce::String(join.fftSize)+"-pt / "+juce::String(join.hopMs,1)+" ms frames   |   ";
    const int infoY=getHeight()-104;
    g.drawText(span,14,infoY,420,13,juce::Justification::centredLeft);
    const int seamX=14+juce::roundToInt(juce::Font(10.0f).getStringWidthFloat(span));
    g.setColour(ratio<1.6?whiteAccent:ratio<3.0?amber:amber.brighter(.3f));g.setFont(juce::Font(10.0f,juce::Font::bold));
    g.drawText("SEAM "+juce::String(ratio,2)+"x ("+verdict+")",seamX,infoY,260,13,juce::Justification::centredLeft);
}

void PunchTailKickDisplay::paint(juce::Graphics& g) {
    applyRemix();
    const auto full = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRoundedRectangle(full, 9.0f);
    g.setColour(thinLine);
    g.drawRoundedRectangle(full.reduced(0.5f), 9.0f, 1.0f);

    g.setColour(titleColour);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("FINAL KICK WAVEFORM", 13, 7, 240, 18, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(juce::Font(11.0f));
    const auto noteName = juce::MidiMessage::getMidiNoteName(note, true, false, 5);
    const juce::String pitchInfo = updating ? " (updating...)" : hasTail && !pitchReady ? " (current playable tail)" : " / ONE SHOT";
    const bool showMessage=exportMessage.isNotEmpty() && juce::Time::getMillisecondCounter()-exportMessageTime<4000;
    if(showMessage)g.setColour(amber);
    g.drawFittedText(showMessage?exportMessage:"ROOT " + noteName + pitchInfo, 650, 7, getWidth() - 740, 18, juce::Justification::centredRight,1,.7f);

    auto plot = plotBounds();
    if (plot.isEmpty()) return;
    g.setColour(bg.brighter(0.02f));
    g.fillRoundedRectangle(plot.expanded(4.0f, 2.0f), 4.0f);
    if(joinView) {   // v0.4.65: spectrogram instead of the waveform
        paintJoinView(g,plot);
        g.setColour(muted);g.setFont(juce::Font(10.0f));
        g.drawText("JOIN " + juce::String(kick.tailStartMs, 1) + " ms   /   TOTAL " + juce::String(kick.totalMs, 1) + " ms",
                   105, getHeight() - 104, getWidth() - 119, 13, juce::Justification::centredRight);
        return;
    }
    g.setColour(muted.withAlpha(0.15f));
    for (int i = 1; i < 8; ++i) {
        const float x = plot.getX() + plot.getWidth() * float(i) / 8.0f;
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
    }
    g.setColour(muted.withAlpha(0.23f));
    g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()), plot.getX(), plot.getRight());

    if (kick.envelope.bins.empty() || kick.totalMs <= 0.0) {
        g.setColour(muted);
        g.setFont(juce::Font(13.0f));
        g.drawFittedText("Load punch and tail samples to display the complete waveform",
                         plot.toNearestInt(), juce::Justification::centred, 1);
        return;
    }

    // Peak min/max columns are computed from the combined stereo output of
    // pt::Voice, the very same punch/tail mixer used during live playback.
    const auto& envelope = kick.envelope;
    const float gain = 1.0f; // Output waveform uses a fixed full-scale amplitude.
    const float halfH = plot.getHeight() * 0.45f;
    for (size_t i = 0; i < envelope.bins.size(); ++i) {
        const float position = (float(i) + 0.5f) / float(envelope.bins.size());
        const double ms = position * kick.totalMs;
        const float x=timeX(ms);
        if(x<plot.getX() || x>plot.getRight())continue;
        float blend = 0.0f;
        if (!hasPunch) blend = 1.0f;
        else if (hasTail && ms >= kick.tailStartMs) {
            blend = kick.overlapMs > 0.0 ? float(std::clamp((ms-kick.tailStartMs) / kick.overlapMs, 0.0, 1.0)) : 1.0f;
        }
        const auto colour = amber.interpolatedWith(whiteAccent, blend);
        const auto& bin = envelope.bins[i];
        const float y1 = plot.getCentreY() - bin.maximum * gain * halfH;
        const float y2 = plot.getCentreY() - bin.minimum * gain * halfH;
        g.setColour(colour.withAlpha(0.35f));
        g.drawVerticalLine(juce::roundToInt(x), y1 - 1.0f, y2 + 1.0f);
        g.setColour(colour.withAlpha(0.98f));
        g.drawVerticalLine(juce::roundToInt(x), y1, y2);
    }

    g.setColour(muted);
    g.setFont(juce::Font(10.0f));
    g.drawText(juce::String(viewStart,1)+" ms", 14, getHeight() - 104, 85, 13, juce::Justification::centredLeft);
    const auto label = "JOIN " + juce::String(kick.tailStartMs, 1) +
                       " ms   /   TOTAL " + juce::String(kick.totalMs, 1) + " ms";
    g.drawText(label, 105, getHeight() - 104, getWidth() - 119, 13, juce::Justification::centredRight);
}

PunchTailEditor::PunchTailEditor(PunchTailProcessor& p)
    : AudioProcessorEditor(p), proc(p),
      keys(p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard),
      punchWave(true), tailWave(false), reverbWave(false, true),eqGraph(p) {
    refreshThemeColours();   // v0.4.62: theme colours before any control takes one
    // Preserve the supplied mark exactly. Its monochrome brightness is the
    // opacity mask; original black pixels become transparent, including gaps.
    static const juce::Image logo=[] {
        auto src=juce::ImageFileFormat::loadFrom(BinaryData::jeilogo_png,BinaryData::jeilogo_pngSize);
        if(!src.isValid())return juce::Image{};
        juce::Image out(juce::Image::ARGB,src.getWidth(),src.getHeight(),true);
        int x0=src.getWidth(),y0=src.getHeight(),x1=-1,y1=-1;
        for(int y=0;y<src.getHeight();++y)for(int x=0;x<src.getWidth();++x){
            const auto pixel=src.getPixelAt(x,y);
            const auto alpha=(std::max)({pixel.getRed(),pixel.getGreen(),pixel.getBlue()});
            if(alpha){out.setPixelAt(x,y,juce::Colours::white.withAlpha(alpha));x0=(std::min)(x0,x);y0=(std::min)(y0,y);x1=(std::max)(x1,x);y1=(std::max)(y1,y);}
        }
        return x1>=x0?out.getClippedImage({x0,y0,x1-x0+1,y1-y0+1}):juce::Image{};
    }();
    jeiLogo=logo;
    // v0.4.61: ABOUT button switches to the About page (and BACK returns).
    aboutButton.setTooltip("About Kicktone: who made it and how, version details, credits and shortcuts.");
    aboutButton.onClick=[this]{showAbout(!aboutPage.isVisible());};
    content.addAndMakeVisible(aboutButton);
    aboutPage.setLogo(jeiLogo);aboutPage.setProductName(proc.getName());
    aboutPage.onBack=[this]{showAbout(false);};
    aboutPage.defaultsPage.setContext(&proc,&proc.params);   // v0.5.3
    aboutPage.onTheme=[this](int index){tt::ui::setTheme(index);applyTheme();};
    aboutPage.onPrepEffort=[](int e){tt::ui::setPrepEffort(e);pt::parallel::effort().store(tt::ui::prepEffort());};   // v0.5.10
    content.addChildComponent(aboutPage);
    applyTheme();
    setLookAndFeel(&theme);

    addAndMakeVisible(content);
    content.addAndMakeVisible(punchWave);
    content.addAndMakeVisible(tailWave);
    content.addAndMakeVisible(reverbWave);
    content.addAndMakeVisible(completedKick);
    for (auto* button : {&punchButton, &tailButton, &reverbButton, &detectButton, &punchDetectButton}) content.addAndMakeVisible(button);
    punchButton.onClick = [this] { browse(0); };
    tailButton.onClick = [this] { browse(1); };
    reverbButton.onClick = [this] { browse(2); };
    for(int slot=0;slot<3;++slot) {
        auto& button=unloadButtons[size_t(slot)];
        button.setTooltip("Unload this sound. Playing notes stop using it and it is removed from the project; the other sounds stay loaded.");
        button.onClick=[this,slot] { proc.unloadFile(slot); timerCallback(); };
        content.addAndMakeVisible(button);
    }
    for(int i=0;i<2;++i) {
        auto& button=trimButtons[size_t(i)];content.addAndMakeVisible(button);
        button.setTooltip("Cut the silence from the start and end of this sample (a tiny 0.5 ms margin is kept). Your file on disk is not changed. Saved with the project.");
        button.onClick=[this,i]{proc.trimSilence(i);};
    }
    detectButton.onClick = [this] { proc.redetect(); };
    punchDetectButton.onClick=[this]{detectPunchEnd();};
    punchDetectButton.setTooltip("Set PUNCH END to the spot where the hit turns into the tail (found when the sample was loaded). Fine-tune it with the END marker afterwards.");
    // v0.4.66 LOCK SPLIT: while locked both waveforms show the whole split file
    // and their marker is the shared cut; the move is applied on release.
    content.addAndMakeVisible(lockSplitButton);
    lockSplitButton.setClickingTogglesState(true);
    lockSplitAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"lockSplit",lockSplitButton);
    lockSplitButton.setTooltip("LOCK SPLIT: for one complete kick loaded into both Punch and Tail. Punch END and Tail START then move together "
        "as one cut, so the join stays seamless: move either marker and the other follows. Detect Punch End jumps back to the detected cut. "
        "Off: each marker only trims its own sample. Has no effect when Punch and Tail are different files.");
    punchWave.onGesture=[this](bool begin){if(begin)trimGesture(true);else endGesture();};
    tailWave.onGesture=[this](bool begin){if(begin)trimGesture(false);else endGesture();};
    reverbWave.onGesture=[this](bool begin){if(begin)beginGesture({"reverbOffset","reverbExtra"});else endGesture();};
    punchWave.onTrim=[this](double ms){setTrim(true,ms);};
    tailWave.onTrim=[this](double ms){setTrim(false,ms);};
    tailWave.onEndGesture=[this](bool begin){if(begin)beginGesture({"tailEndTrim"});else endGesture();};
    tailWave.onEndTrim=[this](double ms){setParameter("tailEndTrim",ms);};
    reverbWave.onTrim=[this](double ms){setReverbTrim(ms);};
    completedKick.onGesture=[this](pt::KickHandle h,bool begin){
        if(!begin){endGesture();return;}
        if(h==pt::KickHandle::punchEnd)trimGesture(true);
        else beginGesture({h==pt::KickHandle::tailStart?"join":"crossfade"});
    };
    completedKick.onEdit=[this](pt::KickHandle h,double value){
        if(h==pt::KickHandle::punchEnd)setTrim(true,value);
        else setParameter(h==pt::KickHandle::tailStart?"join":"crossfade",value);
    };
    completedKick.onOutputGesture=[this](bool tail,bool begin){
        const char* id=tail?"tailNormalize":"punchNormalize";
        if(begin)beginGesture({id});else endGesture();
    };
    completedKick.onOutputEdit=[this](bool tail,double db){setParameter(tail?"tailNormalize":"punchNormalize",db);};
    completedKick.onExport=[this]{exportKick();};
    completedKick.onDragOut=[this]{dragKickOut();};
    const char* ids[] = {"root", "cents", "trim", "punchOffset", "join", "crossfade", "punch", "tail", "output", "stretch", "reverbOffset", "reverbDelay", "reverbLevel", "punchPitch"};
    const char* names[] = {"TAIL ROOT", "SOURCE CENTS", "TAIL OFFSET", "PUNCH END", "JOIN SHIFT", "CROSSFADE", "PUNCH LEVEL", "TAIL LEVEL", "OUTPUT", "TAIL STRETCH", "REVERB OFFSET", "REVERB DELAY", "REVERB LEVEL", "PUNCH PITCH"};
    const char* tips[] = {
        "The note the loaded tail plays, C2 to D#3 (FL Studio note names). Play pitched notes on keys D#2 to D#5; every key sounds one octave lower than its name, so the root C3 is played on key C4. If no clear note was detected, set this to the note you hear; pitching still works.",
        "Fine-tune the tail if it is slightly sharp or flat (100 cents = 1 semitone). Goes back to 0 when a note is detected.",
        "Skip the beginning of the tail. The range follows the length of the loaded file.",
        "Cut the end of the punch away. You can also drag its END marker or use Detect Punch End.",
        "Moves the tail earlier (negative) or later (positive) where it meets the punch.",
        "How long the end of the punch and the start of the tail overlap and blend. The finished waveform shows the result.",
        "Punch volume.", "Tail volume.", "Overall volume of Kicktone. If the OUTPUT meter turns red, turn this down.",
        "Makes the tail longer or shorter (80-150 %) without changing its pitch. New notes use it once it is prepared in the background. Alt-click for 100 %.",
        "Skip the beginning of the reverb file. This does not change when the reverb starts playing.",
        "Start the reverb later, 0-300 ms after the punch ends. 0 ms = right at the end of the punch. Alt-click to reset.",
        "Volume of the reverb sample only, -60 dB (silent) to +6 dB. 0 dB = as recorded. Alt-click to reset.",
        "Pitch the punch up or down, -12 to +12 semitones (0 = unchanged). PITCH MODE chooses how. Keys E4 to D#5 also move the punch with the note you play (every key with PUNCH FOLLOWS TAIL PITCH); keys D2 to C#3 can use the octave-down punch. New notes use it once it is prepared in the background."
    };
    for (size_t i = 0; i < controls.size(); ++i) {
        auto& c = controls[i];
        c.label.setText(names[i], juce::dontSendNotification);
        c.label.setJustificationType(juce::Justification::centred);
        c.label.setFont(juce::Font(10.0f, juce::Font::bold));
        c.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 24);
        c.slider.setTooltip(tips[i]);
        if ((i >= 2 && i <= 5) || i == 10 || i == 11) c.slider.setTextValueSuffix(" ms");
        if ((i >= 6 && i <= 8) || i == 12) c.slider.setTextValueSuffix(" dB");
        if (i == 13) c.slider.setTextValueSuffix(" st");
        if (i == 9) { c.slider.setTextValueSuffix(" %"); c.slider.setDoubleClickReturnValue(true,100.0); }
        if (i == 11 || i == 12) c.slider.setDoubleClickReturnValue(true, 0.0);
        content.addAndMakeVisible(c.slider);
        content.addAndMakeVisible(c.label);
        if(i==2 || i==3 || i==10) {
            const bool punch=i==3, reverb=i==10;
            c.slider.setRange(0,600,0.1);c.slider.setDoubleClickReturnValue(true,0);
            c.slider.onDragStart=[this,punch,reverb]{
                if(reverb)beginGesture({"reverbOffset","reverbExtra"});
                else trimGesture(punch);
            };
            c.slider.onDragEnd=[this]{endGesture();};
            c.slider.onValueChange=[this,punch,reverb,i]{
                if(!syncingTrimControls) {
                    if(reverb)setReverbTrim(controls[i].slider.getValue());
                    else setTrim(punch,controls[i].slider.getValue());
                }
            };
        } else c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, ids[i], c.slider);
        // Attach first: JUCE installs its own text conversion functions.
        if (i == 0) {
            c.slider.textFromValueFunction = [](double v) {
                return juce::MidiMessage::getMidiNoteName(int(v), true, true, 5) + " / " + juce::String(int(v));
            };
            c.slider.valueFromTextFunction = [](const juce::String& t) {
                return double(t.contains("/") ? t.fromLastOccurrenceOf("/", false, false).getIntValue() : t.getIntValue());
            };
            c.slider.updateText();
        }
    }
    {   // v0.4.57 dry Tail end cut. The reverb keeps the full Tail.
        const char* ids[]={"tailEndTrim","tailEndFade"};const char* names[]={"TAIL END","END FADE"};
        const char* tips[]={"Make the dry tail shorter by cutting this much off its end (0 = full length). You can also drag the END marker on the tail waveform. The reverb keeps ringing. Alt-click for 0.",
                            "How long the tail fades out before the TAIL END cut, 1-2000 ms (default 10 ms). Only used when TAIL END is above 0. Alt-click for 10 ms."};
        for(size_t i=0;i<2;++i){auto& c=tailEndControls[i];
            c.label.setText(names[i],juce::dontSendNotification);c.label.setJustificationType(juce::Justification::centred);c.label.setFont(juce::Font(10.0f,juce::Font::bold));
            c.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,78,24);
            c.slider.setTooltip(tips[i]);
            content.addAndMakeVisible(c.slider);content.addAndMakeVisible(c.label);
            c.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,ids[i],c.slider);
            c.slider.setTextValueSuffix(" ms");c.slider.setDoubleClickReturnValue(true,i==0?0.0:10.0);}
    }
    reverbLowCutLabel.setText("REVERB LOW CUT / 200 Hz",juce::dontSendNotification);
    reverbLowCutLabel.setFont(juce::Font(11.0f,juce::Font::bold));
    reverbLowCut.addItem("OFF",1);
    reverbLowCut.addItem("12 dB/oct",2);
    reverbLowCut.addItem("24 dB/oct",3);
    reverbLowCut.addItem("48 dB/oct",4);
    reverbLowCut.setTooltip("Removes the low end (below 200 Hz) from the reverb only. OFF = no change. A higher number cuts more steeply.");
    content.addChildComponent(reverbLowCutLabel);
    content.addChildComponent(reverbLowCut);
    reverbLowCutAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,"reverbLowCut",reverbLowCut);
    phaseHeading.setText("PUNCH / TAIL PHASE",juce::dontSendNotification);
    phaseHeading.setColour(juce::Label::textColourId,whiteAccent);
    phaseHeading.setFont(juce::Font(12.0f,juce::Font::bold));
    phaseFineLabel.setText("FINE PHASE SHIFT",juce::dontSendNotification);
    phaseFineLabel.setFont(juce::Font(10.0f,juce::Font::bold));
    phaseFine.setSliderStyle(juce::Slider::LinearHorizontal);
    phaseFine.setTextBoxStyle(juce::Slider::TextBoxRight,false,82,26);
    phaseFine.setTextValueSuffix(" ms");
    phaseFine.setDoubleClickReturnValue(true,0.0);
    phaseFine.enableStepButtons(0.01);
    phaseFine.setTooltip("Shift the tail slightly against the punch, -3 to +3 ms, so they line up better. The timing and length of the kick do not change. Added on top of AUTO MATCH.");
    phaseAuto.setTooltip("Lines the tail up with the punch automatically (up to 3 ms) so they do not cancel each other out at the join. Worked out in the background for every note and only used when the match is reliable.");
    phaseInvert.setTooltip("Flip the tail upside down (polarity). Try it if the join sounds thin or hollow. The punch and the reverb are not affected.");
    phaseScore.setFont(juce::Font(11.0f));
    phaseScore.setJustificationType(juce::Justification::centredLeft);
    phaseScore.setTooltip("How well the punch and tail line up where they overlap, on the root note (measured before EQ and distortion). Shows no value when the overlap or the bass is too short to measure.");
    for(juce::Component* c:std::initializer_list<juce::Component*>{&phaseHeading,&phaseFineLabel,&phaseFine,&phaseAuto,&phaseInvert,&phaseScore})content.addAndMakeVisible(c);
    phaseAutoAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"phaseAuto",phaseAuto);
    phaseInvertAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"phaseInvert",phaseInvert);
    content.addAndMakeVisible(punchInvert);
    punchInvert.setTooltip("Flip the punch upside down (polarity). Timing does not change, and AUTO MATCH takes it into account. Used from the next note you play.");
    punchInvertAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"punchInvert",punchInvert);
    content.addAndMakeVisible(punchFollowTail);
    punchFollowTail.setTooltip("The punch follows the pitch of the tail on every key (D#2 to D#5), on top of PUNCH PITCH. Off: only keys E4 to D#5 follow; lower keys play the punch at PUNCH PITCH. The octave-down punch keys (D2 to C#3) keep working as before. New notes use it once it is prepared in the background.");
    punchFollowTailAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"punchFollowTail",punchFollowTail);
    // v0.5.4: with the Punch layer, only Punch 2 (the decay) follows the keys.
    content.addAndMakeVisible(punchFollowDecayOnly);
    punchFollowDecayOnly.setTooltip("Only Punch 2 (the decay) follows the keys; Punch 1 (the attack) stays at PUNCH PITCH on every key. Off: the whole punch follows. Needs PUNCH LAYER and PUNCH FOLLOWS TAIL PITCH.");
    punchFollowDecayOnlyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"punchFollowDecayOnly",punchFollowDecayOnly);
    // v0.5.10 PUNCH HARMONY: third / fifth above the played key, punch only.
    for(auto* l:{&punchHarmonyLabel,&punchHarmonyKeyLabel,&punchHarmonyScaleLabel}){content.addAndMakeVisible(*l);l->setFont(juce::Font(10.0f,juce::Font::bold));l->setJustificationType(juce::Justification::centredLeft);}
    punchHarmonyLabel.setText("PUNCH HARMONY",juce::dontSendNotification);punchHarmonyKeyLabel.setText("KEY",juce::dontSendNotification);punchHarmonyScaleLabel.setText("SCALE",juce::dontSendNotification);
    for(int i=0;i<pt::harmony::kModeCount;++i)punchHarmony.addItem(pt::harmony::kModeNames[i],i+1);
    for(int i=0;i<pt::harmony::kKeyCount;++i)punchHarmonyKey.addItem(pt::harmony::kKeyNames[i],i+1);
    for(int i=0;i<pt::harmony::kScaleCount;++i)punchHarmonyScale.addItem(pt::harmony::kScales[i].name,i+1);
    punchHarmony.setTooltip("Play the punch a musical interval above each note while the tail stays on the bass note. Off: normal punch pitch. Diatonic Third: the third above the note that fits your KEY and SCALE (+3 or +4 semitones). Perfect Fifth: always 7 semitones up. Works on every key, on top of PUNCH PITCH. With ONLY PUNCH 2 FOLLOWS on, only the decay moves. The tail and reverb do not change.");
    punchHarmonyKey.setTooltip("The key of your track (Diatonic Third only).");
    punchHarmonyScale.setTooltip("The scale of your track (Diatonic Third only). It decides whether each third is minor (+3) or major (+4).");
    content.addAndMakeVisible(punchHarmony);content.addAndMakeVisible(punchHarmonyKey);content.addAndMakeVisible(punchHarmonyScale);
    punchHarmonyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,"punchHarmony",punchHarmony);
    punchHarmonyKeyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,"punchHarmonyKey",punchHarmonyKey);
    punchHarmonyScaleAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,"punchHarmonyScale",punchHarmonyScale);
    // v0.5.5 PUNCH DECAY NOTE.
    content.addAndMakeVisible(decayNoteLabel);content.addAndMakeVisible(decayNoteButton);
    decayNoteLabel.setFont(juce::Font(13.0f));decayNoteLabel.setColour(juce::Label::textColourId,whiteAccent);
    decayNoteLabel.setTooltip("The note the punch's decay (after ATTACK END) plays on the root key: its middle note, and where its pitch starts and ends. Measured in the background, with PUNCH PITCH, PUNCH 2 and TUNE included.");
    decayNoteButton.setTooltip("Tune the punch so its decay plays the same note as TAIL ROOT. Changes PUNCH 2 TUNE when PUNCH LAYER is on (the attack stays as it is), otherwise PUNCH PITCH. Click again after it has measured again to get even closer.");
    decayNoteButton.onClick=[this]{proc.useDecayNote();};
    phaseFineAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"phaseFine",phaseFine);
    punchPitchModeLabel.setText("PITCH MODE",juce::dontSendNotification);
    punchPitchModeLabel.setFont(juce::Font(10.0f,juce::Font::bold));
    punchPitchMode.addItem("Stretch Aligned",1);punchPitchMode.addItem("Resample",2);punchPitchMode.addItem("Highest Fidelity",3);
    punchPitchMode.setTooltip("How the punch is pitched. Stretch Aligned: the punch keeps its length. "
        "Resample: like speeding a tape up or down - +12 makes the punch half as long, -12 twice as long, and the tail starts at the new punch end. "
        "Highest Fidelity: keeps the length and the exact character of the punch by repeating or skipping whole cycles, and follows its pitch sweep.");
    content.addAndMakeVisible(punchPitchModeLabel);content.addAndMakeVisible(punchPitchMode);
    // v0.4.63: three choices over two host parameters (punchPitchMode + the
    // appended punchPitchFidelity override), so no ComboBoxAttachment.
    punchPitchMode.onChange=[this] {
        const int id=punchPitchMode.getSelectedId();if(id<1 || id>3)return;
        beginGesture({"punchPitchMode","punchPitchFidelity"});
        setParameter("punchPitchFidelity",id==3?1:0);
        if(id!=3)setParameter("punchPitchMode",id-1);
        endGesture();
    };
    syncPunchPitchMode();
    qualityLabel.setText("PITCH ENGINE",juce::dontSendNotification);
    qualityLabel.setFont(juce::Font(11.0f,juce::Font::bold));
    qualityLabel.setColour(juce::Label::textColourId,whiteAccent);
    rebuildEngineItems();
    adaptiveLabel.setText("ADAPTIVE AMOUNT",juce::dontSendNotification);
    adaptiveLabel.setFont(juce::Font(11.0f,juce::Font::bold));
    adaptiveLabel.setJustificationType(juce::Justification::centredRight); // v0.4.52: sits right next to its knob
    adaptiveAmount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    adaptiveAmount.setTextBoxStyle(juce::Slider::TextBoxRight,false,64,22);
    adaptiveAmount.setTextValueSuffix(" %");
    adaptiveAmount.setTooltip("Makes the dry tail shorter on notes away from the root: higher notes get a lot shorter, lower notes a little. The root does not change. 0 % = the same as Highest Fidelity v4; up to 400 % for a stronger effect, even between nearby notes. A tail never gets shorter than 25 %. The reverb can keep ringing after the dry tail.");
    content.addAndMakeVisible(adaptiveLabel);content.addAndMakeVisible(adaptiveAmount);
    adaptiveAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"adaptiveAmount",adaptiveAmount);
    qualityMode.setTooltip("How the tail is pitched. Highest Fidelity v4: every cycle of the tail stays exactly as it is (crunch and noise included); "
        "the length is kept by repeating or skipping whole cycles where they match best. "
        "Adaptive Stretch v4: the same, but notes away from the root get shorter (ADAPTIVE AMOUNT). "
        "Extreme v5: the same idea with finer, smoother joins, and the tail keeps evolving through them. Takes longer to prepare.");
    content.addAndMakeVisible(qualityLabel);
    content.addAndMakeVisible(qualityMode);
    qualityMode.onChange=[this] {
        // v0.4.61: Adaptive Stretch v4 (4) and Highest Fidelity v4 (5); v0.4.68: Extreme v5 (6).
        const int mode=qualityMode.getSelectedId();if(mode!=4 && mode!=5 && mode!=6)return;
        beginGesture({"pitchAdaptive","pitchExtremeV5"});
        setParameter("pitchAdaptive",mode==4?1:0);setParameter("pitchExtremeV5",mode==6?1:0);endGesture();
        resizeLayout();
    };
    content.addAndMakeVisible(gate);
    gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "gate", gate);
    gate.setTooltip("On: the kick stops when you let go of the key, fading out over RELEASE. Off: the kick plays to its end (or until the next note).");
    content.addAndMakeVisible(lowKeyPunch);
    lowKeyPunch.setTooltip("Keys D2 to C#3: the tail plays one octave higher than these keys normally would, and the punch plays one octave below the tail, following each key (Resample, plus PUNCH PITCH). Off: these keys play like every other key.");
    lowKeyPunchAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"lowKeyPunch",lowKeyPunch);
    // v0.4.49 Punch layer and Glide: clear opt-in switches in the header.
    for(juce::Component* c:std::initializer_list<juce::Component*>{&punchLayer,&glide,&glideTime,&punch2Title,&punch2Name,&punch2Load,&punch2Unload})content.addAndMakeVisible(c);
    punchLayer.setTooltip("Use a second punch sample (Punch 2) for the decay. Punch plays the attack and Punch 2 takes over at ATTACK END (in Punch Attack / Decay), blended over TRANSITION. OFFSET chooses which part of Punch 2 you hear. The punch keeps its length, so the tail timing does not move. Off: one punch only.");
    glide.setTooltip("Hold a key and press another: the tail slides to the new note without a new punch. The tail keeps its stretch and length while it slides. Keys played one at a time still play a new kick each.");
    glideTime.setSliderStyle(juce::Slider::LinearHorizontal);glideTime.setTextBoxStyle(juce::Slider::TextBoxRight,false,70,22);
    glideTime.setTextValueSuffix(" ms");glideTime.setTooltip("How long the slide to the new note takes.");
    punchLayerAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"punchLayer",punchLayer);
    glideAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"glide",glide);
    glideTimeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"glideTime",glideTime);
    punch2Title.setText("PUNCH 2",juce::dontSendNotification);punch2Title.setFont(juce::Font(13.0f,juce::Font::bold));
    punch2Load.onClick=[this]{browse(3);};
    punch2Unload.onClick=[this]{proc.unloadFile(3);timerCallback();};
    punch2Unload.setTooltip("Unload Punch 2. The first punch keeps playing on its own.");
    content.addAndMakeVisible(punch2Offset);content.addAndMakeVisible(punch2OffsetLabel);
    punch2OffsetLabel.setText("OFFSET",juce::dontSendNotification);punch2OffsetLabel.setFont(juce::Font(11.0f,juce::Font::bold));
    punch2Offset.setSliderStyle(juce::Slider::LinearHorizontal);punch2Offset.setTextBoxStyle(juce::Slider::TextBoxRight,false,72,22);
    punch2Offset.setTextValueSuffix(" ms");punch2Offset.enableStepButtons(0.1);
    punch2Offset.setTooltip("Slides Punch 2 left or right in time, -25 to +25 ms. Drag right (+) to move it later, left (-) to move it earlier. Where the two punches meet is set by ATTACK END and TRANSITION in Punch Attack / Decay.");
    punch2OffsetAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"punch2Offset",punch2Offset);
    content.addAndMakeVisible(punch2Pitch);content.addAndMakeVisible(punch2PitchLabel);
    punch2PitchLabel.setText("TUNE",juce::dontSendNotification);punch2PitchLabel.setFont(juce::Font(11.0f,juce::Font::bold));
    punch2Pitch.setSliderStyle(juce::Slider::LinearHorizontal);punch2Pitch.setTextBoxStyle(juce::Slider::TextBoxRight,false,72,22);
    punch2Pitch.setTextValueSuffix(" st");punch2Pitch.enableStepButtons(0.1);
    punch2PitchAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"punch2Pitch",punch2Pitch);
    punch2Pitch.setDoubleClickReturnValue(true,0.0);
    punch2Pitch.setTooltip("Tune Punch 2 on its own, -12 to +12 semitones (Shift-click - / + for 1-cent steps). The attack (Punch 1) does not change. After that the whole punch follows PUNCH PITCH and the keys as usual. 0 st = Punch 2 as it is.");
    punchLayer.onClick=[this]{resizeLayout();};
    content.addAndMakeVisible(formantAmount);content.addAndMakeVisible(formantLabel);
    formantLabel.setText("CRUNCH",juce::dontSendNotification);formantLabel.setFont(juce::Font(11.0f,juce::Font::bold));
    formantAmount.setSliderStyle(juce::Slider::LinearHorizontal);formantAmount.setTextBoxStyle(juce::Slider::TextBoxRight,false,56,22);
    formantAmount.setTextValueSuffix(" %");formantAmount.enableStepButtons(1.0);
    formantAmount.setTooltip("Adds a layer of crunch made from the tail itself: the tail distorted, with only the highs above LOW CUT kept. Lower notes get more drive so they sound as bright as the root. Changing the amount is instant. 0 % = off.");
    formantAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"crunchMix",formantAmount);
    content.addAndMakeVisible(crunchLowCut);content.addAndMakeVisible(crunchLowCutLabel);
    crunchLowCutLabel.setText("LOW CUT",juce::dontSendNotification);crunchLowCutLabel.setFont(juce::Font(11.0f,juce::Font::bold));
    crunchLowCut.setSliderStyle(juce::Slider::LinearHorizontal);crunchLowCut.setTextBoxStyle(juce::Slider::TextBoxRight,false,72,20);
    crunchLowCut.setTextValueSuffix(" Hz");crunchLowCut.enableStepButtons(100.0);
    crunchLowCut.setTooltip("Only crunch above this frequency is added (2000-10000 Hz), so the bass and body stay clean.");
    crunchLowCutAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"crunchLowCut",crunchLowCut);
    crunchCharacter.addItemList({"Soft","Hard","Asym"},1);
    crunchCharacter.setTooltip("The kind of distortion used for the crunch: Soft (smooth), Hard (harder clipping) or Asym (lopsided, adds warmth).");
    content.addAndMakeVisible(crunchCharacter);
    crunchCharacterAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,"crunchCharacter",crunchCharacter);
    // v0.5.7 LOST ONLY
    content.addAndMakeVisible(crunchLostOnly);
    crunchLostOnly.setTooltip("Crunch only adds back the high end that a lower note lost compared to the root, and only as much as was lost, so it stays in phase with the note. The root and higher notes get no crunch. Off: normal crunch.");
    crunchLostOnlyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"crunchLostOnly",crunchLostOnly);
    {const char* ids[]={"verbPunchDuck","verbPunchDuckRelease"};const char* names[]={"PUNCH DUCK (%)","DUCK RELEASE (ms)"};
     const char* tips[]={"Test feature: turns all reverb down while the punch plays (up to -24 dB at 100 %), so the attack stays clean. 0 % = off.",
                         "How quickly the reverb comes back after the punch, 5-1000 ms."};
     for(int i=0;i<2;++i){auto& c=verbDuckControls[size_t(i)];content.addAndMakeVisible(c.slider);content.addAndMakeVisible(c.label);
        c.label.setText(names[i],juce::dontSendNotification);c.label.setFont(juce::Font(11.0f,juce::Font::bold));
        c.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,95,22);
        c.slider.setTooltip(tips[i]);c.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,ids[i],c.slider);}}
    const char* suffixes[]={"Drive","Tone","Mix","Gain"};
    const char* names2[]={"DRIVE", "TONE", "DRY / WET", "OUTPUT"};
    const char* units[]={" dB", " Hz", " %", " dB"};
    for(int k=0;k<2;++k) {
        auto& fx=distortion[size_t(k)];
        const juce::String prefix=k==0 ? "punchDist" : "tailDist";
        fx.title.setText(k==0?"PUNCH DISTORTION":"TAIL + REVERB DISTORTION",juce::dontSendNotification);
        fx.title.setColour(juce::Label::textColourId,k==0?amber:violet);
        fx.title.setFont(juce::Font(14.0f,juce::Font::bold));
        content.addAndMakeVisible(fx.title);
        content.addAndMakeVisible(fx.enabled);
        fx.enabled.setTooltip(k==0?"Distort the punch only.":"Distort the tail and the reverb together.");
        fx.enabledAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,prefix+"On",fx.enabled);
        fx.algorithm.addItem("Soft Clip",1);
        fx.algorithm.addItem("Hard Clip",2);
        fx.algorithm.addItem("Saturation",3);
        fx.algorithm.setTooltip("The type of distortion. It runs at 4x resolution so it stays clean, without harsh digital artefacts.");
        content.addAndMakeVisible(fx.algorithm);
        fx.algorithmAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,prefix+"Mode",fx.algorithm);
        for(int i=0;i<4;++i) {
            auto& c=fx.knobs[size_t(i)];
            c.label.setText(names2[i],juce::dontSendNotification);
            c.label.setJustificationType(juce::Justification::centred);
            c.label.setFont(juce::Font(10.0f,juce::Font::bold));
            c.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,78,23);
            c.slider.setTextValueSuffix(units[i]);
            if(i==0)c.slider.setTooltip("How hard the sound is pushed into the distortion, 0 to +30 dB.");
            if(i==1)c.slider.setTooltip("Makes the distorted sound darker: lower = darker. Only the distorted part changes.");
            if(i==2)c.slider.setTooltip("Blend between the clean sound (0 %) and the distorted sound (100 %).");
            if(i==3)c.slider.setTooltip("Volume of the distorted sound, -18 to +6 dB.");
            content.addAndMakeVisible(c.label);
            content.addAndMakeVisible(c.slider);
            c.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,prefix+suffixes[i],c.slider);
        }
    }
    for(int k=0;k<2;++k) {
        auto& tab=distortionTabs[size_t(k)];
        tab.setClickingTogglesState(false);
        tab.setTooltip(k==0 ? "Edit the punch distortion. The other distortion keeps working if it is on."
                            : "Edit the tail + reverb distortion. The punch distortion keeps working if it is on.");
        tab.onClick=[this,k] { showDistortionPage(k);saveLayout(); };
        content.addAndMakeVisible(tab);
    }
    showDistortionPage(0);
    transientHeading.setText("PUNCH TRANSIENT SHAPER",juce::dontSendNotification);
    transientHeading.setFont(juce::Font(14.0f,juce::Font::bold));
    transientHeading.setColour(juce::Label::textColourId,amber);
    content.addAndMakeVisible(transientHeading);
    content.addAndMakeVisible(transientEnable);
    transientEnable.setTooltip("Shape the hit and the body of the punch (after the Punch EQ, before the punch distortion). Off = no change at all.");
    transientEnableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"transientOn",transientEnable);
    const char* shaperIds[]={"transientAttack","transientSustain","transientAttackMs","transientRecovery",
                             "transientFocus","transientSensitivity","transientMix","transientOutput"};
    const char* shaperNames[]={"ATTACK","SUSTAIN","ATTACK TIME","RECOVERY", "FOCUS HPF","SENSITIVITY","DRY / WET","OUTPUT TRIM"};
    const char* shaperUnits[]={" %"," %"," ms"," ms"," Hz"," dB"," %"," dB"};
    const char* shaperTips[]={
      "Make the start of the punch harder (+) or softer (-), up to 12 dB. 0 % = no change.",
      "Make the body of the punch after the hit louder (+) or quieter (-), up to 12 dB.",
      "How quickly the hit is picked up, 1-35 ms. Shorter catches sharper hits.",
      "How quickly the shaper lets go after the hit, 15-300 ms. Short = tighter, long = smoother.",
      "Makes the shaper ignore the bass when it listens for the hit (20-500 Hz). You do not hear this filter.",
      "Sound quieter than this is ignored (-60 to -6 dB). Higher = only the loudest part of the hit is shaped.",
      "Blend between the original punch (0 %) and the shaped punch (100 %).",
      "Volume of the shaped punch, -12 to +12 dB. Turning the shaper off brings back the original volume."
    };
    for(size_t i=0;i<transientControls.size();++i) {
        auto& c=transientControls[i];
        c.label.setText(shaperNames[i],juce::dontSendNotification);
        c.label.setFont(juce::Font(9.5f,juce::Font::bold));
        c.label.setJustificationType(juce::Justification::centredLeft);
        c.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        c.slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,52,21);
        c.slider.setTextValueSuffix(shaperUnits[i]);
        c.slider.setTooltip(shaperTips[i]);
        if(i==0||i==1||i==7) c.slider.setDoubleClickReturnValue(true,i==0?30.0:0.0);
        {const double steps[]={1,1,.1,1,1,.5,1,.1};c.slider.enableStepButtons(steps[i]);} // v0.4.60
        content.addAndMakeVisible(c.label);
        content.addAndMakeVisible(c.slider);
        c.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,shaperIds[i],c.slider);
    }
    // Eight bands remain fixed APVTS parameters for DAW automation/state;
    // extra nodes activate on demand instead of adding parameters at runtime.
    content.addAndMakeVisible(monoSub);
    monoSub.setTooltip("Makes the bass of the Tail reverb mono up to the frequency on the slider (90-300 Hz); above it the stereo comes back. Works even when the EQ is off. With LINEAR PHASE off, switching it on adds a small delay that your DAW makes up for automatically (with a short 5 ms fade).");
    monoSubAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbSubMono",monoSub);
    content.addAndMakeVisible(monoSubHz);
    monoSubHz.setSliderStyle(juce::Slider::LinearHorizontal);monoSubHz.setTextBoxStyle(juce::Slider::TextBoxRight,false,62,20);
    monoSubHz.setTextValueSuffix(" Hz");monoSubHz.setDoubleClickReturnValue(true,90.0);
    monoSubHz.setTooltip("Below this frequency the Tail reverb becomes mono (90-300 Hz, default 90). Full stereo comes back about 60 Hz above it.");
    monoSubHzAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"tailVerbSubMonoHz",monoSubHz);
    content.addChildComponent(eqPostButton);   // v0.5.20
    eqPostButton.setTooltip("Off: this EQ comes before the distortion. "
        "On: it comes after the distortion, so it shapes the distorted sound. On the Tail EQ page it then shapes the tail and the reverb together. "
        "Switches smoothly while playing.");
    content.addAndMakeVisible(linearPhase);
    linearPhase.setTooltip("For the Tail reverb EQ. Linear phase changes only the tone, not the timing, but adds a little delay; Kicktone tells your DAW so it lines everything up (FL Studio does this automatically). You can switch while playing: the sound fades out for 5 ms and back in. Older projects keep their saved setting.");
    linearAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbLinearPhase",linearPhase);
    eqHeading.setText("PARAMETRIC EQ",juce::dontSendNotification);
    eqHeading.setFont(juce::Font(17.0f,juce::Font::bold));
    eqBandLabel.setText("BAND 2",juce::dontSendNotification);
    eqBandLabel.setFont(juce::Font(12.0f,juce::Font::bold));
    for(juce::Component* c:std::initializer_list<juce::Component*>{&eqHeading,&eqGraph,&eqBandLabel,&eqEnable,
                &bandEnable,&bandType,&bandSlope,&bandFreq,&bandGain,&bandQ,&addBand,
                &removeBand,&eqFreqLabel,&eqGainLabel,&eqQLabel,&eqSlopeLabel,&uiScaleBox,&uiScaleLabel,&eqSectionBox}) content.addAndMakeVisible(c);
    for(juce::Component* c:std::initializer_list<juce::Component*>{&punchRegionWave,&punchRegionEnable,&punchRegionTitle,
        &regionSplitLabel,&regionTransitionLabel,&attackGainLabel,&decayGainLabel,&regionSplit,&regionTransition,&attackGain,&decayGain,&attackEQButton,&decayEQButton})content.addAndMakeVisible(c);
    content.addAndMakeVisible(masterEnvelopeTitle);content.addAndMakeVisible(masterEnvelopeNote);content.addAndMakeVisible(masterEnvelopeGraph);
    for(juce::Component* c:std::initializer_list<juce::Component*>{&pitchDropTitle,&pitchDropNote,&pitchDropEnable,&pitchDropTarget})content.addAndMakeVisible(c);
    pitchDropTitle.setText("PITCH DROP / HIGHEST FIDELITY",juce::dontSendNotification);
    pitchDropTitle.setFont(juce::Font(12,juce::Font::bold));
    pitchDropNote.setText("Each selected lane drops to its normal pitch",juce::dontSendNotification);pitchDropNote.setFont(juce::Font(11));
    pitchDropTarget.addItemList(juce::StringArray{"Punch","Tail","Both"},1);
    pitchDropTarget.setTooltip("What gets the pitch drop: the punch, the tail or both. Each one drops from the moment it starts. With ROOT RENDER + PITCH the tail and its reverb drop together.");
    pitchDropEnable.setTooltip("Starts each note higher and slides it down to its normal pitch. Works with Highest Fidelity v4 and Extreme v5. Changes are prepared in the background and used for new notes when ready.");
    pitchDropEnableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"pitchDropOn",pitchDropEnable);
    pitchDropTargetAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,"pitchDropTarget",pitchDropTarget);
    const char* dropIds[]={"pitchDropAmount","pitchDropTime","pitchDropCurve"};
    const char* dropNames[]={"AMOUNT (st)","TIME (ms)","CURVE"};
    const char* dropTips[]={"How far above the normal pitch the note starts, 0-24 semitones. 0 = no drop.","How long the drop down to the normal pitch takes, 1-500 ms.","Shape of the drop. 1 = even. Above 1 = drops fast at first. Below 1 = drops slowly at first."};
    for(size_t i=0;i<3;++i){auto& slider=pitchDropSliders[i];auto& label=pitchDropLabels[i];
        content.addAndMakeVisible(slider);content.addAndMakeVisible(label);
        label.setText(dropNames[i],juce::dontSendNotification);label.setFont(juce::Font(11,juce::Font::bold));
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,85,22);slider.setTooltip(dropTips[i]);
        pitchDropAttachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,dropIds[i],slider);
    }
    // v0.4.62 ATTACK LAYER row.
    for(juce::Component* c:std::initializer_list<juce::Component*>{&attackTitle,&attackNote,&attackEnable})content.addAndMakeVisible(c);
    attackTitle.setText("ATTACK LAYER",juce::dontSendNotification);attackTitle.setFont(juce::Font(12,juce::Font::bold));
    attackNote.setText("A pitch-swept click added to the start of the Punch. It sweeps from START to END (KEY = the note you play).",juce::dontSendNotification);
    attackNote.setFont(juce::Font(11));attackNote.setJustificationType(juce::Justification::topLeft);
    attackEnable.setTooltip("Adds a short extra click sound to the start of the punch (1-50 ms). Works on every key and with every engine; the Punch EQ, transient shaper and distortion shape it too. Used from the next note you play.");
    attackEnableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"attackLayerOn",attackEnable);
    {const char* ids[]={"attackLayerLength","attackLayerStart","attackLayerLevel","attackLayerEnd"};
     const char* names[]={"LENGTH (ms)","START (Hz)","LEVEL (dB)","END (Hz)"};
     const char* tips[]={"How long the click lasts, 1-50 ms. Short (1-5 ms) is a click; longer becomes a zap that sweeps down to END.",
                         "The pitch the click starts at, 500-12000 Hz. It sweeps to END over LENGTH.",
                         "Volume of the click, -48 to +12 dB.",
                         "The pitch the sweep ends at. KEY (fully left, the default) = the note you play. "
                         "20-12000 Hz = the same end pitch on every key; set it above START to make the sweep rise. Double-click to type a value (K for KEY); Alt-click resets to KEY."};
     for(size_t i=0;i<4;++i){auto& slider=attackSliders[i];auto& label=attackLabels[i];
        content.addAndMakeVisible(slider);content.addAndMakeVisible(label);
        label.setText(names[i],juce::dontSendNotification);label.setFont(juce::Font(11,juce::Font::bold));
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,85,22);slider.setTooltip(tips[i]);
        attackAttachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,ids[i],slider);
        auto* parameter=proc.params.getParameter(ids[i]);slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));}
     attackSliders[2].setTextValueSuffix(" dB");}
    // v0.5 MONO BASS: the stereo difference below FREQUENCY is removed from the complete output.
    for(juce::Component* c:std::initializer_list<juce::Component*>{&bassMonoTitle,&bassMonoNote,&bassMonoEnable,&bassMonoLinear,&bassMonoHzLabel,&bassMonoMixLabel,&bassMonoHz,&bassMonoMix})content.addAndMakeVisible(c);
    // v0.5.1 LINEAR PHASE for MONO BASS.
    bassMonoLinear.setTooltip("Linear phase: the low end becomes mono without changing the shape or timing of the punch and tail. "
        "It adds about 35 ms of delay while MONO BASS is on; Kicktone tells your DAW, so FL Studio lines it up automatically. "
        "You can switch it at any time. Off: no delay.");
    bassMonoLinearAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"bassMonoLinear",bassMonoLinear);
    bassMonoTitle.setText("MONO BASS",juce::dontSendNotification);bassMonoTitle.setFont(juce::Font(12,juce::Font::bold));
    bassMonoHzLabel.setText("MONO BELOW",juce::dontSendNotification);bassMonoMixLabel.setText("MIX",juce::dontSendNotification);
    for(auto* l:{&bassMonoHzLabel,&bassMonoMixLabel})l->setFont(juce::Font(11,juce::Font::bold));
    bassMonoNote.setText("MIX 100 % = pure mono below the frequency, lower keeps some stereo. Above it nothing changes.",juce::dontSendNotification);
    bassMonoNote.setFont(juce::Font(11));
    bassMonoEnable.setTooltip("Makes the low end of the whole kick mono (punch, tail and reverb). Only the difference between left and right below the frequency is removed, so the kick sounds the same on a mono system. No added delay unless LINEAR PHASE is ticked.");
    bassMonoEnableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"bassMono",bassMonoEnable);
    for(auto* slider:{&bassMonoHz,&bassMonoMix}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,80,22);}
    bassMonoHz.setTextValueSuffix(" Hz");bassMonoHz.enableStepButtons(1.0);
    bassMonoHz.setTooltip("Everything below this frequency becomes mono (90-300 Hz). 100-150 Hz is typical.");
    bassMonoMix.setTextValueSuffix(" %");bassMonoMix.enableStepButtons(1.0);
    bassMonoMix.setTooltip("How mono the low end gets: 100 % = fully mono below the frequency, 50 % = half of the stereo width kept, 0 % = no change.");
    bassMonoHzAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"bassMonoHz",bassMonoHz);
    bassMonoMixAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"bassMonoMix",bassMonoMix);
    bassMonoHz.setDoubleClickReturnValue(true,tt::BassMono::kDefaultHz);bassMonoMix.setDoubleClickReturnValue(true,100.0);
    masterEnvelopeTitle.setText("COMPLETE WAVEFORM ENVELOPE",juce::dontSendNotification);
    masterEnvelopeTitle.setFont(juce::Font(12,juce::Font::bold));
    masterEnvelopeNote.setText("Previous kick chokes at the next MIDI note",juce::dontSendNotification);
    masterEnvelopeNote.setFont(juce::Font(11));
    const char* envelopeIDs[]={"masterAttack","masterDecay","masterSustain","masterRelease"};
    const char* envelopeNames[]={"ATTACK (ms)","DECAY (ms)","SUSTAIN (%)","RELEASE (ms)"};
    const char* envelopeTips[]={"Fades the whole kick in. 0 ms keeps the original start.",
        "How long the kick takes to fall from full volume to SUSTAIN after the attack.","The volume the whole kick stays at after DECAY. Default 100 %.",
        "Fades the whole kick out, reverb included, at its end or when you let go of the key. Default 1 ms."};
    for(size_t i=0;i<4;++i){auto& slider=masterEnvelopeSliders[i];auto& label=masterEnvelopeLabels[i];
        content.addAndMakeVisible(slider);content.addAndMakeVisible(label);
        label.setText(envelopeNames[i],juce::dontSendNotification);label.setFont(juce::Font(11,juce::Font::bold));
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,22);
        slider.onValueChange=[this]{masterEnvelopeGraph.settings=proc.masterEnvelopeSettings();masterEnvelopeGraph.repaint();};
        slider.setTooltip(juce::String(envelopeTips[i])+" Used from the next note you play.");
        masterEnvelopeAttachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,envelopeIDs[i],slider);
    }
    punchRegionTitle.setText("PUNCH ATTACK / DECAY",juce::dontSendNotification);punchRegionTitle.setFont(juce::Font(12,juce::Font::bold));
    regionSplitLabel.setText("ATTACK END (%)",juce::dontSendNotification);regionTransitionLabel.setText("TRANSITION (ms)",juce::dontSendNotification);
    attackGainLabel.setText("ATTACK GAIN (dB)",juce::dontSendNotification);decayGainLabel.setText("DECAY GAIN (dB)",juce::dontSendNotification);
    for(auto* label:{&regionSplitLabel,&regionTransitionLabel,&attackGainLabel,&decayGainLabel})label->setFont(juce::Font(11,juce::Font::bold));
    for(auto* slider:{&regionSplit,&regionTransition,&attackGain,&decayGain}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,50,22);}
    regionSplit.enableStepButtons(1.0);regionTransition.enableStepButtons(0.1);attackGain.enableStepButtons(0.1);decayGain.enableStepButtons(0.1); // v0.4.60
    regionSplit.setTooltip("Where the attack ends and the decay begins, as a percentage of the punch. You can also drag the line on the small waveform; hold Shift to move it slowly.");
    regionTransition.setTooltip("How smoothly the attack and decay settings blend into each other around the split, 0-20 ms. The punch length does not change.");
    punchRegionWave.setTooltip("The current punch. Drag the line: the attack is before it, the decay after it. Hold Shift to move it slowly.");
    punchRegionEnable.setTooltip("Turn on separate EQ and volume for the attack and the decay of the punch. Off: the punch plays as normal.");
    regionEnableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"punchRegionOn",punchRegionEnable);
    regionSplitAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"punchRegionSplit",regionSplit);
    regionTransitionAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"punchRegionTransition",regionTransition);
    attackGainAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"punchAttackGain",attackGain);
    decayGainAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"punchDecayGain",decayGain);
    attackGain.setTooltip("Volume of the attack part after its EQ, -24 to +24 dB. Needs Punch Attack / Decay turned on.");
    decayGain.setTooltip("Volume of the decay part after its EQ, -24 to +24 dB. Needs Punch Attack / Decay turned on.");
    punchRegionWave.onGesture=[this](bool begin){if(begin)beginGesture({"punchRegionSplit"});else endGesture();};
    punchRegionWave.onSplit=[this](double value){setParameter("punchRegionSplit",value);};
    attackEQButton.onClick=[this]{collapsed[2]=false;eqSectionBox.setSelectedId(6,juce::sendNotificationSync);resizeLayout();saveLayout();};
    decayEQButton.onClick=[this]{collapsed[2]=false;eqSectionBox.setSelectedId(7,juce::sendNotificationSync);resizeLayout();saveLayout();};
    eqSectionBox.addItem("TAIL EQ",1);
    eqSectionBox.addItem("PUNCH EQ",2);
    eqSectionBox.addItem("REVERB EQ",3);
    eqSectionBox.addItem("TAIL WET EQ",4);
    eqSectionBox.addItem("PUNCH WET EQ",5);
    eqSectionBox.addItem("PUNCH ATTACK EQ",6);eqSectionBox.addItem("PUNCH DECAY EQ",7);
    eqSectionBox.setTooltip("Choose which EQ to edit: Tail, Punch, Reverb, the Experimental Reverb EQs (Tail wet, Punch wet) or the Punch attack / decay EQs.");
    eqSectionBox.onChange=[this]{
        const int view=eqSectionBox.getSelectedId()-1;if(view<0)return;
        setParameter("punchRegionView",view>=5?view-4:0);setParameter("eqGeneratedView",view==3||view==4?1:0);setParameter("eqPunchGeneratedView",view==4?1:0);if(view<3)setParameter("eqView",view);
        setEQView(view);
    };
    setEQView(selectedEQView());
    bandType.addItem("Low Cut",1);bandType.addItem("High Cut",2);
    bandType.addItem("Bell",3);bandType.addItem("Low Shelf",4);bandType.addItem("High Shelf",5);
    bandSlope.addItem("12 dB/oct",1);bandSlope.addItem("24 dB/oct",2);bandSlope.addItem("48 dB/oct",3);
    for(auto* a:{&bandFreq,&bandGain,&bandQ}){
        a->setSliderStyle(juce::Slider::LinearHorizontal);
        a->setTextBoxStyle(juce::Slider::TextBoxRight,false,96,24);
    }
    bandFreq.setRange(20.0,20000.0,0.01);
    bandFreq.setSkewFactorFromMidPoint(1000.0);
    bandFreq.setTextValueSuffix(" Hz");
    bandGain.setRange(-18.0,18.0,0.1);bandGain.setTextValueSuffix(" dB");
    // Relative dragging avoids a click jumping to an aggressive gain setting.
    // Keep the actual dB range and host mapping unchanged for saved projects.
    bandGain.setSliderSnapsToMousePosition(false);
    bandGain.setTooltip("Boost or cut of this band in dB. Dragging is gentle: twice as far for the same change. Double-click to type an exact value.");
    bandQ.setRange(0.2,8.0,0.01);
    // v0.4.60 -/+ steps: frequency moves 0.5% of its (log-like) track, gain 0.1 dB, Q 0.01.
    bandFreq.enableStepButtons(0.005,true);bandGain.enableStepButtons(0.1);bandQ.enableStepButtons(0.01);
    eqFreqLabel.setText("FREQUENCY",juce::dontSendNotification);
    eqGainLabel.setText("GAIN",juce::dontSendNotification);
    eqQLabel.setText("Q / WIDTH",juce::dontSendNotification);
    eqSlopeLabel.setText("CUT SLOPE",juce::dontSendNotification);
    uiScaleLabel.setText("UI SIZE",juce::dontSendNotification);
    for(auto* l:{&eqFreqLabel,&eqGainLabel,&eqQLabel,&eqSlopeLabel,&uiScaleLabel})l->setFont(juce::Font(11.0f,juce::Font::bold));
    // v0.5.22: item ids stay the stored percent; the text is relative to the default size (75 = 100%).
    for(int preset:{50,75,100,125,150,200})uiScaleBox.addItem(juce::String(PunchTailProcessor::displayedUiScale(float(preset)))+"%",preset);
    uiScaleBox.setTooltip("Window size, 67 % to 267 % (100 % is the normal size). You can also drag the bottom-right corner.");
    uiScaleBox.onChange=[this]{if(uiScaleBox.getSelectedId()>0)setUIScale(uiScaleBox.getSelectedId()/100.0);};
    bandType.onChange=[this]{if(!updatingEQControls && bandType.getSelectedId()>0)
        setBandParameter(selectedBand,"Type",bandType.getSelectedId()-1);};
    bandSlope.onChange=[this]{if(!updatingEQControls && bandSlope.getSelectedId()>0)
        setBandParameter(selectedBand,"Slope",bandSlope.getSelectedId()-1);};
    bandEnable.onClick=[this]{if(!updatingEQControls)setBandParameter(selectedBand,"On",bandEnable.getToggleState()?1:0);};
    bandFreq.onValueChange=[this]{if(!updatingEQControls)setBandParameter(selectedBand,"Freq",bandFreq.getValue());};
    bandGain.onValueChange=[this]{if(!updatingEQControls)setBandParameter(selectedBand,"Gain",bandGain.getValue());};
    bandQ.onValueChange=[this]{if(!updatingEQControls)setBandParameter(selectedBand,"Q",bandQ.getValue());};
    eqGraph.onSelect=[this](int index){selectBand(index);};
    eqGraph.onType=[this](int index,int type){setBandParameter(index,"Type",type);selectBand(index);};
    eqGraph.onDrag=[this](int index,double hz,double gain){
        setBandParameter(index,"Freq",hz);
        const auto type=juce::roundToInt(proc.params.getRawParameterValue(eqPrefix()+juce::String(index)+"Type")->load());
        if(type!=tt::LowCut && type!=tt::HighCut)setBandParameter(index,"Gain",gain);
    };
    eqGraph.onAdd=[this](double f,double gain){addEQBand(f,gain);};
    eqGraph.onAddTyped=[this](double f,double gain,int type){addEQBand(f,gain,type);};   // v0.5.9 edge shelves
    // v0.4.69: right-click "Turn off band N". The band stays selected, so its ON switch turns it back on.
    eqGraph.onTurnOff=[this](int index){setBandParameter(index,"On",0);selectBand(index);};
    eqGraph.onQ=[this](int index,double q){setBandParameter(index,"Q",q);if(index==selectedBand)syncEQControls();};
    addBand.onClick=[this]{addEQBand();};
    removeBand.onClick=[this]{if(selectedBand>=3){setBandParameter(selectedBand,"On",0);selectBand(1);}};
    eqGraph.setSelected(selectedBand);
    syncEQControls();
    content.addAndMakeVisible(statusLabel);
    content.addAndMakeVisible(prepBar);   // v0.5.19
    prepBar.setTooltip("How far Kicktone is with preparing the 37 pitched tail keys (D#2 to D#5) in the background. Keys you play are prepared first. "
                       "The time left is an estimate. How many CPU cores are used is set by Background rendering on the About page.");
    prepBar.setSnapshot(proc.preparationProgress());
    content.addAndMakeVisible(pitchLabel);
    content.addAndMakeVisible(keys);
    pitchLabel.setColour(juce::Label::textColourId, whiteAccent);
    statusLabel.setFont(juce::Font(13.0f));
    keys.setAvailableRange(12, 96);
    keys.setLowestVisibleKey(24);
    keys.setOctaveForMiddleC(5);
    // Resize callbacks during construction must never overwrite the saved scale.
    for(int i=0;i<3;++i) {
        const char* names[]={"PUNCH","TAIL","REVERB"};
        const char* muteNames[]={"MUTE PUNCH","MUTE TAIL","MUTE REVERB"};
        const char* muteIds[]={"mutePunch","muteTail","muteReverb"};
        layerTabs[size_t(i)].setButtonText(names[i]);
        layerMute[size_t(i)].setButtonText(muteNames[i]);
        layerMute[size_t(i)].setTooltip(juce::String("Mute the ")+names[i]+" (with a short 10 ms fade). Saved with the project and can be automated.");
        content.addAndMakeVisible(layerMute[size_t(i)]);
        layerMuteAttachments[size_t(i)]=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,muteIds[i],layerMute[size_t(i)]);
        content.addAndMakeVisible(layerTabs[size_t(i)]);
        content.addAndMakeVisible(expandWave[size_t(i)]);
        content.addAndMakeVisible(sourceNames[size_t(i)]);
        content.addAndMakeVisible(foldButtons[size_t(i)]);
        content.addAndMakeVisible(effectActivity[size_t(i)]);
        layerTabs[size_t(i)].onClick=[this,i]{selectLayer(i);};
        expandWave[size_t(i)].onClick=[this,i]{selectedLayer=i;if(i==2)reverbMinimized=false;waveFocus=waveFocus==i?-1:i;saveLayout();resizeLayout();};
        expandWave[size_t(i)].setTooltip("Make this waveform bigger for trimming. Click again to go back.");
        foldButtons[size_t(i)].onClick=[this,i]{collapsed[size_t(i)]=!collapsed[size_t(i)];saveLayout();resizeLayout();};
    }
    for(int i=0;i<2;++i) {
        content.addAndMakeVisible(normalizeButtons[size_t(i)]);content.addAndMakeVisible(normalizeReset[size_t(i)]);
        normalizeButtons[size_t(i)].setButtonText(i==0?"Normalize Punch":"Normalize Tail + Rev");
        normalizeReset[size_t(i)].setButtonText("Reset");
        normalizeButtons[size_t(i)].setTooltip("Set the volume so the loudest peak of the finished root note (after EQ, transient shaper and distortion) is at -1 dB. Only the output volume changes. Click again after changing effects or pitch. Tail includes the reverb sample.");
        normalizeButtons[size_t(i)].onClick=[this,i]{
            timerCallback();if(completedKick.updating || processedPeaks[size_t(i)]<=1.e-7f)return;
            const double db=std::clamp(-1.0-20.0*std::log10(processedPeaks[size_t(i)]),-48.0,36.0);
            const char* id=i==0?"punchNormalize":"tailNormalize";
            beginGesture({id});setParameter(id,db);endGesture();timerCallback();
        };
        normalizeReset[size_t(i)].onClick=[this,i]{const char* id=i==0?"punchNormalize":"tailNormalize";beginGesture({id});setParameter(id,0);endGesture();timerCallback();};
    }
    // v0.5.21 ATTACK / DECAY NORMALIZE (user request): each part of the Punch on its own,
    // split where the Punch Attack/Decay panel's SPLIT and TRANSITION put it.
    for(int i=0;i<2;++i) {
        content.addAndMakeVisible(splitNormalizeButtons[size_t(i)]);
        splitNormalizeButtons[size_t(i)].setButtonText(i==0?"Norm Attack":"Norm Decay");
        splitNormalizeButtons[size_t(i)].setTooltip(juce::String("Set the volume of the punch ")+(i==0?"ATTACK (before":"DECAY (after")+
            " the split in Punch Attack / Decay, even when that panel is off) so its loudest peak is at -1 dB, without changing the "+
            (i==0?"decay":"attack")+". Normalize Punch can still set the whole punch afterwards. Click again after changing effects, pitch or the split.");
        splitNormalizeButtons[size_t(i)].onClick=[this,i]{normalizeSplit(i);};
    }
    content.addAndMakeVisible(splitNormalizeReset);splitNormalizeReset.setButtonText("Reset");
    splitNormalizeReset.onClick=[this]{beginGesture({"punchAttackNormalize","punchDecayNormalize"});
        setParameter("punchAttackNormalize",0);setParameter("punchDecayNormalize",0);endGesture();timerCallback();};
    content.addAndMakeVisible(generatedEnable);content.addAndMakeVisible(generatedKeepDry);content.addAndMakeVisible(generatedDistortWet);content.addAndMakeVisible(generatedPunchSend);content.addAndMakeVisible(generatedTailSend);content.addAndMakeVisible(generatedRootPitch);content.addAndMakeVisible(generatedEQButton);content.addAndMakeVisible(generatedTitle);content.addAndMakeVisible(generatedTailSettings);content.addAndMakeVisible(generatedPunchSettings);
    generatedTitle.setText("EXPERIMENTAL REVERB",juce::dontSendNotification);generatedTitle.setFont(juce::Font(12.0f,juce::Font::bold));
    generatedEnableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbOn",generatedEnable);
    generatedEnable.setTooltip("Turn on the experimental reverb. This also unmutes the Reverb layer so you can hear it; use MUTE REVERB if you want it silent again.");
    generatedEnable.onClick=[this]{
        if(generatedEnable.getToggleState() && proc.params.getRawParameterValue("muteReverb")->load()>.5f){
            beginGesture({"muteReverb"});setParameter("muteReverb",0.0);endGesture();
        }
    };
    generatedKeepDryAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbKeepDry",generatedKeepDry);
    generatedKeepDry.setTooltip("On: the dry tail stays at full volume and the reverb is added to it. Off: the dry tail fades out as the Tail MIX goes up.");
    generatedDistortWetAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbDistortWet",generatedDistortWet);
    generatedDistortWet.setTooltip("Send the experimental reverb through the Tail + Reverb distortion too.");
    generatedPunchSendAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbPunchSend",generatedPunchSend);
    generatedPunchSend.setTooltip("Send the punch into its own reverb (at -12 dB). PUNCH PRE-DELAY and PUNCH FADE-IN only change this reverb.");
    generatedTailSendAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbTailSend",generatedTailSend);
    generatedTailSend.setTooltip("Send the pitched tail into its own reverb. You can use the punch reverb, the tail reverb or both.");
    generatedRootPitchAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbRootPitch",generatedRootPitch);
    content.addAndMakeVisible(wetAutoMatch);content.addAndMakeVisible(wetPhaseLock);
    wetAutoMatch.setTooltip("For ROOT RENDER + PITCH: moves the tail reverb a little (up to 5 ms) or flips it, whichever cancels the bass of the dry tail the least. Worked out once on the root and used on every note.");
    wetPhaseLock.setTooltip("For ROOT RENDER + PITCH: below 200 Hz the tail reverb moves together with the dry tail, so its bass always adds to the kick's bass instead of cancelling it. Gives a tighter low end. Above 200 Hz nothing changes.");
    wetAutoMatchAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbAutoMatch",wetAutoMatch);
    wetPhaseLockAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"tailVerbPhaseLock",wetPhaseLock);
    generatedRootPitch.setTooltip("Makes the tail reverb once from the unpitched tail, then pitches and stretches it with the tail engine for every note. The punch reverb is not affected.");
    generatedTailSettings.onClick=[this]{showGeneratedSettings(false);};generatedPunchSettings.onClick=[this]{showGeneratedSettings(true);};
    generatedEQButton.onClick=[this]{
        collapsed[2]=false;setParameter("punchRegionView",0);setParameter("eqGeneratedView",1);setParameter("eqPunchGeneratedView",punchGeneratedPage?1:0);setEQView(punchGeneratedPage?4:3);saveLayout();resizeLayout();eqGraph.repaint();
    };
    const char* tailVerbIds[]={"tailVerbMix","tailVerbSpace","tailVerbWidth","tailVerbDistance","tailVerbDuck","tailVerbSubHz","tailVerbRecovery","tailVerbLevel"};
    const char* punchVerbIds[]={"punchVerbMix","punchVerbSpace","punchVerbWidth","punchVerbDistance","punchVerbDuck","punchVerbSubHz","punchVerbRecovery","punchVerbLevel"};
    const char* verbNames[]={"MIX (%)","SPACE (ms)","STEREO WIDTH (%)","DISTANCE (%)","SUB DUCK (%)","SUB RANGE (Hz)","RECOVERY (ms)","REVERB LEVEL (dB)"};
    const char* verbTips[]={"How much reverb you hear. Punch and Tail each have their own settings.","How long the reverb lasts, 20-3000 ms.","Stereo width of the reverb: 0 = mono, 100 = normal, 200 = extra wide.","How far away the sound seems: more distance = a later start and a darker, quieter reverb.","Turns down the bass of the reverb while the dry sound is loud, so the low end stays clean. 0 = off.","Up to which frequency SUB DUCK turns the reverb bass down, 40-250 Hz.","How quickly the reverb bass comes back after ducking, 20-300 ms.","Volume of this reverb after its EQ and ducking, -60 dB (silent) to +12 dB (Punch: +24 dB)."};
    for(int i=0;i<8;++i){auto& t=generatedControls[size_t(i)];content.addAndMakeVisible(t.slider);content.addAndMakeVisible(t.label);t.label.setText(verbNames[i],juce::dontSendNotification);t.label.setFont(juce::Font(11.0f,juce::Font::bold));t.slider.setSliderStyle(i==7?juce::Slider::LinearHorizontal:juce::Slider::RotaryHorizontalVerticalDrag);t.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,95,22);t.slider.setTooltip(verbTips[i]);t.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,tailVerbIds[i],t.slider);
        auto& punch=punchGeneratedControls[size_t(i)];content.addAndMakeVisible(punch.slider);content.addAndMakeVisible(punch.label);punch.label.setText(verbNames[i],juce::dontSendNotification);punch.label.setFont(juce::Font(11.0f,juce::Font::bold));punch.slider.setSliderStyle(i==7?juce::Slider::LinearHorizontal:juce::Slider::RotaryHorizontalVerticalDrag);punch.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,95,22);punch.slider.setTooltip(verbTips[i]);punch.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,punchVerbIds[i],punch.slider);
    }
    const char* timingIds[]={"tailVerbPunchPreDelay","tailVerbPunchFade"};const char* timingNames[]={"PUNCH PRE-DELAY (ms)","PUNCH FADE-IN (ms)"};const char* timingTips[]={"Starts the punch reverb later, 0-300 ms. The dry punch and tail do not move.","Fades the punch reverb in once it starts, 0-1000 ms. 0 = starts at full volume."};
    for(int i=0;i<2;++i){auto& c=generatedControls[size_t(i+8)];content.addAndMakeVisible(c.slider);content.addAndMakeVisible(c.label);c.label.setText(timingNames[i],juce::dontSendNotification);c.label.setFont(juce::Font(11.0f,juce::Font::bold));c.slider.setSliderStyle(juce::Slider::LinearHorizontal);c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,95,22);c.slider.setTooltip(timingTips[i]);c.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,timingIds[i],c.slider);}
    {auto& c=generatedControls[10];content.addAndMakeVisible(c.slider);content.addAndMakeVisible(c.label);c.label.setText("TAIL REVERB OFFSET (ms)",juce::dontSendNotification);c.label.setFont(juce::Font(11.0f,juce::Font::bold));c.slider.setSliderStyle(juce::Slider::LinearHorizontal);c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,95,22);c.slider.setTooltip("Moves the tail reverb earlier (negative) or later (positive), -300 to +300 ms. Earlier only works with ROOT RENDER + PITCH, and anything before the tail starts is cut off. The dry tail, punch and reverb sample do not move.");c.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"tailVerbOffset",c.slider);}
    {auto& c=generatedControls[11];content.addAndMakeVisible(c.slider);content.addAndMakeVisible(c.label);c.label.setText("TAIL FADE-IN (ms)",juce::dontSendNotification);c.label.setFont(juce::Font(11.0f,juce::Font::bold));c.slider.setSliderStyle(juce::Slider::LinearHorizontal);c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,95,22);c.slider.setTooltip("Fades the tail reverb in once it starts, 0-1000 ms. 0 = starts at full volume. The dry tail, punch and reverb sample do not change.");c.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params,"tailVerbTailFade",c.slider);}
    generatedEngine.addItemList({"Legacy","V2"},1);content.addAndMakeVisible(generatedEngine);content.addAndMakeVisible(generatedEngineLabel);
    generatedEngineLabel.setText("REVERB ENGINE",juce::dontSendNotification);generatedEngineLabel.setFont(juce::Font(11.0f,juce::Font::bold));generatedEngineLabel.setJustificationType(juce::Justification::centredRight);
    generatedEngine.setTooltip("The reverb sound, for both the punch and the tail reverb. Legacy: the original, simpler room. V2: a denser, smoother room with early reflections, where the highs fade faster as DISTANCE goes up.");
    generatedEngineAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params,"tailVerbEngine",generatedEngine);
    content.addAndMakeVisible(generatedFold);   // v0.5.13
    generatedFold.setTooltip("Hide or show the Experimental Reverb settings. ENABLE stays visible, and the reverb keeps working either way. Saved with the project.");
    generatedFold.onClick=[this]{generatedMinimized=!generatedMinimized;updateGeneratedVisibility();saveLayout();resizeLayout();};
    showGeneratedSettings(false);
    content.addAndMakeVisible(reverbFold);
    reverbFold.setTooltip("Hide or show the reverb sample panel. Saved with the project.");
    reverbFold.onClick=[this]{
        reverbMinimized=!reverbMinimized;
        if(reverbMinimized && waveFocus==2)waveFocus=-1;
        saveLayout();resizeLayout();
    };
    punchWave.onSelect=[this]{selectLayer(0);};tailWave.onSelect=[this]{selectLayer(1);};reverbWave.onSelect=[this]{selectLayer(2);};
    for(juce::Component* c:std::initializer_list<juce::Component*>{static_cast<juce::Component*>(&clipButton),&layerHeading,&editHelp,&previewLabel})content.addAndMakeVisible(c);
    editHelp.setText("Waveform: drag orange Punch / white Tail up-down for post-effects volume  |  Shift: fine",juce::dontSendNotification);
    editHelp.setFont(juce::Font(11.0f));
    previewLabel.setText("Root-note preview: EQ + transient + distortion",juce::dontSendNotification);
    previewLabel.setFont(juce::Font(11.0f));
    clipButton.setTooltip("Output level. With OUTPUT LIMITER on it shows how much the limiter turns the sound down (GR). With it off, CLIP lights up when the output goes over 0 dB and stays lit until you click it. If it lights up, turn OUTPUT down.");
    clipButton.onClick=[this]{proc.clipLatched.store(false);};
    // v0.4.60: optional output safety limiter, ticked for new instances.
    content.addAndMakeVisible(limiterToggle);
    limiterToggle.setTooltip("Stops the output from going over -0.3 dB, without adding delay. Quieter sound is not changed at all. "
        "On by default; off uses a simple 0 dB safety clip instead. The OUTPUT meter shows GR while it is working. Projects saved before v0.4.60 open with it off so they sound the same.");
    limiterAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,"outputLimiter",limiterToggle);
    for(size_t i=0;i<controls.size();++i) {
        auto* parameter=proc.params.getParameter(ids[i]);
        controls[i].slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    for(int bus=0;bus<2;++bus)for(int i=0;i<4;++i) {
        const auto id=juce::String(bus==0?"punchDist":"tailDist")+suffixes[i];auto* parameter=proc.params.getParameter(id);
        distortion[size_t(bus)].knobs[size_t(i)].slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    const char* transIds[]={"transientAttack","transientSustain","transientAttackMs","transientRecovery","transientFocus","transientSensitivity","transientMix","transientOutput"};
    for(int i=0;i<8;++i){auto* parameter=proc.params.getParameter(transIds[i]);transientControls[size_t(i)].slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));}
    restoreLayout();
    applyView(storedView());lastView=currentView();   // v0.5.1
    setResizable(true,true);
    setResizeLimits(baseWidth/2,baseHeight/2,baseWidth*2,baseHeight*2);
    if(auto* constrainer=getConstrainer())constrainer->setFixedAspectRatio(double(baseWidth)/baseHeight);
    // v0.5.24: open no larger than the screen. The stored uiScale is not rewritten,
    // so the project opens at its saved size again on a larger screen.
    const double storedScale=fitToScreen(juce::jlimit(0.5,2.0,
        double(proc.params.getRawParameterValue("uiScale")->load())/100.0));
    resizingFromPreset=true;
    setSize(juce::roundToInt(baseWidth*storedScale),juce::roundToInt(baseHeight*storedScale));
    resizingFromPreset=false;
    updateScale(false);
    timerCallback(); // Show restored sample waveforms immediately when the UI opens.
    startTimerHz(12);
}

PunchTailEditor::~PunchTailEditor() { endGesture(); stopTimer(); setLookAndFeel(nullptr); }

void PunchTailEditor::beginGesture(std::initializer_list<const char*> ids) {
    endGesture();for(auto id:ids){auto* p=proc.params.getParameter(id);p->beginChangeGesture();gestureParams.push_back(p);}
}
void PunchTailEditor::endGesture() {
    for(auto* p:gestureParams)p->endChangeGesture();gestureParams.clear();
}
void PunchTailEditor::syncPunchPitchMode() {
    punchPitchMode.setSelectedId(proc.punchPitchModeValue()+1,juce::dontSendNotification);
}
void PunchTailEditor::setParameter(const char* id,double value) {
    auto* p=proc.params.getParameter(id);const float normal=p->convertTo0to1(float(value));
    if(p->getValue()!=normal)p->setValueNotifyingHost(normal);
}
// v0.5.21: the part's FINAL peak (with the whole-Punch trim on top) becomes -1 dBFS.
// processedPeaks[2/3] are measured before any trim, so repeating the click is idempotent.
void PunchTailEditor::normalizeSplit(int part) {
    timerCallback();
    const float peak=processedPeaks[size_t(2+part)];
    if(completedKick.updating || peak<=1.e-7f)return;
    const double whole=proc.params.getRawParameterValue("punchNormalize")->load();
    const double db=std::clamp(-1.0-20.0*std::log10(peak)-whole,-48.0,36.0);
    const char* id=part==0?"punchAttackNormalize":"punchDecayNormalize";
    beginGesture({id});setParameter(id,db);endGesture();timerCallback();
}
void PunchTailEditor::trimGesture(bool punch) {
    if(lockedSplitActive())beginGesture({"punchOffset","punchExtra","trim","tailExtra"});
    else if(punch)beginGesture({"punchOffset","punchExtra"});
    else beginGesture({"trim","tailExtra"});
}
void PunchTailEditor::setTrim(bool punch,double ms) {
    const auto a=proc.asset(punch?0:1);if(!a)return;
    const double duration=a->sample.duration()*1000.0;
    const double limit=std::ceil(duration*10.0)/10.0;
    const bool own=gestureParams.empty();
    if(own)trimGesture(punch);
    if(lockedSplitActive()) {
        // v0.4.66 LOCK SPLIT: one cut in the shared sample. The Punch ends at the
        // cut; the Tail starts CROSSFADE ms before it, so the crossfade blends the
        // same audio and the root note plays the original kick back unchanged.
        const double fade=proc.params.getRawParameterValue("crossfade")->load();
        const double cut=clampLockedCut(punch?duration-ms:ms+fade);
        const auto head=pt::splitTrim(std::clamp(duration-cut,0.0,limit)),tail=pt::splitTrim((std::max)(0.0,cut-fade));
        setParameter("trim",tail.base);setParameter("tailExtra",tail.extra);
        setParameter("punchOffset",head.base);setParameter("punchExtra",head.extra);
    } else {
        const auto parts=pt::splitTrim(ms>=duration ? limit : std::clamp(ms,0.0,limit));
        setParameter(punch?"punchOffset":"trim",parts.base);
        setParameter(punch?"punchExtra":"tailExtra",parts.extra);
    }
    if(own)endGesture();
}
// ---- v0.4.66 LOCK SPLIT ----------------------------------------------------
void PunchTailEditor::updateLockedPair() {
    const auto& p=displayed[0];const auto& t=displayed[1];
    if(p.get()==lockedSeen[0] && t.get()==lockedSeen[1])return;   // compare bytes only on a change
    lockedSeen={{p.get(),t.get()}};
    lockedPair=PunchTailProcessor::isSplitPair(p.get(),t.get());
}
double PunchTailEditor::clampLockedCut(double ms) const {
    const auto& t=displayed[1];if(!t)return ms;
    const double duration=t->sample.duration()*1000.0;
    const double lo=(std::min)(3.0,duration*.5),hi=(std::max)(lo,duration-40.0);
    return std::clamp(ms,lo,hi);
}
double PunchTailEditor::lockedCutTarget(bool& changed) const {
    // The Punch END is the cut; the Tail START must sit CROSSFADE ms before it.
    changed=false;
    const auto& t=displayed[1];if(!t)return 0;
    auto get=[&](const char* id){return double(proc.params.getRawParameterValue(id)->load());};
    const double cut=t->sample.duration()*1000.0-get("punchOffset")-get("punchExtra");
    const double target=std::round((std::max)(0.0,cut-get("crossfade"))*10.0)/10.0;
    changed=std::abs(get("trim")+get("tailExtra")-target)>.001;
    return target;
}
void PunchTailEditor::syncLockedGuides() {
    if(!lockedSplitActive()){punchWave.setLockCaption({});tailWave.setLockCaption({});return;}
    const auto& t=displayed[1];
    const double cut=t->sample.duration()*1000.0-proc.params.getRawParameterValue("punchOffset")->load()-proc.params.getRawParameterValue("punchExtra")->load();
    const juce::String caption="LOCKED  CUT "+juce::String(cut,1)+" ms";
    punchWave.setLockCaption(caption);tailWave.setLockCaption(caption);
}
void PunchTailEditor::setReverbTrim(double ms) {
    const auto a=proc.asset(2);if(!a)return;
    const double duration=a->sample.duration()*1000.0;
    const double limit=std::ceil(duration*10.0)/10.0;
    const auto parts=pt::splitTrim(ms>=duration ? limit : std::clamp(ms,0.0,limit));
    const bool own=gestureParams.empty();
    if(own)beginGesture({"reverbOffset","reverbExtra"});
    setParameter("reverbOffset",parts.base);
    setParameter("reverbExtra",parts.extra);
    if(own)endGesture();
}
void PunchTailEditor::detectPunchEnd() {
    const auto a=proc.asset(0);if(!a || !a->punchEnd.valid)return;
    setTrim(true,a->sample.duration()*1000.0-a->punchEnd.endMs);
    timerCallback();
}

void PunchTailEditor::browse(int slot) {
    chooser = std::make_unique<juce::FileChooser>(slot == 0 ? "Choose punch" : slot == 1 ? "Choose tail" : slot == 3 ? "Choose punch 2" : "Choose reverb", juce::File{}, "*.wav;*.mp3");
    auto safe = juce::Component::SafePointer<PunchTailEditor>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe, slot](const juce::FileChooser& c) {
                             if (!safe || !c.getResult().existsAsFile()) return;
                             safe->proc.loadFile(slot, c.getResult());
                         });
}

bool PunchTailEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) if (juce::File(f).hasFileExtension("wav;mp3")) return true;
    return false;
}
juce::Point<int> PunchTailEditor::logicalPoint(int x,int y) const {
    const float scale=(std::max)(0.5f,uiScale);
    return {juce::roundToInt(x/scale),juce::roundToInt(y/scale)};
}
int PunchTailEditor::dropSlotAt(int logicalX,int logicalY) const {
    // v0.4.69: the Punch 2 row is its own drop target while it is shown.
    if(punchLayerShown() && punch2Panel.contains(logicalX,logicalY)) return 3;
    int slot=selectedLayer;
    for(int i=0;i<3;++i)if(logicalX>=sourcePanels[size_t(i)].getX() && logicalX<sourcePanels[size_t(i)].getRight())slot=i;
    return slot;
}
void PunchTailEditor::setPunch2DropHover(bool hover) {
    if(hover==punch2DropHover)return;
    punch2DropHover=hover;repaint();
}
void PunchTailEditor::fileDragEnter(const juce::StringArray& files,int x,int y) {fileDragMove(files,x,y);}
void PunchTailEditor::fileDragMove(const juce::StringArray& files,int x,int y) {
    const auto at=logicalPoint(x,y);
    setPunch2DropHover(isInterestedInFileDrag(files) && dropSlotAt(at.x,at.y)==3);
}
void PunchTailEditor::fileDragExit(const juce::StringArray&) {setPunch2DropHover(false);}
void PunchTailEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    setPunch2DropHover(false);
    const auto at=logicalPoint(x,y);
    if (dropSlotAt(at.x,at.y)==3) {
        // v0.4.69: dropped on the Punch 2 row -> Punch 2 (the first audio file if several).
        for (const auto& f : files) if (juce::File(f).hasFileExtension("wav;mp3")) {proc.loadFile(3,juce::File(f));return;}
        return;
    }
    if (files.size() >= 2) {
        for (int i=0;i<(std::min)(3,files.size());++i)
            proc.loadFile(i,juce::File(files[i]));
    } else if (files.size() == 1) {
        const int slot=dropSlotAt(at.x,at.y);
        proc.loadFile(slot,juce::File(files[0]));
    }
}

void PunchTailEditor::timerCallback() {
    syncViewState();   // v0.5.1: Reverb TAIL/PUNCH page, zooms, join view, export bits
    if(aboutPage.isVisible())refreshAboutInfo();
    if(tt::ui::themeEpoch().load()!=seenThemeEpoch || tt::ui::themeIndex()!=appliedTheme)applyTheme(); // another window changed it
    const bool requestedMono=proc.params.getRawParameterValue("tailVerbSubMono")->load()>.5f;
    monoSub.setButtonText("MONO SUB");   // v0.5.1: switches live
    monoSubHz.setEnabled(requestedMono);
    const bool requestedLinear=proc.params.getRawParameterValue("tailVerbLinearPhase")->load()>.5f;
    linearPhase.setButtonText("LINEAR PHASE");(void)requestedLinear;   // v0.5.1: switches live
    const bool extreme=proc.tailEngine()==5;   // v0.4.68
    const bool adaptive=!extreme && proc.params.getRawParameterValue("pitchAdaptive")->load()>.5f;
    const bool character=!adaptive; // v0.4.61: Highest Fidelity v4 (or Extreme v5) unless Adaptive Stretch v4
    pitchDropEnable.setEnabled(character);pitchDropTarget.setEnabled(character);
    for(auto& c:pitchDropSliders)c.setEnabled(character);
    for(auto& c:pitchDropLabels)c.setEnabled(character);
    pitchDropNote.setText(character?"Each selected lane drops to its normal pitch":"Select Highest Fidelity v4 or Extreme v5 to use Pitch Drop",juce::dontSendNotification);
    adaptiveAmount.setEnabled(adaptive);adaptiveLabel.setEnabled(adaptive);
    qualityMode.setSelectedId(extreme?6:adaptive?4:5,juce::dontSendNotification);
    syncPunchPitchMode();
    int mask=0;for(int i=0;i<3;++i)if(collapsed[size_t(i)])mask|=1<<i;
    if(int(proc.params.state.getProperty("uiLayer",0))!=selectedLayer ||
       int(proc.params.state.getProperty("uiWaveFocus",-1))!=waveFocus ||
       int(proc.params.state.getProperty("uiCollapsed",0))!=mask ||
       bool(proc.params.state.getProperty("uiReverbMinimized",true))!=reverbMinimized ||
       bool(proc.params.state.getProperty("uiGeneratedMinimized",false))!=generatedMinimized ||
       int(proc.params.state.getProperty("uiDistPage",0))!=distortionPage) {restoreLayout();resizeLayout();}
    const auto nextPunch = proc.asset(0), nextTail = proc.asset(1), nextReverb=proc.asset(2);
    const bool changed = displayed[0] != nextPunch || displayed[1] != nextTail || displayed[2] != nextReverb;
    displayed = {nextPunch, nextTail, nextReverb};
    for(int i=0;i<3;++i) unloadButtons[size_t(i)].setEnabled(bool(displayed[size_t(i)]));
    {   const auto layerAsset=proc.asset(3);
        punch2Name.setText(layerAsset?layerAsset->name:"Drop Punch 2 here",juce::dontSendNotification);
        punch2Name.setTooltip(layerAsset?layerAsset->name+": the decay, from ATTACK END. Drop another .wav or .mp3 file on this row to replace it.":"Load the decay punch (Punch 2): drop a .wav or .mp3 file on this row, or click Load punch 2.");
        punch2Unload.setEnabled(bool(layerAsset));
        glideTime.setEnabled(glide.getToggleState());
        {const bool harmonyOn=punchHarmony.getSelectedId()>1;   // v0.5.10: a harmony mode implies the follow
         punchFollowDecayOnly.setEnabled(punchLayerShown()&&(punchFollowTail.getToggleState()||harmonyOn));
         const bool third=punchHarmony.getSelectedId()==2;
         punchHarmonyKey.setEnabled(third);punchHarmonyScale.setEnabled(third);}
        {const bool mono=bassMonoEnable.getToggleState();bassMonoHz.setEnabled(mono);bassMonoMix.setEnabled(mono);   // v0.5
         }
        {const bool root=proc.params.getRawParameterValue("tailVerbRootPitch")->load()>.5f;wetAutoMatch.setEnabled(root);wetPhaseLock.setEnabled(root);}
        if(punchLayerShown()!=punch2Load.isVisible())resizeLayout(); }
    if((selectedLayer==1)!=formantAmount.isVisible())resizeLayout();
    for(int i=0;i<2;++i)trimButtons[size_t(i)].setEnabled(bool(displayed[size_t(i)]));
    updateLockedPair();   // v0.4.66
    // Locked: the Tail START follows the Punch END (the cut) whenever they
    // disagree outside a gesture - engaging the lock, a CROSSFADE change, a
    // restored project or host automation of one trim.
    if(lockedSplitActive() && gestureParams.empty()) {
        bool changed=false;const double target=lockedCutTarget(changed);
        if(changed){const auto parts=pt::splitTrim(target);beginGesture({"trim","tailExtra"});
            setParameter("trim",parts.base);setParameter("tailExtra",parts.extra);endGesture();}
    }
    punchWave.setSample(nextPunch);
    // v0.5.33: Punch 2 in purple on the Punch waveform, where it plays.
    punchWave.setLayer(punchLayerShown()?proc.asset(3):nullptr,proc.params.getRawParameterValue("punchRegionSplit")->load(),
                       proc.params.getRawParameterValue("punchRegionTransition")->load(),proc.params.getRawParameterValue("punch2Offset")->load());
    tailWave.setSample(nextTail);
    reverbWave.setSample(nextReverb);
    // Re-render the displayed complete kick only on a file, bank or relevant
    // parameter change. The audio callback never generates GUI waveforms.
    const char* previewIds[] = {"root", "cents", "trim", "punchOffset", "join", "crossfade", "punch", "tail", "output", "stretch", "punchExtra", "tailExtra", "reverbOffset", "reverbExtra", "reverbDelay", "reverbLevel", "reverbLowCut", "phaseAuto", "phaseFine", "phaseInvert", "punchInvert"};
    std::array<float, 21> previewParams{};
    for (size_t i = 0; i < previewParams.size(); ++i)
        previewParams[i] = proc.params.getRawParameterValue(previewIds[i])->load();
    previewParams[3]+=previewParams[10];previewParams[2]+=previewParams[11];previewParams[12]+=previewParams[13];
    syncLockedGuides();   // v0.4.66 CUT captions follow the lock and the trims
    syncingTrimControls=true;
    for(int i:{2,3,10}) {
        auto& slider=controls[size_t(i)].slider;
        const auto a=displayed[i==3?0:i==10?2:1];
        const double maximum=a ? (std::max)(.1,std::ceil(a->sample.duration()*10000.0)/10.0) : 600.0;
        slider.setRange(0,maximum,.1);slider.setValue(previewParams[size_t(i==10?12:i)],juce::dontSendNotification);slider.setEnabled(bool(a));
    }
    syncingTrimControls=false;
    punchDetectButton.setEnabled(displayed[0] && displayed[0]->punchEnd.valid);
    if(displayed[0])punchDetectButton.setTooltip(displayed[0]->punchEnd.valid
        ? "Suggested punch end: " + juce::String(displayed[0]->punchEnd.endMs,1) + " ms. Click to use it, then fine-tune by dragging the END marker."
        : "No clear spot where the punch turns into the tail was found. Drag the punch END marker by hand.");
    auto nextBank = proc.previewBank();
    const auto nextPreparedPunch=proc.previewPunch();
    const auto nextTailWet=proc.previewTailWet(juce::jlimit(0,127,juce::roundToInt(previewParams[0])));
    const int regionRoot=juce::jlimit(0,127,juce::roundToInt(previewParams[0]));
    const double regionScale=nextPreparedPunch?nextPreparedPunch->forNote(regionRoot).punchTimeScale:1.;
    const double sourceEnd=displayed[0]?(std::max)(0.,displayed[0]->sample.duration()*1000-previewParams[3]):0.;
    const auto region=proc.punchRegionSettings();
    punchRegionWave.setAudio(displayed[0],sourceEnd,regionScale,region.splitPercent,region.transitionMs,region.enabled);
    attackEQButton.setEnabled(sourceEnd>0);decayEQButton.setEnabled(sourceEnd>0);
    const auto nextCrunch=proc.previewCrunch();
    const bool bankChanged = nextBank != displayedBank || nextPreparedPunch!=displayedPreparedPunch || nextTailWet!=displayedTailWet || nextCrunch!=displayedCrunch;
    auto nextPhase=proc.previewPhase();
    const bool phaseChanged=nextPhase != displayedPhase;
    std::vector<float> effectParams{proc.tailWetUsesLinearPhase()?1.f:0.f,proc.tailWetUsesSubMono()?1.f:0.f,proc.bassMonoUsesLinearPhase()?1.f:0.f};
    // v0.5.32: the Punch chain (Punch Attack/Decay incl. its gains and trims, Punch
    // EQ and its position, transient, Punch distortion settings) touches only the
    // Punch bus. Punch distortion ON is not in it: switching it also switches the
    // Tail lane's matched 4x oversampling delay, so it needs a full render.
    std::vector<float> punchParams;
    for(auto* parameter:proc.getParameters())if(auto* p=dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
        const auto id=p->paramID;
        if(id=="punchDistOn")effectParams.push_back(p->getValue());
        else if(id.startsWith("transient")||id.startsWith("punchDist")||id.startsWith("punchEq")||id.startsWith("punchRegion")||id.startsWith("punchAttack")||id.startsWith("punchDecay"))
            punchParams.push_back(p->getValue());
        else if(id.startsWith("tailDist")||id.startsWith("master")||id.startsWith("eq")||id.startsWith("reverbEq")||id.startsWith("tailVerb")||id.startsWith("punchVerb")||id.startsWith("verbPunchDuck")||id.startsWith("crunch")||id.startsWith("tailEnd")||id=="mutePunch"||id=="muteTail"||id=="muteReverb"||id=="outputLimiter"||id=="tailEqPost"||id.startsWith("attackLayer")||id.startsWith("bassMono"))
            effectParams.push_back(p->getValue());
    }
    const double previewRate=proc.getSampleRate()>0?proc.getSampleRate():44100;
    // v0.5.28: Punch / Tail normalize gains are tracked apart. The waveform remixes
    // them at once (showOutputGains); during a drag on it they wait for mouse-up
    // instead of restarting the render on every tick.
    std::array<float,2> normalizeGains{};
    for(int i=0;i<2;++i)normalizeGains[size_t(i)]=juce::Decibels::decibelsToGain(proc.params.getRawParameterValue(i==0?"punchNormalize":"tailNormalize")->load());
    const bool gainsChanged=normalizeGains!=lastNormalizeGains && !completedKick.outputDragActive();
    const bool otherChanged = previewParams != lastPreviewParams || effectParams!=lastEffectParams || previewRate!=lastPreviewRate || gainsChanged;
    const bool parameterChanged = otherChanged || punchParams!=lastPunchParams;
    if (changed) repaint(); // Update filenames and individual source-duration labels.
    if (changed || bankChanged || phaseChanged || parameterChanged) {
        // Update trim overlays on both original full-length sample waveforms.
        punchWave.setGuideMilliseconds(previewParams[3]);
        tailWave.setGuideMilliseconds(previewParams[2]);
        tailWave.setEndGuide(proc.tailEndTrimValue(),proc.tailEndFadeValue());
        reverbWave.setGuideMilliseconds(previewParams[12]);
        syncLockedGuides();
        const int root = juce::jlimit(0, 127, juce::roundToInt(previewParams[0]));
        const double actualRoot = double(previewParams[0]) + double(previewParams[1]) / 100.0;
        const bool ready = nextBank && nextBank->usable() && std::abs(nextBank->root - actualRoot) < 0.0001
            && std::abs(nextBank->stretch-previewParams[9]/100.0) < 0.00001 && displayed[1];
        const pt::Sample* punch = nextPreparedPunch ? &nextPreparedPunch->forNote(root).sample : nullptr;
        // The prepared bank is the same immutable audio used by processBlock;
        // while rebuilding, draw the last published bank, matching audible playback.
        const pt::Sample* tail = nextBank ? &nextBank->notes[size_t(root)]
                              : displayed[1] ? &displayed[1]->sample : nullptr;
        pt::Settings settings;
        settings.trimMs = previewParams[2] * (nextBank ? nextBank->stretch : 1.0);
        settings.tailEndTrimMs = proc.tailEndTrimValue() * (nextBank ? nextBank->stretch : 1.0);
        settings.tailEndFadeMs = proc.tailEndFadeValue();
        settings.reverbTrimMs = previewParams[12];
        settings.reverbDelayMs = previewParams[14];
        settings.reverbGain = juce::Decibels::decibelsToGain(previewParams[15], -60.0f);
        settings.reverbLowCutSlope = int(previewParams[16])==1 ? 12 : int(previewParams[16])==2 ? 24 : int(previewParams[16])==3 ? 48 : 0;
        settings.punchEndOffsetMs = previewParams[3];
        settings.punchTimeScale=nextPreparedPunch?nextPreparedPunch->forNote(root).punchTimeScale:1.;
        settings.joinMs = previewParams[4];
        settings.crossfadeMs = previewParams[5];
        settings.punchGain = juce::Decibels::decibelsToGain(previewParams[6]);
        settings.tailGain = juce::Decibels::decibelsToGain(previewParams[7]);
        settings.output = juce::Decibels::decibelsToGain(previewParams[8]);
        settings.phaseShiftMs=previewParams[18];
        settings.invertTail=previewParams[19]>.5f;settings.invertPunch=previewParams[20]>.5f;
        proc.applyAttackLayer(settings);   // v0.4.62 attack layer, mirrored on the final waveform
        if(nextPhase && nextPhase->automatic==(previewParams[17]>.5f) &&
           nextPhase->matches(settings,nextPreparedPunch.get(),nextBank.get())) {
            const auto& solution=nextPhase->notes[size_t(root)];
            if(solution.valid) {
                if(previewParams[17]>.5f)settings.phaseShiftMs+=solution.offsetMs;
                settings.joinCorrelation=previewParams[17]>.5f?solution.after:solution.before;
                phaseScore.setText("CORRELATION  " + juce::String(solution.before,2) +
                   (previewParams[17]>.5f ? " > " + juce::String(solution.after,2) : "  [manual]") +
                   "\n" + (previewParams[17]>.5f ? "Auto " + juce::String(solution.offsetMs,2) + " ms" : "Auto OFF"),
                   juce::dontSendNotification);
            } else phaseScore.setText("PHASE: INSUFFICIENT OVERLAP\nTry a longer crossfade",juce::dontSendNotification);
        } else phaseScore.setText("PHASE: ANALYSING...",juce::dontSendNotification);
        PreviewRequest request;
        for(int i=0;i<3;++i)if(displayed[size_t(i)])request.samples[size_t(i)]=std::shared_ptr<const pt::Sample>(displayed[size_t(i)],&displayed[size_t(i)]->sample);
        if(nextPreparedPunch)request.samples[0]=std::shared_ptr<const pt::Sample>(nextPreparedPunch,&nextPreparedPunch->forNote(root).sample);
        if(nextBank)request.tail=std::shared_ptr<const pt::Sample>(nextBank,&nextBank->notes[size_t(root)]);
        // v0.4.53: the final waveform plays the root note with its crunch layer.
        request.settings.crunchMix=proc.crunchMixValue();
        if(nextCrunch && nextCrunch->dry==nextBank)request.crunchLayer=nextCrunch->dryLayers[size_t(root)];
        request.settings=settings;request.root=root;request.ready=bool(ready);request.rate=previewRate;
        request.layerMuted={proc.params.getRawParameterValue("mutePunch")->load()>.5f,proc.params.getRawParameterValue("muteTail")->load()>.5f,proc.params.getRawParameterValue("muteReverb")->load()>.5f};
        request.normalizeGains=normalizeGains;
        for(int i=0;i<2;++i)request.splitNormalizeGains[size_t(i)]=proc.splitNormalizeTarget(i);   // v0.5.21
        request.envelope=proc.masterEnvelopeSettings();request.punchRegion=proc.punchRegionSettings();request.attackBands=proc.eqSettings(5);request.decayBands=proc.eqSettings(6);
        request.attackEQ=proc.params.getRawParameterValue("punchAttackEqOn")->load()>.5f;request.decayEQ=proc.params.getRawParameterValue("punchDecayEqOn")->load()>.5f;
        request.transient=proc.transientSettings();request.generated=proc.tailReverbSettings();request.punchGenerated=proc.punchReverbSettings();request.generatedBands=proc.eqSettings(3);request.linearPhase=proc.tailWetUsesLinearPhase();request.subMono=proc.tailWetUsesSubMono();request.subMonoHz=proc.tailWetSubMonoHz();request.generatedEQ=proc.params.getRawParameterValue("tailVerbEqOn")->load()>.5f;request.punchGeneratedBands=proc.eqSettings(4);request.punchGeneratedEQ=proc.params.getRawParameterValue("punchVerbEqOn")->load()>.5f;
        if(request.generated.tailRootPitch&&request.generated.enabled&&request.generated.tailSend&&request.generated.mix>0.f&&
           !request.layerMuted[2]&&nextTailWet) {
            request.tail=nextTailWet;
            request.settings.preparedCompositeTail=true;
            request.crunchLayer=nextCrunch && nextCrunch->wet && nextCrunch->wet->notes[size_t(root)]==nextTailWet ? nextCrunch->wetLayers[size_t(root)] : nullptr;
            request.settings.compositeDryDuration=proc.previewTailCompositeDryDuration(root);
        }
        request.distortion={proc.distortionSettings("punchDist"),proc.distortionSettings("tailDist")};
        request.limiter=proc.outputLimiterOn();
        {const auto m=proc.bassMonoSettings();request.bassMono=m.enabled;request.bassMonoHz=m.hz;request.bassMonoMix=m.mix;request.bassMonoLinear=proc.bassMonoUsesLinearPhase();}   // v0.5 / v0.5.1
        const char* eqIds[]={"eqOn","punchEqOn","reverbEqOn"};
        for(int i=0;i<3;++i){request.bands[size_t(i)]=proc.eqSettings(i);request.eqOn[size_t(i)]=proc.params.getRawParameterValue(eqIds[i])->load()>.5f;}
        request.eqPost={{proc.eqAfterDistortion(0),proc.eqAfterDistortion(1)}};   // v0.5.20
        // v0.5.32: only the Punch chain changed and the shown render belongs to the
        // latest full render -> redo just the Punch (fast, so it follows a drag).
        const auto punchBase=completedKick.renderedBuses();
        const bool punchOnly=!changed && !bankChanged && !phaseChanged && !otherChanged && punchBase && punchBase->punchSource &&
                             fullPreviewGeneration!=0 && shownFullGeneration==fullPreviewGeneration;
        if(punchOnly){request.punchBase=punchBase;request.baseGeneration=fullPreviewGeneration;}
        previewGeneration=previewWorker.submit(std::move(request));completedKick.updating=true;completedKick.refreshExportState();completedKick.repaint();
        if(!punchOnly)fullPreviewGeneration=previewGeneration;
        lastEffectParams=std::move(effectParams);lastPunchParams=std::move(punchParams);lastPreviewRate=previewRate;lastNormalizeGains=normalizeGains;
        displayedPreparedPunch=nextPreparedPunch;
        displayedBank = std::move(nextBank);displayedCrunch=nextCrunch;
        displayedTailWet=nextTailWet;
        displayedPhase = std::move(nextPhase);
        lastPreviewParams = previewParams;
    }
    if(auto result=previewWorker.take())if(result->request.generation==previewGeneration){processedPeaks=result->processedPeaks;completedKick.setRendered(*result);
        shownFullGeneration=result->request.baseGeneration!=0?result->request.baseGeneration:result->request.generation;}
    if(!completedKick.outputDragActive())completedKick.showOutputGains(normalizeGains);   // v0.5.28: automation, NORMALIZE, mouse-up
    for(int i=0;i<2;++i) {
        normalizeButtons[size_t(i)].setEnabled(!completedKick.updating && processedPeaks[size_t(i)]>1.e-7f);
        normalizeReset[size_t(i)].setTooltip("Current volume change: "+juce::String(proc.params.getRawParameterValue(i==0?"punchNormalize":"tailNormalize")->load(),2)+" dB. Click to reset it to 0 dB.");
        splitNormalizeButtons[size_t(i)].setEnabled(!completedKick.updating && processedPeaks[size_t(2+i)]>1.e-7f);
    }
    splitNormalizeReset.setTooltip("Attack "+juce::String(proc.params.getRawParameterValue("punchAttackNormalize")->load(),2)+" dB, decay "+
        juce::String(proc.params.getRawParameterValue("punchDecayNormalize")->load(),2)+" dB. Click to reset both to 0 dB.");
    for(int i=0;i<3;++i)meterHold[size_t(i)]=(std::max)(meterHold[size_t(i)]*.75f,proc.busPeaks[size_t(i)].exchange(0));
    const auto activeText=[](bool enabled,float peak){return !enabled?"BYPASS":peak>.0001f?"ACTIVE":"ON";};
    effectActivity[0].setText(juce::String("Punch ")+activeText(proc.params.getRawParameterValue("punchDistOn")->load()>.5f,meterHold[0])+" / Tail+Rev "+activeText(proc.params.getRawParameterValue("tailDistOn")->load()>.5f,(std::max)(meterHold[1],meterHold[2])),juce::dontSendNotification);
    effectActivity[1].setText(activeText(proc.params.getRawParameterValue("transientOn")->load()>.5f,meterHold[0]),juce::dontSendNotification);
    effectActivity[2].setText(activeEQ>=5&&!proc.punchRegionSettings().enabled?"REGION OFF":activeText(proc.params.getRawParameterValue(eqOnID())->load()>.5f,meterHold[size_t(activeEQ==0?1:activeEQ==1||activeEQ>=5?0:2)]),juce::dontSendNotification);
    for(auto& label:effectActivity)label.setColour(juce::Label::textColourId,
        label.getText().contains("ACTIVE")?tt::ui::pal().ok:muted);
    const bool clipped=proc.clipLatched.load();
    // v0.4.60: hold the deepest limiter gain reduction briefly so it is readable.
    limiterHold=(std::min)(1.0f,(std::min)(limiterHold*1.06f,proc.limiterGain.exchange(1.0f)));
    const float reduction=-juce::Decibels::gainToDecibels(limiterHold,-60.0f);
    const bool limiting=reduction>=.05f;
    clipButton.setButtonText(clipped?"CLIP - click to clear":"OUTPUT "+juce::String(juce::Decibels::gainToDecibels(proc.peak.load(),-60.0f),1)+" dB"+
        (limiting?"  GR "+juce::String(reduction,1):juce::String()));
    clipButton.setColour(juce::TextButton::buttonColourId,clipped?tt::ui::pal().clip:limiting?tt::ui::pal().limit:panel);
    for(int i=0;i<3;++i)sourceNames[size_t(i)].setText(displayed[size_t(i)]?displayed[size_t(i)]->name:"No sample loaded",juce::dontSendNotification);
    statusLabel.setText(proc.status(), juce::dontSendNotification);
    prepBar.setSnapshot(proc.preparationProgress());   // v0.5.19
    {   // v0.5.5 decay note of the prepared punch
        const auto note=proc.punchDecayNote();
        decayNoteLabel.setText(decayNoteText(note,bool(displayed[0])),juce::dontSendNotification);
        decayNoteButton.setEnabled(note.valid);
    }
    if (auto a = displayed[1]) {
        const auto& pitch = a->pitch;
        pitchLabel.setText(pitch.valid
            ? "Detected: " + juce::MidiMessage::getMidiNoteName(int(std::round(pitch.midi)), true, false, 5)
               + "  |  MIDI " + juce::String(int(std::round(pitch.midi)))
               + "  |  " + juce::String(pitch.hz, 1) + " Hz  |  " + juce::String(int(pitch.confidence * 100)) + "% confidence"
            : "No stable note detected - pitching from manual TAIL ROOT "
               + juce::MidiMessage::getMidiNoteName(juce::roundToInt(proc.params.getRawParameterValue("root")->load()), true, true, 5)
               + " (turn TAIL ROOT to the tail's note)", juce::dontSendNotification);
        detectButton.setEnabled(pitch.valid);
    } else {
        pitchLabel.setText("Tail note detection / manual correction", juce::dontSendNotification);
        detectButton.setEnabled(false);
    }
    // The response graph and the selected controls follow host automation.
    const int view=selectedEQView();
    if(view!=activeEQ)setEQView(view);
    const int savedBand=juce::jlimit(0,7,int(proc.params.state.getProperty("uiBand"+juce::String(activeEQ),selectedBand)));
    if(savedBand!=selectedBand)selectBand(savedBand);
    syncEQControls();
    eqGraph.repaint();
    repaint(juce::roundToInt(1104*uiScale),juce::roundToInt(27*uiScale),
            juce::roundToInt(16*uiScale),juce::roundToInt(16*uiScale));
}

void PunchTailEditor::showGeneratedSettings(bool punch) {
    punchGeneratedPage=punch;
    generatedTailSettings.setToggleState(!punch,juce::dontSendNotification);
    generatedPunchSettings.setToggleState(punch,juce::dontSendNotification);
    generatedEQButton.setButtonText(punch?"EDIT PUNCH WET EQ":"EDIT TAIL WET EQ");
    updateGeneratedVisibility();
    resizeLayout();
}
// v0.5.13: which Experimental Reverb controls are shown. Minimized = only the
// fold button and ENABLE; otherwise the TAIL / PUNCH page as before.
void PunchTailEditor::updateGeneratedVisibility() {
    const bool open=!generatedMinimized,punch=punchGeneratedPage;
    generatedFold.setButtonText(generatedMinimized?"+ EXPERIMENTAL REVERB":"- EXPERIMENTAL REVERB");
    generatedTitle.setVisible(false);generatedFold.setVisible(true);generatedEnable.setVisible(true);
    for(juce::Component* c:std::initializer_list<juce::Component*>{&generatedKeepDry,&generatedDistortWet,&generatedPunchSend,&generatedTailSend,&generatedEQButton,
            &generatedTailSettings,&generatedPunchSettings,&generatedEngineLabel,&generatedEngine})c->setVisible(open);
    generatedRootPitch.setVisible(open&&!punch);wetAutoMatch.setVisible(open&&!punch);wetPhaseLock.setVisible(open&&!punch);
    for(int i=0;i<8;++i){generatedControls[size_t(i)].label.setVisible(open&&!punch);generatedControls[size_t(i)].slider.setVisible(open&&!punch);punchGeneratedControls[size_t(i)].label.setVisible(open&&punch);punchGeneratedControls[size_t(i)].slider.setVisible(open&&punch);}
    for(int i=8;i<10;++i){generatedControls[size_t(i)].label.setVisible(open&&punch);generatedControls[size_t(i)].slider.setVisible(open&&punch);}
    for(int i=10;i<12;++i){generatedControls[size_t(i)].label.setVisible(open&&!punch);generatedControls[size_t(i)].slider.setVisible(open&&!punch);}
    for(auto& c:verbDuckControls){c.label.setVisible(open);c.slider.setVisible(open);}
}

int PunchTailEditor::selectedEQView() const {
    const int region=juce::jlimit(0,2,juce::roundToInt(proc.params.getRawParameterValue("punchRegionView")->load()));
    if(region)return region+4;
    return proc.params.getRawParameterValue("eqGeneratedView")->load()>.5f
        ?(proc.params.getRawParameterValue("eqPunchGeneratedView")->load()>.5f?4:3)
        :juce::jlimit(0,2,juce::roundToInt(proc.params.getRawParameterValue("eqView")->load()));
}
juce::String PunchTailEditor::eqPrefix() const {
    return activeEQ==1 ? "punchEq" : activeEQ==2 ? "reverbEq" : activeEQ==3 ? "tailVerbEq" : activeEQ==4 ? "punchVerbEq" : activeEQ==5 ? "punchAttackEq" : activeEQ==6 ? "punchDecayEq" : "eq";
}
juce::String PunchTailEditor::eqOnID() const {
    return activeEQ==1 ? "punchEqOn" : activeEQ==2 ? "reverbEqOn" : activeEQ==3 ? "tailVerbEqOn" : activeEQ==4 ? "punchVerbEqOn" : activeEQ==5 ? "punchAttackEqOn" : activeEQ==6 ? "punchDecayEqOn" : "eqOn";
}
void PunchTailEditor::setEQView(int view) {
    view=juce::jlimit(0,6,view);
    selectedBandPerEQ[size_t(activeEQ)]=selectedBand;
    activeEQ=view;
    linearPhase.setVisible(activeEQ==3);monoSub.setVisible(activeEQ==3&&!collapsed[2]);monoSubHz.setVisible(activeEQ==3&&!collapsed[2]);effectActivity[2].setVisible(activeEQ!=3);
    selectedBand=selectedBandPerEQ[size_t(view)];
    eqSectionBox.setSelectedId(view+1,juce::dontSendNotification);
    eqGraph.setView(view);
    eqGraph.setSelected(selectedBand);
    eqEnableAttachment.reset();
    eqEnableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.params,eqOnID(),eqEnable);
    eqPostAttachment.reset();
    if(const char* postId=eqPostID(activeEQ))eqPostAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params,postId,eqPostButton);
    syncEQControls();resizeLayout();
}
void PunchTailEditor::setBandParameter(int index,const char* suffix,double value) {
    if(index<0||index>=tt::kBands)return;
    const auto id=eqPrefix()+juce::String(index)+suffix;
    setParameter(id.toRawUTF8(),value);
    eqGraph.repaint();
}
void PunchTailEditor::selectBand(int index) {
    selectedBand=juce::jlimit(0,tt::kBands-1,index);
    selectedBandPerEQ[size_t(activeEQ)]=selectedBand;
    proc.params.state.setProperty("uiBand"+juce::String(activeEQ),selectedBand,nullptr);
    eqGraph.setSelected(selectedBand);
    syncEQControls();
}
void PunchTailEditor::addEQBand(double freq,double gain,int type) {
    // Default nodes 0..2 stay present; up to five optional extra points.
    // v0.5.9: type comes from where the plot was double-clicked (edge = shelf).
    if(type!=tt::LowShelf&&type!=tt::HighShelf)type=tt::Bell;
    for(int i=3;i<tt::kBands;++i) {
        if(proc.params.getRawParameterValue(eqPrefix()+juce::String(i)+"On")->load()<0.5f) {
            setBandParameter(i,"Type",type);
            setBandParameter(i,"Freq",juce::jlimit(20.0,20000.0,freq));
            setBandParameter(i,"Gain",juce::jlimit(-18.0,18.0,gain));
            setBandParameter(i,"Q",0.71);
            setBandParameter(i,"Slope",0);
            setBandParameter(i,"On",1);
            selectBand(i);return;
        }
    }
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "Kicktone EQ","All five optional bands are already active. Disable or remove one to reuse it.");
}
void PunchTailEditor::syncEQControls() {
    updatingEQControls=true;
    const auto prefix=eqPrefix()+juce::String(selectedBand);
    const auto get=[&](const char* suffix){return proc.params.getRawParameterValue(prefix+suffix)->load();};
    const int type=juce::jlimit(0,4,juce::roundToInt(get("Type")));
    const bool cut=type==tt::LowCut || type==tt::HighCut;
    eqBandLabel.setText("BAND "+juce::String(selectedBand+1),juce::dontSendNotification);
    bandType.setSelectedId(type+1,juce::dontSendNotification);
    bandSlope.setSelectedId(juce::jlimit(1,3,juce::roundToInt(get("Slope"))+1),juce::dontSendNotification);
    bandEnable.setToggleState(get("On")>0.5f,juce::dontSendNotification);
    bandFreq.setValue(get("Freq"),juce::dontSendNotification);
    bandGain.setValue(get("Gain"),juce::dontSendNotification);
    for(auto entry:std::initializer_list<std::pair<TailtoneSlider*,const char*>>{{&bandFreq,"Freq"},{&bandGain,"Gain"},{&bandQ,"Q"}}) {
        auto* parameter=proc.params.getParameter(eqPrefix()+juce::String(selectedBand)+entry.second);
        entry.first->setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    bandQ.setValue(get("Q"),juce::dontSendNotification);
    bandGain.setEnabled(!cut);
    bandSlope.setEnabled(cut);
    bandQ.setTooltip(cut?"How much the filter rings at its corner (default 0.71). The steepness is chosen separately.":"How wide the band is: higher = narrower. On a shelf it sets how gradual the step is.");
    removeBand.setEnabled(selectedBand>=3 && get("On")>0.5f);
    addBand.setEnabled(true);
    // Above add button is kept enabled; reaching eight bands shows a message.
    showUiScale();
    updatingEQControls=false;
}
void PunchTailEditor::setUIScale(double scale) {
    scale=juce::jlimit(0.5,2.0,scale);
    resizingFromPreset=true;
    setSize(juce::roundToInt(baseWidth*scale),juce::roundToInt(baseHeight*scale));
    resizingFromPreset=false;
    updateScale(true);
}
void PunchTailEditor::updateScale(bool writeParameter) {
    uiScale=juce::jlimit(0.5f,2.0f,float(getWidth())/baseWidth);
    if(writeParameter)setParameter("uiScale",std::round(uiScale*100.0f));
    showUiScale();
}
void PunchTailEditor::showUiScale() {
    // A free corner resize or a screen fit may fall between presets; don't mislabel the menu.
    const int pct=juce::roundToInt(uiScale*100.0f);
    const bool preset=pct==50||pct==75||pct==100||pct==125||pct==150||pct==200;
    if(preset)uiScaleBox.setSelectedId(pct,juce::dontSendNotification);
    else uiScaleBox.setText(juce::String(PunchTailProcessor::displayedUiScale(float(pct)))+"%",juce::dontSendNotification);
}
double PunchTailEditor::screenFitScale(double wanted,int height,juce::Rectangle<int> area) {
    if(area.isEmpty()||height<=0)return wanted;
    const double room=(std::min)(double(area.getWidth()-screenMarginX)/baseWidth,double(area.getHeight()-screenMarginY)/height);
    return (std::max)(0.5,(std::min)(wanted,room));
}
double PunchTailEditor::fitToScreen(double wanted) const {
    // Tests construct processors directly (wrapperType_Undefined) and keep the exact stored size.
    if(proc.wrapperType==juce::AudioProcessor::wrapperType_Undefined)return wanted;
    const auto& displays=juce::Desktop::getInstance().getDisplays();
    const auto* display=displays.getDisplayForPoint(juce::Desktop::getMousePosition());   // where the host was clicked
    if(display==nullptr)display=displays.getPrimaryDisplay();
    return display==nullptr?wanted:screenFitScale(wanted,baseHeight,display->userArea);
}
void PunchTailEditor::place(juce::Component& c,int x,int y,int w,int h) {
    // Children use unscaled logical coordinates. The canvas receives the only
    // scaling transform, avoiding compounded offsets and clipped widgets.
    c.setBounds(x,y,w,h);
}

void PunchTailEditor::showDistortionPage(int page) {
    distortionPage=juce::jlimit(0,1,page);
    for(int i=0;i<2;++i) {
        auto& fx=distortion[size_t(i)];
        const bool visible=i==distortionPage && !collapsed[0];
        distortionTabs[size_t(i)].setToggleState(visible,juce::dontSendNotification);
        fx.enabled.setVisible(visible);
        fx.algorithm.setVisible(visible);
        fx.title.setVisible(false); // Title is on its selectable tab.
        for(auto& knob:fx.knobs) {knob.label.setVisible(visible);knob.slider.setVisible(visible);}
    }
}
void PunchTailEditor::resized() {
    updateScale(!resizingFromPreset);
    content.setBounds(0,0,baseWidth,baseHeight);
    content.setTransform(juce::AffineTransform::scale(uiScale));
    applyLayout();
}

void PunchTailEditor::paint(juce::Graphics& g) {
    g.fillAll(bg);g.addTransform(juce::AffineTransform::scale(uiScale));
    g.setColour(titleColour);g.setFont(juce::Font(27.0f,juce::Font::bold));
    g.drawImageWithin(jeiLogo,28,10,45,43,juce::RectanglePlacement::centred,true); // white mark tinted by the theme
    g.drawText(proc.getName().toUpperCase()+" v"+PunchTailProcessor::versionString(),85,12,540,39,juce::Justification::centredLeft);
    g.setColour(panel);
    for(const auto& rect:sourcePanels)g.fillRoundedRectangle(rect.toFloat(),9);
    for(const auto& rect:effectPanels)g.fillRoundedRectangle(rect.toFloat(),9);
    g.fillRoundedRectangle(generatedPanel.toFloat(),9);
    g.fillRoundedRectangle(punchRegionPanel.toFloat(),9);
    g.fillRoundedRectangle(masterEnvelopePanel.toFloat(),9);
    g.fillRoundedRectangle(pitchDropPanel.toFloat(),9);
    g.fillRoundedRectangle(bassMonoPanel.toFloat(),9);   // v0.5
    g.setColour(thinLine);g.drawHorizontalLine(pitchDropPanel.getY()+146,float(pitchDropPanel.getX()+13),float(pitchDropPanel.getRight()-13)); // v0.4.62 attack row
    g.setColour(panel);
    if(punchLayerShown())g.fillRoundedRectangle(punch2Panel.toFloat(),9);
    if(punchLayerShown()&&punch2DropHover){g.setColour(titleColour);g.drawRoundedRectangle(punch2Panel.toFloat().reduced(1.0f),9,2.0f);} // v0.4.69 drop target
    g.setColour(thinLine);g.drawHorizontalLine(layerHeading.getY()-5,28,1111);
    // v0.5.8 (user request): an arrow from PUNCH FOLLOWS TAIL PITCH to ONLY PUNCH 2 FOLLOWS,
    // showing that the second switch refines the first. Dim while it has no effect.
    if(punchFollowTail.isVisible()&&punchFollowDecayOnly.isVisible()) {
        const float y=float(punchFollowTail.getBounds().getCentreY());
        const float x0=float(punchFollowTail.getRight()+4),x1=float(punchFollowDecayOnly.getX()-4);
        g.setColour(punchFollowDecayOnly.isEnabled()?titleColour:muted.withAlpha(.6f));
        juce::Path arrow;arrow.startNewSubPath(x0,y);arrow.lineTo(x1-7,y);
        g.strokePath(arrow,juce::PathStrokeType(2.0f));
        juce::Path head;head.addTriangle(x1-9,y-6,x1-9,y+6,x1,y);g.fillPath(head);
    }
}

void PunchTailEditor::saveLayout() {
    auto tree=proc.params.state;
    tree.setProperty("uiLayer",selectedLayer,nullptr);tree.setProperty("uiWaveFocus",waveFocus,nullptr);
    tree.setProperty("uiReverbMinimized",reverbMinimized,nullptr);
    tree.setProperty("uiGeneratedMinimized",generatedMinimized,nullptr);   // v0.5.13
    int mask=0;for(int i=0;i<3;++i)if(collapsed[size_t(i)])mask|=1<<i;
    tree.setProperty("uiCollapsed",mask,nullptr);tree.setProperty("uiDistPage",distortionPage,nullptr);
    for(int i=0;i<7;++i)tree.setProperty("uiBand"+juce::String(i),selectedBandPerEQ[size_t(i)],nullptr);
}
PunchTailEditor::ViewState PunchTailEditor::currentView() const {
    ViewState v;v.reverbPage=punchGeneratedPage?1:0;v.punchZoom=punchWave.zoomIndex();v.tailZoom=tailWave.zoomIndex();
    v.kickZoom=completedKick.zoomIndex();v.joinView=completedKick.joinViewShown()?1:0;v.joinSpan=completedKick.joinSpanIndex();
    v.bitDepth=completedKick.bitDepthIndex();return v;
}
PunchTailEditor::ViewState PunchTailEditor::storedView() const {
    const auto& t=proc.params.state;ViewState v;
    v.reverbPage=juce::jlimit(0,1,int(t.getProperty("uiReverbPage",0)));
    v.punchZoom=juce::jlimit(0,PunchTailWaveform::kZoomSteps-1,int(t.getProperty("uiPunchZoom",0)));
    v.tailZoom=juce::jlimit(0,PunchTailWaveform::kZoomSteps-1,int(t.getProperty("uiTailZoom",0)));
    v.kickZoom=juce::jlimit(0,3,int(t.getProperty("uiKickZoom",0)));
    v.joinView=juce::jlimit(0,1,int(t.getProperty("uiJoinView",0)));
    v.joinSpan=juce::jlimit(0,3,int(t.getProperty("uiJoinSpan",1)));
    v.bitDepth=juce::jlimit(0,2,int(t.getProperty("uiExportBits",1)));
    return v;
}
void PunchTailEditor::storeView(const ViewState& v) {
    auto t=proc.params.state;
    t.setProperty("uiReverbPage",v.reverbPage,nullptr);t.setProperty("uiPunchZoom",v.punchZoom,nullptr);t.setProperty("uiTailZoom",v.tailZoom,nullptr);
    t.setProperty("uiKickZoom",v.kickZoom,nullptr);t.setProperty("uiJoinView",v.joinView,nullptr);t.setProperty("uiJoinSpan",v.joinSpan,nullptr);
    t.setProperty("uiExportBits",v.bitDepth,nullptr);
}
void PunchTailEditor::applyView(const ViewState& v) {
    if((punchGeneratedPage?1:0)!=v.reverbPage)showGeneratedSettings(v.reverbPage==1);
    if(punchWave.zoomIndex()!=v.punchZoom)punchWave.setZoomIndex(v.punchZoom);
    if(tailWave.zoomIndex()!=v.tailZoom)tailWave.setZoomIndex(v.tailZoom);
    if(completedKick.zoomIndex()!=v.kickZoom)completedKick.setZoomIndex(v.kickZoom);
    if(completedKick.joinSpanIndex()!=v.joinSpan)completedKick.setJoinSpanIndex(v.joinSpan);
    completedKick.setJoinView(v.joinView==1);
    completedKick.setBitDepthIndex(v.bitDepth);
}
void PunchTailEditor::syncViewState() {
    // A project load (or another editor) changed the stored view: show it.
    // Otherwise a change made here is saved with the project.
    const auto stored=storedView();
    if(stored!=lastView){applyView(stored);lastView=currentView();if(lastView!=stored)storeView(lastView);return;}
    const auto now=currentView();
    if(now!=lastView){storeView(now);lastView=now;}
}
void PunchTailEditor::restoreLayout() {
    auto tree=proc.params.state;
    selectedLayer=juce::jlimit(0,2,int(tree.getProperty("uiLayer",0)));
    waveFocus=juce::jlimit(-1,2,int(tree.getProperty("uiWaveFocus",-1)));
    reverbMinimized=bool(tree.getProperty("uiReverbMinimized",true));
    if(reverbMinimized && waveFocus==2)waveFocus=-1;
    generatedMinimized=bool(tree.getProperty("uiGeneratedMinimized",false));   // v0.5.13
    updateGeneratedVisibility();
    const int mask=int(tree.getProperty("uiCollapsed",0));
    for(int i=0;i<3;++i){collapsed[size_t(i)]=(mask&(1<<i))!=0;selectedBandPerEQ[size_t(i)]=juce::jlimit(0,7,int(tree.getProperty("uiBand"+juce::String(i),1)));}
    for(int i=3;i<7;++i)selectedBandPerEQ[size_t(i)]=juce::jlimit(0,7,int(tree.getProperty("uiBand"+juce::String(i),1)));
    distortionPage=juce::jlimit(0,1,int(tree.getProperty("uiDistPage",0)));
    selectedBand=selectedBandPerEQ[size_t(activeEQ)];eqGraph.setSelected(selectedBand);
    baseHeight=(std::max)(807+generatedHeight()+(selectedLayer==1?139:34)+(selectedLayer==0?34:0)+(waveFocus>=0?60:0)+(punchLayerShown()?40:0),   // v0.5.10: +34 harmony row
        fullHeight-(collapsed[0]?114:0)-(collapsed[1]?124:0)-(collapsed[2]?304:0));
}
void PunchTailEditor::resizeLayout() {
    baseHeight=(std::max)(807+generatedHeight()+(selectedLayer==1?139:34)+(selectedLayer==0?34:0)+(waveFocus>=0?60:0)+(punchLayerShown()?40:0),   // v0.5.10: +34 harmony row
        fullHeight-(collapsed[0]?114:0)-(collapsed[1]?124:0)-(collapsed[2]?304:0));
    const double scale=fitToScreen(uiScale);   // v0.5.24: a taller page may not grow past the screen
    resizingFromPreset=true;
    setResizeLimits(baseWidth/2,baseHeight/2,baseWidth*2,baseHeight*2);
    if(auto* c=getConstrainer())c->setFixedAspectRatio(double(baseWidth)/baseHeight);
    setSize(juce::roundToInt(baseWidth*scale),juce::roundToInt(baseHeight*scale));
    resized();resizingFromPreset=false;repaint();
}
void PunchTailEditor::selectLayer(int layer) {
    selectedLayer=juce::jlimit(0,2,layer);if(selectedLayer==2)reverbMinimized=false;saveLayout();resizeLayout();
}
juce::String PunchTailEditor::decayNoteText(const pt::decaynote::Result& n,bool punchLoaded) {
    if(!punchLoaded)return "DECAY NOTE: load a punch";
    if(!n.valid)return "DECAY NOTE: no stable pitch in the decay (after ATTACK END)";
    const auto name=[](double midi){const int m=juce::roundToInt(midi);const int cents=juce::roundToInt((midi-m)*100);
        return juce::MidiMessage::getMidiNoteName(m,true,true,5)+(cents?juce::String(cents>0?" +":" ")+juce::String(cents)+"c":juce::String());};
    return "DECAY NOTE: "+name(n.midi())+"  ("+juce::String(n.midHz,1)+" Hz)   sweep "+name(n.startMidi())+"  ->  "+name(n.endMidi());
}
bool PunchTailEditor::punchLayerShown() const { return proc.params.getRawParameterValue("punchLayer")->load()>.5f; }
void PunchTailEditor::rebuildEngineItems() {
    // v0.4.61: Legacy v1, High Fidelity and Extreme were removed. Item IDs are kept.
    qualityMode.clear(juce::dontSendNotification);
    qualityMode.addItem("Adaptive Stretch v4",4);
    qualityMode.addItem("Highest Fidelity v4",5);
    qualityMode.addItem("Extreme v5",6);   // v0.4.68
}
// v0.4.62: themes. Look-and-feel colours come from the palette; explicit
// component colours (labels, scrollbars) are remapped from the previously
// applied palette to the new one, so every control follows the switch live.
static std::vector<std::pair<juce::Colour,juce::Colour>> themeColourMap(const tt::ui::Palette& a,const tt::ui::Palette& b) {
    // Order matters when one palette reuses a value for two roles: text roles first.
    return {{a.text,b.text},{a.muted,b.muted},{a.amber,b.amber},{a.violet,b.violet},{a.title,b.title},{a.bg,b.bg},
            {a.panel,b.panel},{a.thinLine,b.thinLine},{a.scrollThumb,b.scrollThumb},{a.ok,b.ok},{a.clip,b.clip},{a.limit,b.limit},
            {a.button,b.button},{a.buttonOn,b.buttonOn},{a.combo,b.combo},{a.knobTrack,b.knobTrack},{a.highlight,b.highlight}};
}
static void remapComponentColours(juce::Component& c,const std::vector<std::pair<juce::Colour,juce::Colour>>& map) {
    auto& props=c.getProperties();
    for(int i=0;i<props.size();++i) {
        const auto name=props.getName(i);
        if(!name.toString().startsWith("jcclr_"))continue;
        const juce::Colour old((juce::uint32)static_cast<int>(props.getValueAt(i)));
        for(const auto& [from,to]:map)
            if((old.getARGB()&0xffffffu)==(from.getARGB()&0xffffffu)){props.set(name,(int)to.withAlpha(old.getAlpha()).getARGB());break;}
    }
    for(auto* child:c.getChildren())remapComponentColours(*child,map);
}
void PunchTailEditor::applyTheme() {
    const int index=tt::ui::themeIndex();
    const auto& previous=tt::ui::palettes()[size_t(appliedTheme<0?index:appliedTheme)];
    refreshThemeColours();
    const auto& p=tt::ui::pal();
    if(index==0) theme.setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());   // Charcoal: unchanged
    else theme.setColourScheme({p.bg,p.combo,p.panel,p.thinLine,p.text,p.button,p.title,p.buttonOn,p.text});
    theme.setColour(juce::Slider::rotarySliderFillColourId, whiteAccent);
    theme.setColour(juce::Slider::rotarySliderOutlineColourId, p.knobTrack);
    theme.setColour(juce::Slider::thumbColourId, whiteAccent);
    theme.setColour(juce::Slider::textBoxBackgroundColourId, panel);
    theme.setColour(juce::Slider::textBoxHighlightColourId, p.highlight);
    theme.setColour(juce::Slider::textBoxTextColourId, p.title);
    theme.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    theme.setColour(juce::TextButton::buttonColourId, p.button);
    theme.setColour(juce::TextButton::buttonOnColourId, p.buttonOn);
    theme.setColour(juce::ComboBox::backgroundColourId, p.combo);
    theme.setColour(juce::ComboBox::textColourId, whiteAccent);
    theme.setColour(juce::ComboBox::outlineColourId, thinLine);
    theme.setColour(juce::ToggleButton::tickColourId, whiteAccent);
    theme.setColour(juce::ToggleButton::textColourId, whiteAccent);
    theme.setColour(juce::TextButton::textColourOffId, p.title);
    theme.setColour(juce::Label::textColourId, muted);
    if(index!=0) {   // Charcoal keeps JUCE's dark defaults for menus and tooltips
        theme.setColour(juce::PopupMenu::backgroundColourId,p.panel);theme.setColour(juce::PopupMenu::textColourId,p.text);
        theme.setColour(juce::PopupMenu::highlightedBackgroundColourId,p.buttonOn);theme.setColour(juce::PopupMenu::highlightedTextColourId,p.title);
        theme.setColour(juce::TooltipWindow::backgroundColourId,p.panel);theme.setColour(juce::TooltipWindow::textColourId,p.text);
        theme.setColour(juce::TooltipWindow::outlineColourId,p.thinLine);
    }
    if(appliedTheme>=0 && appliedTheme!=index)remapComponentColours(*this,themeColourMap(previous,p));
    appliedTheme=index;seenThemeEpoch=tt::ui::themeEpoch().load();
    aboutPage.setThemeIndex(index);
    sendLookAndFeelChange();repaint();
}
// ---------------------------------------------------------------------------
// v0.4.62 Export WAV and drag-and-drop of the rendered kick.
juce::File PunchTailEditor::lastExportFolder() const {
    if(tt::ui::themeStorageDisabled())return juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::PropertiesFile file(tt::ui::settingsOptions());
    const juce::File saved(file.getValue("exportFolder"));
    return saved.isDirectory()?saved:juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}
void PunchTailEditor::exportKick() {
    const auto audio=completedKick.renderedAudio();
    if(!audio){completedKick.setExportMessage("Load a punch or tail first, or wait for the waveform");return;}
    const int bits=completedKick.exportBits();
    const auto name=tt::exporter::defaultFileName(completedKick.renderedNote());
    chooser=std::make_unique<juce::FileChooser>("Export kick as WAV",lastExportFolder().getChildFile(name),"*.wav");
    juce::Component::SafePointer<PunchTailEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,
        [safe,audio,bits](const juce::FileChooser& fc){
            if(!safe)return;
            auto file=fc.getResult();if(file==juce::File())return;
            if(!file.hasFileExtension("wav"))file=file.withFileExtension("wav");
            const bool ok=tt::exporter::writeWav(*audio,file,bits,safe->proc.getName()+" v"+PunchTailProcessor::versionString());
            if(ok && !tt::ui::themeStorageDisabled()){juce::PropertiesFile props(tt::ui::settingsOptions());props.setValue("exportFolder",file.getParentDirectory().getFullPathName());props.saveIfNeeded();}
            safe->completedKick.setExportMessage(ok?"Saved "+file.getFileName():"Could not write "+file.getFileName());
        });
}
juce::File PunchTailEditor::writeDragFile() {
    const auto audio=completedKick.renderedAudio();
    if(!audio)return {};
    // One folder per drag: hosts may still read the previous file while we write.
    const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Kicktone Drag")
                          .getChildFile(juce::String(juce::Time::currentTimeMillis()));
    const auto file=folder.getChildFile(tt::exporter::defaultFileName(completedKick.renderedNote()));
    return tt::exporter::writeWav(*audio,file,completedKick.exportBits(),proc.getName()+" v"+PunchTailProcessor::versionString())?file:juce::File();
}
void PunchTailEditor::dragKickOut() {
    const auto file=writeDragFile();
    if(file==juce::File()){completedKick.setExportMessage("Load a punch or tail first, or wait for the waveform");return;}
    // Keep only the most recent drag files (older ones may already be imported by the host).
    auto root=file.getParentDirectory().getParentDirectory();
    auto folders=root.findChildFiles(juce::File::findDirectories,false);
    std::sort(folders.begin(),folders.end(),[](const juce::File& a,const juce::File& b){return a.getFileName()>b.getFileName();});
    for(int i=8;i<folders.size();++i)folders.getReference(i).deleteRecursively();
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,&completedKick);
}
void PunchTailEditor::showAbout(bool show) {
    if(show)refreshAboutInfo();
    aboutPage.setVisible(show);
    if(show)aboutPage.toFront(false);
    aboutButton.setToggleState(show,juce::dontSendNotification);
}
void PunchTailEditor::refreshAboutInfo() {
    const double rate=proc.getSampleRate();const int latency=proc.getLatencySamples();
    const bool adaptive=proc.params.getRawParameterValue("pitchAdaptive")->load()>.5f;
   #if defined(_MSC_VER) && !defined(__clang__)
    const juce::String compiler="MSVC "+juce::String(_MSC_VER);
   #elif defined(__clang__)
    const juce::String compiler=juce::String("Clang ")+__clang_version__;
   #elif defined(__GNUC__)
    const juce::String compiler=juce::String("GCC ")+__VERSION__;
   #else
    const juce::String compiler="unknown";
   #endif
    juce::StringArray rows;
    rows.add("Product|"+proc.getName());
    rows.add("Version|"+juce::String(JucePlugin_VersionString)+" (beta)");
    rows.add("Plugin format|"+juce::String(juce::AudioProcessor::getWrapperTypeDescription(proc.wrapperType)));
    rows.add("Pitch engine|"+juce::String(PunchTailProcessor::engineName(proc.tailEngine())));
    rows.add("Sample rate|"+(rate>0?juce::String(rate,0)+" Hz":juce::String("not playing yet")));
    rows.add("Latency|"+juce::String(latency)+" samples"+(rate>0?" ("+juce::String(1000.0*latency/rate,1)+" ms)":juce::String()));
    rows.add("Built|"+juce::String(__DATE__)+" with "+compiler.upToFirstOccurrenceOf("(",false,false).trim());
    rows.add("System|"+juce::SystemStats::getOperatingSystemName());
    aboutPage.setBuildInfo(rows);
}
void PunchTailEditor::applyLayout() {
    place(aboutButton,baseWidth-106,20,78,29);
    place(aboutPage,0,0,baseWidth,baseHeight);
    // v0.4.61: ABOUT sits at the right end of the header, clear of the title.
    place(uiScaleLabel,baseWidth-264,22,56,25);place(uiScaleBox,baseWidth-206,20,92,29);
    for(int i=0;i<3;++i)place(layerMute[size_t(i)],1070+i*135,20,132,29);
    // v0.5.25: back at their pre-v0.5.23 places (the STRETCHED switch is gone).
    place(punchLayer,560,20,140,29);place(glide,712,20,86,29);place(glideTime,800,22,250,25);
    const int extra=waveFocus>=0?60:0;
    int x=28;
    juce::Component* waves[]={&punchWave,&tailWave,&reverbWave};
    juce::Component* loads[]={&punchButton,&tailButton,&reverbButton};
    for(int i=0;i<3;++i) {
        const int width=reverbMinimized?(i==2?105:waveFocus<0?471:(i==waveFocus?599:343)):
                        waveFocus<0?349:(i==waveFocus?599:224);
        sourcePanels[size_t(i)]={x,69,width,i==2&&reverbMinimized?76:220+extra};
        const bool visible=i!=2 || !reverbMinimized;
        waves[i]->setVisible(visible);loads[i]->setVisible(visible);
        unloadButtons[size_t(i)].setVisible(visible);sourceNames[size_t(i)].setVisible(visible);
        expandWave[size_t(i)].setVisible(visible);
        place(layerTabs[size_t(i)],x+8,77,i==2?(reverbMinimized?width-16:width-120):width-89,25);
        layerTabs[size_t(i)].setToggleState(selectedLayer==i,juce::dontSendNotification);
        expandWave[size_t(i)].setButtonText(waveFocus==i?"Restore":"Expand");
        place(expandWave[size_t(i)],x+width-77,77,69,25);
        place(*loads[i],x+8,109,width-102,25);place(unloadButtons[size_t(i)],x+width-86,109,78,25);
        place(*waves[i],x+8,141,width-16,i==waveFocus?180:120);
        place(sourceNames[size_t(i)],x+8,265+extra,width-(i<2?116:16),20);
        if(i<2)place(trimButtons[size_t(i)],x+width-105,263+extra,97,24);
        if(i==2) {
            reverbFold.setButtonText(reverbMinimized?"Show":"-");
            place(reverbFold,reverbMinimized?x+8:x+width-109,reverbMinimized?109:77,reverbMinimized?width-16:26,25);
        }
        x+=width+18;
    }
    // Punch 2 row appears only while PUNCH LAYER is ticked.
    const bool layerShown=punchLayerShown();
    const int layerRow=layerShown?40:0;
    punch2Panel={28,297+extra,1083,32};
    for(juce::Component* c:std::initializer_list<juce::Component*>{&punch2Title,&punch2Name,&punch2Load,&punch2Unload,&punch2Offset,&punch2OffsetLabel,&punch2Pitch,&punch2PitchLabel})c->setVisible(layerShown);
    place(punch2Title,40,300+extra,90,26);place(punch2Name,132,300+extra,200,26);
    place(punch2OffsetLabel,338,300+extra,52,26);place(punch2Offset,392,300+extra,210,26);
    place(punch2PitchLabel,610,300+extra,42,26);place(punch2Pitch,654,300+extra,210,26);   // v0.4.67
    place(punch2Load,872,300+extra,118,26);place(punch2Unload,996,300+extra,107,26);
    const int detection=297+extra+layerRow;
    // v0.5.13: AUTO-SPLIT KICK removed (user request); the pitch label takes its room.
    place(punchDetectButton,28,detection,173,27);place(pitchLabel,211,detection,562,27);
    place(lockSplitButton,783,detection,139,27);   // v0.4.66
    place(detectButton,932,detection,179,27);
    const int heading=detection+38,controlsY=heading+24;
    const char* names[]={"PUNCH SETTINGS","TAIL SETTINGS","REVERB SETTINGS"};
    layerHeading.setText(names[selectedLayer],juce::dontSendNotification);place(layerHeading,28,heading,300,22);
    place(editHelp,530,heading,581,22);
    for(auto& c:controls){c.slider.setVisible(false);c.label.setVisible(false);}
    const std::vector<int> indices=selectedLayer==0?std::vector<int>{3,6,13}:selectedLayer==1?std::vector<int>{0,1,2,7,9}:std::vector<int>{10,11,12};
    int column=0;
    // v0.4.57: the Tail page carries two more knobs (TAIL END, END FADE), so it
    // uses a 101 px pitch; seven knobs still end before the join controls (741).
    const int pitch=selectedLayer==1?101:118,knobWidth=selectedLayer==1?97:112;
    for(int i:indices){auto& c=controls[size_t(i)];c.label.setVisible(true);c.slider.setVisible(true);place(c.label,28+column*pitch,controlsY,knobWidth,18);place(c.slider,28+column*pitch,controlsY+18,knobWidth,84);++column;}
    for(auto& c:tailEndControls){c.label.setVisible(selectedLayer==1);c.slider.setVisible(selectedLayer==1);
        if(selectedLayer==1){place(c.label,28+column*pitch,controlsY,knobWidth,18);place(c.slider,28+column*pitch,controlsY+18,knobWidth,84);++column;}}
    // v0.4.60: JOIN SHIFT / CROSSFADE knobs removed (user request) - they duplicated
    // TAIL JOIN SHIFT / CROSSFADE under the final waveform. controls[4]/[5] stay
    // attached but are never shown. OUTPUT keeps its former column.
    {auto& c=controls[8];c.label.setVisible(true);c.slider.setVisible(true);place(c.label,741+2*123,controlsY,117,18);place(c.slider,741+2*123,controlsY+18,117,84);}
    punchPitchModeLabel.setVisible(selectedLayer==0);punchPitchMode.setVisible(selectedLayer==0);
    place(punchPitchModeLabel,392,controlsY+10,230,18);
    place(punchPitchMode,392,controlsY+35,200,30);
    punchInvert.setVisible(selectedLayer==0);place(punchInvert,600,controlsY+35,137,30);
    // v0.4.67; v0.5.9: the toggle is only as wide as its text so the arrow to
    // ONLY PUNCH 2 FOLLOWS starts right after the words and spans the whole gap.
    {const int textW=juce::Font(15.0f).getStringWidth(punchFollowTail.getButtonText());
     punchFollowTail.setVisible(selectedLayer==0);place(punchFollowTail,392,controlsY+70,juce::jlimit(120,300,textW+30),26);}
    punchFollowDecayOnly.setVisible(selectedLayer==0);place(punchFollowDecayOnly,745,controlsY+70,240,26);   // v0.5.4
    decayNoteLabel.setVisible(selectedLayer==0);decayNoteButton.setVisible(selectedLayer==0);   // v0.5.5
    place(decayNoteLabel,28,controlsY+106,760,28);place(decayNoteButton,796,controlsY+106,190,28);
    // v0.5.10 PUNCH HARMONY row below the decay note (the Punch page is 34 px taller).
    for(auto* c:std::initializer_list<juce::Component*>{&punchHarmonyLabel,&punchHarmony,&punchHarmonyKeyLabel,&punchHarmonyKey,&punchHarmonyScaleLabel,&punchHarmonyScale})c->setVisible(selectedLayer==0);
    place(punchHarmonyLabel,28,controlsY+143,110,22);place(punchHarmony,140,controlsY+140,170,28);
    place(punchHarmonyKeyLabel,330,controlsY+143,34,22);place(punchHarmonyKey,366,controlsY+140,80,28);
    place(punchHarmonyScaleLabel,466,controlsY+143,46,22);place(punchHarmonyScale,514,controlsY+140,190,28);
    const int phaseY=controlsY+109;
    const bool showPhase=selectedLayer==1;
    for(juce::Component* c:std::initializer_list<juce::Component*>{&phaseHeading,&phaseAuto,&phaseInvert,&phaseScore})c->setVisible(showPhase);
    // v0.5.22 (user request): FINE PHASE SHIFT removed from the UI. The slider stays
    // attached but hidden, so saved/automated phaseFine values still play as before.
    phaseFineLabel.setVisible(false);phaseFine.setVisible(false);
    place(phaseHeading,28,phaseY+3,198,24);
    place(phaseAuto,226,phaseY+2,181,28);
    place(phaseInvert,411,phaseY+2,175,28);
    place(phaseFineLabel,609,phaseY-1,210,22);
    place(phaseFine,601,phaseY+27,305,37);
    place(phaseScore,920,phaseY-1,184,67);
    qualityLabel.setVisible(showPhase);
    qualityMode.setVisible(showPhase);
    adaptiveLabel.setVisible(showPhase);adaptiveAmount.setVisible(showPhase);
    place(adaptiveLabel,740,phaseY+79,140,24);place(adaptiveAmount,882,phaseY+69,140,57);
    place(qualityLabel,28,phaseY+79,188,24);
    place(qualityMode,222,phaseY+77,235,29);

    {const bool show=showPhase; // v0.4.61: CRUNCH works with both remaining engines (Tail page only)
     formantLabel.setVisible(show);formantAmount.setVisible(show);crunchCharacter.setVisible(show);
     crunchLowCut.setVisible(show);crunchLowCutLabel.setVisible(show);crunchLostOnly.setVisible(show);}
    place(crunchLostOnly,734,phaseY+107,144,26);   // v0.5.7
    // v0.4.60: CRUNCH slider widened for its -/+ buttons; character sits left of LOW CUT.
    place(formantLabel,466,phaseY+79,56,24);place(formantAmount,524,phaseY+77,204,29);place(crunchCharacter,391,phaseY+107,66,26);
    place(crunchLowCutLabel,466,phaseY+108,56,22);place(crunchLowCut,524,phaseY+108,204,24);
    const int waveY=controlsY+108+(showPhase?139:34)+(selectedLayer==0?34:0);   // v0.5.5: decay-note row; v0.5.10: harmony row
    place(completedKick,28,waveY,810,225);place(gate,860,waveY+6,240,26);
    place(clipButton,860,waveY+36,239,29);place(limiterToggle,860,waveY+69,240,24);place(previewLabel,860,waveY+94,239,18);place(lowKeyPunch,860,waveY+113,240,26);
    // v0.5.21: Punch / Attack + Decay / Tail rows, all inside the waveform's height.
    for(int i=0;i<2;++i){place(normalizeButtons[size_t(i)],860,waveY+145+i*52,176,24);place(normalizeReset[size_t(i)],1041,waveY+145+i*52,58,24);}
    place(splitNormalizeButtons[0],860,waveY+171,86,24);place(splitNormalizeButtons[1],950,waveY+171,86,24);place(splitNormalizeReset,1041,waveY+171,58,24);
    reverbLowCut.setVisible(false);reverbLowCutLabel.setVisible(false);
    const int generatedY=waveY+237;
    generatedPanel={28,generatedY,1083,generatedHeight()};
    // v0.5.13: the title is the fold button; minimized, ENABLE sits beside it.
    updateGeneratedVisibility();
    place(generatedTitle,42,generatedY+9,255,22);place(generatedFold,38,generatedY+6,218,24);
    if(generatedMinimized)place(generatedEnable,270,generatedY+6,205,24);else place(generatedEnable,42,generatedY+39,205,25);place(generatedKeepDry,42,generatedY+66,205,25);place(generatedDistortWet,42,generatedY+93,205,25);place(generatedPunchSend,42,generatedY+120,205,25);place(generatedTailSend,42,generatedY+147,205,25);place(generatedRootPitch,42,generatedY+201,205,23);place(wetAutoMatch,58,generatedY+228,189,23);place(wetPhaseLock,58,generatedY+255,189,23);
    place(generatedEQButton,42,generatedY+174,205,23);
    place(generatedTailSettings,270,generatedY+5,150,24);place(generatedPunchSettings,425,generatedY+5,150,24);
    place(generatedEngineLabel,600,generatedY+7,120,20);place(generatedEngine,725,generatedY+5,140,24);
    for(int i=0;i<8;++i){const int row=i<5?0:1,col=i<5?i:i-5;const int x=270+col*164,y=generatedY+30+row*104;auto& t=generatedControls[size_t(i)];place(t.label,x,y,158,18);place(t.slider,x,y+18,158,82);auto& p=punchGeneratedControls[size_t(i)];place(p.label,x,y,158,18);place(p.slider,x,y+18,158,82);}
    for(int i=8;i<10;++i){auto& c=generatedControls[size_t(i)];const int col=i-8;place(c.label,270+col*300,generatedY+240,290,16);place(c.slider,270+col*300,generatedY+256,290,30);}
    {auto& c=generatedControls[10];place(c.label,270,generatedY+240,290,16);place(c.slider,270,generatedY+256,290,30);}
    {auto& c=generatedControls[11];place(c.label,570,generatedY+240,290,16);place(c.slider,570,generatedY+256,290,30);}
    // v0.4.51: punch ducking of all reverb, shown on both Tail and Punch pages.
    for(int i=0;i<2;++i){auto& c=verbDuckControls[size_t(i)];const int x=270+(3+i)*164,y=generatedY+30+104;place(c.label,x,y,158,18);place(c.slider,x,y+18,158,82);}
    // Effects occupy a persistent right column, beside source/settings/waveform.
    const int dx=1107,fxY=69;
    effectPanels[0]={1135,fxY,577,collapsed[0]?36:150};
    const int transientY=effectPanels[0].getBottom()+12;
    effectPanels[1]={1135,transientY,577,collapsed[1]?36:160};
    foldButtons[0].setButtonText(collapsed[0]?"+ DISTORTION":"- DISTORTION");
    foldButtons[1].setButtonText(collapsed[1]?"+ TRANSIENT":"- TRANSIENT");
    place(foldButtons[0],38+dx,fxY+6,164,24);place(effectActivity[0],209+dx,fxY+6,338,24);
    place(foldButtons[1],38+dx,transientY+6,174,24);place(effectActivity[1],227+dx,transientY+6,245,24);
    showDistortionPage(distortionPage);
    for(int k=0;k<2;++k){
        distortionTabs[size_t(k)].setVisible(!collapsed[0]);place(distortionTabs[size_t(k)],42+dx+k*163,fxY+38,155,23);
        auto& fx=distortion[size_t(k)];place(fx.enabled,459+dx,fxY+37,91,25);place(fx.algorithm,42+dx,fxY+72,151,26);
        for(int i=0;i<4;++i){const int cx=198+dx+i*87;place(fx.knobs[size_t(i)].label,cx,fxY+67,84,19);place(fx.knobs[size_t(i)].slider,cx,fxY+87,84,59);}
    }
    transientHeading.setVisible(false);transientEnable.setVisible(true);place(transientEnable,1600,transientY+6,99,24);
    // v0.4.60: two columns of label + [-][+] slider rows, so each track stays usable.
    for(int i=0;i<8;++i){auto& c=transientControls[size_t(i)];c.label.setVisible(!collapsed[1]);c.slider.setVisible(!collapsed[1]);const int xx=1149+(i%2)*277,yy=transientY+37+(i/2)*30;place(c.label,xx,yy,84,26);place(c.slider,xx+86,yy,184,26);}
    const int eqY=effectPanels[1].getBottom()+12,eqHeight=collapsed[2]?36:340; // v0.4.60: 20 px tighter rows
    effectPanels[2]={1135,eqY,577,eqHeight};
    foldButtons[2].setButtonText(collapsed[2]?"+ EQ":"- EQ");place(foldButtons[2],1145,eqY+6,70,24);
    eqHeading.setVisible(false);place(eqSectionBox,1224,eqY+5,210,27);place(eqEnable,1440,eqY+5,90,27);place(effectActivity[2],1538,eqY+5,160,27);
    linearPhase.setVisible(activeEQ==3);effectActivity[2].setVisible(activeEQ!=3);place(linearPhase,1538,eqY+5,160,27);
    for(juce::Component* c:std::initializer_list<juce::Component*>{static_cast<juce::Component*>(&eqGraph),&eqBandLabel,&bandEnable,&bandType,&eqFreqLabel,&bandFreq,&eqGainLabel,&bandGain,&eqQLabel,&bandQ,&eqSlopeLabel,&bandSlope,&addBand,&removeBand})c->setVisible(!collapsed[2]);
    monoSub.setVisible(activeEQ==3&&!collapsed[2]);monoSubHz.setVisible(activeEQ==3&&!collapsed[2]);
    eqPostButton.setVisible(eqPostID(activeEQ)!=nullptr&&!collapsed[2]);   // v0.5.20
    if(activeEQ==3){place(addBand,1148,eqY+171,116,25);place(removeBand,1270,eqY+171,116,25);place(monoSub,1392,eqY+171,124,25);place(monoSubHz,1518,eqY+171,180,25);}
    else if(eqPostID(activeEQ)){place(addBand,1148,eqY+171,170,25);place(removeBand,1324,eqY+171,170,25);place(eqPostButton,1502,eqY+171,196,25);}
    else {place(addBand,1148,eqY+171,263,25);place(removeBand,1430,eqY+171,263,25);}
    place(eqGraph,1148,eqY+42,550,123);place(eqBandLabel,1148,eqY+202,70,27);place(bandType,1224,eqY+202,210,27);place(bandEnable,1460,eqY+202,235,27);
    place(eqFreqLabel,1148,eqY+235,263,16);place(bandFreq,1148,eqY+251,263,27);
    place(eqGainLabel,1430,eqY+235,263,16);place(bandGain,1430,eqY+251,263,27);
    bandGain.setMouseDragSensitivity((std::max)(2,2*bandGain.getLookAndFeel().getSliderLayout(bandGain).sliderBounds.getWidth()));
    place(eqQLabel,1148,eqY+285,263,16);place(bandQ,1148,eqY+301,263,27);place(eqSlopeLabel,1430,eqY+285,263,16);place(bandSlope,1430,eqY+301,263,27);
    const int regionY=effectPanels[2].getBottom()+12;
    punchRegionPanel={1135,regionY,577,199};
    place(punchRegionTitle,1148,regionY+6,365,25);place(punchRegionEnable,1600,regionY+6,99,25);
    place(punchRegionWave,1148,regionY+34,550,80);
    place(attackEQButton,1148,regionY+120,263,24);place(decayEQButton,1430,regionY+120,263,24);
    const std::array<juce::Label*,4> regionLabels{{&regionSplitLabel,&regionTransitionLabel,&attackGainLabel,&decayGainLabel}};
    const std::array<TailtoneSlider*,4> regionSliders{{&regionSplit,&regionTransition,&attackGain,&decayGain}};
    // v0.4.60: 2 x 2 label + [-][+] slider rows.
    for(int i=0;i<4;++i){const int x=1148+(i%2)*277,y=regionY+149+(i/2)*24;place(*regionLabels[size_t(i)],x,y,106,22);place(*regionSliders[size_t(i)],x+108,y,162,22);}
    // v0.4.64 (user request): Complete Waveform Envelope and Pitch Drop / Attack
    // Layer form a new column right of the effects column, level with Distortion.
    const int envelopeY=fxY;
    masterEnvelopePanel={1724,envelopeY,577,168};
    place(masterEnvelopeTitle,1737,envelopeY+6,550,25);
    for(size_t i=0;i<4;++i){const int x=1737+int(i)*88;
        place(masterEnvelopeLabels[i],x,envelopeY+37,85,18);
        place(masterEnvelopeSliders[i],x,envelopeY+57,85,75);}
    place(masterEnvelopeGraph,2100,envelopeY+37,187,95);
    place(masterEnvelopeNote,1737,envelopeY+139,550,22);
    const int dropY=masterEnvelopePanel.getBottom()+12;
    pitchDropPanel={1724,dropY,577,258};
    place(pitchDropTitle,1737,dropY+5,370,24);place(pitchDropEnable,2192,dropY+5,95,24);
    for(size_t i=0;i<3;++i){const int x=1737+int(i)*105;
        place(pitchDropLabels[i],x,dropY+34,98,18);place(pitchDropSliders[i],x,dropY+54,98,65);}
    place(pitchDropTarget,2089,dropY+50,198,28);place(pitchDropNote,1737,dropY+122,550,22);
    // v0.4.62: third row, ATTACK LAYER.
    place(attackTitle,1737,dropY+150,370,24);place(attackEnable,2192,dropY+150,95,24);
    // v0.5.13: four knobs LENGTH / START / LEVEL / END; the note narrows to the right.
    for(size_t i=0;i<4;++i){const int x=1737+int(i)*105;
        place(attackLabels[i],x,dropY+176,98,18);place(attackSliders[i],x,dropY+194,98,60);}
    place(attackNote,2157,dropY+180,130,70);
    // v0.5 MONO BASS panel, 12 px under Pitch Drop.
    const int monoY=pitchDropPanel.getBottom()+12;
    bassMonoPanel={1724,monoY,577,128};
    place(bassMonoTitle,1737,monoY+5,200,24);place(bassMonoLinear,1990,monoY+5,190,24);place(bassMonoEnable,2192,monoY+5,95,24);
    place(bassMonoHzLabel,1737,monoY+36,110,26);place(bassMonoHz,1850,monoY+36,437,26);
    place(bassMonoMixLabel,1737,monoY+68,110,26);place(bassMonoMix,1850,monoY+68,437,26);
    place(bassMonoNote,1737,monoY+100,550,22);
    const int statusY=(std::max)({generatedY+generatedHeight(),punchRegionPanel.getBottom()+8,pitchDropPanel.getBottom()+8,bassMonoPanel.getBottom()+8});
    // v0.5.19: the preparation bar takes the right end of the status line.
    place(statusLabel,28,statusY,baseWidth-56-prepBarWidth-12,23);place(prepBar,baseWidth-28-prepBarWidth,statusY,prepBarWidth,23);
    place(keys,28,statusY+30,baseWidth-56,53);
}
