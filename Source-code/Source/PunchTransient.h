#pragma once
#include <algorithm>
#include <cmath>

// Zero-latency, allocation-free PUNCH ONLY transient envelope shaper.
// It is intentionally placed after punch EQ and before punch distortion.
// A linked stereo detector preserves left/right placement and cancels no bass.
namespace pt {
struct TransientSettings {
    bool enabled = false;
    float attack = 30.0f;     // -100..100%; full scale = +/-12 dB of detected onset
    float sustain = 0.0f;     // -100..100%; full scale = +/-12 dB of body
    float attackMs = 8.0f;    // 1..35 ms, slow-detector rise time
    float recoveryMs = 70.0f; // 15..300 ms, detector release
    float focusHz = 140.0f;   // 20..500 Hz, detector high pass (not audible)
    float sensitivityDb = -38.0f;
    float mix = 100.0f;       // 0..100%
    float outputDb = 0.0f;    // -12..12 dB
};

class PunchTransient {
public:
    void prepare(double sampleRate) {
        rate = std::clamp(sampleRate,8000.0,384000.0);
        fast=slow=lowL=lowR=0.0;
        shapedAttack=shapedSustain=shapedMix=shapedOutput=shapedEnable=0.0;
        started=false;
        setSettings({});
    }
    void setSettings(TransientSettings s) {
        s.attack=std::clamp(s.attack,-100.0f,100.0f);
        s.sustain=std::clamp(s.sustain,-100.0f,100.0f);
        s.attackMs=std::clamp(s.attackMs,1.0f,35.0f);
        s.recoveryMs=std::clamp(s.recoveryMs,15.0f,300.0f);
        s.focusHz=std::clamp(s.focusHz,20.0f,500.0f);
        s.sensitivityDb=std::clamp(s.sensitivityDb,-60.0f,-6.0f);
        s.mix=std::clamp(s.mix,0.0f,100.0f);
        s.outputDb=std::clamp(s.outputDb,-12.0f,12.0f);
        settings=s;
        constexpr double pi=3.14159265358979323846;
        const auto onePole=[this](double ms){return std::exp(-1000.0/(rate*ms));};
        fastAttack=onePole(0.55);
        fastRelease=onePole(std::max(6.0,double(s.recoveryMs)*0.16));
        slowAttack=onePole(s.attackMs);
        slowRelease=onePole(s.recoveryMs);
        detectorLowPass=std::exp(-2.0*pi*s.focusHz/rate);
        threshold=std::pow(10.0,double(s.sensitivityDb)/20.0);
        smooth=onePole(5.0);
        if(!started) {
            shapedAttack=s.attack/100.0;
            shapedSustain=s.sustain/100.0;
            shapedMix=s.mix/100.0;
            shapedOutput=s.outputDb;
            shapedEnable=s.enabled?1.0:0.0;
            started=true;
        }
    }
    void process(float& left,float& right) {
        const double dryL=left,dryR=right;
        // Detector-only high pass removes sub-bass from onset detection.
        lowL=detectorLowPass*lowL+(1.0-detectorLowPass)*dryL;
        lowR=detectorLowPass*lowR+(1.0-detectorLowPass)*dryR;
        const double observed=std::max(std::abs(dryL-lowL),std::abs(dryR-lowR));
        fast=(observed>fast?fastAttack:fastRelease)*fast+
             (1.0-(observed>fast?fastAttack:fastRelease))*observed;
        slow=(observed>slow?slowAttack:slowRelease)*slow+
             (1.0-(observed>slow?slowAttack:slowRelease))*observed;
        shapedAttack=smooth*shapedAttack+(1.0-smooth)*(settings.attack/100.0);
        shapedSustain=smooth*shapedSustain+(1.0-smooth)*(settings.sustain/100.0);
        shapedMix=smooth*shapedMix+(1.0-smooth)*(settings.mix/100.0);
        shapedOutput=smooth*shapedOutput+(1.0-smooth)*settings.outputDb;
        shapedEnable=smooth*shapedEnable+(1.0-smooth)*(settings.enabled?1.0:0.0);
        if(shapedEnable<1.0e-7) return; // exact bypass at rest
        // Normalise the difference in detector envelopes: a new impact makes
        // fast > slow; the steady punch body becomes the sustain region.
        const double transient=std::clamp((fast-slow)/(fast+1.0e-7),0.0,1.0);
        // The sensitivity control gently gates quiet residuals and noise.
        const double activity=std::clamp((fast-threshold)/(threshold+1.0e-9),0.0,1.0);
        const double attack=activity*transient;
        const double sustain=activity*(1.0-transient);
        const double db=12.0*(shapedAttack*attack+shapedSustain*sustain);
        const double gain=std::pow(10.0,(db+shapedOutput)/20.0);
        const double wet=std::clamp(shapedMix*shapedEnable,0.0,1.0);
        left=static_cast<float>(dryL*(1.0-wet)+dryL*gain*wet);
        right=static_cast<float>(dryR*(1.0-wet)+dryR*gain*wet);
    }
private:
    double rate=44100.0;
    TransientSettings settings{};
    double fast=0,slow=0,lowL=0,lowR=0;
    double fastAttack=0,fastRelease=0,slowAttack=0,slowRelease=0;
    double detectorLowPass=0,threshold=0,smooth=0;
    double shapedAttack=0,shapedSustain=0,shapedMix=0,shapedOutput=0,shapedEnable=0;
    bool started=false;
};
}
