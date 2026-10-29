#include "../Source/KickOverview.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static pt::Sample makeSample(int count, int rate, float amplitude) {
    pt::Sample sample;
    sample.rate = rate;
    sample.left.resize(size_t(count));
    sample.right.resize(size_t(count));
    for (int i = 0; i < count; ++i) {
        const auto v = amplitude * std::sin(float(2.0 * pt::pi * 70.0 * i / rate));
        sample.left[size_t(i)] = v;
        sample.right[size_t(i)] = v;
    }
    return sample;
}
static void closeEnough(double a, double b) { assert(std::abs(a-b) < 0.11); }
int main() {
    auto punch = makeSample(4410, 44100, 0.7f); // 100ms
    auto tail = makeSample(17640, 44100, 0.55f); // 400ms
    pt::Settings s;
    s.root = 36;
    s.crossfadeMs = 8;
    const auto full = pt::buildKickOverview(&punch,&tail,s,36,880);
    closeEnough(full.punchEndMs,100.0);
    closeEnough(full.tailStartMs,92.0);
    closeEnough(full.tailEndMs,492.0);
    closeEnough(full.totalMs,492.0);
    closeEnough(full.overlapMs,8.0);
    assert(full.envelope.bins.size() == 880);
    assert(full.envelope.peak > 0.3f && full.envelope.peak <= 1.0f);
    s.trimMs = 30.0;
    s.joinMs = 10.0;
    const auto adjusted = pt::buildKickOverview(&punch,&tail,s,36);
    closeEnough(adjusted.tailStartMs,102.0);
    closeEnough(adjusted.totalMs,472.0);
    closeEnough(adjusted.overlapMs,0.0);
    auto onlyPunch = pt::buildKickOverview(&punch,nullptr,s,36);
    closeEnough(onlyPunch.totalMs,100.0);
    auto onlyTail = pt::buildKickOverview(nullptr,&tail,s,36);
    closeEnough(onlyTail.totalMs,380.0); // 30ms trimmed tail with 10ms join starts at 10ms
    // Punch-end trim must shorten the punch (not delay its onset), and the
    // tail must still blend at the new end of the punch.
    s = pt::Settings{};
    s.punchEndOffsetMs = 20.0;
    const auto punchShortened = pt::buildKickOverview(&punch,&tail,s,36);
    closeEnough(punchShortened.punchEndMs,80.0);
    closeEnough(punchShortened.tailStartMs,72.0);
    closeEnough(punchShortened.totalMs,472.0);
    closeEnough(punchShortened.overlapMs,8.0);

    // Both offset controls allow 600 ms. If the offset exceeds its sample,
    // the affected part becomes silent safely, with the other part playable.
    s.punchEndOffsetMs = 600.0;
    const auto noPunch = pt::buildKickOverview(&punch,&tail,s,36);
    closeEnough(noPunch.punchEndMs,0.0);
    closeEnough(noPunch.tailStartMs,0.0);
    closeEnough(noPunch.totalMs,400.0);
    s.punchEndOffsetMs = 0;
    s.trimMs = 600.0;
    const auto noTail = pt::buildKickOverview(&punch,&tail,s,36);
    closeEnough(noTail.punchEndMs,100.0);
    closeEnough(noTail.tailEndMs,100.0);
    closeEnough(noTail.totalMs,100.0);

    // Verify 600ms of trimming on longer samples without quantization drift.
    const auto longPunch = makeSample(44100,44100,0.7f);
    const auto longTail = makeSample(44100,44100,0.55f);
    s.punchEndOffsetMs = 600;
    const auto both600 = pt::buildKickOverview(&longPunch,&longTail,s,36);
    closeEnough(both600.punchEndMs,400.0);
    closeEnough(both600.tailStartMs,392.0);
    closeEnough(both600.tailEndMs,792.0);
    closeEnough(both600.totalMs,792.0);
    s = pt::Settings{};
    auto missing = pt::buildKickOverview(nullptr,nullptr,s,36);
    assert(missing.envelope.bins.empty());
    s.output = 0;
    auto muted = pt::buildKickOverview(&punch,&tail,s,36);
    assert(muted.envelope.peak == 0.0f);
    const auto disabled = pt::buildKickOverview(&punch,&tail,s,36,0);
    assert(disabled.envelope.bins.empty());
    std::cout << "Complete kick overview tests passed.\n";
}
