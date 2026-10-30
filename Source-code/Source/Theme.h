#pragma once
// v0.4.62 editor colour themes. Theme choice is a per-user preference (saved in
// the user's Tailtone settings file, not in projects) and applies to every open
// Tailtone window. Theme 0 "Charcoal" is exactly the pre-v0.4.62 look.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

namespace tt::ui {
struct Palette {
    const char* name;
    bool light;
    juce::Colour bg, panel, muted, text, title, amber, thinLine, violet;
    juce::Colour plot, grid, plotText, plotDim, button, buttonOn, combo, knobTrack, highlight, nodeText;
    juce::Colour attack, decay, envBg, envGrid, envCurve, ok, clip, limit, scrollThumb;
    juce::Colour punch2;   // v0.5.33: Punch 2 on the Punch waveform (purple)
    juce::Colour waveOff;  // v0.5.35: unplayed parts of the Punch / Tail waveforms (dark grey)
};
inline const std::array<Palette,5>& palettes() {
    using C=juce::Colour;
    static const std::array<Palette,5> p{{
        {"Charcoal",false, C(0xff242424),C(0xff353535),C(0xffc3c3c3),C(0xfff5f5f5),C(0xffffffff),C(0xffe1b477),C(0xff515151),C(0xffb5a6df),
         C(0xff1b1b1b),C(0xff393939),C(0xffbcbcbc),C(0xffaaaaaa),C(0xff4a4a4a),C(0xff646464),C(0xff414141),C(0xff666666),C(0xff686868),C(0xff202020),
         C(0xffe3b66f),C(0xff89baca),C(0xff131619),C(0xff33383d),C(0xffffbb65),C(0xffa6ddb0),C(0xff9f3939),C(0xff6b5530),C(0xff8a8a8a),C(0xffa678ee),C(0xff121212)},
        {"Black / white",false, C(0xff000000),C(0xff121212),C(0xffcfcfcf),C(0xfffafafa),C(0xffffffff),C(0xffe6b878),C(0xff3c3c3c),C(0xffbcaee6),
         C(0xff060606),C(0xff2b2b2b),C(0xffc4c4c4),C(0xff9c9c9c),C(0xff2a2a2a),C(0xff464646),C(0xff1f1f1f),C(0xff4c4c4c),C(0xff565656),C(0xff000000),
         C(0xffe6b878),C(0xff8cc0d2),C(0xff060606),C(0xff2b2b2b),C(0xffffbb65),C(0xffa6ddb0),C(0xffa83c3c),C(0xff6b5530),C(0xff8a8a8a),C(0xffab7cf4),C(0xff2c2c2c)},
        {"Grey / white",false, C(0xff55595e),C(0xff676b71),C(0xffe9ebee),C(0xfffcfcfc),C(0xffffffff),C(0xffffd28c),C(0xff868a90),C(0xffd8ccff),
         C(0xff3f4246),C(0xff5a5e63),C(0xffe2e4e7),C(0xffc5c8cc),C(0xff7b7f85),C(0xff94989e),C(0xff73777d),C(0xff8f9399),C(0xff9da1a7),C(0xff2a2c2f),
         C(0xffffd28c),C(0xffaee0f0),C(0xff3f4246),C(0xff5a5e63),C(0xffffcf86),C(0xffbff0c8),C(0xffb84545),C(0xff8a6d3a),C(0xffc8cbcf),C(0xffd2adff),C(0xff2a2c2f)},
        {"White / black",true, C(0xffeeeeee),C(0xffffffff),C(0xff4b4b4b),C(0xff141414),C(0xff000000),C(0xffa8640f),C(0xffcacaca),C(0xff5d47a8),
         C(0xffe6e6e6),C(0xffcdcdcd),C(0xff383838),C(0xff6a6a6a),C(0xffe2e2e2),C(0xffc9c9c9),C(0xfff3f3f3),C(0xffbdbdbd),C(0xffb9d3ef),C(0xfffefefe),
         C(0xffb06d1a),C(0xff2f7891),C(0xffe6e6e6),C(0xffcdcdcd),C(0xffb06d1a),C(0xff23843a),C(0xffe39a9a),C(0xfff0d39a),C(0xff8c8c8c),C(0xff7a3fc8),C(0xff9a9a9a)},
        {"Midnight / white",false, C(0xff131a27),C(0xff1e2739),C(0xffb8c3d8),C(0xfff3f6fb),C(0xffffffff),C(0xffe8bb7c),C(0xff3a4662),C(0xffb9abe6),
         C(0xff0d121c),C(0xff2a3449),C(0xffb6c1d6),C(0xff8d9ab2),C(0xff2d3851),C(0xff42517c),C(0xff27314a),C(0xff4a5779),C(0xff586a92),C(0xff131a27),
         C(0xffe8bb7c),C(0xff8fc4d6),C(0xff0d121c),C(0xff2a3449),C(0xffffbb65),C(0xffa6ddb0),C(0xffa53e4a),C(0xff6b5530),C(0xff8d9ab2),C(0xffaa7ff2),C(0xff0a0e16)},
    }};
    return p;
}
inline std::atomic<int>& currentThemeIndex() { static std::atomic<int> index{-1}; return index; }
inline std::atomic<int>& themeEpoch() { static std::atomic<int> epoch{0}; return epoch; }
inline juce::PropertiesFile::Options settingsOptions() {
    juce::PropertiesFile::Options o;
    o.applicationName="Kicktone";o.folderName="JeiAudio";o.filenameSuffix=".settings";
    o.osxLibrarySubFolder="Application Support";o.storageFormat=juce::PropertiesFile::storeAsXML;
    // v0.5.29: the plugin was renamed from Tailtone. The first time, copy the old
    // settings file (theme, custom defaults, export folder, background rendering).
    static const bool migrated=[o]{
        auto legacy=o;legacy.applicationName="Tailtone";
        const auto now=o.getDefaultFile(),old=legacy.getDefaultFile();
        return !now.existsAsFile()&&old.existsAsFile()&&old.copyFileTo(now);
    }();
    juce::ignoreUnused(migrated);
    return o;
}
// Optional override (tests): when set, the theme is neither read from nor saved to disk.
inline std::atomic<bool>& themeStorageDisabled() { static std::atomic<bool> off{false}; return off; }
inline int themeIndex() {
    int i=currentThemeIndex().load();
    if(i<0) {
        i=0;
        if(!themeStorageDisabled()) {
            juce::PropertiesFile file(settingsOptions());
            i=juce::jlimit(0,int(palettes().size())-1,file.getIntValue("theme",0));
        }
        currentThemeIndex()=i;
    }
    return i;
}
inline const Palette& pal() { return palettes()[size_t(themeIndex())]; }
inline void setTheme(int index) {
    index=juce::jlimit(0,int(palettes().size())-1,index);
    if(index==themeIndex())return;
    currentThemeIndex()=index;++themeEpoch();
    if(!themeStorageDisabled()) {
        juce::PropertiesFile file(settingsOptions());
        file.setValue("theme",index);file.saveIfNeeded();
    }
}
// v0.5.10 PREPARATION CPU: pt::parallel::Effort stored next to the theme
// (key "prepEffort"; default Quiet since v0.5.12, Balanced in v0.5.10..v0.5.11). Read by every new processor, written by
// the About page. themeStorageDisabled() keeps tests off the disk.
inline std::atomic<int>& prepEffortMemory() { static std::atomic<int> value{-1}; return value; }
inline int prepEffort() {
    int e=prepEffortMemory().load();
    if(e<0) {
        e=0;   // v0.5.12: Quiet (1 core) unless this computer saved another choice
        if(!themeStorageDisabled()) {
            juce::PropertiesFile file(settingsOptions());
            e=juce::jlimit(0,2,file.getIntValue("prepEffort",0));
        }
        prepEffortMemory()=e;
    }
    return e;
}
inline void setPrepEffort(int e) {
    e=juce::jlimit(0,2,e);
    prepEffortMemory()=e;
    if(!themeStorageDisabled()) {
        juce::PropertiesFile file(settingsOptions());
        file.setValue("prepEffort",e);file.saveIfNeeded();
    }
}
} // namespace tt::ui
