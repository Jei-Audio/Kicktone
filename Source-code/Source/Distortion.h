#pragma once
#include <juce_dsp/juce_dsp.h>
#include "DistortionCurve.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace pt {
// These are POST-mix effects: each path combines all active voices before
// waveshaping, so one distortion processes PUNCH and the other TAIL+REVERB.
// Both oversamplers are run at the same fixed factor to maintain alignment.
struct DistortionSettings {
    bool enabled = false;
    int type = 0;  // 0 soft clip, 1 hard clip, 2 tanh saturation
    float driveDb = 6.0f, toneHz = 16000.0f, mix = 1.0f, outputDb = -3.0f;
};


class StereoDistortion {
public:
    static constexpr int oversampleFactor = 4;
    StereoDistortion()
       : oversampler(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                     true, true) {}

    void prepare(double sampleRate, int maxBlock) {
        oversampler.initProcessing(static_cast<size_t>(maxBlock));
        oversampler.reset();
        highRate = std::max(8000.0, sampleRate) * oversampleFactor;
        drive.reset(highRate, 0.015);
        tone.reset(highRate, 0.015);
        wet.reset(highRate, 0.015);
        output.reset(highRate, 0.015);
        drive.setCurrentAndTargetValue(1.0f);
        tone.setCurrentAndTargetValue(16000.0f);
        wet.setCurrentAndTargetValue(0.0f);
        output.setCurrentAndTargetValue(1.0f);
        history = {};
        cachedToneHz=-1.0f;
    }
    float latencySamples() const noexcept { return oversampler.getLatencyInSamples(); }

    // Called with scratch storage allocated in prepareToPlay(). Never allocates.
    void process(juce::AudioBuffer<float>& buffer, int numSamples, const DistortionSettings& cfg) noexcept {
        drive.setTargetValue(juce::Decibels::decibelsToGain(cfg.driveDb));
        tone.setTargetValue(std::clamp(cfg.toneHz, 500.0f, 16000.0f));
        wet.setTargetValue(cfg.enabled ? std::clamp(cfg.mix,0.0f,1.0f) : 0.0f);
        output.setTargetValue(juce::Decibels::decibelsToGain(cfg.outputDb));

        juce::dsp::AudioBlock<float> storage(buffer);
        auto block = storage.getSubBlock(0,static_cast<size_t>(numSamples));
        juce::dsp::AudioBlock<const float> input(block);
        auto up = oversampler.processSamplesUp(input);
        const auto count = up.getNumSamples();
        if (!cfg.enabled && !wet.isSmoothing() && wet.getCurrentValue() < 0.00001f) {
            // Keep this lane's 4x filtering latency matched to the other lane
            // without wasting CPU on a bypassed waveshaper.
            oversampler.processSamplesDown(block);
            return;
        }
        auto* left = up.getChannelPointer(0);
        auto* right = up.getChannelPointer(1);
        // Smooth once per oversampled frame (not independently per channel).
        for (size_t i = 0; i < count; ++i) {
            const float g=drive.getNextValue();
            const float cutoff=tone.getNextValue();
            const float blend=wet.getNextValue();
            const float level=output.getNextValue();
            if (std::abs(cachedToneHz-cutoff)>0.01f) {
                cachedToneHz=cutoff;
                cachedCoefficient=1.0f - std::exp(-juce::MathConstants<float>::twoPi * cutoff / float(highRate));
            }
            const float coefficient=cachedCoefficient;
            auto apply=[&](int channel, float dry) noexcept {
                const float shaped=distort(dry*g,cfg.type);
                // The tone filter affects only the distorted branch, not dry.
                float& z=history[size_t(channel)];
                z += coefficient*(shaped-z);
                return dry*(1.0f-blend)+(z*level)*blend;
            };
            left[i]=apply(0,left[i]);
            right[i]=apply(1,right[i]);
        }
        oversampler.processSamplesDown(block);
    }
private:
    juce::dsp::Oversampling<float> oversampler;
    juce::SmoothedValue<float> drive, tone, wet, output;
    std::array<float,2> history {};
    double highRate=176400.0;
    float cachedToneHz=-1, cachedCoefficient=1;
};
}
