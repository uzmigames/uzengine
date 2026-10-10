#include "vfx_spawn_rate_curve.h"

#include <algorithm>
#include <cmath>

namespace uz {

namespace {

/// Linear interpolate between two keys, guarding against identical
/// timestamps (which would divide by zero).
float LerpKeys(const VfxSpawnRateKey& a, const VfxSpawnRateKey& b, float t) noexcept {
    const float span = b.time - a.time;
    if (!(span > 0.0f)) {
        // Coincident keys — return the later key's multiplier. This covers
        // step-function authoring where two keys share a timestamp.
        return b.multiplier;
    }
    const float u = (t - a.time) / span;
    return a.multiplier + (b.multiplier - a.multiplier) * u;
}

} // namespace

float VfxSpawnRateCurve::Evaluate(float t) const noexcept {
    if (!enabled) {
        return 1.0f;
    }

    // Clamp to [first.time, last.time] so out-of-range inputs return the
    // edge key's multiplier rather than linearly extrapolating.
    if (t <= keys[0].time) {
        return keys[0].multiplier;
    }
    if (t >= keys[3].time) {
        return keys[3].multiplier;
    }

    // Walk the 3 segments; for a 4-point table the loop is trivial.
    for (int i = 0; i < 3; ++i) {
        if (t <= keys[i + 1].time) {
            return LerpKeys(keys[i], keys[i + 1], t);
        }
    }
    return keys[3].multiplier;  // Unreachable in practice; defensive.
}

void VfxSpawnRateCurve::SetFadeInOut(float rampInEnd, float rampOutStart) noexcept {
    const float inEnd  = std::clamp(rampInEnd, 0.0f, 1.0f);
    const float outStart = std::clamp(rampOutStart, 0.0f, 1.0f);

    keys[0] = {0.0f,   0.0f};    // Silent at spawn.
    keys[1] = {inEnd,  1.0f};    // Plateau start.
    keys[2] = {outStart, 1.0f};  // Plateau end.
    keys[3] = {1.0f,   0.0f};    // Silent at death.
    enabled = true;
}

} // namespace uz
