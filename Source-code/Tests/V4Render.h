#pragma once
// v0.4.61: Legacy v1, the old High Fidelity and Extreme tail engines were removed
// from Tailtone. Highest Fidelity v4 (Kick Character) is the only tail engine, so
// the historical pitch/stretch regressions now exercise it through these helpers.
#include "../Source/KickCharacterPitch.h"
namespace v4 {
inline pt::Sample render(const pt::Sample& x,double semitones,const pt::cycle::Analysis& analysis,double stretch=1.0) {
    if(std::abs(semitones)<1.e-5 && std::abs(stretch-1.0)<1.e-9)return x; // exact root
    return pt::character::render(x,semitones,pt::character::prepare(x,analysis,stretch));
}
inline pt::Sample pitchShift(const pt::Sample& x,double semitones,const pt::cycle::Analysis& analysis) {
    return render(x,semitones,analysis);
}
inline pt::Sample pitchShift(const pt::Sample& x,double semitones) {
    if(std::abs(semitones)<1.e-5)return x;
    return render(x,semitones,pt::cycle::analyze(x)); // analyze() rejects non-periodic input
}
}
