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
#include "multi_light.h"

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
    /// @source UE5 SceneRendering.cpp — no hardcoded ambient minimum; all ambient
    /// comes from the SkyLight SH system. Zero when no SkyLight exists.
    Vec3 ambientColor = {0.0f, 0.0f, 0.0f};
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

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameMultiLightCB — multi-light CB population
// ─────────────────────────────────────────────────────────────────────────────

/// Populates a `PerFrameMultiLightCB` from camera state, timing, lighting,
/// and an array of CPU-side dynamic lights.
///
/// The directional light from `SceneLighting` is injected as `lightArray[0]`
/// (type = Directional, infinite range), then `SelectNearestLights()` fills
/// the remaining slots with the nearest point/spot lights from `candidates`.
///
/// # Arguments
/// * `camera`     — Camera providing view/projection matrices and position.
/// * `totalTime`  — Total elapsed time in seconds since application start.
/// * `deltaTime`  — Frame delta time in seconds.
/// * `frameIndex` — Current frame number (cast to float for shader use).
/// * `lighting`   — Scene lighting parameters (directional + ambient).
/// * `candidates` — Array of CPU-side dynamic lights to cull and upload.
/// * `candidateCount` — Number of entries in `candidates`.
///
/// # Returns
/// A fully populated `PerFrameMultiLightCB` ready for GPU upload (768 bytes).
[[nodiscard]] inline PerFrameMultiLightCB BuildPerFrameMultiLightCB(
    const Camera&        camera,
    float                totalTime,
    float                deltaTime,
    uint32_t             frameIndex,
    const SceneLighting& lighting,
    const LightCPU*      candidates,
    uint32_t             candidateCount)
{
    const Vec3& camPos = camera.GetPosition();

    // Start with the multi-light CB from candidates (fills lightArray via culling).
    PerFrameMultiLightCB cb = BuildMultiLightCB(
        candidates, candidateCount, camPos, lighting.ambientColor);

    // Fill camera and timing fields.
    cb.viewProj   = camera.GetViewProjection();
    cb.cameraPos  = {camPos.x, camPos.y, camPos.z, 0.0f};
    cb.timeParams = {totalTime, deltaTime, static_cast<float>(frameIndex), 0.0f};

    // Inject the directional sun light as lightArray[0] by shifting existing
    // lights right by one slot (up to kMaxMultiLights - 1).
    if (cb.lightCount < kMaxMultiLights) {
        // Shift existing lights to make room at index 0.
        for (uint32_t i = cb.lightCount; i > 0; --i) {
            cb.lightArray[i] = cb.lightArray[i - 1];
        }
        ++cb.lightCount;
    } else {
        // Array full — shift and evict the last light.
        for (uint32_t i = kMaxMultiLights - 1; i > 0; --i) {
            cb.lightArray[i] = cb.lightArray[i - 1];
        }
    }

    // Write directional sun light at slot 0.
    LightGPU& sun  = cb.lightArray[0];
    sun             = LightGPU{};
    sun.positionOrDir = lighting.sunDirection;
    sun.range         = 0.0f; // Directional — infinite range.
    sun.color         = lighting.sunColor;
    sun.lightType     = static_cast<float>(static_cast<uint32_t>(LightType::Directional));

    return cb;
}

} // namespace uz
