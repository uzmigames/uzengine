#pragma once

/// VFX volumetric particle renderer — density-splat + raymarch.
///
/// Alternative to billboard overdraw for large low-density effects (smoke
/// clouds, dust, fog sheets). Particles splat their density into a 3D grid;
/// a fullscreen raymarch samples the grid with Beer–Lambert absorption and
/// a Henyey–Greenstein phase function so back-scattering behaves correctly.
///
/// # Pipeline
///
/// ```
/// CS:  For each alive particle, scatter its density into the 3D grid
///      within an AABB centered on its position (3×3×3 tent filter).
/// PS:  For each screen pixel, march from camera through the grid AABB
///      accumulating transmittance (Beer-Lambert) and in-scattering
///      (Henyey-Greenstein · sunColor) until opacity saturates or the
///      ray exits the volume.
/// ```
///
/// This header ships the configuration struct plus the CPU-side math
/// helpers (grid coordinate mapping, HG phase function, Beer-Lambert
/// transmittance) that the HLSL shaders mirror. Unit tests exercise the
/// CPU helpers; visual verification is a follow-up GPU task.
///
/// @ref UE5 FNiagaraVolumetricRenderer
/// @ref Henyey & Greenstein 1941 "Diffuse radiation in the galaxy"
/// @ref Beer 1852 "Bestimmung der Absorption rothen Lichts in farbigen Flüssigkeiten"
/// @ref docs/engine/25-vfx/32-volumetric-renderer.md

#include "vector3.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for a single volumetric particle emitter.
struct VfxVolumetricConfig {
    /// Grid resolution (voxels per axis). Total voxels = res³.
    /// Typical: 64 (editor preview) to 256 (shipped).
    uint32_t gridResolution = 128u;

    /// World-space AABB half-extent (metres). The grid covers a cube of
    /// side `2 * halfExtent` centred on `gridCenter`.
    float halfExtent = 50.0f;

    /// World-space grid center. Emitters typically update this per-frame to
    /// follow their bounding box centroid so off-axis emissions stay inside.
    Vec3 gridCenter{0, 0, 0};

    /// Per-particle density contribution before splat (0..∞). Higher values
    /// produce denser clouds at the same particle count.
    float densityPerParticle = 0.1f;

    /// Raymarch step count. Balances quality vs cost; 32–64 is typical.
    uint32_t raymarchSteps = 48u;

    /// Extinction coefficient (σ_t, 1/m). Drives Beer–Lambert absorption.
    /// Higher = denser-looking clouds. Typical: 0.5 – 3.0.
    float extinctionCoeff = 1.5f;

    /// Henyey–Greenstein anisotropy parameter `g` in (-1, +1).
    ///   g > 0 : forward-scattering (clouds, dust — light continues).
    ///   g < 0 : back-scattering.
    ///   g = 0 : isotropic.
    /// UE5 default: 0.8 (strongly forward, realistic water droplets).
    float hgAnisotropy = 0.8f;

    /// In-scattering colour (linear RGB, multiplied by sun intensity at
    /// sample time). Typically the sun colour or the atmosphere's single-
    /// scattering contribution.
    Vec3 scatteringColor{1.0f, 0.95f, 0.85f};
};

// ─────────────────────────────────────────────────────────────────────────────
// Grid coordinate mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a world-space position to a voxel index triplet `(ix, iy, iz)`.
///
/// Out-of-bounds positions return the closest edge voxel rather than a
/// negative / overflow index, so callers don't need to pre-filter.
/// The mapping is uniform: `worldPos → (worldPos - gridCenter) / (2·hE) + 0.5`
/// then scale by `res` and floor.
///
/// # Arguments
/// - `cfg` — grid configuration.
/// - `worldPos` — world-space sample point.
/// - `outX/outY/outZ` — voxel indices clamped to `[0, res-1]`.
///
/// # Returns
/// `true` when the sample falls inside the grid AABB; `false` when it was
/// clamped. Callers use the boolean to decide whether to splat.
[[nodiscard]] bool VfxVolumetricWorldToVoxel(const VfxVolumetricConfig& cfg,
                                               const Vec3& worldPos,
                                               uint32_t& outX,
                                               uint32_t& outY,
                                               uint32_t& outZ) noexcept;

/// Converts a voxel center from grid coordinates back to world space.
[[nodiscard]] Vec3 VfxVolumetricVoxelToWorld(const VfxVolumetricConfig& cfg,
                                               uint32_t ix, uint32_t iy, uint32_t iz) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Henyey–Greenstein phase function
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates the Henyey–Greenstein phase function at cosine angle `cosTheta`.
///
/// `cosTheta` is the dot product of the view direction with the light
/// direction. `g` controls anisotropy:
///   - g → +1 : strongly forward
///   - g = 0  : isotropic (returns 1/(4π))
///   - g → -1 : strongly back
///
/// Formula (Henyey & Greenstein 1941):
///   p(θ) = (1 − g²) / (4π · (1 + g² − 2g·cosθ)^{1.5})
///
/// `g` is clamped to `[-0.9999, 0.9999]` to avoid the pole at `|g| = 1`.
[[nodiscard]] float VfxHenyeyGreenstein(float cosTheta, float g) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Beer–Lambert transmittance
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates Beer–Lambert transmittance `exp(-σ_t · density · stepDistance)`.
///
/// Returns the fraction of light that survives a single raymarch step of
/// length `stepDistance` through a region of `density` units per volume.
[[nodiscard]] float VfxBeerLambertTransmittance(float extinctionCoeff,
                                                  float density,
                                                  float stepDistance) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Raymarch step contribution
// ─────────────────────────────────────────────────────────────────────────────

/// Single-step in-scattering contribution combining HG phase, density,
/// transmittance, and scattering colour. The raymarch accumulates this
/// into the final pixel colour and reduces running transmittance by the
/// per-step factor.
///
/// `stepTransmittance` = `VfxBeerLambertTransmittance(...)` at the sample.
/// `accumulatedTransmittance` is reduced by the caller as:
///   `transmittance *= stepTransmittance`
/// after consuming the returned contribution.
///
/// # Returns
/// Vec3 `(r, g, b)` in the scattering colour's linear RGB space.
[[nodiscard]] Vec3 VfxVolumetricStepContribution(
    const VfxVolumetricConfig& cfg,
    float density,
    float cosTheta,
    float stepTransmittance,
    float accumulatedTransmittance) noexcept;

} // namespace uz
