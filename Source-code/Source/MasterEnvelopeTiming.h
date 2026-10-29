#pragma once
#include "Engine.h"
#include "TailReverb.h"
#include <array>

namespace tt {
// Shared by playback and the processed waveform. Muted unused files must not
// postpone the fade beyond the actual audible kick.
inline double completeOutputDuration(const pt::Voice& voice,bool hasPunch,bool hasTail,
    const pt::TailReverbSettings& tailVerb,const pt::TailReverbSettings& punchVerb,
    const std::array<bool,3>& muted) {
    double duration=0;
    if(!muted[0]&&hasPunch)duration=voice.punchEnd;
    // v0.4.57: a cut dry Tail ends at dryEnd (== tailEnd when uncut).
    if(!muted[1]&&hasTail)duration=(std::max)({duration,voice.dryEnd,voice.compositeTail?voice.compositeTailEnd:0.0});
    if(!muted[2]) {
        duration=(std::max)({duration,voice.reverbEnd>voice.reverbStart?voice.reverbEnd:0.0,
            voice.generatedTailReverbEnd>voice.generatedTailReverbStart?voice.generatedTailReverbEnd:0.0});
        if(tailVerb.enabled&&tailVerb.mix>0&&tailVerb.tailSend&&!tailVerb.tailRootPitch&&hasTail)
            duration=(std::max)(duration,voice.tailEnd+
                (tailVerb.spaceMs*1.5+tailVerb.distance*.35+(std::max)(0.f,tailVerb.tailOffsetMs)+std::clamp(tailVerb.tailFadeMs,0.f,1000.f)+47)*.001);
        if(tailVerb.enabled&&punchVerb.mix>0&&tailVerb.punchSend&&hasPunch)
            duration=(std::max)(duration,voice.punchEnd+
                (punchVerb.spaceMs*1.5+punchVerb.distance*.35+punchVerb.punchPreDelayMs+punchVerb.punchFadeMs+47)*.001);
    }
    return duration;
}
}
