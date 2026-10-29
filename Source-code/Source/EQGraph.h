#pragma once
#include "Theme.h"
#include "PluginProcessor.h"
#include <functional>
#include <cmath>

class TailtoneEQGraph final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit TailtoneEQGraph(PunchTailProcessor& p):processor(p){
        setTooltip("Drag a point: left/right changes the frequency, up/down the boost or cut (gently). Hold Ctrl and drag up/down to change the width: up = narrower, down = wider. Hold Shift to move slowly. Right-click a point to change its filter type or turn the band off. Double-click an empty spot to add a band; near the left edge it adds a low shelf, near the right edge a high shelf.");
    }
    std::function<void(int)> onSelect;
    std::function<void(int,double,double)> onDrag;
    std::function<void(int,int)> onType;
    std::function<void(double,double)> onAdd;
    // v0.5.9 (user request): double-clicking close to the LEFT edge of the plot
    // adds a LOW SHELF, close to the RIGHT edge a HIGH SHELF, elsewhere a bell.
    // "Close" = within kShelfEdgeShare of the plot width (12 % ~ 20-60 Hz and
    // 8.7-20 kHz). onAddTyped(hz, gain, type) is preferred when set; onAdd keeps
    // the old bell-only behaviour for anyone who only wired that.
    std::function<void(double,double,int)> onAddTyped;
    static constexpr double kShelfEdgeShare=0.12;
    static int typeForAdd(float x,juce::Rectangle<float> r) {
        if(r.getWidth()<=0)return tt::Bell;
        const double share=double((x-r.getX())/r.getWidth());
        return share<kShelfEdgeShare?tt::LowShelf:share>1.0-kShelfEdgeShare?tt::HighShelf:tt::Bell;
    }
    // The band is placed at the click frequency, except that a shelf's corner
    // frequency is kept inside the audible edge zone (>= 40 Hz / <= 12 kHz) so the
    // shelf still shapes something when the click is right at the plot border.
    static double freqForAdd(double hz,int type) {
        if(type==tt::LowShelf)return (std::max)(hz,40.0);
        if(type==tt::HighShelf)return (std::min)(hz,12000.0);
        return hz;
    }
    // v0.4.69: right-click menu "Turn off band N" (the band's On switch -> off).
    std::function<void(int)> onTurnOff;
    static constexpr int turnOffItemId=100;
    static juce::PopupMenu nodeMenu(int band,int currentType) {
        juce::PopupMenu menu;
        const char* types[]={"Low Cut","High Cut","Bell","Low Shelf","High Shelf"};
        for(int t=0;t<5;++t)menu.addItem(t+1,types[t],true,currentType==t);
        menu.addSeparator();
        menu.addItem(turnOffItemId,"Turn off band "+juce::String(band+1));
        return menu;
    }
    // v0.4.60: Ctrl-drag (Cmd on macOS) edits the selected band's Q / width.
    std::function<void(int,double)> onQ;
    static constexpr float qPixelsPerOctave=40.0f; // 40 px of Ctrl-drag doubles / halves Q
    static double qFromDrag(double startQ,float upPixels) {
        return std::clamp(std::round(startQ*std::pow(2.0,double(upPixels)/qPixelsPerOctave)*100.0)/100.0,0.2,8.0);
    }
    void setSelected(int band) {selected=band;repaint();}
    void setView(int v) {draggingNode=false;view=juce::jlimit(0,6,v);repaint();}
    void paint(juce::Graphics& g) override {
        auto box=getLocalBounds().toFloat().reduced(12,12);
        box.removeFromBottom(21); box.removeFromLeft(30);
        g.setColour(tt::ui::pal().plot);g.fillRoundedRectangle(getLocalBounds().toFloat(),6.0f);
        if(box.getWidth()<2 || box.getHeight()<2)return;
        for(int hz:{20,50,100,200,500,1000,2000,5000,10000,20000}) {
            const auto x=toX(hz,box);
            g.setColour(tt::ui::pal().grid);g.drawVerticalLine(int(x),box.getY(),box.getBottom());
            if(hz==20||hz==100||hz==1000||hz==10000||hz==20000){
                g.setColour(tt::ui::pal().plotText);g.setFont(juce::Font(10));
                g.drawText(juce::String(hz>=1000?juce::String(double(hz)/1000.0, hz%1000?1:0)+"k":juce::String(hz)),
                           int(x)-26,int(box.getBottom()+3),52,15,juce::Justification::centred);
            }
        }
        for(int db=-18;db<=18;db+=6) {
            const auto y=toY(db,box);
            g.setColour(juce::Colour(db==0?0xff777777:0xff393939));
            g.drawHorizontalLine(int(y),box.getX(),box.getRight());
            g.setColour(tt::ui::pal().plotDim);g.setFont(juce::Font(10));
            if(box.getHeight()>=110 || db%12==0)
                g.drawText(juce::String(db),0,int(y)-7,38,14,juce::Justification::centredRight);
        }
        const auto bands=processor.eqSettings(view);
        if(processor.params.getRawParameterValue(view==1?"punchEqOn":view==2?"reverbEqOn":view==3?"tailVerbEqOn":view==4?"punchVerbEqOn":view==5?"punchAttackEqOn":view==6?"punchDecayEqOn":"eqOn")->load()>0.5f) {
            const double sr=processor.getSampleRate()>0?processor.getSampleRate():44100.0;
            juce::Path curve;
            for(int step=0;step<=500;++step) {
                const auto x=box.getX()+box.getWidth()*float(step)/500.0f;
                const auto hz=toFreq(x,box);
                const auto db=tt::responseDb(bands,hz,sr);
                const auto y=toY(db,box);
                if(step==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);
            }
            g.setColour(tt::ui::pal().title.withAlpha(0.9f));g.strokePath(curve,juce::PathStrokeType(2.0f));
        }
        for(int i=0;i<tt::kBands;++i) {
            const auto& b=bands[size_t(i)];if(!b.enabled)continue;
            const auto x=toX(b.hz,box),y=toY((b.type==tt::LowCut||b.type==tt::HighCut)?0:b.gain,box);
            g.setColour(i==selected?tt::ui::pal().title:tt::ui::pal().plotDim);g.fillEllipse(x-9,y-9,18,18);
            g.setColour(tt::ui::pal().nodeText);g.setFont(juce::Font(10.0f,juce::Font::bold));
            g.drawText(juce::String(i+1),int(x)-8,int(y)-8,16,16,juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        draggingNode=false;
        const auto rect=plotBounds();
        const auto bands=processor.eqSettings(view);
        double best=20*20;int index=-1;
        for(int i=0;i<tt::kBands;++i)if(bands[size_t(i)].enabled){
            const auto& b=bands[size_t(i)];
            const float dx=toX(b.hz,rect)-e.position.x;
            const float dy=toY((b.type==tt::LowCut||b.type==tt::HighCut)?0:b.gain,rect)-e.position.y;
            const double distance=dx*dx+dy*dy;
            if(distance<best){best=distance;index=i;}
        }
        if(index>=0) {
            dragStartY=e.position.y; dragStartGain=bands[size_t(index)].gain; dragStartQ=bands[size_t(index)].q; qUpPixels=0;
            dragInput=e.position;dragPosition={toX(bands[size_t(index)].hz,rect),e.position.y};
            selected=index;draggingNode=e.mods.isLeftButtonDown();if(onSelect)onSelect(index);repaint();
            if(e.mods.isPopupMenu()) {
                auto menu=nodeMenu(index,bands[size_t(index)].type);
                auto safe=juce::Component::SafePointer<TailtoneEQGraph>(this);
                const int openedOnView=view;
                menu.showMenuAsync(juce::PopupMenu::Options(),[safe,index,openedOnView](int result){
                    if(!safe || safe->view!=openedOnView)return;
                    if(result>=1&&result<=5 && safe->onType)safe->onType(index,result-1);
                    else if(result==turnOffItemId && safe->onTurnOff)safe->onTurnOff(index);
                });
            }
        } else if(e.getNumberOfClicks()>=2) {
            const int type=typeForAdd(e.position.x,rect);
            const double hz=freqForAdd(toFreq(e.position.x,rect),type),gain=0.5*toGain(e.position.y,rect);
            if(onAddTyped)onAddTyped(hz,gain,type);else if(onAdd)onAdd(hz,gain);
        }
    }
    void mouseUp(const juce::MouseEvent&) override {draggingNode=false;}
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!draggingNode||!e.mods.isLeftButtonDown()||selected<0)return;
        const auto bands=processor.eqSettings(view);if(!bands[size_t(selected)].enabled)return;
        const auto rect=plotBounds();
        const auto delta=(e.position-dragInput)*(e.mods.isShiftDown()?0.1f:1.0f);dragInput=e.position;
        // Ctrl held: up/down changes only the width; frequency and gain stay put,
        // and pressing/releasing Ctrl mid-drag never makes either jump.
        if(e.mods.isCtrlDown()||e.mods.isCommandDown()) {
            qUpPixels-=delta.y;
            if(onQ)onQ(selected,qFromDrag(dragStartQ,qUpPixels));
            return;
        }
        dragPosition+=delta;
        const int type=bands[size_t(selected)].type;
        const double gain=type==tt::LowCut||type==tt::HighCut?bands[size_t(selected)].gain:
            std::round(std::clamp(dragStartGain + 18.0*double(dragStartY-dragPosition.y)
                                 / (std::max)(1.0f,rect.getHeight()),-18.0,18.0)*10.0)/10.0;
        if(onDrag)onDrag(selected,toFreq(dragPosition.x,rect),gain);
    }
private:
    PunchTailProcessor& processor;
    int selected=1, view=0;bool draggingNode=false;
    float dragStartY=0, qUpPixels=0; double dragStartGain=0, dragStartQ=0.71;
    juce::Point<float> dragInput,dragPosition;
    juce::Rectangle<float> plotBounds() const {auto r=getLocalBounds().toFloat().reduced(12,12);r.removeFromBottom(21);r.removeFromLeft(30);return r;}
    static float toX(double hz,juce::Rectangle<float> r) {return r.getX()+r.getWidth()*float(std::log(std::clamp(hz,20.0,20000.0)/20.0)/std::log(1000.0));}
    static double toFreq(float x,juce::Rectangle<float> r) {return std::clamp(20.0*std::pow(1000.0,double((x-r.getX())/r.getWidth())),20.0,20000.0);}
    static float toY(double db,juce::Rectangle<float> r) {return r.getY()+r.getHeight()*float((18-std::clamp(db,-18.0,18.0))/36.0);}
    static double toGain(float y,juce::Rectangle<float> r){return std::round(std::clamp(18.0-36.0*double((y-r.getY())/r.getHeight()),-18.0,18.0)*10.0)/10.0;}
};
