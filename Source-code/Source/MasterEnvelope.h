#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace tt {
struct MasterEnvelopeSettings {
    double attackMs=0, decayMs=0, sustainPercent=100, releaseMs=1;
};
// One envelope for the complete post-effects output. Natural release fades
// inside the existing duration; MIDI release fades from the current level.
class MasterEnvelope {
public:
    void reset() { active=false; age=0; released=false; releaseFinished=false; lastGain=0; }
    void start(double sampleRate,double duration,MasterEnvelopeSettings settings) {
        sr=(std::max)(1.0,sampleRate); end=(std::max)(0.0,duration); s=settings;
        age=0; released=false; releaseFinished=false; lastGain=0; active=end>0;
    }
    void extendDuration(double duration) {
        if(!released&&!releaseFinished){end=(std::max)(end,duration);active=end>double(age)/sr;}
    }
    void noteOff() {
        if(!released&&!releaseFinished){released=true;releaseAge=0;releaseFrom=lastGain;
            end=(std::max)(end,double(age)/sr+(std::max)(0.0,s.releaseMs)*.001);active=true;}
    }
    float next() {
        if(!active){++age;return 0;}
        const double t=double(age)/sr;
        if(t>=end){active=false;lastGain=0;releaseFinished=released;++age;return 0;}
        const double a=(std::max)(0.0,s.attackMs)*.001;
        const double d=(std::max)(0.0,s.decayMs)*.001;
        const double sustain=std::clamp(s.sustainPercent*.01,0.0,1.0);
        double gain=sustain;
        if(a>0&&t<a)gain=t/a;
        else if(d>0&&t<a+d)gain=1+(sustain-1)*(t-a)/d;
        const double r=(std::max)(0.0,s.releaseMs)*.001;
        if(r>0)gain*=std::clamp((end-t-1/sr)/r,0.0,1.0);
        if(released){
            if(r<=0 || double(releaseAge)/sr>=r){active=false;lastGain=0;releaseFinished=true;++age;return 0;}
            gain=(std::min)(gain,releaseFrom*std::clamp(1-double(releaseAge)/sr/r,0.0,1.0));
            ++releaseAge;
        }
        ++age;lastGain=float(gain);return lastGain;
    }
    double releaseSeconds() const {return (std::max)(0.0,s.releaseMs)*.001;}
    bool isActive() const {return active;}
    bool isReleased() const {return released;}
    bool finishedAfterNoteOff() const {return releaseFinished;}
private:
    MasterEnvelopeSettings s;
    double sr=44100,end=0,releaseFrom=0;
    long long age=0,releaseAge=0;
    bool active=false,released=false,releaseFinished=false;
    float lastGain=0;
};
}
