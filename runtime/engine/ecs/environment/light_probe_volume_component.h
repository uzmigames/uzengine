/// UzEngine LightProbeVolumeComponent — Author-placed irradiance probe volume.
///
/// Defines a box region within which a 3D grid of L2 SH probes captures
/// indirect diffuse lighting. Equivalent to standard engine "Light Probe
/// Volume" / "Irradiance Volume" / "Volumetric Lightmap" authoring primitives.
///
/// The renderer gathers all enabled volumes, builds an `IrradianceProbeGrid`
/// per volume, and samples the probes during deferred lighting for indirect
/// diffuse contribution that varies spatially through indoor spaces.
///
/// # References
/// @ref Tatarchuk 2005 "Irradiance Volumes for Games" — authored probe grids
/// @ref Greger 1998 "The Irradiance Volume" — SH-based volumetric irradiance

#pragma once

#include "vector3.h"

#include <cstdint>

namespace uz {

/// Author-placed box volume containing a 3D grid of irradiance probes.
///
/// The volume stores only configuration. The renderer allocates probe SH
/// storage in an `IrradianceProbeGrid` keyed by the entity id and samples
/// the grid during deferred lighting.
///
/// # Coordinate Convention
/// `extentMin`/`extentMax` are world-space AABB corners in centimeters.
/// `autoFitScene` causes the renderer to override the stored extents with
/// the scene's bounding box each frame (useful during iteration).
struct LightProbeVolumeComponent {
    // ── Grid Layout ──────────────────────────────────────────────────────

    /// Number of probes along each axis (X, Y, Z).
    /// 8x4x8 = 256 probes is a balanced default for a medium interior.
    /// @ref Industry default — irradiance volumes commonly use 6-12 per axis.
    uint32_t gridDimensionsX = 8;
    uint32_t gridDimensionsY = 4;
    uint32_t gridDimensionsZ = 8;

    // ── World-Space Extents ──────────────────────────────────────────────

    /// Minimum corner of the probe volume AABB (cm).
    Vec3 extentMin{-500.0f, 0.0f, -500.0f};

    /// Maximum corner of the probe volume AABB (cm).
    Vec3 extentMax{500.0f, 400.0f, 500.0f};

    /// When true, the renderer overrides `extentMin`/`extentMax` with the
    /// current scene bounding box each frame. Useful for "just works"
    /// placement during iteration; disable for authored tight bounds.
    bool autoFitScene = false;

    /// Padding applied to the auto-fit AABB (cm). Pushes probes slightly
    /// beyond scene geometry so wall surfaces sample valid interior probes.
    float autoFitPadding = 100.0f;

    // ── State ────────────────────────────────────────────────────────────

    /// Whether this volume contributes to lighting.
    bool enabled = true;

    /// Dirty flag — set when extents or dimensions change, cleared after
    /// the renderer rebuilds the probe grid.
    bool needsRebuild = true;

    // ── Runtime (filled by renderer, not user) ───────────────────────────

    /// SRV index of the probe SH structured buffer in the bindless heap.
    /// `UINT32_MAX` indicates the volume has not been built yet.
    uint32_t probeBufferSrv = UINT32_MAX;
};

// ─────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────

/// Returns the total probe count for a volume (`dimX * dimY * dimZ`).
[[nodiscard]] inline uint32_t GetProbeCount(const LightProbeVolumeComponent& v) noexcept {
    return v.gridDimensionsX * v.gridDimensionsY * v.gridDimensionsZ;
}

/// Returns the world-space center of the volume.
[[nodiscard]] inline Vec3 GetVolumeCenter(const LightProbeVolumeComponent& v) noexcept {
    return Vec3{
        (v.extentMin.x + v.extentMax.x) * 0.5f,
        (v.extentMin.y + v.extentMax.y) * 0.5f,
        (v.extentMin.z + v.extentMax.z) * 0.5f,
    };
}

/// Returns the world-space size (extent) of the volume.
[[nodiscard]] inline Vec3 GetVolumeSize(const LightProbeVolumeComponent& v) noexcept {
    return Vec3{
        v.extentMax.x - v.extentMin.x,
        v.extentMax.y - v.extentMin.y,
        v.extentMax.z - v.extentMin.z,
    };
}

} // namespace uz
