/// UzEngine Scene Lighting & PerFrameCB Builder
///
/// Provides a `SceneLighting` struct to describe scene light data and a
/// `BuildPerFrameCB()` helper that populates a `PerFrameCB` from a Camera
/// and lighting parameters in a single call.
///
/// Eliminates the ~20 lines of identical per-frame CB setup duplicated
/// across pbr_test, deferred_test, and mesh_test demos.
///
/// # Usage
/// ```cpp
/// SceneLighting lighting;
/// lighting.sunDirection = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
/// lighting.sunColor     = {3.0f, 2.85f, 2.7f};
/// lighting.ambientColor = {0.03f, 0.03f, 0.05f};
///
/// PerFrameCB cb = BuildPerFrameCB(camera, totalTime, dt, frameIndex, lighting);
/// ```

#pragma once

#include "camera.h"
#include "constant_buffers.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SceneLighting — describes the scene's light environment
// ─────────────────────────────────────────────────────────────────────────────

/// Scene-level lighting parameters for the primary directional light and ambient.
///
/// This struct decouples light data from the raw `PerFrameCB` layout, making
/// it easy to pass lighting state between systems without knowing the CB format.
struct SceneLighting {
    /// Normalised direction toward the primary directional light source.
    ///
    /// Convention: points FROM the surface TOWARD the light (same as shader
    /// `lightDir`). Must be unit length.
    Vec3 sunDirection = Vec3{0.5f, 0.3f, 0.8f}.Normalized();

    /// Primary directional light color multiplied by intensity (linear HDR).
    ///
    /// Values > 1.0 are valid and expected for sunlight (typically 2.0–5.0).
    Vec3 sunColor = {3.0f, 2.85f, 2.7f};

    /// Ambient light color contribution (constant term in lighting equation).
    ///
    /// Represents indirect illumination. Keep low (0.01–0.1) for outdoor
    /// scenes. Higher values flatten contrast.
    Vec3 ambientColor = {0.03f, 0.03f, 0.05f};
};

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — one-call CB population
// ─────────────────────────────────────────────────────────────────────────────

/// Populates a `PerFrameCB` from camera state, timing, and lighting.
///
/// Computes `viewProj`, `invViewProj`, packs camera position, time parameters,
/// and lighting into the standard CB layout. Replaces the ~20 lines of manual
/// CB fill duplicated across every rendering demo.
///
/// # Arguments
/// * `camera`     — Camera providing view/projection matrices and position.
/// * `totalTime`  — Total elapsed time in seconds since application start.
/// * `deltaTime`  — Frame delta time in seconds.
/// * `frameIndex` — Current frame number (cast to float for shader use).
/// * `lighting`   — Scene lighting parameters.
///
/// # Returns
/// A fully populated `PerFrameCB` ready for GPU upload.
///
/// # Examples
/// ```cpp
/// PerFrameCB cb = BuildPerFrameCB(camera, 12.5f, 0.016f, 750, lighting);
/// memcpy(alloc.cpuPtr, &cb, sizeof(cb));
/// ```
[[nodiscard]] inline PerFrameCB BuildPerFrameCB(
    const Camera&        camera,
    float                totalTime,
    float                deltaTime,
    uint32_t             frameIndex,
    const SceneLighting& lighting)
{
    PerFrameCB cb{};

    cb.viewProj    = camera.GetViewProjection();
    cb.invViewProj = cb.viewProj.Inverse();

    const Vec3& camPos = camera.GetPosition();
    cb.cameraPos  = {camPos.x, camPos.y, camPos.z, 0.0f};
    cb.timeParams = {totalTime, deltaTime, static_cast<float>(frameIndex), 0.0f};

    const Vec3& sunDir = lighting.sunDirection;
    cb.lightDir     = {sunDir.x, sunDir.y, sunDir.z, 0.0f};
    cb.lightColor   = {lighting.sunColor.x, lighting.sunColor.y, lighting.sunColor.z, 0.0f};
    cb.ambientColor = {lighting.ambientColor.x, lighting.ambientColor.y, lighting.ambientColor.z, 0.0f};

    cb.nearPlane = camera.GetNearPlane();
    cb.farPlane  = camera.GetFarPlane();

    return cb;
}

} // namespace uz
