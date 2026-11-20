/// UzEngine Volumetric Fog Pass — Froxel-based 3D fog.
///
/// Three-phase pipeline (phase2_froxel-volumetric-fog):
///   1. Inject (compute): density + in-scatter at each froxel — sun (cascaded-shadowed), point and
///      spot lights (local-atlas-shadowed), sky ambient, Henyey-Greenstein phase — written as
///      (in-scatter rgb, extinction) per froxel, sample positions jittered per frame.
///   2. Scatter (compute): temporal reprojection of the injected volume (previous unjittered
///      view-projection, EMA alpha ~0.05, neighbourhood variance clamp, frustum validity), then the
///      front-to-back integration of in-scatter and transmittance per column.
///   3. Composite (pixel): fullscreen pass samples the integrated volume at the scene depth (sky at
///      the far edge of the grid) and blends it over the lit, height-fogged scene.
///
/// Grid: 160×90×64 RGBA16F — injected (rgb in-scatter, a extinction), integrated (rgb, a
/// transmittance) and two temporal-history ping-pong volumes (allocated on the first temporal frame).
/// Depth distribution: exponential (more slices near the camera).
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"

#pragma once

#include "render_graph_types.h"
#include "cb_ring_allocator.h"
#include "engine/rhi/rhi_types.h"
#include "mat4.h"

#include <cstddef>
#include <cstdint>

