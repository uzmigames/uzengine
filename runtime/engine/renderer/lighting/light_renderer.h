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

/// Per-spot-light shadow info collected from ECS, used to build shadow passes.
///
/// Populated by `ExtractLightingFromECS()` for each spot light with castShadows=true.
/// The scene renderer uses this data to set up the shadow depth render pass
/// and fill the LocalShadowCB entry.
struct SpotLightShadowInfo {
    Vec3     position;    ///< World-space position of the spot light.
    Vec3     direction;   ///< Normalised world-space cone direction.
    float    outerAngle;  ///< Outer cone half-angle in radians (clamped).
    float    radius;      ///< Attenuation radius (far plane for shadow projection).
    float    bias;        ///< Shadow depth bias.
    uint32_t lightIndex;  ///< Index into LightingCB::spotLights[] for this light.
};

/// Number of cubemap faces rendered per point light shadow (one per axis direction).
static constexpr uint32_t kPointLightShadowFaces = 6;

/// Per-point-light shadow info collected from ECS, used to build shadow passes.
///
/// Populated by `ExtractLightingFromECS()` for each point light with castShadows=true.
/// The scene renderer uses this data to set up 6 cubemap face shadow depth passes
/// and fill 6 consecutive LocalShadowCB entries.
struct PointLightShadowInfo {
    Vec3     position;    ///< World-space position of the point light.
    float    radius;      ///< Attenuation radius (far plane for shadow projection).
    float    bias;        ///< Shadow depth bias.
    uint32_t lightIndex;  ///< Index into LightingCB::pointLights[] for this light.
};

/// Contains all lighting data extracted from the ECS for the current frame.
///
/// Includes directional light parameters (direction, color, shadow bias),
/// the uploaded LightingCB allocation for point/spot lights, and
/// spot/point light shadow info for the scene renderer to build shadow passes.
struct LightFrameData {
    /// Normalized direction toward the sun. With no DirectionalLightComponent in the world it is
    /// SkyAtmosphereComponent::sunDirection (the sky still needs one); the value here is only the
    /// fallback for a world with neither.
    Vec3  sunDir   = {0.5f, 0.3f, 0.8f};
    /// Sun color * intensity (linear HDR). Zero unless a DirectionalLightComponent exists: until
    /// 2026-10-05 the default was (3, 2.85, 2.7) and every scene without a sun entity was lit by
    /// that phantom light (docs/analysis/hardcoded/01-rendering-defaults.md, re-audit 2026-10-05).
    Vec3  sunColor = {0.0f, 0.0f, 0.0f};
    bool  hasDirectionalLight = false;    ///< True when a DirectionalLightComponent was found.
    Vec3  ambientColor = {0.0f, 0.0f, 0.0f}; ///< Ambient light color (zero unless populated by SkyLight SH).
    float perLightShadowBias      = 0.5f; ///< Per-light shadow bias.
    float perLightShadowSlopeBias = 0.5f; ///< Per-light slope bias.
    float lightSourceAngleDeg     = 0.5357f; ///< Sun disk angular diameter in degrees.

    uint32_t farShadowCascadeCount = 0;      ///< Far shadow cascade count (0 = disabled).
    float farShadowDistance = 300000.0f;      ///< Far shadow max distance in cm.
    int32_t farShadowLodBias = 0;             ///< LOD bias for far shadow cascades.
    float dynamicShadowDistance = 40000.0f;   ///< CSM max distance in cm (DirectionalLightComponent).

    CBAllocation lightingCBAlloc;          ///< LightingCB upload in CB ring.

    /// Spot lights that want shadow maps (castShadows=true).
    /// Limited to kMaxLocalLightShadows entries. Excess lights are unshadowed.
    SpotLightShadowInfo spotShadows[kMaxLocalLightShadows];
    uint32_t spotShadowCount = 0;

    /// Point lights that want cubemap shadow maps (castShadows=true).
    /// Each point light consumes kPointLightShadowFaces (6) tiles in the local
    /// shadow atlas, so the maximum number of shadowed point lights is
    /// (kMaxLocalLightShadows - spotShadowCount) / kPointLightShadowFaces.
    /// Excess lights beyond the tile budget are left unshadowed.
    PointLightShadowInfo pointShadows[kMaxLocalLightShadows / kPointLightShadowFaces];
    uint32_t pointShadowCount = 0;

    /// Rect area lights of the frame (phase3a 3.33.0), uploaded by PopulatePerFrameConstants into
    /// the per-frame StructuredBuffer the UZ_RECT_LIGHTS lighting permutation reads. Empty (count 0)
    /// unless the world has a RectLightComponent and r.Lights.AreaLTC is on.
    RectLightData rectLights[kMaxRectLights];
    uint32_t rectLightCount = 0;
};

/// True when the world has at least one RectLightComponent and r.Lights.AreaLTC is on: the frame
/// setup then creates the LTC tables on demand (nothing is created for other scenes).
[[nodiscard]] bool WorldHasRectLights(World* world);

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
/// Shadow candidates are sorted by visual importance (intensity * radius^2 / distance^2)
/// so the most impactful lights receive shadow atlas tiles first.
///
/// # Arguments
/// * `world`          — The ECS World to query. May be null (uses defaults).
/// * `cbRing`         — CB ring allocator for uploading LightingCB.
/// * `cameraPosition` — World-space camera position for shadow importance ranking.
///
/// # Returns
/// LightFrameData with extracted light parameters and CB allocation.
[[nodiscard]] LightFrameData ExtractLightingFromECS(
    World* world,
    CBRingAllocator& cbRing,
    const Vec3& cameraPosition);

} // namespace uz
