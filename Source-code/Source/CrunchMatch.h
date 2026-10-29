#pragma once
// v0.4.52/53 Auto Crunch Match (replaces the v0.4.51 FORMANT test).
//
// Pitching a distorted tail down makes it darker: its crunch moves down with
// the note. Auto Crunch Match measures how bright/crunchy the ROOT tail is and
// gives every pitched note the same amount by driving the COMPLETE pitched
// tail through a saturation stage — no band split, no noise layer, no EQ.
// The drive is searched per note so the note's crunch (share of energy above
// 3 kHz) reaches the root's; notes that are already as bright as the root are
// left untouched. The saturation runs at 4x resolution (anti-aliased), the
// 40 ms loudness envelope of the note is restored afterwards (so decay, level
// and duration stay the same) and DC is removed with a zero-phase filter.
// Worker-only, offline, deterministic.

#include "PitchShift.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace pt::crunchmatch {
enum class Character { Soft = 0, Hard = 1, Asymmetric = 2 };

constexpr double kBrightHz = 3000.0;
constexpr double kMinDrive = 0.25, kMaxDrive = 64.0;

// Share of energy above 3 kHz (linear ratio), ignoring the first 5% (onset).
inline double brightness(const Sample& s) {
    if (s.left.empty() || s.rate <= 0) return 0;
    const double a = std::exp(-2 * pi * kBrightHz / s.rate);
    double hf = 0, all = 0;
    const size_t from = s.left.size() / 20;
    for (int ch = 0; ch < 2; ++ch) {
        const auto& x = ch == 0 ? s.left : s.right;
        double l1 = 0, l2 = 0, l3 = 0, l4 = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            // 4-pole high-pass = input minus 4-pole low-pass cascade
            double v = x[i];
            l1 = a * l1 + (1 - a) * v; v -= l1;
            l2 = a * l2 + (1 - a) * v; v -= l2;
            l3 = a * l3 + (1 - a) * v; v -= l3;
            l4 = a * l4 + (1 - a) * v; v -= l4;
            if (i >= from) { hf += v * v; all += double(x[i]) * x[i]; }
        }
    }
    return all > 0 ? hf / all : 0;
}
inline double brightnessDb(const Sample& s) { return 10 * std::log10(brightness(s) + 1e-15); }
// v0.4.60: mean energy per sample (both channels), ignoring the first 5% like brightness().
inline double energy(const Sample& s) {
    if (s.left.empty()) return 0;
    const size_t from = s.left.size() / 20; double e = 0; size_t n = 0;
    for (size_t i = from; i < s.left.size(); ++i, ++n) e += double(s.left[i]) * s.left[i] + double(s.right[i]) * s.right[i];
    return n ? e / double(n) : 0;
}

inline float shape(double v, Character c) {
    switch (c) {
        case Character::Hard: { const double k = 1.0 / (1.0 + std::exp(-12.0 * (std::abs(v) - 1.0))); // soft knee
                                const double clipped = std::clamp(v, -1.0, 1.0); return float(clipped * k + std::tanh(v) * (1 - k)); }
        case Character::Asymmetric: return float(std::tanh(v + 0.25) - std::tanh(0.25));
        default: return float(std::tanh(v));
    }
}

