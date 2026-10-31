// v0.5.13: AUTO-SPLIT KICK was removed (user request). LOCK SPLIT stays for a
// Punch + Tail pair that holds the same sample (one kick loaded into both lanes
// by hand, or a project saved with an earlier AUTO-SPLIT). This test replaces
// AutoSplitKick: split-pair detection on real loads, the project round trip
// of a pair with LOCK SPLIT on, unload isolation, and that loading the same
// file twice leaves every trim and the lock untouched (nothing auto-splits).
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <stdexcept>
static void ck(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* par=p.params.getParameter(id);par->setValueNotifyingHost(par->convertTo0to1(v));}
static float value(PunchTailProcessor& p,const char* id){return p.params.getRawParameterValue(id)->load();}
static void waitFor(PunchTailProcessor& p,int slot,const juce::String& name) {
    for(int i=0;i<30000;++i){auto a=p.asset(slot);if(a&&a->name==name)return;juce::Thread::sleep(1);}
    throw std::runtime_error(("lane did not load "+name+": "+p.status()).toStdString());
}
int main(int argc,char** argv) try {
    juce::ScopedJuceInitialiser_GUI init;
    ck(argc>2,"usage: SplitPairTests <virus-kick-3.wav> <kick-nc-lol-2.0.wav>");
    const juce::File kick(juce::String::fromUTF8(argv[1])),other(juce::String::fromUTF8(argv[2]));
    ck(kick.existsAsFile()&&other.existsAsFile(),"fixture missing");
    PunchTailProcessor p;
    ck(!p.lockSplitOn(),"LOCK SPLIT must default OFF");
    // The same kick in both lanes, loaded by hand: a split pair; no trims change.
    set(p,"punchOffset",120);set(p,"trim",35);
    p.loadFile(0,kick);waitFor(p,0,kick.getFileName());
    p.loadFile(1,kick);waitFor(p,1,kick.getFileName());
    auto punch=p.asset(0),tail=p.asset(1);
    ck(PunchTailProcessor::isSplitPair(punch.get(),tail.get()),"the same file in both lanes must form a split pair");
    ck(punch->bytes==tail->bytes&&punch->sample.left==tail->sample.left,"lanes must hold the same audio");
    // v0.5.36 Detect End / Detect Start: measured on the worker at load, in both lanes.
    {const auto expected=pt::detectTailStart(punch->sample);
     ck(expected.valid&&expected.punch&&expected.ms>85&&expected.ms<100,"virus-kick-3: tail start not found");
     ck(punch->tailStart.valid&&punch->tailStart.punch&&punch->tailStart.ms==expected.ms,"Punch lane: tail start not measured at load");
     ck(tail->tailStart.valid&&tail->tailStart.ms==expected.ms,"Tail lane: tail start not measured at load");
     ck(punch->punchEnd.valid&&punch->punchEnd.endMs==expected.ms,"Punch lane: Detect End must end the punch where the tail begins");}
    juce::Thread::sleep(100);
    ck(value(p,"punchOffset")==120&&value(p,"trim")==35&&value(p,"punchExtra")==0&&value(p,"tailExtra")==0,"loading the same kick twice must not move any trim (no AUTO-SPLIT)");
    ck(!p.lockSplitOn(),"loading the same kick twice must not switch LOCK SPLIT on");
    ck(!p.status().containsIgnoreCase("AUTO-SPLIT"),"status still mentions AUTO-SPLIT");
    // Project round trip of a pair with LOCK SPLIT on.
    set(p,"lockSplit",1);
    juce::MemoryBlock saved;p.getStateInformation(saved);
    {PunchTailProcessor restored;restored.setStateInformation(saved.getData(),int(saved.getSize()));
     for(int i=0;i<30000;++i){if(restored.asset(0)&&restored.asset(1))break;juce::Thread::sleep(1);}
     auto rp=restored.asset(0),rt=restored.asset(1);
     ck(rp&&rt&&PunchTailProcessor::isSplitPair(rp.get(),rt.get())&&rp->sample.left==punch->sample.left,"split pair did not survive a project round trip");
     ck(restored.lockSplitOn()&&value(restored,"punchOffset")==120&&value(restored,"trim")==35,"LOCK SPLIT / trims did not survive a project round trip");
     ck(rp->punchEnd.endMs==punch->punchEnd.endMs&&rt->tailStart.ms==tail->tailStart.ms&&rt->tailStart.valid,"a reopened project must detect the same punch end / tail start");}
    // Unloading one lane leaves the other alone; one lane is no pair.
    p.unloadFile(1);for(int i=0;i<2000&&p.asset(1);++i)juce::Thread::sleep(1);
    ck(!p.asset(1)&&p.asset(0)==punch,"unload touched the other lane");
    ck(!PunchTailProcessor::isSplitPair(p.asset(0).get(),nullptr),"one lane is no split pair");
    // A Tail from another file is no split pair.
    p.loadFile(1,other);waitFor(p,1,other.getFileName());
    ck(!PunchTailProcessor::isSplitPair(p.asset(0).get(),p.asset(1).get()),"different files must not form a split pair");
    {const auto t=p.asset(1)->tailStart;ck(t.valid&&t.punch&&t.ms>125&&t.ms<145,"kick-nc-lol-2.0 in the Tail lane: tail start not measured");}
    std::cout<<"PASS split pair: manual double load, no auto-split side effects, round trip with LOCK SPLIT, unload, different files, tail start / punch end measured at load\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
