#pragma once
#include <algorithm>
#include <cmath>

namespace pt {
// The knob scales a continuous, asymmetric duration curve around the root.
// At full amount: +12 -> 50%, -12 -> 70.71%. The global 80-150% stretch
// remains the base duration. Up to 400% gives four times the adaptation
// strength, still bounded at 25% duration. Pitch is rendered by Extreme.
inline double adaptiveDurationRatio(double semitones,double amount) {
    if(!std::isfinite(semitones)||!std::isfinite(amount))return 1.;
    const double strength=std::clamp(amount,0.,400.)*.01;
    const double octaves=std::abs(semitones)/(semitones>=0?12.:24.);
    return (std::max)(.25,std::exp2(-strength*octaves));
}
}