// Saturate the complete stereo tail with drive (input gain relative to its
// peak), at 4x resolution, then restore the 40 ms loudness envelope and DC.
inline Sample saturate(const Sample& in, double drive, Character character) {
    float peak = 0;
    for (size_t i = 0; i < in.left.size(); ++i) peak = (std::max)({peak, std::abs(in.left[i]), std::abs(in.right[i])});
    if (peak <= 0) return in;
    constexpr double over = 4.0;
    Sample out; out.rate = in.rate;
    for (int ch = 0; ch < 2; ++ch) {
        const auto& x = ch == 0 ? in.left : in.right;
        auto up = cycle::resample(x, in.rate, 1.0 / over);
        const double g = drive / peak;
        for (auto& v : up) v = shape(v * g, character) * float(peak / drive);
        auto down = cycle::resample(up, in.rate * over, over);
        down.resize(x.size(), 0.f);
        (ch == 0 ? out.left : out.right) = std::move(down);
    }
    // Restore the original loudness envelope: per-sample gain from centred
    // 40 ms sliding-window energies (longer than a bass cycle), so saturation
    // never changes the decay or the level, even across fast envelope steps.
    const size_t n = in.left.size(), half = (std::max)(size_t(8), size_t(in.rate * .020));
    std::vector<double> pin(n + 1, 0.0), pout(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) {
        pin[i + 1] = pin[i] + double(in.left[i]) * in.left[i] + double(in.right[i]) * in.right[i];
        pout[i + 1] = pout[i] + double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
    }
    for (size_t i = 0; i < n; ++i) {
        const size_t a0 = i > half ? i - half : 0, b0 = (std::min)(n, i + half + 1);
        const double ei = pin[b0] - pin[a0], eo = pout[b0] - pout[a0];
        const double g = eo > 1e-20 ? std::sqrt(ei / eo) : 1.0;
        out.left[i] = float(out.left[i] * g); out.right[i] = float(out.right[i] * g);
    }
    // Zero-phase removal of only the DC / sub-8 Hz offset that saturation
    // ADDED (output's very-low band minus the input's), after the restore.
    const double a = std::exp(-2 * pi * 8.0 / in.rate);
    const auto veryLow = [&](const std::vector<float>& x) {
        std::vector<double> lp(x.size());
        double st = 0; for (size_t i = 0; i < x.size(); ++i) { st = a * st + (1 - a) * x[i]; lp[i] = st; }
        st = 0; for (size_t i = x.size(); i-- > 0;) { st = a * st + (1 - a) * lp[i]; lp[i] = st; }
        return lp;
    };
    for (int ch = 0; ch < 2; ++ch) {
        auto& o = ch == 0 ? out.left : out.right;
        const auto lo = veryLow(o), li = veryLow(ch == 0 ? in.left : in.right);
        for (size_t i = 0; i < o.size(); ++i) o[i] = float(o[i] - (lo[i] - li[i]));
    }
    return out;
}

// ---------------------------------------------------------------------------
// v0.4.53 parallel crunch layer (replaces the v0.4.52 in-place match).
//
// layer = HP_lowcut( saturate(note, drive) ), zero phase, in phase with the kick.
// Playback adds note + MIX * layer: the saturated highs are added on top of the
// clean kick, everything below LOW CUT stays clean. The layer is
// computed per note on the worker; MIX is applied live (no re-preparation).
// The root note uses a fixed reference drive; every other note gets the drive
// that brings its brightness (with the layer at 100%) to the root's, never
// less than the root's drive: every note gets crunch, darker notes get more.
constexpr double kRootDrive = 4.0;
constexpr double kMinLowCutHz = 2000.0, kMaxLowCutHz = 10000.0;

