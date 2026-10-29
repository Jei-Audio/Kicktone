#pragma once
#include <cmath>
#include <algorithm>
namespace pt {
inline float distort(float x, int mode) noexcept {
    // Bounded input keeps tanh/square and downstream DSP finite under bad MIDI
    // or wildly boosted input. Soft and saturation are odd-symmetric.
    x = std::clamp(x, -64.0f, 64.0f);
    switch (mode) {
        case 1: return std::clamp(x, -1.0f, 1.0f);
        case 2: return std::tanh(x);
        default: return x / std::sqrt(1.0f + x*x);
    }
}

}
