// v0.4.48 keyboard mapping: every key plays one octave lower internally, and
// keys D2..C#3 (v0.4.49; was D1..C#2) can use the low punch lane (tail as the key one octave up,
// punch Resampled one octave below that tail, per key).
#include "../Source/PluginProcessor.h"
#include "../Source/PunchHarmony.h"
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static void set(PunchTailProcessor& p,const char* id,float v){auto* a=p.params.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}

struct TailtoneKeyMappingTest {
 static void policy() {
    // OFF: every key = key - 12.  ON: only D2..C#3 change.
    for(int key=0;key<128;++key) {
        check(pt::tailNoteForKey(key,false)==(std::max)(0,key-12),"Octave shift wrong for key "+std::to_string(key));
        const bool low=key>=26&&key<=37;
        check(pt::tailNoteForKey(key,true)==(low?key:(std::max)(0,key-12)),"Low lane tail note wrong for key "+std::to_string(key));
        check(pt::punchIndexForKey(key,true)==(low?128+key-26:(std::max)(0,key-12)),"Low lane punch index wrong for key "+std::to_string(key));
        check(pt::punchIndexForKey(key,false)==(std::max)(0,key-12),"Punch index without lane wrong");
    }
    // Pitched tail range moves to keys D#2..D#5 (27..63); upper punch lane E4..D#5 (52..63).
    check(pt::tailNoteForKey(27,false)==pt::kLowestPitchNote && pt::tailNoteForKey(63,true)==pt::kHighestPitchNote,"Pitched key range not D#2..D#5");
    check(!pt::isLowPunchKey(14)&&!pt::isLowPunchKey(25)&&pt::isLowPunchKey(26)&&pt::isLowPunchKey(37)&&!pt::isLowPunchKey(38),"Low lane must be D2..C#3 only");
    check(pt::followsPunchMidi(pt::tailNoteForKey(52,true)) && !pt::followsPunchMidi(pt::tailNoteForKey(51,true)),"Upper punch lane not E4..D#5");
    check(std::abs(pt::lowLanePunchPitch(25,36,0)-(-23))<1e-12 && std::abs(pt::lowLanePunchPitch(14,36,2)-(-32))<1e-12,"Low lane punch pitch formula");
 }
 // v0.5.10 PUNCH HARMONY interval policy.
 static void harmonyPolicy() {
    using namespace pt::harmony;
    for(int n=0;n<128;++n){check(interval(Off,0,0,n)==0,"Off must add nothing");check(interval(PerfectFifth,5,1,n)==7,"Perfect fifth must be +7 everywhere");}
    // C major: C E G B D F A have major/minor thirds m M m M m m M -> 4 3 4 3 4 3 3... per degree:
    const int cMajor[12]={4,3,3,4,3,4,3,4,3,3,4,3};   // C C# D D# E F F# G G# A A# B
    for(int pc=0;pc<12;++pc)for(int oct=0;oct<10;++oct)check(interval(DiatonicThird,0,0,pc+12*oct)==cMajor[pc],"C major third wrong for pitch class "+std::to_string(pc));
    // A natural minor (relative): same thirds as C major, key 9.
    for(int pc=0;pc<12;++pc)check(interval(DiatonicThird,9,1,pc)==cMajor[pc],"A natural minor must match C major for pitch class "+std::to_string(pc));
    // F major: F G A Bb C D E -> 4 3 3 4 4 3 3; the chromatic F# (1 st above F) takes A (+3).
    check(interval(DiatonicThird,5,0,5)==4&&interval(DiatonicThird,5,0,7)==3&&interval(DiatonicThird,5,0,10)==4&&interval(DiatonicThird,5,0,6)==3,"F major thirds wrong");
    // Every scale note gets a third of 3 or 4 that lands in the scale, for every key
    // and scale; a chromatic note still lands in the scale (3 or 4, except the one
    // harmonic-minor gap, F# in C harmonic minor, which takes +2).
    for(int sc=0;sc<kScaleCount;++sc)for(int key=0;key<12;++key)for(int n=0;n<128;++n){
        const int i=interval(DiatonicThird,key,sc,n);
        if(inScale(sc,key,n))check(i==3||i==4,"Diatonic third must be 3 or 4 on a scale note");
        else check(i==3||i==4||(sc==2&&pitchClass(n-key)==6&&i==2),"Chromatic note must take a third in the scale");
        check(inScale(sc,key,n+i),"Diatonic third must land in the scale");}
    check(kScaleCount==8&&std::string(kScales[0].name)=="Major (Ionian)"&&std::string(kScales[1].name)=="Natural Minor","Scale list changed");
 }
 static void stop(PunchTailProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static std::shared_ptr<PunchTailProcessor::Bank> markedBank() {
    auto bank=std::make_shared<PunchTailProcessor::Bank>();bank->prepared=true;bank->root=36;
    for(int n=0;n<128;++n){auto& s=bank->notes[size_t(n)];s.rate=44100;s.left.assign(44100,.001f*float(n+1));s.right=s.left;}
    return bank;
 }
 static void processor() {
    PunchTailProcessor p;
    check(p.params.getRawParameterValue("lowKeyPunch")->load()>.5f,"Low key punch lane must default ON for new instances");
    set(p,"phaseAuto",0);set(p,"root",36);set(p,"punchPitch",0);set(p,"punchPitchMode",0);set(p,"gate",1);
    auto punch=std::make_shared<PunchTailProcessor::Asset>();punch->sample.rate=44100;
    punch->sample.left.resize(4410);for(size_t i=0;i<4410;++i)punch->sample.left[i]=float(.5*std::sin(i*.05));punch->sample.right=punch->sample.left;
    const auto waitPunch=[&](float pitch){
        p.notify();
        for(int i=0;i<10000;++i){{const juce::ScopedLock lock(p.assetLock);
            if(p.preparedPunchSource==punch&&std::abs(p.preparedPunchPitch-pitch)<.001&&p.preparedPunch&&p.preparedPunch->lowPunches[11])return;}
            juce::Thread::sleep(1);}
        throw std::runtime_error("Punch worker did not publish");
    };
    {const juce::ScopedLock lock(p.assetLock);p.assets[0]=punch;p.bank=markedBank();}
    waitPunch(0);
    const auto prepared=p.previewPunch();
    check(prepared&&prepared!=punch,("Punch snapshot not prepared: "+p.status()).toStdString());
    for(int key=26;key<=37;++key) {
        const auto& low=prepared->forNote(128+key-26);
        check(&low!=prepared.get(),"Low lane punch missing for key "+std::to_string(key));
        // Resample one octave below the tail heard on the key: duration scales by 2^-(shift/12).
        const double shift=pt::lowLanePunchPitch(key,36,0);
        const double expected=4410.0/std::pow(2.0,shift/12.0);
        check(std::abs(double(low.sample.left.size())-expected)<2.0,"Low lane punch is not Resampled to the expected pitch for key "+std::to_string(key));
        check(std::abs(low.punchTimeScale-double(low.sample.left.size())/4410.0)<1e-12,"Low lane punch time scale wrong");
    }
    // v0.5.10 PUNCH HARMONY: defaults Off / C / Major, appended after crunchLostOnly.
    check(p.params.getRawParameterValue("punchHarmony")->load()==0&&p.params.getRawParameterValue("punchHarmonyKey")->load()==0&&p.params.getRawParameterValue("punchHarmonyScale")->load()==0,"Punch harmony must default to Off / C / Major");
    check(p.params.getParameter("punchHarmony")->getParameterIndex()>p.params.getParameter("crunchLostOnly")->getParameterIndex(),"punchHarmony not appended");
    // Off: the lanes are exactly the v0.5.9 ones (follow OFF -> only 40..51 pitched).
    check(&prepared->forNote(30)==prepared.get()&&&prepared->forNote(45)!=prepared.get(),"Harmony Off changed which keys follow");
    {const auto waitHarmony=[&](int mode,int key,int scale){
        set(p,"punchHarmony",float(mode));set(p,"punchHarmonyKey",float(key));set(p,"punchHarmonyScale",float(scale));p.notify();
        for(int i=0;i<20000;++i){{const juce::ScopedLock lock(p.assetLock);
            if(p.preparedPunchSource==punch&&p.preparedPunch&&p.preparedPunchHarmony.mode==mode&&p.preparedPunchHarmony.key==key&&p.preparedPunchHarmony.scale==scale)return p.preparedPunch;}
            juce::Thread::sleep(1);}
        throw std::runtime_error("Harmony lanes not prepared");};
     set(p,"punchPitchMode",1);   // Resample: the lane length tells the shift exactly
     // Perfect fifth in A minor: every tail key (15..51) follows at key - root + 7.
     auto fifth=waitHarmony(2,9,1);
     for(int note=15;note<=51;++note){const auto& lane=fifth->forNote(note);check(&lane!=fifth.get(),"Fifth lane missing for note "+std::to_string(note));
         check(std::abs(double(lane.sample.left.size())-4410.0/std::pow(2.0,(note-36+7)/12.0))<2.0,"Perfect fifth lane not +7 for note "+std::to_string(note));}
     // The low lane (D2..C#3) takes the fifth too; the tail is untouched (no bank change).
     check(std::abs(double(fifth->forNote(128+3).sample.left.size())-4410.0/std::pow(2.0,(pt::lowLanePunchPitch(29,36,0)+7)/12.0))<2.0,"Low lane did not take the fifth");
     check(p.bank->notes[36].left[0]==markedBank()->notes[36].left[0]&&p.bank->notes[47].left[0]==markedBank()->notes[47].left[0],"Harmony must not touch the tail bank");
     // Diatonic third in C major: C (note 36) -> E (+4), D (38) -> F (+3), E (40) -> G (+3), F (41) -> A (+4), B (47) -> D (+3).
     auto third=waitHarmony(1,0,0);
     const int expect[5][2]={{36,4},{38,3},{40,3},{41,4},{47,3}};
     for(const auto& e:expect)check(std::abs(double(third->forNote(e[0]).sample.left.size())-4410.0/std::pow(2.0,(e[0]-36+e[1])/12.0))<2.0,"Diatonic third lane wrong for note "+std::to_string(e[0]));
     // Same played note, other key: in A minor, E (40) -> G (+3) as well, but in E major (key 4) E -> G# (+4).
     auto eMajor=waitHarmony(1,4,0);
     check(std::abs(double(eMajor->forNote(40).sample.left.size())-4410.0/std::pow(2.0,(40-36+4)/12.0))<2.0,"Key change did not change the third");
     // Nothing accumulates: after several changes the root lane is still computed from the
     // note alone (C is chromatic in E major: the minor third D# is in the scale -> +3).
     check(std::abs(double(eMajor->forNote(36).sample.left.size())-4410.0/std::pow(2.0,3/12.0))<2.0,"Harmony accumulated between rebuilds");
     // Back to Off: the exact v0.5.9 lanes again.
     auto off=waitHarmony(0,0,0);
     check(&off->forNote(30)==off.get()&&std::abs(double(off->forNote(45).sample.left.size())-4410.0/std::pow(2.0,9/12.0))<2.0,"Harmony Off must restore the plain lanes");
     set(p,"punchPitchMode",0);}
    // Low lane punch ignores Stretch Aligned mode and tracks manual Punch Pitch.
    set(p,"punchPitch",5);waitPunch(5);
    check(std::abs(double(p.previewPunch()->forNote(128+11).sample.left.size())-4410.0/std::pow(2.0,(37-36-12+5)/12.0))<2.0,"Low lane punch ignores manual Punch Pitch");
    set(p,"punchPitch",0);waitPunch(0);
    p.prepareToPlay(44100,128);
    juce::AudioBuffer<float> block(2,128);juce::MidiBuffer midi;
    const auto play=[&](int key)->PunchTailProcessor::PlayingVoice&{
        midi.clear();midi.addEvent(juce::MidiMessage::noteOn(1,key,1.f),0);p.processBlock(block,midi);
        for(auto& v:p.voices)if(v.active)return v;
        throw std::runtime_error("No active voice for key "+std::to_string(key));
    };
    // Normal key: plays the note one octave lower (key C4=48 -> internal C3=36, the root).
    {auto& v=play(48);check(v.note==36&&v.key==48&&v.punchIndex==36,"Key 48 must play internal note 36");
     check(v.playbackTail()==&p.bank->notes[36],"Key 48 did not use the bank's internal note 36");}
    // Upper punch lane follows the key one octave up too (E4 = 52 -> internal 40).
    {auto& v=play(52);check(v.note==40&&v.punchIndex==40&&v.punch->forNote(40).sample.left.size()==4410,"Upper punch lane not mapped to E4");}
    // Low lane ON: C#3 (37) -> tail of new C#4 (internal 37), punch = low lane.
    {auto& v=play(37);check(v.note==37&&v.punchIndex==128+11,"C#3 must play the C#4 tail with the low punch lane");
     check(v.playbackTail()==&p.bank->notes[37],"C#3 did not use the C#4 tail");
     check(v.settings.punchTimeScale>1.5,"C#3 punch was not pitched down (longer Resample)");}
    {auto& v=play(26);check(v.note==26&&v.punchIndex==128,"D2 low lane mapping wrong");}
    // Just outside the lane: D3 (38) and C#2 (25) play one octave lower normally.
    {auto& v=play(38);check(v.note==26&&v.punchIndex==26,"D3 must not use the low lane");}
    {auto& v=play(25);check(v.note==13&&v.punchIndex==13,"C#2 must no longer use the low lane");}
    // Note-off releases by played key, not internal note.
    {play(37);midi.clear();midi.addEvent(juce::MidiMessage::noteOff(1,25),0);p.processBlock(block,midi);
     check(!p.masterEnvelope.isReleased(),"Note-off of the internal note number released the key");
     midi.clear();midi.addEvent(juce::MidiMessage::noteOff(1,37),0);p.processBlock(block,midi);
     check(p.masterEnvelope.isReleased(),"Note-off of the played key did not release");}
    // Lane OFF: C#3 plays one octave lower like every key.
    set(p,"lowKeyPunch",0);
    {auto& v=play(37);check(v.note==25&&v.punchIndex==25,"Lane OFF must play C#3 as internal 25");}
    // Phase map covers the low lane slots.
    set(p,"lowKeyPunch",1);
    check(PunchTailProcessor::PhaseMap{}.notes.size()==size_t(pt::kPunchIndexCount),"Phase map lacks low lane slots");
    stop(p);
    // State: saved, legacy migration OFF, appended after pitchCharacter.
    set(p,"lowKeyPunch",0);juce::MemoryBlock saved;p.getStateInformation(saved);
    PunchTailProcessor restored;stop(restored);restored.setStateInformation(saved.getData(),int(saved.getSize()));
    check(restored.params.getRawParameterValue("lowKeyPunch")->load()<.5f,"Low lane OFF not restored");
    auto legacy=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
    for(int i=legacy.getNumChildren()-1;i>=0;--i)if(legacy.getChild(i).getProperty("id").toString()=="lowKeyPunch")legacy.removeChild(i,nullptr);
    juce::MemoryBlock old;{juce::MemoryOutputStream stream(old,false);legacy.writeToStream(stream);}
    set(restored,"lowKeyPunch",1);restored.setStateInformation(old.getData(),int(old.getSize()));
    check(restored.params.getRawParameterValue("lowKeyPunch")->load()<.5f,"Old projects must load with the low lane OFF");
    check(p.params.getParameter("lowKeyPunch")->getParameterIndex()>p.params.getParameter("pitchCharacter")->getParameterIndex(),"lowKeyPunch not appended");
 }
};

int main() try {
    juce::ScopedJuceInitialiser_GUI init;
    TailtoneKeyMappingTest::policy();
    TailtoneKeyMappingTest::harmonyPolicy();
    TailtoneKeyMappingTest::processor();
    std::cout<<"PASS keys one octave up (D#2-D#5 pitched, E4-D#5 punch lane), D2-C#3 octave-down Resample punch lane, note-off by key, phase slots, state/migration, punch harmony (third/fifth lanes)\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
