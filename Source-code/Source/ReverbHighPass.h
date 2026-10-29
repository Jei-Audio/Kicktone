#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace pt {
// Sample-rate-specific Butterworth 200 Hz high-pass coefficients.
// Designed OFF the audio thread, with no allocations during playback.
struct ReverbHPFDesign {
    struct Section { double b0=1, b1=0, b2=0, a1=0, a2=0; };
    std::array<Section,4> sections{};
    int count=0, slope=0;
};
inline ReverbHPFDesign makeReverbHPFAt(double sampleRate, int slope, double cutoffHz) {
    ReverbHPFDesign d;
    if (slope!=12 && slope!=24 && slope!=48) return d;
    d.slope=slope; d.count=slope/12;
    constexpr double pi=3.14159265358979323846;
    const double frequency=(std::min)((std::max)(1.0,cutoffHz),sampleRate*0.4);
    const double w=2*pi*frequency/sampleRate;
    const double cs=std::cos(w), sn=std::sin(w);
    const int poles=2*d.count;
    for(int k=0;k<d.count;++k) {
        // Butterworth poles yield a -3 dB response at the fixed cutoff.
        const double q=1.0/(2*std::cos(pi*(2*k+1)/(2*poles)));
        const double alpha=sn/(2*q), a0=1+alpha;
        auto& s=d.sections[size_t(k)];
        s.b0=(1+cs)/(2*a0);
        s.b1=-(1+cs)/a0;
        s.b2=s.b0;
        s.a1=-2*cs/a0;
        s.a2=(1-alpha)/a0;
    }
    return d;
}
inline ReverbHPFDesign makeReverbHPF(double sampleRate, int slope) {
    return makeReverbHPFAt(sampleRate,slope,200.0);
}
// Stereo biquads with per-voice history. The unfiltered branch is a strict
// identity: older projects (OFF) never alter the reverb samples.
struct ReverbHPFState {
    ReverbHPFDesign design;
    struct Stage { double z1=0, z2=0; };
    std::array<std::array<Stage,4>,2> channels{};
    void reset(const ReverbHPFDesign& d) { design=d; channels={}; }
    float process(int channel, float input) {
        double value=input;
        for(int i=0;i<design.count;++i) {
            const auto& c=design.sections[size_t(i)];
            auto& s=channels[size_t(channel)][size_t(i)];
            const double out=c.b0*value+s.z1;
            s.z1=c.b1*value-c.a1*out+s.z2;
            s.z2=c.b2*value-c.a2*out;
            value=out;
        }
        return float(value);
    }
};
} // namespace pt
