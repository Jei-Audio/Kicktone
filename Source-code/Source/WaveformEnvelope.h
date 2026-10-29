#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace pt {
struct WaveformBin {
    float minimum = 0.0f;
    float maximum = 0.0f;
};
struct WaveformEnvelope {
    std::vector<WaveformBin> bins;
    float peak = 0.0f;
};

// Computes a drawable peak envelope from immutable decoded PCM. Stereo channels
// are combined by extrema (never simply averaged, which could cancel them).
// Call this on the GUI thread when an asset changes, not in processBlock().
inline WaveformEnvelope buildWaveformEnvelope(const std::vector<float>& left,
                                               const std::vector<float>& right,
                                               size_t resolution = 1024) {
    WaveformEnvelope out;
    if (left.empty() || resolution == 0) return out;
    out.bins.resize((std::min)(left.size(), resolution));
    for (size_t b = 0; b < out.bins.size(); ++b) {
        const size_t first = b * left.size() / out.bins.size();
        const size_t last = (std::max)(first + 1, (b + 1) * left.size() / out.bins.size());
        float lo = (std::numeric_limits<float>::max)();
        float hi = std::numeric_limits<float>::lowest();
        for (size_t i = first; i < last; ++i) {
            const float a = std::isfinite(left[i]) ? left[i] : 0.0f;
            const float c = i < right.size() && std::isfinite(right[i]) ? right[i] : a;
            lo = (std::min)({lo, a, c});
            hi = (std::max)({hi, a, c});
        }
        out.bins[b] = {lo, hi};
        out.peak = (std::max)({out.peak, std::abs(lo), std::abs(hi)});
    }
    return out;
}
} // namespace pt
