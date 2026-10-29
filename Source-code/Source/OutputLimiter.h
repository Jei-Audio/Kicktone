#pragma once
#include <algorithm>
#include <cmath>

namespace tt {
// v0.4.60 optional output safety limiter (replaces the hard 0 dBFS clip while ON).
// Stereo-linked, zero latency (no look-ahead, so host PDC and every exact
// note-boundary timing stay unchanged): the gain drops instantly to whatever
// keeps the frame at the ceiling, is HELD while the output keeps reaching the
// ceiling (so a steady loud bass cycle is scaled, not flattened), then recovers
// exponentially. Audio that never reaches the ceiling passes bit-identically
// (gain stays exactly 1). Switching OFF never jumps: a reduced gain releases
// back to 1. prepare() is called from prepareToPlay; process() allocates nothing.
class OutputLimiter {
public:
    static constexpr float kCeiling=0.96605088f;  // -0.3 dBFS
    static constexpr float kHoldZone=0.94f;       // within ~0.5 dB of the ceiling refreshes the hold
    static constexpr double kHoldMs=30.0;         // longer than half a 20 Hz cycle
    static constexpr double kReleaseMs=80.0;
    void prepare(double sampleRate) {
        const double sr=(std::max)(1.0,sampleRate);
        release=float(1.0-std::exp(-1.0/(kReleaseMs*.001*sr)));
        holdSamples=(std::max)(1,int(std::lround(kHoldMs*.001*sr)));
        reset();
    }
    void reset() { gain=1.f; reduction=0.f; hold=0; }
    // Limits one stereo frame in place and returns the applied gain.
    float process(float& l,float& r,bool enabled) {
        if(!enabled)hold=0;
        if(hold>0)--hold;
        else if(reduction>0.f){ // release tracked as 1-gain: keeps float precision near unity
            reduction*=1.f-release;
            if(reduction<1.e-5f)reduction=0.f;
            gain=1.f-reduction;
        }
        if(enabled) {
            const float peak=(std::max)(std::abs(l),std::abs(r));
            if(peak*gain>kCeiling){gain=kCeiling/peak;reduction=1.f-gain;}
            if(gain<1.f && peak*gain>kCeiling*kHoldZone)hold=holdSamples;
        }
        if(gain<1.f){l*=gain;r*=gain;}
        return gain;
    }
    float currentGain() const { return gain; }
private:
    float gain=1.f, reduction=0.f, release=.0003f;
    int hold=0, holdSamples=1440;
};
}
