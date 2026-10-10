#include "vfx_oit.h"

#include <algorithm>
#include <cmath>

namespace uz {

const char* VfxBlendModeToString(VfxBlendMode mode) noexcept {
    switch (mode) {
        case VfxBlendMode::AlphaBlend:  return "alpha_blend";
        case VfxBlendMode::Additive:    return "additive";
        case VfxBlendMode::PremulAlpha: return "premul_alpha";
        case VfxBlendMode::WeightedOIT: return "weighted_oit";
    }
    return "alpha_blend";
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxOITWeight — McGuire & Bavoil 2013 Equation 10 variant
// ─────────────────────────────────────────────────────────────────────────────

float VfxOITWeight(float linearZ, float alpha, float blendFactor) noexcept {
    // Clamp inputs to physical ranges. Negative alpha or out-of-unit linearZ
    // would produce weird weights; clamp conservatively.
    alpha   = std::clamp(alpha,   0.0f, 1.0f);
    linearZ = std::clamp(linearZ, 0.0f, 1.0f);

    // Paper's depth-weighting function: heavy on near samples, small but
    // non-zero at the far plane so background atmospherics still resolve.
    const float zPow4  = std::pow(linearZ, 4.0f);
    const float zTerm  = 1.0f / (1.0e-5f + zPow4);
    const float zWeight = std::clamp(0.03f * zTerm, 1.0e-2f, 3.0e3f);

    // α⁵ emphasises near-opaque samples; the lerp with blendFactor provides
    // a lower bound so low-α samples still contribute.
    const float alphaWeight = std::min(3.0e-2f, alpha * alpha * alpha * alpha * alpha * zWeight);
    return alpha * std::max(blendFactor, alphaWeight);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxOITResolve — McGuire & Bavoil 2013 Equation 11
// ─────────────────────────────────────────────────────────────────────────────

Vec3 VfxOITResolve(const Vec3& accumRGB, float accumAlphaSum,
                    float revealage, const Vec3& backgroundRGB) noexcept {
    const float safeSum = std::max(accumAlphaSum, 1.0e-4f);
    const Vec3 average{
        accumRGB.x / safeSum,
        accumRGB.y / safeSum,
        accumRGB.z / safeSum,
    };

    // lerp(background, average, 1 - revealage)
    const float blend = 1.0f - std::clamp(revealage, 0.0f, 1.0f);
    return Vec3{
        backgroundRGB.x + (average.x - backgroundRGB.x) * blend,
        backgroundRGB.y + (average.y - backgroundRGB.y) * blend,
        backgroundRGB.z + (average.z - backgroundRGB.z) * blend,
    };
}

float VfxOITResolveAlpha(float revealage) noexcept {
    return 1.0f - std::clamp(revealage, 0.0f, 1.0f);
}

} // namespace uz
