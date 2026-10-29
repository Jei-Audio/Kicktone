#pragma once
#include <algorithm>
#include <cmath>
namespace pt {
struct PitchDropSettings {
    bool enabled=false;
    double amount=12, timeMs=60, curve=1;
    int target=1; // 0 Punch, 1 Tail, 2 Both. Each lane starts at its audible onset.
    bool active() const {return enabled && std::isfinite(amount) && amount>0;}
    bool operator==(const PitchDropSettings& b) const {
        if(!active()&&!b.active())return true;
        return enabled==b.enabled&&amount==b.amount&&timeMs==b.timeMs&&curve==b.curve&&target==b.target;
    }
    bool operator!=(const PitchDropSettings& b) const {return !(*this==b);}
    double semitones(double seconds) const {
        if(!active())return 0;
        const double duration=std::clamp(std::isfinite(timeMs)?timeMs:60.,1.,500.)*.001;
        const double x=std::clamp(seconds/duration,0.,1.);
        return std::clamp(amount,0.,24.)*std::pow(1.-x,std::clamp(std::isfinite(curve)?curve:1.,.25,4.));
    }
};
}