// 2nd-order Butterworth high-pass, run forward and backward (zero phase).
inline void highPassZeroPhase(std::vector<float>& x, double hz, double rate) {
    hz = std::clamp(hz, 20.0, rate * .45);
    const double w = std::tan(pi * hz / rate), k = std::sqrt(2.0);
    const double n = 1.0 / (1.0 + k * w + w * w);
    const double b0 = n, b1 = -2 * n, b2 = n, a1 = 2 * (w * w - 1) * n, a2 = (1 - k * w + w * w) * n;
    auto pass = [&](bool reverse) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        const size_t len = x.size();
        for (size_t c = 0; c < len; ++c) {
            const size_t i = reverse ? len - 1 - c : c;
            const double v = x[i], y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = v; y2 = y1; y1 = y; x[i] = float(y);
        }
    };
    pass(false); pass(true);
}
inline Sample layerAt(const Sample& note, double drive, Character character, double lowCutHz) {
    // The WHOLE saturated tail (not saturated-minus-dry): it stays in phase with
    // the kick's own highs (measured correlation ~0.75-0.9), so mixing it in only
    // adds brightness and can never cancel/phase against the rest of the kick.
    Sample layer = saturate(note, drive, character);
    // Two zero-phase passes: a steep (48 dB/oct) low cut, no phase shift.
    for (int pass = 0; pass < 2; ++pass) {
        highPassZeroPhase(layer.left, lowCutHz, note.rate);
        highPassZeroPhase(layer.right, lowCutHz, note.rate);
    }
    return layer;
}
inline double mixedBrightnessDb(const Sample& note, const Sample& layer) {
    Sample sum = note;
    for (size_t i = 0; i < sum.left.size(); ++i) { sum.left[i] += layer.left[i]; sum.right[i] += layer.right[i]; }
    return brightnessDb(sum);
}
struct Layer { Sample audio; double drive = 0, mixedDb = 0; };
inline Layer rootLayer(const Sample& root, Character character, double lowCutHz) {
    Layer l; l.drive = kRootDrive; l.audio = layerAt(root, kRootDrive, character, lowCutHz);
    l.mixedDb = mixedBrightnessDb(root, l.audio); return l;
}
// Per note: drive rises with how much darker the note is than the root (6 dB
// darker = twice the drive, 4..64), then the layer level is set so the note's
// brightness with the layer at 100% equals the root's. Because the layer is in
// phase with the note, brightness rises monotonically with level (bisection).
// Level is limited to 0.25x..4x of the root's: every note keeps some crunch.
// v0.4.60 (user request): a note BRIGHTER than the root (pitched up) is no
// longer brightness-matched - that pushed its layer to the 0.25x floor, so the
// crunch nearly vanished up high. With rootLayerEnergy > 0 its layer is instead
// level-matched: same energy as the root's crunch layer, so it feels the same.
struct NoteLayer : Layer { double gain = 1; };
inline NoteLayer matchedLayer(const Sample& note, double targetDb, Character character, double lowCutHz, double rootOwnDb = 0, bool haveRootOwn = false, double rootLayerEnergy = 0) {
    NoteLayer best;
    const double own = brightnessDb(note);
    const double darker = haveRootOwn ? (std::max)(0.0, rootOwnDb - own) : 0.0;
    best.drive = std::clamp(kRootDrive * std::pow(2.0, darker / 6.0), kRootDrive, kMaxDrive);
    const Sample base = layerAt(note, best.drive, character, lowCutHz);
    const auto mixedAt = [&](double g) {
        Sample sum = note;
        for (size_t i = 0; i < sum.left.size(); ++i) { sum.left[i] += float(g * base.left[i]); sum.right[i] += float(g * base.right[i]); }
        return brightnessDb(sum);
    };
    double lo = 0.25, hi = 4.0, g = 1.0;
    const double baseEnergy = energy(base);
    if (haveRootOwn && own > rootOwnDb && rootLayerEnergy > 0 && baseEnergy > 0)
        g = std::clamp(std::sqrt(rootLayerEnergy / baseEnergy), lo, hi);
    else if (mixedAt(lo) >= targetDb) g = lo;
    else if (mixedAt(hi) <= targetDb) g = hi;
    else {
        for (int it = 0; it < 14; ++it) {
            g = std::sqrt(lo * hi);
            const double db = mixedAt(g);
            if (std::abs(db - targetDb) < 0.03) break;
            if (db < targetDb) lo = g; else hi = g;
        }
    }
    best.gain = g; best.mixedDb = mixedAt(g); best.audio = base;
    for (size_t i = 0; i < best.audio.left.size(); ++i) { best.audio.left[i] = float(g * base.left[i]); best.audio.right[i] = float(g * base.right[i]); }
    return best;
}

