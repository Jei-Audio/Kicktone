#pragma once
// v0.4.51 (test): punch -> reverb sidechain ducking. While the dry Punch is
// loud, every reverb return (generated Tail + Punch wet and the recorded
// Reverb layer) is turned down, so the attack stays clean. Fixed-cost,
// allocation-free, stereo-linked; used by processBlock and the preview worker.
#include <algorithm>
#include <cmath>

namespace pt {
struct PunchReverbDuck {
    // depthPercent: 0 = off (exact bypass), 100 = up to -24 dB.
    void prepare(double sampleRate) { rate = sampleRate > 0 ? sampleRate : 44100.0; attack = coefficient(0.3); reset(); }
    void setSettings(float depthPercent, float releaseMs) {
        depth = std::clamp(depthPercent, 0.f, 100.f) * .01f;
        release = coefficient(std::clamp(double(releaseMs), 5.0, 1000.0));
    }
    void reset() { env = 0; }
    bool active() const { return depth > 0.f; }
    // Returns the gain for the reverb returns at this sample.
    float process(float punchL, float punchR) {
        const float level = (std::max)(std::abs(punchL), std::abs(punchR));
        env = level > env ? attack * env + (1.f - attack) * level : release * env + (1.f - release) * level;
        if (depth <= 0.f) return 1.f;
        // Punch at or above -12 dBFS ducks fully; quieter punch ducks less.
        const float amount = (std::min)(1.f, env / kFullScaleLevel);
        return std::pow(10.f, -kMaxDuckDb * depth * amount / 20.f);
    }
    static constexpr float kMaxDuckDb = 24.f;
    static constexpr float kFullScaleLevel = 0.2511886f; // -12 dBFS
private:
    float coefficient(double ms) const { return float(std::exp(-1.0 / (ms * .001 * rate))); }
    double rate = 44100.0;
    float depth = 0, env = 0, attack = 0, release = 0;
};
}
