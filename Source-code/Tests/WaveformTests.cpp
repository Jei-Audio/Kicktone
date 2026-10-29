#include "../Source/WaveformEnvelope.h"
#include <stdexcept>
#include <cmath>
#include <vector>

static void check(bool ok) { if (!ok) throw std::runtime_error("Waveform envelope test failed"); }

int main() {
    using pt::buildWaveformEnvelope;
    const auto empty = buildWaveformEnvelope({}, {});
    check(empty.bins.empty() && empty.peak == 0.0f);

    const std::vector<float> left {0, 0.5f, -0.5f, 0, 0, 0, 0, 0};
    const std::vector<float> right {0, -0.9f, 0.1f, 0, 0, 0, 0, 0};
    const auto result = buildWaveformEnvelope(left, right, 4);
    check(result.bins.size() == 4);
    check(std::abs(result.peak - 0.9f) < 0.00001f);
    check(std::abs(result.bins[0].minimum + 0.9f) < 0.00001f);
    check(std::abs(result.bins[0].maximum - 0.5f) < 0.00001f);
    check(std::abs(result.bins[1].minimum + 0.5f) < 0.00001f);
    check(result.bins[3].maximum == 0);

    const auto silence = buildWaveformEnvelope(std::vector<float>(20000, 0), {}, 1024);
    check(silence.bins.size() == 1024);
    check(silence.peak == 0.0f);
    for (const auto& bin : silence.bins) check(bin.minimum == 0 && bin.maximum == 0);

    // Channels with opposite phase must not cancel in the displayed waveform.
    const auto opposite = buildWaveformEnvelope({1.0f, 1.0f}, {-1.0f, -1.0f}, 1);
    check(opposite.peak == 1.0f);
    check(opposite.bins[0].minimum == -1.0f && opposite.bins[0].maximum == 1.0f);

    // One displayed bin for every sample in a tiny sample; finite values only.
    const auto small = buildWaveformEnvelope({NAN, 1.0f}, {}, 1024);
    check(small.bins.size() == 2);
    check(small.bins[0].minimum == 0 && small.bins[0].maximum == 0);
    check(small.bins[1].maximum == 1);
    return 0;
}
