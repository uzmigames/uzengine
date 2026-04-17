#include "vfx_ddgi_injection.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Luminance
// ─────────────────────────────────────────────────────────────────────────────

float VfxEmissiveSample::Luminance() const noexcept {
    // Rec. 709 / sRGB luma coefficients. Matches the rest of the engine.
    return 0.2126f * emissive.x + 0.7152f * emissive.y + 0.0722f * emissive.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Karis attenuation
// ─────────────────────────────────────────────────────────────────────────────

float VfxDDGIAttenuation(float distance, float influenceRadius) noexcept {
    if (influenceRadius <= 0.0f) return 0.0f;
    const float d      = std::max(distance, 0.0f);
    const float dOverR = d / influenceRadius;
    // smooth cutoff: saturate(1 − (d/r)^4)^2
    const float s  = std::max(0.0f, 1.0f - dOverR * dOverR * dOverR * dOverR);
    const float s2 = s * s;
    const float inverseSquare = 1.0f / (d * d + 1.0f);
    return s2 * inverseSquare;
}

Vec3 VfxDDGIProbeContribution(const VfxEmissiveSample& sample,
                                 const Vec3& probePosition) noexcept {
    const float dx = sample.position.x - probePosition.x;
    const float dy = sample.position.y - probePosition.y;
    const float dz = sample.position.z - probePosition.z;
    const float d  = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (d >= sample.influenceRadius) {
        return Vec3{0.0f, 0.0f, 0.0f};
    }
    const float a = VfxDDGIAttenuation(d, sample.influenceRadius);
    return Vec3{
        sample.emissive.x * a,
        sample.emissive.y * a,
        sample.emissive.z * a,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Top-N selection
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxDDGISelectTopN(const VfxEmissiveSample* in, uint32_t inCount,
                             VfxEmissiveSample* out, uint32_t maxN) noexcept {
    if (in == nullptr || out == nullptr || inCount == 0u || maxN == 0u) {
        return 0u;
    }

    // Stage: copy to out, then partial_sort descending by luminance,
    // then keep the first min(inCount, maxN). Pointer aliasing is safe
    // because we copy element-by-element.
    const uint32_t keep = std::min(inCount, maxN);

    // Copy via std::copy to allow aliasing; std::partial_sort handles
    // any overlap because it acts on a single range.
    // When out == in, skip the copy entirely.
    if (out != in) {
        for (uint32_t i = 0; i < inCount; ++i) out[i] = in[i];
    }

    std::partial_sort(
        out, out + keep, out + inCount,
        [](const VfxEmissiveSample& a, const VfxEmissiveSample& b) {
            return a.Luminance() > b.Luminance();
        });

    return keep;
}

// ─────────────────────────────────────────────────────────────────────────────
// Probe accumulation
// ─────────────────────────────────────────────────────────────────────────────

Vec3 VfxDDGIAccumulateAtProbe(const VfxEmissiveSample* samples, uint32_t count,
                                 const Vec3& probePosition) noexcept {
    if (samples == nullptr || count == 0u) return Vec3{0, 0, 0};
    Vec3 sum{0, 0, 0};
    for (uint32_t i = 0; i < count; ++i) {
        const Vec3 c = VfxDDGIProbeContribution(samples[i], probePosition);
        sum.x += c.x;
        sum.y += c.y;
        sum.z += c.z;
    }
    return sum;
}

} // namespace uz
