// Regression test for Windows-style min/max macros leaking into the DSP.
// Include STL and the real r8brain interface before simulating Windows.h.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>
#include <functional>
#include <memory>
#include <complex>
#include <CDSPResampler.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#define min(a,b) PUNCHTAIL_WINDOWS_MIN_MACRO_LEAK
#define max(a,b) PUNCHTAIL_WINDOWS_MAX_MACRO_LEAK
#include "../Source/ParametricEQ.h"
#include "../Source/Engine.h"
#include "../Source/MasterEnvelope.h"  // v0.4.8 also tests high-pass header under macro contamination
#include "../Source/PitchShift.h"
#include "V4Render.h"
#include "../Source/PunchPitch.h"
#include "../Source/KickCharacterPitch.h"
#include "../Source/AdaptiveStretch.h"
#include "../Source/LinearPhaseEQ.h"
#include "../Source/TailReverbPitchBank.h"
#include "../Source/KickOverview.h"
#include "../Source/TailReverb.h"
#include "../Source/WaveformEnvelope.h"

int main() {
    pt::Sample input;
    input.rate = 44100.0;
    input.left = {0.0f, 0.25f, -0.25f, 0.0f};
    input.right = input.left;
    const auto unchanged = v4::pitchShift(input, 0.0);
    const auto envelope = pt::buildWaveformEnvelope(input.left, input.right, 2);
    return unchanged.left.size() == input.left.size() && envelope.bins.size() == 2 ? 0 : 1;
}
