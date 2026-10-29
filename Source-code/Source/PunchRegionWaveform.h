#pragma once
#include "Theme.h"
#include "PluginProcessor.h"
#include "WaveformEnvelope.h"
#include <functional>

// A second view of the selected Punch, never a new sample slot. Envelope work
// happens only on the GUI thread when its source or selected Punch End changes.
class PunchRegionWaveform final : public juce::Component,public juce::SettableTooltipClient {
public:
    std::function<void(bool)> onGesture;
    std::function<void(double)> onSplit;
    void setAudio(std::shared_ptr<const PunchTailProcessor::Asset> next,double sourceEndMs,
                  double timeScale,double percent,double transitionMs,bool on) {
        const size_t frames=next?(std::min)(next->sample.left.size(),size_t(std::llround((std::max)(0.,sourceEndMs)*next->sample.rate*.001))):0;
        if(next!=sample || frames!=visibleFrames) {
            sample=std::move(next);visibleFrames=frames;envelope={};
            const size_t bins=(std::min)(size_t(2048),frames);envelope.bins.resize(bins);
            for(size_t b=0;b<bins;++b) {
                const size_t first=b*frames/bins,last=(std::max)(first+1,(b+1)*frames/bins);
                float lo=1e30f,hi=-1e30f;
                for(size_t i=first;i<last;++i){const float l=sample->sample.left[i],r=sample->sample.right[i];lo=(std::min)({lo,l,r});hi=(std::max)({hi,l,r});}
                envelope.bins[b]={lo,hi};envelope.peak=(std::max)({envelope.peak,std::abs(lo),std::abs(hi)});
            }
        }
        durationMs=frames && sample?double(frames)/sample->sample.rate*1000*timeScale:0;
        if(!dragging)split=std::clamp(percent,0.,100.);
        widthMs=transitionMs;active=on;setEnabled(frames>0);repaint();
    }
    juce::Rectangle<float> plot() const {return getLocalBounds().toFloat().reduced(10,0).withTrimmedTop(24).withTrimmedBottom(25);}
    void paint(juce::Graphics& g) override {
        g.setColour(tt::ui::pal().plot);g.fillRoundedRectangle(getLocalBounds().toFloat(),5);
        auto box=plot();if(box.getWidth()<1||box.getHeight()<1)return;
        if(!visibleFrames){g.setColour(tt::ui::pal().plotDim);g.drawText("Load a Punch to select Attack / Decay",getLocalBounds(),juce::Justification::centred);return;}
        const float x=box.getX()+box.getWidth()*float(split*.01);
        const auto attack=tt::ui::pal().attack,decay=tt::ui::pal().decay;
        g.setColour(attack.withAlpha(active?.15f:.06f));g.fillRect(box.withWidth(x-box.getX()));
        g.setColour(decay.withAlpha(active?.15f:.06f));g.fillRect(box.withLeft(x));
        const float half=float((std::min)({widthMs*.5,durationMs*split*.01,durationMs*(1-split*.01)})/(std::max)(.001,durationMs)*box.getWidth());
        g.setColour(tt::ui::pal().title.withAlpha(.09f));g.fillRect(juce::Rectangle<float>(x-half,box.getY(),2*half,box.getHeight()));
        const float mid=box.getCentreY(),gain=box.getHeight()*.44f/(std::max)(1e-6f,envelope.peak);
        g.setColour(tt::ui::pal().title.withAlpha(active?.9f:.5f));
        for(size_t b=0;b<envelope.bins.size();++b){const float px=box.getX()+float(b)*box.getWidth()/float(envelope.bins.size());g.drawVerticalLine(int(px),mid-envelope.bins[b].maximum*gain,mid-envelope.bins[b].minimum*gain);}
        g.setColour(attack);g.setFont(juce::Font(11,juce::Font::bold));g.drawText("ATTACK",10,3,int(getWidth()/2)-10,18,juce::Justification::centredLeft);
        g.setColour(decay);g.drawText("DECAY",getWidth()/2,3,getWidth()/2-10,18,juce::Justification::centredRight);
        g.setColour(tt::ui::pal().title);g.drawLine(x,box.getY(),x,box.getBottom(),2);
        g.fillEllipse(x-4,box.getY()-4,8,8);
        g.setFont(juce::Font(10));g.drawText("0 ms",10,getHeight()-22,60,18,juce::Justification::centredLeft);
        g.drawText("Split "+juce::String(durationMs*split*.01,1)+" ms",70,getHeight()-22,getWidth()-160,18,juce::Justification::centred);
        g.drawText(juce::String(durationMs,1)+" ms",getWidth()-90,getHeight()-22,80,18,juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if(!visibleFrames||!plot().contains(e.position)||!e.mods.isLeftButtonDown())return;
        dragging=true;lastX=e.position.x;if(onGesture)onGesture(true);
        if(!e.mods.isShiftDown())split=std::clamp(double((e.position.x-plot().getX())/plot().getWidth())*100,0.,100.);
        if(onSplit)onSplit(split);repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!dragging)return;
        split=std::clamp(split+double(e.position.x-lastX)/plot().getWidth()*100*(e.mods.isShiftDown()?.1:1.),0.,100.);
        lastX=e.position.x;if(onSplit)onSplit(split);repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {if(dragging){dragging=false;if(onGesture)onGesture(false);}}
private:
    std::shared_ptr<const PunchTailProcessor::Asset> sample;
    pt::WaveformEnvelope envelope;
    size_t visibleFrames=0;
    double durationMs=0,split=20,widthMs=2;
    float lastX=0;
    bool active=false,dragging=false;
};