namespace uz {

class IRHIDevice;
class PipelineLibrary;

namespace rg { class RgBuilder; }

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Hillaire 2015 — 160×90×64 grid is standard Frostbite resolution.
static constexpr uint32_t kFroxelGridX = 160;
static constexpr uint32_t kFroxelGridY = 90;
static constexpr uint32_t kFroxelGridZ = 64;

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogCB — constant buffer for all 3 phases (256-byte aligned)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-dispatch parameters for froxel fog compute + composite shaders.
struct VolumetricFogCB {
    Mat4     invViewProj;          ///< Inverse view-projection (64 bytes).
    float    cameraPosX;           ///< Camera world position X (cm).
    float    cameraPosY;           ///< Camera world position Y (cm).
    float    cameraPosZ;           ///< Camera world position Z (cm).
    float    nearZ;                ///< Near clip plane (cm).
    float    farZ;                 ///< Far fog distance (cm, e.g. 640000 = 6.4km).
    float    globalDensity;        ///< Base fog density.
    float    heightFalloff;        ///< Exponential height falloff rate.
    float    heightOffset;         ///< Height offset for fog base (cm).
    float    albedoR;              ///< Fog albedo R (scattering color).
    float    albedoG;              ///< Fog albedo G.
    float    albedoB;              ///< Fog albedo B.
    float    phaseG;               ///< HG phase function asymmetry (-1..1, default 0.5).
    float    sunDirX;              ///< Sun direction X (normalised, toward sun).
    float    sunDirY;              ///< Sun direction Y.
    float    sunDirZ;              ///< Sun direction Z.
    float    sunIntensity;         ///< Sun illuminance scalar.
    float    sunColorR;            ///< Sun color R.
    float    sunColorG;            ///< Sun color G.
    float    sunColorB;            ///< Sun color B.
    float    temporalAlpha;        ///< Temporal blend weight (0.05 typical).
    uint32_t inscatteringUavIdx;   ///< UAV index for inscattering 3D volume (inject writes).
    uint32_t inscatteringSrvIdx;   ///< SRV index for inscattering 3D volume (scatter reads).
    uint32_t integratedUavIdx;     ///< UAV index for integrated volume (scatter writes).
    uint32_t integratedSrvIdx;     ///< SRV index for integrated volume (composite reads).
    uint32_t depthSrvIdx;          ///< SRV index for scene depth.
    uint32_t frameIndex;           ///< Frame counter for temporal jitter.
    uint32_t gridX;                ///< Froxel grid width (160).
    uint32_t gridY;                ///< Froxel grid height (90).
    uint32_t gridZ;                ///< Froxel grid depth (64).
    float    extinctionScale;      ///< Extinction = density * extinctionScale.
    float    emissiveR;            ///< Fog emissive R.
    float    emissiveG;            ///< Fog emissive G.
    float    emissiveB;            ///< Fog emissive B.
    float    fogStartDistance;     ///< Min depth for fog grid (cm). Pushes slice 0 away from camera.
    float    nearFadeInDistance;   ///< Fade-in ramp distance (cm). 0 = no fade.
    float    cameraFarZ = 0.0f;    ///< Camera far plane (cm): the composite linearises depth with it (block B.4).
    uint32_t sunShadowEnabled = 1; ///< r.VolumetricFog.SunShadow: the sun in-scatter is shadowed by the CSM.
    uint32_t debugMode = 0;        ///< r.VolumetricFog.Debug (offset 212): the composite writes its depth / slice / transmittance.
    // ── Temporal reprojection + local lights (phase2_froxel-volumetric-fog), offset 216 ──
    uint32_t historySrvIdx = UINT32_MAX;   ///< SRV of the previous frame's filtered injected volume (scatter reads).
    uint32_t historyUavIdx = UINT32_MAX;   ///< UAV of this frame's filtered injected volume (scatter writes).
    uint32_t temporalEnabled = 0;          ///< 1 = the scatter blends the reprojected history (valid history, continuous camera).
    uint32_t localLightsEnabled = 0;       ///< 1 = the inject adds the point and spot lights of LightingCB (b1).
    uint32_t localShadowEnabled = 0;       ///< 1 = the local lights are shadowed by the local shadow atlas (one tap).
    uint32_t jitterEnabled = 0;            ///< 1 = the inject jitters the sample position inside each froxel per frame.
    uint32_t debugStage = 0;               ///< r.VolumetricFog.Stage: 0 = full, 1 = solid colour, 2 = density only, 3 = + sun, 4 = + shadows, 5 = + temporal.
    float    temporalClamp = 1.5f;         ///< History clamp half-width in neighbourhood standard deviations (0 = no clamp).
    float    ambientR = 0.0f;              ///< Sky ambient radiance for the isotropic in-scatter (scalars: a float3 at 248 would move to 256).
    float    ambientG = 0.0f;
    float    ambientB = 0.0f;
    float    localLightScale = 1.0f;       ///< Multiplier on the point / spot in-scatter (VolumetricFogComponent::localLightScattering).
    float    _pad0[2] = {};                ///< 264..272: align the matrix to 16 bytes.
    Mat4     prevViewProj;                 ///< Previous frame's unjittered view-projection (offset 272): froxel reprojection.
    float    _pad1[44] = {};               ///< Pad to 512 bytes (336 data + 176 pad).
};

static_assert(sizeof(VolumetricFogCB) == 512,
    "VolumetricFogCB must be a multiple of 256 bytes (D3D12 CB alignment)");
static_assert(offsetof(VolumetricFogCB, historySrvIdx) == 216);
static_assert(offsetof(VolumetricFogCB, debugStage) == 240);
static_assert(offsetof(VolumetricFogCB, ambientR) == 248);
static_assert(offsetof(VolumetricFogCB, localLightScale) == 260);
static_assert(offsetof(VolumetricFogCB, prevViewProj) == 272);
// The HLSL copies declare scalars where a float3 would be padded (scripts/rendering/test_cbuffer_layouts.py, block B.4).
static_assert(offsetof(VolumetricFogCB, extinctionScale) == 180);
static_assert(offsetof(VolumetricFogCB, emissiveR) == 184);
static_assert(offsetof(VolumetricFogCB, fogStartDistance) == 196);
static_assert(offsetof(VolumetricFogCB, nearFadeInDistance) == 200);
static_assert(offsetof(VolumetricFogCB, cameraFarZ) == 204);
static_assert(offsetof(VolumetricFogCB, sunShadowEnabled) == 208);
static_assert(offsetof(VolumetricFogCB, debugMode) == 212);

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogFrameInputs — what the frame hands the three passes
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame render-graph resources and constant buffers the fog passes bind.
/// The inject pass reads both shadow atlases (so the graph orders it after the shadow passes) and
/// binds ShadowDataCB at b4 and LightingCB (the frame's point and spot lights) at b1; the composite
/// writes `hdrRes` after every earlier writer of the frame (the height fog among them).
struct VolumetricFogFrameInputs {
    rg::RgResourceId hdrRes{};
    rg::RgResourceId depthRes{};
    rg::RgResourceId shadowAtlasRes{};        ///< Cascaded shadow atlas (sun volumetric shadows).
    rg::RgResourceId localShadowAtlasRes{};   ///< Local light atlas (point / spot volumetric shadows).
    CBAllocation perFrameAlloc{};
    CBAllocation shadowAlloc{};               ///< ShadowDataCB of the frame (b4).
    CBAllocation lightingAlloc{};             ///< LightingCB of the frame (b1 in the inject).
    bool  bindless = false;
    float vpWidth = 0.f;
    float vpHeight = 0.f;
    /// The temporal filter is wanted this frame (r.VolumetricFog.Temporal and the stage allow it).
    bool  temporalWanted = false;
    /// The previous frame's matrix is continuous with this one (TemporalDecision::matrixValid);
    /// false restarts the history (camera cut, frame gap, resize).
    bool  historyReusable = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogPass — manages 3D textures and dispatches
// ─────────────────────────────────────────────────────────────────────────────

/// Owns the froxel grid textures and registers the three fog passes.
class VolumetricFogPass {
public:
    /// Initialize 3D textures (160×90×64). Returns true on success.
    bool Init(IRHIDevice* device);