// ---------------------------------------------------------------------------
// v0.5.7 CRUNCH "LOST ONLY" (user request: "the high end only covers the lost
// frequencies ONLY ... when root is G and I pitch down to D#, add some
// frequencies for the lost very high end and make it phase match").
//
// A pitched-DOWN note's spectrum has moved down with the note, so above a
// certain point it has less energy than the root. This mode fills exactly
// that: the note's own saturated layer (same construction as CRUNCH, so it is
// in phase with the note's cycles) is shaped by a per-band curve that equals
// the energy the note is missing against the root - nothing where the note is
// already as bright as the root. The curve is applied with a zero-phase
// (magnitude-only FFT) filter, so no phase is added. Root, unshifted and
// pitched-UP notes get a silent layer (user choice: fill only pitched down).
namespace lost {
constexpr double kMaxHz = 20000.0;
constexpr double kBandsPerOctave = 3.0;
constexpr double kMaxBandGain = 8.0;   // never more than +18 dB on the saturated layer

inline void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = (inverse ? 2 : -2) * pi / double(len); const std::complex<double> w(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> x(1, 0); for (size_t j = 0; j < len / 2; ++j) { const auto u = a[i + j], v = a[i + j + len / 2] * x; a[i + j] = u + v; a[i + j + len / 2] = u - v; x *= w; } }
    }
    if (inverse) for (auto& x : a) x /= double(n);
}
// Band edges (Hz) from lowCut up to kMaxHz (or Nyquist), 1/3 octave each.
inline std::vector<double> bandEdges(double lowCutHz, double rate) {
    std::vector<double> e; const double top = (std::min)(kMaxHz, rate * .5);
    for (double f = lowCutHz; f < top; f *= std::pow(2.0, 1.0 / kBandsPerOctave)) e.push_back(f);
    e.push_back(top); return e;
}
// Mean power per band over the tail (mono sum, first 5 % skipped, Hann frames).
inline std::vector<double> bandPower(const Sample& s, const std::vector<double>& edges) {
    std::vector<double> power(edges.size() - 1, 0.0);
    const size_t n = s.left.size(); if (n < 64 || edges.size() < 2) return power;
    const size_t frame = 4096, hop = frame / 2, start = n / 20;
    std::vector<std::complex<double>> x(frame); int frames = 0;
    for (size_t t = start; t + frame <= n; t += hop) {
        for (size_t i = 0; i < frame; ++i) { const double w = .5 - .5 * std::cos(2 * pi * double(i) / double(frame)); x[i] = .5 * (double(s.left[t + i]) + double(s.right[t + i])) * w; }
        fft(x, false); ++frames;
        for (size_t k = 1; k < frame / 2; ++k) {
            const double hz = double(k) * s.rate / double(frame);
            const auto it = std::upper_bound(edges.begin(), edges.end(), hz);
            if (it == edges.begin() || it == edges.end()) continue;
            power[size_t(it - edges.begin()) - 1] += std::norm(x[k]);
        }
    }
    if (frames) for (auto& v : power) v /= frames;
    return power;
}
// Band powers of the note and the layer plus their cross power (the layer is
// in phase with the note, so |note + g layer|^2 = N + 2 g C + g^2 L per band).
inline void bandPowers(const Sample& note, const Sample& layer, const std::vector<double>& edges,
                       std::vector<double>& noteP, std::vector<double>& layerP, std::vector<double>& crossP) {
    const size_t bands = edges.size() - 1;
    noteP.assign(bands, 0.0); layerP.assign(bands, 0.0); crossP.assign(bands, 0.0);
    const size_t n = (std::min)(note.left.size(), layer.left.size()); if (n < 64 || edges.size() < 2) return;
    const size_t frame = 4096, hop = frame / 2, start = n / 20;
    std::vector<std::complex<double>> x(frame), y(frame); int frames = 0;
    std::vector<size_t> band(frame / 2, size_t(-1));
    for (size_t k = 1; k < frame / 2; ++k) {
        const double hz = double(k) * note.rate / double(frame);
        const auto it = std::upper_bound(edges.begin(), edges.end(), hz);
        if (it != edges.begin() && it != edges.end()) band[k] = size_t(it - edges.begin()) - 1;
    }
    for (size_t t = start; t + frame <= n; t += hop) {
        for (size_t i = 0; i < frame; ++i) {
            const double w = .5 - .5 * std::cos(2 * pi * double(i) / double(frame));
            x[i] = .5 * (double(note.left[t + i]) + double(note.right[t + i])) * w;
            y[i] = .5 * (double(layer.left[t + i]) + double(layer.right[t + i])) * w;
        }
        fft(x, false); fft(y, false); ++frames;
        for (size_t k = 1; k < frame / 2; ++k) if (band[k] != size_t(-1)) {
            noteP[band[k]] += std::norm(x[k]); layerP[band[k]] += std::norm(y[k]); crossP[band[k]] += (x[k] * std::conj(y[k])).real();
        }
    }
    if (frames) for (size_t b = 0; b < bands; ++b) { noteP[b] /= frames; layerP[b] /= frames; crossP[b] /= frames; }
}
// Gain per band so that |note + gain * layer|^2 reaches the root's band power
// where the note is below the root (0 elsewhere): the positive root of
// L g^2 + 2 C g + (N - R) = 0. Clamped to kMaxBandGain.
inline std::vector<double> lostGains(const std::vector<double>& rootP, const std::vector<double>& noteP, const std::vector<double>& layerP, const std::vector<double>& crossP) {
    std::vector<double> g(rootP.size(), 0.0);
    for (size_t b = 0; b < g.size(); ++b) {
        const double missing = rootP[b] - noteP[b];
        if (missing <= 0 || layerP[b] <= 0) continue;
        const double disc = crossP[b] * crossP[b] + layerP[b] * missing;
        g[b] = (std::min)(kMaxBandGain, (std::max)(0.0, (-crossP[b] + std::sqrt(disc)) / layerP[b]));
    }
    return g;
}
// Zero-phase band shaping of a whole sample: magnitude multiplied by the gain
// curve (log-frequency linear between band centres, 0 below the first edge
// and above the last), identical for both channels.
inline void shapeZeroPhase(Sample& s, const std::vector<double>& edges, const std::vector<double>& gains) {
    const size_t n = s.left.size(); if (n == 0 || gains.empty()) return;
    size_t m = 1; while (m < 2 * n) m *= 2;
    std::vector<double> centre(gains.size());
    for (size_t b = 0; b < gains.size(); ++b) centre[b] = std::sqrt(edges[b] * edges[b + 1]);
    const auto gainAt = [&](double hz) {
        if (hz < edges.front() || hz > edges.back()) return 0.0;
        if (hz <= centre.front()) return gains.front();
        if (hz >= centre.back()) return gains.back();
        const auto it = std::upper_bound(centre.begin(), centre.end(), hz);
        const size_t k = size_t(it - centre.begin());
        const double u = std::log(hz / centre[k - 1]) / std::log(centre[k] / centre[k - 1]);
        return gains[k - 1] + (gains[k] - gains[k - 1]) * u;
    };
    std::vector<double> curve(m / 2 + 1);
    for (size_t k = 0; k <= m / 2; ++k) curve[k] = gainAt(double(k) * s.rate / double(m));
    std::vector<std::complex<double>> x(m);
    for (auto* ch : { &s.left, &s.right }) {
        std::fill(x.begin(), x.end(), std::complex<double>{});
        for (size_t i = 0; i < n; ++i) x[i] = (*ch)[i];
        fft(x, false);
        for (size_t k = 0; k <= m / 2; ++k) { x[k] *= curve[k]; if (k && k < m / 2) x[m - k] = std::conj(x[k]); }
        fft(x, true);
        for (size_t i = 0; i < n; ++i) (*ch)[i] = float(x[i].real());
    }
}
struct Result { Sample audio; double drive = 0; std::vector<double> gains; };
// The note's layer for LOST ONLY. `rootPower` = bandPower(root, edges) computed once.
inline Result layer(const Sample& note, const std::vector<double>& rootPower, const std::vector<double>& edges, Character character, double lowCutHz, double rootOwnDb) {
    Result r;
    const double own = brightnessDb(note);
    r.drive = std::clamp(kRootDrive * std::pow(2.0, (std::max)(0.0, rootOwnDb - own) / 6.0), kRootDrive, kMaxDrive);
    r.audio = layerAt(note, r.drive, character, lowCutHz);
    std::vector<double> noteP, layerP, crossP;
    bandPowers(note, r.audio, edges, noteP, layerP, crossP);
    r.gains = lostGains(rootPower, noteP, layerP, crossP);
    shapeZeroPhase(r.audio, edges, r.gains);
    return r;
}
inline Sample silentLike(const Sample& note) { Sample s; s.rate = note.rate; s.left.assign(note.left.size(), 0.f); s.right.assign(note.right.size(), 0.f); return s; }
} // namespace lost
} // namespace pt::crunchmatch
