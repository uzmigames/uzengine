#pragma once

/// VFX Weighted Blended Order-Independent Transparency.
///
/// Alternative to back-to-front depth sorting for transparent particles.
/// Two passes:
///   1. **Accumulation** — emitters write premultiplied
///      `(color·alpha·weight, alpha·weight)` into a RGBA16F RT and `(1−alpha)`
///      into an R8 revealage RT with additive blending. Depth read-only.
///   2. **Resolve** — fullscreen pass divides the accumulation by alpha sum
///      and composites `(1 − revealage)` over the framebuffer.
///
/// Order independence is achieved by weighting each sample's contribution so
/// closer-to-camera samples dominate the resolve automatically, and the
/// commutative additive blend means draw order no longer matters.
///
/// # Weight function (McGuire & Bavoil 2013, §3.2)
///
/// ```
/// w(z, α) = α · max(blendFactor, min(3 · 10⁻², α⁵ · (8 + 200 · z_near))
///                                     · (1 − z_far)³)                 (simplified)
/// ```
///
/// UzEngine uses the "Equation 10" variant that takes linear-Z in [0, 1]:
/// ```
/// w(z, α) = α · max(blendFactor, min(3 · 10⁻², α⁵ · zWeight(z)))
/// zWeight(z) = clamp(0.03 / (1e-5 + pow(z, 4)), 0.01, 3·10³)
/// ```
///
/// The constants are calibrated so near samples (z→0) dominate and far
/// samples (z→1) still contribute a small amount to avoid losing background
/// atmospherics. `blendFactor` (a per-emitter authoring knob) provides a
/// lower bound so fully-opaque samples always contribute.
///
/// # Resolve composite
///
/// ```
/// accumColor  = accum.rgb / max(accum.a, 1e-4)
/// finalColor  = lerp(backgroundColor, accumColor, 1 − revealage)
/// ```
///
/// Mathematically equivalent to the McGuire–Bavoil eq. 11.
///
/// @ref McGuire & Bavoil 2013, "Weighted Blended OIT"
/// @ref docs/engine/25-vfx/33-weighted-oit.md

#include "vector3.h"
#include "vector4.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Per-emitter opt-in via blendMode enum (extends the existing values)
// ─────────────────────────────────────────────────────────────────────────────

/// Blend modes accepted by VFXBillboardCB::blendModeHint. The first three
/// match the pre-8.16 shipping values. `WeightedOIT` is new and routes
/// emitters through the accumulation pass instead of sorted back-to-front.
enum class VfxBlendMode : uint8_t {
    AlphaBlend   = 0,
    Additive     = 1,
    PremulAlpha  = 2,
    WeightedOIT  = 3,
};

/// Returns a static-storage string for `mode` (used by the inspector / logs).
[[nodiscard]] const char* VfxBlendModeToString(VfxBlendMode mode) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// OIT math helpers (CPU mirror of the shader code for tests)
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the OIT weight for one fragment. Mirrors the
/// `OITAccumulation` pixel shader exactly so tests exercise the same
/// formula the GPU uses.
///
/// # Arguments
/// - `linearZ`     — view-space depth normalised to [0, 1] (near=0, far=1).
/// - `alpha`       — fragment alpha in [0, 1].
/// - `blendFactor` — authoring knob; typical 0.01, never returns below this.
///
/// # Returns
/// Non-negative weight used by the accumulation pass:
///   `accumColor.rgb += fragment.rgb · alpha · w`
///   `accumColor.a   += alpha · w`
///   `revealage     *= (1 − alpha)`
[[nodiscard]] float VfxOITWeight(float linearZ, float alpha,
                                   float blendFactor = 0.01f) noexcept;

/// Composite the resolve step: given the accumulation RT value
/// (`accum.rgb`, `accum.a` = sum of α·w) and the revealage RT value
/// (`R8` = product of `(1 − α)`), produce the final RGB over a background.
///
/// # Arguments
/// - `accumRGB`       — sum of `color·α·w` across fragments.
/// - `accumAlphaSum`  — sum of `α·w` across fragments (accum.a).
/// - `revealage`      — product of `(1 − α)` across fragments.
/// - `backgroundRGB`  — linear-RGB of the opaque framebuffer at this pixel.
///
/// # Returns
/// Resolved pixel RGB, linear space, ready to blit into the scene RT.
[[nodiscard]] Vec3 VfxOITResolve(const Vec3& accumRGB,
                                   float accumAlphaSum,
                                   float revealage,
                                   const Vec3& backgroundRGB) noexcept;

/// Returns the resolved alpha (`1 − revealage`).
[[nodiscard]] float VfxOITResolveAlpha(float revealage) noexcept;

} // namespace uz
