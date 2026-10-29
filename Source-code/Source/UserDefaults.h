#pragma once
// v0.5.3 CUSTOM DEFAULTS (user request: "create custom default values ... keep the
// ones default as it is now, but in the about page allow people to adjust all the
// default values").
//
// The factory defaults stay exactly as they are (each parameter's own default).
// A user can store their own start values per computer, in the same settings file
// as the theme (JeiAudio/Tailtone.settings, key "defaults"). They are applied ONLY
// to a NEW plugin instance, right after it is created; a project or preset that is
// opened afterwards sets every parameter it contains, and parameters missing from
// older projects still get their legacy values from setStateInformation's
// ensure(). Values are stored as real (denormalised) parameter values by ID.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include <map>

namespace tt::defaults {
using Map = std::map<juce::String,float>;

// Tests: keep the store in memory instead of the user's settings file.
inline std::atomic<bool>& storageDisabled() { static std::atomic<bool> off{false}; return off; }
inline Map& memoryStore() { static Map m; return m; }

inline bool storable(const juce::String& id);

// Only storable entries are returned, so values saved by an older version that are
// no longer allowed (v0.5.30: EQ) are ignored, and dropped at the next save.
inline Map load() {
    Map m;
    if(storageDisabled()){for(const auto& [id,v]:memoryStore())if(storable(id))m[id]=v;return m;}
    juce::PropertiesFile file(tt::ui::settingsOptions());
    if(auto xml=file.getXmlValue("defaults"))
        for(auto* p:xml->getChildIterator())
            if(p->hasTagName("P") && p->hasAttribute("id") && storable(p->getStringAttribute("id")))
                m[p->getStringAttribute("id")]=float(p->getDoubleAttribute("v"));
    return m;
}
inline void save(const Map& m) {
    if(storageDisabled()){memoryStore()=m;return;}
    juce::PropertiesFile file(tt::ui::settingsOptions());
    if(m.empty())file.removeValue("defaults");
    else {
        juce::XmlElement xml("DEFAULTS");
        for(const auto& [id,v]:m){auto* p=xml.createNewChildElement("P");p->setAttribute("id",id);p->setAttribute("v",double(v));}
        file.setValue("defaults",&xml);
    }
    file.saveIfNeeded();
}
inline void setValue(const juce::String& id,float v){auto m=load();m[id]=v;save(m);}
inline void clear(){save({});}

// v0.5.30 (user request: "the EQ is not neutral at start"): every EQ starts neutral
// in a new instance, so no EQ band and no EQ on/off switch is a custom default.
// Matches the band parameters <eq><n><suffix> and the switches <eq>On of the Tail,
// Punch, recorded Reverb, Tail wet, Punch wet, Attack and Decay EQs. EQ AFTER
// DISTORTION (punchEqPost / tailEqPost) is a routing choice, not a curve: it stays.
inline bool isEq(const juce::String& id) {
    for(const char* eq:{"eq","punchEq","reverbEq","tailVerbEq","punchVerbEq","punchAttackEq","punchDecayEq"}) {
        if(!id.startsWith(eq))continue;
        const auto rest=id.substring(juce::String(eq).length());
        if(rest=="On" || (rest.isNotEmpty() && juce::CharacterFunctions::isDigit(rest[0])))return true;
    }
    return false;
}

// Parameters that "save current settings as defaults" leaves out: values that
// belong to one sample (root, trims, offsets, output trims), editor page choices,
// the inert placeholders of removed engines and (v0.5.30) every EQ.
inline bool storable(const juce::String& id) {
    static const juce::StringArray excluded{"root","cents","trim","punchOffset","punchExtra","tailExtra","reverbOffset","reverbExtra",
        "tailEndTrim","join","punchNormalize","tailNormalize","punchAttackNormalize","punchDecayNormalize","punch2Offset",
        "eqView","eqGeneratedView","eqPunchGeneratedView","punchRegionView",
        "pitchQuality","pitchExtreme","pitchCrunch","pitchCharacter","formantPreserve","crunchMatch","glideStretched"};
    return !excluded.contains(id) && !isEq(id);
}

// The value a new instance starts with: the stored custom value, else the factory default.
inline float effective(juce::AudioProcessorValueTreeState& s,const Map& m,const juce::String& id) {
    if(auto it=m.find(id);it!=m.end())return it->second;
    if(auto* p=s.getParameter(id))return p->convertFrom0to1(p->getDefaultValue());
    return 0.f;
}
inline float factory(juce::AudioProcessorValueTreeState& s,const juce::String& id) {
    if(auto* p=s.getParameter(id))return p->convertFrom0to1(p->getDefaultValue());
    return 0.f;
}
// Apply stored values to a fresh instance (unknown IDs are ignored, values clamped by the parameter).
inline int apply(juce::AudioProcessorValueTreeState& s,const Map& m) {
    int applied=0;
    for(const auto& [id,v]:m)if(auto* p=s.getParameter(id)){
        if(!std::isfinite(v))continue;
        p->setValueNotifyingHost(p->convertTo0to1(v));++applied;}
    return applied;
}
// Every storable parameter's current value.
inline Map capture(juce::AudioProcessorValueTreeState& s,juce::AudioProcessor& processor) {
    Map m;
    for(auto* parameter:processor.getParameters())
        if(auto* p=dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if(storable(p->paramID))m[p->paramID]=p->convertFrom0to1(p->getValue());
    (void)s;return m;
}
} // namespace tt::defaults
