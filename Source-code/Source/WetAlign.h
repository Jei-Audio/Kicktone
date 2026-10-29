#pragma once
// v0.4.54 Root Render + Pitch: keep the Tail reverb from phasing out the tail.
// Worker-only, offline, applied ONCE to the root composite (dry + reverb); the
// composite is then pitched as one piece, so the result holds on every note.
//
// 1. WET AUTO MATCH: choose a small time shift (+-5 ms) and polarity for the
//    whole reverb so that, in the low end (< 300 Hz) where phasing hurts, the
//    reverb cancels the dry tail as little as possible. Scored per 50 ms window
//    along the tail: the worst window decides first (then total low energy),
//    and no setting is chosen unless it beats leaving the reverb as it is.
// 2. PHASE LOCK (optional): below 200 Hz the reverb keeps its own loudness over
//    time but takes the dry tail's phase cycle by cycle (analytic signal), so
//    its bass can only add to the kick's bass. Above 200 Hz the reverb is
//    untouched (zero-phase complementary split). Where the dry tail has ended
//    or is near-silent the original reverb low end is used (smooth blend).

#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <vector>

namespace pt::wetalign {
constexpr double kMatchHz = 300.0, kLockHz = 200.0, kMaxShiftMs = 5.0, kWindowMs = 50.0;

// v0.4.55: optional cancel checkpoint (throws to abort). Called between search
// steps and between FFT stages so a knob change stops the worker promptly.
using Checkpoint = std::function<void()>;

struct Match { long shift = 0; int polarity = 1; double score = 0, baseline = 0, worst = 1, baselineWorst = 1; };

inline void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * pi / double(len) * (inverse ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= double(n);
}
// 2nd-order Butterworth low-pass, forward + backward (zero phase).
inline std::vector<double> lowPass(const std::vector<float>& x, double hz, double rate) {
    std::vector<double> y(x.begin(), x.end());
    const double w = std::tan(pi * std::clamp(hz, 10.0, rate * .45) / rate), k = std::sqrt(2.0);
    const double n = 1.0 / (1.0 + k * w + w * w);
    const double b0 = w * w * n, b1 = 2 * b0, b2 = b0, a1 = 2 * (w * w - 1) * n, a2 = (1 - k * w + w * w) * n;
    auto pass = [&](bool reverse) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t c = 0; c < y.size(); ++c) {
            const size_t i = reverse ? y.size() - 1 - c : c;
            const double v = y[i], o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = v; y2 = y1; y1 = o; y[i] = o;
        }
    };
    pass(false); pass(true);
    return y;
}

// dry, wet: same length, already placed on the composite timeline (wet scaled).
inline Match findMatch(const Sample& dry, const Sample& wet, size_t dryFrames, const Checkpoint& checkpoint = {}) {
    Match m;
    const auto check = [&] { if (checkpoint) checkpoint(); };
    const double rate = dry.rate;
    const long maxShift = long(std::lround(kMaxShiftMs * .001 * rate));
    const size_t n = (std::min)(dryFrames, dry.left.size());
    if (n < size_t(rate * .02)) return m;
    std::array<std::vector<double>, 2> d, w;
    for (int ch = 0; ch < 2; ++ch) {
        d[size_t(ch)] = lowPass(ch ? dry.right : dry.left, kMatchHz, rate);
        w[size_t(ch)] = lowPass(ch ? wet.right : wet.left, kMatchHz, rate);
        check();
    }
    const size_t win = (std::max)(size_t(64), size_t(kWindowMs * .001 * rate));
    const size_t windows = n / win;
    if (windows == 0) return m;
    // Per window: dry energy, and for a shift: cross term and wet energy.
    std::vector<double> dryE(windows, 0);
    for (size_t k = 0; k < windows; ++k)
        for (int ch = 0; ch < 2; ++ch) for (size_t i = k * win; i < (k + 1) * win; ++i) dryE[k] += d[size_t(ch)][i] * d[size_t(ch)][i];
    double peakE = 0; for (double e : dryE) peakE = (std::max)(peakE, e);
    struct Score { double worst = 0, total = 0; };
    // worst = lowest (mix / dry) low-band energy ratio over the audible windows,
    // total = overall low energy. The worst window decides first, so the match
    // can never trade a hole somewhere for more bass elsewhere.
    const auto score = [&](long shift, int polarity) {
        Score sc; sc.worst = 1e9;
        for (size_t k = 0; k < windows; ++k) {
            double cross = 0, we = 0;
            for (int ch = 0; ch < 2; ++ch) {
                const auto& dd = d[size_t(ch)]; const auto& ww = w[size_t(ch)];
                for (size_t i = k * win; i < (k + 1) * win; ++i) {
                    const long j = long(i) - shift;
                    const double v = (j >= 0 && size_t(j) < ww.size()) ? ww[size_t(j)] * polarity : 0.0;
                    cross += dd[i] * v; we += v * v;
                }
            }
            const double mix = dryE[k] + 2 * cross + we;
            sc.total += mix;
            if (dryE[k] > peakE * 1e-3) sc.worst = (std::min)(sc.worst, mix / dryE[k]);
        }
        return sc;
    };
    const auto better = [](const Score& a, const Score& b) {
        const double wa = 10 * std::log10((std::max)(a.worst, 1e-9)), wb = 10 * std::log10((std::max)(b.worst, 1e-9));
        if (wa > wb + 0.02) return true;
        return std::abs(wa - wb) <= 0.02 && a.total > b.total * 1.0005;
    };
    Score bestScore = score(0, 1);
    m.baseline = bestScore.total; m.baselineWorst = bestScore.worst;
    const long step = (std::max)(1L, long(rate / 8000));
    long best = 0; int bestPol = 1;
    for (int pol : {1, -1})
        for (long s = -maxShift; s <= maxShift; s += step) {
            check();
            const auto v = score(s, pol);
            if (better(v, bestScore)) { bestScore = v; best = s; bestPol = pol; }
        }
    for (long s = best - step; s <= best + step; ++s) {
        if (s < -maxShift || s > maxShift || (s == best)) continue;
        check();
        const auto v = score(s, bestPol);
        if (better(v, bestScore)) { bestScore = v; best = s; }
    }
    m.score = bestScore.total; m.worst = bestScore.worst;
    m.shift = best; m.polarity = bestPol;
    return m;
}

