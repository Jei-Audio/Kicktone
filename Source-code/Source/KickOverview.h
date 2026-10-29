#pragma once
#include "Engine.h"
#include "WaveformEnvelope.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace pt {
// Display-only preview of a COMPLETE one-shot kick at the selected root note.
// Uses the same Voice and parameters as audio playback; callers should pass a
// pre-pitched bank note if available. Designed to run ONLY outside the audio
// callback and only when sample data or relevant parameters change.
struct KickOverview {
    WaveformEnvelope envelope;
    double totalMs = 0.0;
    double punchEndMs = 0.0;
    double tailStartMs = 0.0;
    double tailEndMs = 0.0;
    double overlapMs = 0.0;
};

inline KickOverview buildKickOverview(const Sample* punch,
                                      const Sample* preparedTail,
                                      Settings params,
                                      int rootNote,
                                      size_t resolution = 900,
                                      const Sample* reverb = nullptr) {
    KickOverview result;
    if (resolution == 0 || (!punch && !preparedTail && !reverb)) return result;
    // The tail passed to this function is already pitched to rootNote, so
    // Voice should not transpose it again. Keep the actual source durations.
    params.root = rootNote;
    params.gate = false;
    Voice voice;
    voice.start(rootNote, 1, 1.0f, punch, preparedTail, params, reverb);
    result.punchEndMs = voice.punchEnd * 1000.0;
    result.tailStartMs = voice.tailStart * 1000.0;
    result.tailEndMs = voice.dryEnd * 1000.0; // v0.4.57: cut dry Tail end (== tailEnd uncut)
    result.overlapMs = voice.overlap * 1000.0;
    result.totalMs = voice.audibleEnd() * 1000.0;
    if (result.totalMs <= 0) return result;

    // Limit preview to 48 kHz; the plugin already downsamples files above
    // 48 kHz. The render is used ONLY for a graph, never as audible output.
    const double sampleRate = std::clamp((std::max)({punch ? punch->rate : 0.0,
                                                   preparedTail ? preparedTail->rate : 0.0,
                                                   reverb ? reverb->rate : 0.0}),
                                         8000.0, 48000.0);
    voice.setReverbFilter(makeReverbHPF(sampleRate,params.reverbLowCutSlope),sampleRate,true);
    const size_t frames = std::max<size_t>(1, size_t(std::ceil(result.totalMs * sampleRate / 1000.0)));
    const size_t bins = (std::min)(resolution, frames);
    result.envelope.bins.resize(bins);
    for (auto& bin : result.envelope.bins) {
        bin.minimum = (std::numeric_limits<float>::max)();
        bin.maximum = std::numeric_limits<float>::lowest();
    }
    for (size_t i = 0; i < frames; ++i) {
        const auto stereo = voice.next(punch, preparedTail, sampleRate, reverb);
        const float left = std::clamp(stereo[0], -1.0f, 1.0f);
        const float right = std::clamp(stereo[1], -1.0f, 1.0f);
        const size_t b = (std::min)(bins - 1, i * bins / frames);
        auto& bin = result.envelope.bins[b];
        bin.minimum = (std::min)({bin.minimum, left, right});
        bin.maximum = (std::max)({bin.maximum, left, right});
    }
    for (auto& bin : result.envelope.bins) {
        // Rounding near the end can leave a bin empty; show silence there.
        if (bin.minimum > bin.maximum) bin = {};
        result.envelope.peak = (std::max)({result.envelope.peak,
                                         std::abs(bin.minimum), std::abs(bin.maximum)});
    }
    return result;
}
} // namespace pt
