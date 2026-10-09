#pragma once

/// VFX → DDGI emissive injection.
///
/// Emissive particles (fire, lightning, magic) contribute to the scene's
/// DDGI probe irradiance so bright VFX events actually light the
/// surrounding world. Niagara feeds Lumen's radiance cache; UzEngine's
/// analogue targets DDGI since that's the indirect-lighting backend.
///
/// # Pipeline
///
/// 1. The VFX readback path (phase 4.13) produces a sorted list of the
///    top-N most-emissive particles each frame (`VfxEmissiveSample`).
/// 2. During the DDGI probe-update compute pass, each probe iterates
///    every sample and accumulates its contribution weighted by the
///    inverse-square attenuation and a simple direction factor.
/// 3. The accumulated emissive irradiance is added to the probe's
///    next-cycle SH coefficients alongside the usual sky + bounce
///    samples.
///
/// # Attenuation model
///
/// The attenuation matches the Karis 2013 falloff used elsewhere by
/// DDGI / indirect-lighting:
///
/// ```
/// f(d) = saturate(1 − (d / r)⁴)² / (d² + 1)
/// ```
///
/// where `d` is the distance from probe to particle and `r` is the
/// particle's influence radius. The saturate² term produces a smooth
/// falloff, and the `d² + 1` denominator gives physically plausible
/// inverse-square without singularities at d=0.
///
/// # Thread safety
/// Pure CPU helpers. Safe to call from any thread.
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — falloff formula
/// @ref UE5 FNiagaraLumenCommitter + FLumenSceneRadianceCache
/// @ref docs/engine/25-vfx/40-ddgi-emissive-injection.md

#include "vector3.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Sample produced by the VFX readback path (phase 4.13)
// ─────────────────────────────────────────────────────────────────────────────

/// One emissive particle contributing to GI.
struct VfxEmissiveSample {
    Vec3  position{0, 0, 0};   ///< World-space particle position.
    Vec3  emissive{0, 0, 0};   ///< Linear-RGB radiance (W/m²/sr scale).
    float influenceRadius = 1.0f; ///< Falloff cutoff in metres.

    /// Effective luminance — the readback path uses this to rank samples
    /// for top-N selection. Standard Rec. 709 luma weighting.
    [[nodiscard]] float Luminance() const noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// Attenuation
// ─────────────────────────────────────────────────────────────────────────────

/// Karis inverse-square attenuation with smooth radius cutoff.
///
/// Returns a scalar multiplier in [0, 1]. At `d = 0` returns 1 (max),
/// at `d >= radius` returns 0 (fully attenuated). Between, the curve is
/// `saturate(1 - (d/r)^4)^2 / (d^2 + 1)`.
[[nodiscard]] float VfxDDGIAttenuation(float distance, float influenceRadius) noexcept;

/// Full per-sample contribution at a probe position:
///
/// ```
/// return sample.emissive · VfxDDGIAttenuation(|probe - sample.position|, sample.radius)
/// ```
///
/// Returns zero vector when the sample is beyond its influence radius so
/// callers can cheaply early-out on far samples.
[[nodiscard]] Vec3 VfxDDGIProbeContribution(const VfxEmissiveSample& sample,
                                               const Vec3& probePosition) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Top-N selection (mirrors the 4.13 readback sort for tests)
// ─────────────────────────────────────────────────────────────────────────────

/// Picks the top-N samples by luminance from the input buffer into the
/// output buffer. In-place partial sort; `out` may alias `in` when the
/// caller wants to sort in-place (pass the same pointer for both).
///
/// `outCount` clamps to `min(inCount, maxN)`. Lower-luminance samples
/// are discarded.
///
/// # Arguments
/// - `in`       — source sample array.
/// - `inCount`  — source count.
/// - `out`      — destination buffer; caller ensures ≥ min(inCount, maxN).
/// - `maxN`     — target count.
///
/// # Returns
/// The number of samples actually written to `out`.
[[nodiscard]] uint32_t VfxDDGISelectTopN(const VfxEmissiveSample* in,
                                           uint32_t inCount,
                                           VfxEmissiveSample* out,
                                           uint32_t maxN) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Full-probe accumulation
// ─────────────────────────────────────────────────────────────────────────────

/// Accumulates total emissive irradiance at `probePosition` across `count`
/// samples. The returned vector is in the same linear-RGB space as the
/// sample's `emissive` field.
[[nodiscard]] Vec3 VfxDDGIAccumulateAtProbe(const VfxEmissiveSample* samples,
                                               uint32_t count,
                                               const Vec3& probePosition) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Per-emitter sample synthesis (phase16_vfx-lumen-gi-contribution)
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a `VfxEmissiveSample` from a single emitter's CPU state.
///
/// Approximates the emitter as one virtual point light located at its
/// world position. Radiance = `initialColor.rgb * emissiveIntensity *
/// aliveFraction`, where `aliveFraction = clamp(aliveCount / maxParticles, 0, 1)`
/// — a denser emitter outweighs a sparser one with the same authored
/// intensity. Influence radius = `max(kMinRadiusCm, spawnRadius * 4)`,
/// reflecting that explosions / magic effects radiate well beyond their
/// authored spawn shape.
///
/// The function is pure CPU; the caller (`GatherAndUploadVfxEmissiveSamples`)
/// produces an array of these and forwards them to the GPU upload feed.
///
/// # Arguments
/// - `worldPosition`     — emitter centre.
/// - `colorRgb`          — `initialColor.xyz` in linear RGB.
/// - `emissiveIntensity` — emitter's authored emissive multiplier.
/// - `aliveCount`        — particles currently alive on the emitter.
/// - `maxParticles`      — emitter capacity (denominator for alive fraction).
/// - `spawnRadius`       — authored spawn radius in cm.
///
/// # Returns
/// A populated `VfxEmissiveSample`. When `emissiveIntensity <= 0` or
/// `aliveCount == 0`, every channel of `emissive` is zero — the caller
/// is expected to skip such emitters before adding them to the feed.
[[nodiscard]] VfxEmissiveSample VfxDDGIBuildSampleForEmitter(
    const Vec3&  worldPosition,
    const Vec3&  colorRgb,
    float        emissiveIntensity,
    uint32_t     aliveCount,
    uint32_t     maxParticles,
    float        spawnRadius) noexcept;

/// Minimum influence radius in centimetres applied by
/// `VfxDDGIBuildSampleForEmitter`. Floors small spawn radii so a tiny
/// spark still lights its immediate vicinity (1 metre).
constexpr float kVfxDDGIMinInfluenceRadiusCm = 100.0f;

} // namespace uz
