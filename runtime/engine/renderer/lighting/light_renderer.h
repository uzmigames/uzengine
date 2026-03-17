/// UzEngine Light Renderer — Per-Frame ECS Light Extraction
///
/// Reads DirectionalLightComponent, PointLightComponent, and SpotLightComponent
/// from the ECS World, builds the per-frame LightingCB (point and spot light arrays),
/// and extracts directional light parameters (direction, color, per-light shadow bias).
///
/// Factored out of `scene_renderer.cpp` to keep `RenderFrame()` as an orchestrator.
/// Free functions — no class state.

#pragma once

#include "constant_buffers.h"
#include "cb_ring_allocator.h"
#include "vector3.h"

#include <cstdint>

namespace uz {

class World;

// ─────────────────────────────────────────────────────────────────────────────
// LightFrameData — per-frame light extraction result
// ─────────────────────────────────────────────────────────────────────────────

/// Contains all lighting data extracted from the ECS for the current frame.
///
/// Includes directional light parameters (direction, color, shadow bias) and
/// the uploaded LightingCB allocation for point lights.
struct LightFrameData {
    Vec3  sunDir   = {0.5f, 0.3f, 0.8f}; ///< Normalized direction toward the sun.
    Vec3  sunColor = {3.0f, 2.85f, 2.7f}; ///< Sun color * intensity (linear HDR).
    Vec3  ambientColor = {0.03f, 0.03f, 0.05f}; ///< Ambient light color.
    float perLightShadowBias      = 0.5f; ///< Per-light shadow bias (UE5: ShadowBias).
    float perLightShadowSlopeBias = 0.5f; ///< Per-light slope bias (UE5: ShadowSlopeBias).
    float lightSourceAngleDeg     = 0.5357f; ///< Sun disk angular diameter in degrees (UE5: LightSourceAngle).

    /// Far shadow cascade count (0 = disabled). @source UE5 DirectionalLightComponent.h:112
    uint32_t farShadowCascadeCount = 0;
    /// Far shadow max distance in cm. @source UE5 DirectionalLightComponent.cpp:1023
    float farShadowDistance = 300000.0f;
    /// LOD bias for far shadow cascades. @source UE5 DirectionalLightComponent.cpp:68
    int32_t farShadowLodBias = 0;

    CBAllocation lightingCBAlloc;          ///< LightingCB upload in CB ring.
};

// ─────────────────────────────────────────────────────────────────────────────
// ExtractLightingFromECS
// ─────────────────────────────────────────────────────────────────────────────

/// Extracts directional and point light data from the ECS World.
///
/// Iterates DirectionalLightComponent (first found) for sun direction, color,
/// temperature tinting, and per-light shadow bias. Iterates PointLightComponent
/// to build the point light array (up to 64 lights) and SpotLightComponent to
/// build the spot light array (up to 16 lights). Uploads LightingCB to the CB ring.
///
/// # Arguments
/// * `world`  — The ECS World to query. May be null (uses defaults).
/// * `cbRing` — CB ring allocator for uploading LightingCB.
///
/// # Returns
/// LightFrameData with extracted light parameters and CB allocation.
[[nodiscard]] LightFrameData ExtractLightingFromECS(
    World* world,
    CBRingAllocator& cbRing);

} // namespace uz
