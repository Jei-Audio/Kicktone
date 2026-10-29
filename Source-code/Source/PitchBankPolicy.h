#pragma once
#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace pt {
// Always retain an audible tail even if pitch analysis or one note fails.
inline void fillDryPitchBank(std::array<Sample,128>& notes,const Sample& source) {
    for(auto& note:notes) note=source;
}
// FL Studio's octave numbering: MIDI 60 = C5, so D#1=15 and D#4=51.
// Inclusive range: 37 notes.  Keep the original source audible everywhere
// else, but only spend offline processing time on this playable bass range.
constexpr int kLowestPitchNote = 15;   // FL Studio D#1
constexpr int kHighestPitchNote = 51;  // FL Studio D#4
constexpr int kPitchNoteCount = kHighestPitchNote - kLowestPitchNote + 1;
constexpr int kLowestPunchPitchNote = 40; // FL Studio E3
inline bool followsPunchMidi(int note) { return note >= 40 && note <= 51; }
inline double punchPitchForNote(int note, double root, double manual) {
    return manual + (followsPunchMidi(note) ? note-root : 0.0);
}
// v0.4.67 PUNCH FOLLOWS TAIL: the punch is pitched with the tail on EVERY
// pitched tail note (internal 15..51 = keys D#2..D#5), not only E4..D#5.
// OFF keeps the v0.4.66 lane exactly (40..51).
inline bool followsPunchMidi(int note, bool wholeTailRange) {
    return wholeTailRange ? (note >= kLowestPitchNote && note <= kHighestPitchNote) : followsPunchMidi(note);
}
inline double punchPitchForNote(int note, double root, double manual, bool wholeTailRange) {
    return manual + (followsPunchMidi(note, wholeTailRange) ? note-root : 0.0);
}
// v0.4.48 keyboard mapping. Every played key sounds like the key one octave
// below did before (internal note = key - 12): the pitched range D#1..D#4
// (internal 15..51) is now played on keys D#2..D#5, the punch lane E3..D#4
// (internal 40..51) on keys E4..D#5. Banks and the punch lane stay internal.
constexpr int kKeyOctaveShift = 12;
// Optional low lane on keys D2..C#3 (MIDI 26..37; v0.4.49, was D1..C#2): the
// tail plays as the key one octave up (internal note == key) and the punch is
// Resampled one octave below the tail, per key, plus manual Punch Pitch.
// D1..C#2 play like every other key again.
constexpr int kLowPunchFirstKey = 26;  // FL Studio D2
constexpr int kLowPunchLastKey = 37;   // FL Studio C#3
constexpr int kLowPunchSlot = 128;     // punch indices 128..139 select the low lane
constexpr int kPunchIndexCount = 140;
inline bool isLowPunchKey(int key) { return key >= kLowPunchFirstKey && key <= kLowPunchLastKey; }
inline int tailNoteForKey(int key, bool lowLane) {
    if(lowLane && isLowPunchKey(key)) return key;
    return std::clamp(key - kKeyOctaveShift, 0, 127);
}
inline int punchIndexForKey(int key, bool lowLane) {
    if(lowLane && isLowPunchKey(key)) return kLowPunchSlot + key - kLowPunchFirstKey;
    return tailNoteForKey(key, false);
}
// Tail note heard with a punch index (low lane: the key itself).
inline int tailNoteForPunchIndex(int index) {
    return index >= kLowPunchSlot ? kLowPunchFirstKey + index - kLowPunchSlot : index;
}
inline double lowLanePunchPitch(int key, double root, double manual) {
    return manual + (double(key) - root) - 12.0;
}
inline bool shouldRenderPitch(int midiNote,double /*root*/) {
    return midiNote >= kLowestPitchNote && midiNote <= kHighestPitchNote;
}
inline bool usablePitchResult(const Sample& source,const Sample& rendered, double stretch = 1.0, float preparedSourcePeak = -1.f) {
    if(rendered.rate!=source.rate || rendered.left.size()!=size_t(std::llround(source.left.size()*stretch)) ||
       rendered.right.size()!=rendered.left.size()) return false;
    float inPeak=(std::max)(0.f,preparedSourcePeak), outPeak=0;
    if(preparedSourcePeak<0)for(size_t i=0;i<source.left.size();++i)
        inPeak=(std::max)({inPeak,std::abs(source.left[i]),std::abs(source.right[i])});
    for(size_t i=0;i<rendered.left.size();++i) {
        if(!std::isfinite(rendered.left[i]) || !std::isfinite(rendered.right[i])) return false;
        outPeak=(std::max)({outPeak,std::abs(rendered.left[i]),std::abs(rendered.right[i])});
    }
    // Detect accidental silence without penalizing genuinely quiet tails.
    return inPeak<1e-8f || outPeak >= inPeak*1e-4f;
}
}
