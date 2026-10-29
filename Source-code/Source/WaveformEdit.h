#pragma once
#include <algorithm>
namespace pt {
// Original 0-600 ms automation stays intact. An appended parameter extends
// manual trimming to the existing five-second sample-load limit.
struct TrimParts { double base=0, extra=0; };
inline TrimParts splitTrim(double value) {
    value=std::clamp(value,0.0,5000.0);
    return {(std::min)(value,600.0),(std::max)(0.0,value-600.0)};
}
inline double trimAtPosition(bool punch,double x,double left,double width,double duration) {
    const double fraction=std::clamp((x-left)/(std::max)(1.0,width),0.0,1.0);
    return (punch?1.0-fraction:fraction)*duration;
}
// Each mouse event is relative to the immediately preceding one.  Scaling
// only the current movement prevents a Shift-toggle from reinterpreting all
// previous movement and jumping the trim marker.
inline double dragWaveformTrim(bool punch, double currentMs, double previousX,
                              double mouseX, double durationMs, double plotWidth,
                              bool fine) {
    const double deltaMs = (mouseX - previousX) * durationMs
        / (std::max)(1.0, plotWidth) * (fine ? 0.1 : 1.0);
    return std::clamp(currentMs + (punch ? -deltaMs : deltaMs), 0.0, durationMs);
}
enum class KickHandle { punchEnd, tailStart, crossfade };
inline double dragKickValue(KickHandle handle,double original,double deltaMs,double punchDuration) {
    if(handle==KickHandle::punchEnd)return std::clamp(original-deltaMs,0.0,punchDuration);
    if(handle==KickHandle::tailStart)return std::clamp(original+deltaMs,-30.0,30.0);
    return std::clamp(original-2.0*deltaMs,0.0,30.0);
}
}