// Shift (positive = later) and polarity-flip a placed wet signal in place.
inline void applyMatch(Sample& wet, const Match& m) {
    for (auto* ch : {&wet.left, &wet.right}) {
        std::vector<float> out(ch->size(), 0.f);
        for (size_t i = 0; i < ch->size(); ++i) {
            const long j = long(i) - m.shift;
            if (j >= 0 && size_t(j) < ch->size()) out[i] = (*ch)[size_t(j)] * float(m.polarity);
        }
        *ch = std::move(out);
    }
}

inline std::vector<std::complex<double>> analytic(const std::vector<double>& x, size_t n) {
    std::vector<std::complex<double>> z(n);
    for (size_t i = 0; i < x.size(); ++i) z[i] = x[i];
    fft(z, false);
    for (size_t k = 1; k < n / 2; ++k) z[k] *= 2.0;
    for (size_t k = n / 2 + 1; k < n; ++k) z[k] = 0;
    fft(z, true);
    return z;
}

// Phase lock: wet low band follows the dry low band's phase.
inline void phaseLock(Sample& wet, const Sample& dry, size_t dryFrames, const Checkpoint& checkpoint = {}) {
    const auto check = [&] { if (checkpoint) checkpoint(); };
    const double rate = wet.rate;
    const size_t len = wet.left.size();
    size_t n = 1; while (n < 2 * len) n <<= 1;
    const size_t fade = (std::max)(size_t(16), size_t(rate * .020));
    for (int ch = 0; ch < 2; ++ch) {
        auto& w = ch ? wet.right : wet.left;
        const auto& d = ch ? dry.right : dry.left;
        const auto wl = lowPass(w, kLockHz, rate);
        const auto dl = lowPass(d, kLockHz, rate);
        check();
        const auto aw = analytic(wl, n);
        check();
        const auto ad = analytic(dl, n);
        check();
        double peak = 0;
        for (size_t i = 0; i < (std::min)(len, dryFrames); ++i) peak = (std::max)(peak, std::abs(ad[i]));
        if (peak <= 0) continue;
        for (size_t i = 0; i < len; ++i) {
            const double magD = std::abs(ad[i]);
            // Blend toward the original low band where the dry tail is absent.
            double b = std::clamp(magD / (0.05 * peak), 0.0, 1.0);
            if (i + fade >= dryFrames) b *= std::clamp(double(dryFrames > i ? dryFrames - i : 0) / double(fade), 0.0, 1.0);
            const double locked = magD > 1e-12 ? std::abs(aw[i]) * ad[i].real() / magD : 0.0;
            const double low = b * locked + (1 - b) * wl[i];
            w[i] = float(double(w[i]) - wl[i] + low);   // high band untouched
        }
    }
}
} // namespace pt::wetalign
