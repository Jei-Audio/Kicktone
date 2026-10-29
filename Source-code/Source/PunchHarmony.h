#pragma once
// v0.5.10 PUNCH HARMONY (user request): the punch can follow the played key at
// a musical interval ABOVE it while the tail keeps playing the bass note.
//   Off            - the existing punch pitch behaviour, untouched.
//   Diatonic Third - +3 or +4 semitones, whichever third of the selected KEY
//                    and SCALE the played note has (a note outside the scale
//                    takes the third that lands IN the scale, minor first;
//                    in harmonic minor's one gap the nearest scale note).
//   Perfect Fifth  - always +7 semitones, key and scale ignored.
// The interval is computed from each incoming note's pitch class alone, so
// nothing accumulates between notes. It is added to the per-key punch shift
// (PUNCH PITCH + key - root) when the punch lanes are prepared on the worker,
// so Resample / Stretch Aligned / Highest Fidelity, the Punch 2 decay-only
// follow and the D2-C#3 octave-down lane all keep working exactly as before;
// only the semitone amount per key changes. Portable: no JUCE.
#include <array>
#include <cstddef>

namespace pt { namespace harmony {
enum Mode { Off = 0, DiatonicThird = 1, PerfectFifth = 2 };
constexpr int kModeCount = 3;
constexpr const char* kModeNames[kModeCount] = { "Off", "Diatonic Third", "Perfect Fifth" };
constexpr int kKeyCount = 12;
constexpr const char* kKeyNames[kKeyCount] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
// Seven-note scales as semitone offsets from the key note.
struct Scale { const char* name; std::array<int,7> steps; };
constexpr int kScaleCount = 8;
constexpr Scale kScales[kScaleCount] = {
    { "Major (Ionian)",   {{0,2,4,5,7,9,11}} },
    { "Natural Minor",    {{0,2,3,5,7,8,10}} },
    { "Harmonic Minor",   {{0,2,3,5,7,8,11}} },
    { "Melodic Minor",    {{0,2,3,5,7,9,11}} },
    { "Dorian",           {{0,2,3,5,7,9,10}} },
    { "Phrygian",         {{0,1,3,5,7,8,10}} },
    { "Lydian",           {{0,2,4,6,7,9,11}} },
    { "Mixolydian",       {{0,2,4,5,7,9,10}} },
};
inline int pitchClass(int note) { return ((note % 12) + 12) % 12; }
inline bool inScale(int scale, int key, int note) {
    const auto& s = kScales[static_cast<size_t>(scale < 0 ? 0 : scale >= kScaleCount ? kScaleCount-1 : scale)];
    const int pc = pitchClass(note - key);
    for (int step : s.steps) if (step == pc) return true;
    return false;
}
// Semitones to add to the punch for a played note (0 when Off).
inline int interval(int mode, int key, int scale, int note) {
    if (mode == PerfectFifth) return 7;
    if (mode != DiatonicThird) return 0;
    key = pitchClass(key);
    const int sc = scale < 0 ? 0 : scale >= kScaleCount ? kScaleCount-1 : scale;
    const auto& steps = kScales[static_cast<size_t>(sc)].steps;
    const int pc = pitchClass(note - key);
    for (size_t d = 0; d < steps.size(); ++d)
        if (steps[d] == pc) {                                  // scale degree d: its diatonic third
            const int third = steps[(d+2) % steps.size()] + (d+2 >= steps.size() ? 12 : 0) - steps[d];
            return third;                                       // 3 or 4 for every scale above
        }
    // Chromatic note (no scale degree): the third that lands in the scale,
    // minor first. Harmonic minor has one gap where neither does (F# in C
    // harmonic minor): then the nearest scale note to a third (+2, else +5).
    for (int candidate : {3, 4, 2, 5}) if (inScale(sc, key, note+candidate)) return candidate;
    return 3;
}
} }