    /// Release GPU resources.
    void Shutdown(IRHIDevice* device);

    bool IsInitialized() const { return m_initialized; }

    /// Register inject + scatter + composite passes in the render graph. `params` carries the
    /// medium, the camera and the lights; the pass fills the descriptor indices, the temporal state
    /// (history ping-pong, `temporalEnabled`) and `prevViewProj` must already be set by the caller.
    void RegisterPasses(
        rg::RgBuilder& rgBuilder,
        const VolumetricFogFrameInputs& in,
        const VolumetricFogCB& params,
        CBRingAllocator& cbRing,
        PipelineLibrary* pipelines);

    /// Call on frames the fog does not render: the next temporal frame restarts its history.
    void InvalidateHistory() { m_historyValid = false; }

    // ── Accessors ─────────────────────────────────────────────────────
    RHITextureHandle GetInscatteringTexture() const { return m_inscattering; }
    uint32_t GetInscatteringSrv() const { return m_inscatteringSrv; }
    uint32_t GetInscatteringUav() const { return m_inscatteringUav; }

    RHITextureHandle GetIntegratedTexture() const { return m_integrated; }
    uint32_t GetIntegratedSrv() const { return m_integratedSrv; }
    uint32_t GetIntegratedUav() const { return m_integratedUav; }

    bool HistoryValid() const { return m_historyValid; }

private:
    bool EnsureHistory();

    bool m_initialized = false;
    IRHIDevice* m_device = nullptr;

    // Inscattering + extinction (inject output, scatter input)
    RHITextureHandle m_inscattering;
    uint32_t m_inscatteringSrv = UINT32_MAX;
    uint32_t m_inscatteringUav = UINT32_MAX;

    // Integrated inscattering + transmittance (scatter output, composite input)
    RHITextureHandle m_integrated;
    uint32_t m_integratedSrv = UINT32_MAX;
    uint32_t m_integratedUav = UINT32_MAX;

    // Temporally filtered injected volume, ping-pong (scatter reads [read], writes [write]).
    // Allocated on the first frame the temporal filter runs (OFF costs no memory).
    RHITextureHandle m_history[2];
    uint32_t m_historySrv[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t m_historyUav[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t m_historyWrite = 0;
    bool     m_historyValid = false;   ///< m_history[1 - m_historyWrite] holds the previous frame.
};

} // namespace uz
