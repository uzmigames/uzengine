/// UzEngine Atmosphere Renderer — Sky, Transmittance, IBL, and SH
///
/// Handles all atmosphere-related rendering: transmittance LUT compute,
/// sky cubemap capture, sky atmosphere fullscreen pass, post-frame IBL bake,
/// and spherical harmonics projection. Reads SkyAtmosphereComponent from ECS.
///
/// Split from `scene_renderer.cpp` to keep RenderFrame as a thin orchestrator.

#pragma once

#include "constant_buffers.h"
#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "rhi_types.h"
#include "sh_projection.h"
#include "ibl_baker.h"
#include "vector3.h"

#include <cstdint>

namespace uz {

class World;
class Camera;
class PipelineLibrary;
class IRHIDevice;
class IRHICommandList;

namespace rg { class RgBuilder; }

// ─────────────────────────────────────────────────────────────────────────────
// AtmosphereFrameContext — parameters for atmosphere passes
// ─────────────────────────────────────────────────────────────────────────────

/// Carries all state needed by atmosphere pass registration and post-frame bake.
///
/// Populated by the caller (SceneRenderer) and passed to AddAtmospherePasses()
/// and PostFrameAtmosphereBake(). Avoids giving atmosphere code direct access
/// to SceneRenderer members.
struct AtmosphereFrameContext {
    IRHIDevice*       device         = nullptr;
    World*            world          = nullptr;
    CBRingAllocator*  cbRing         = nullptr;
    PipelineLibrary*  pipelines      = nullptr;
    const Vec3*       sunDir         = nullptr;
    const Vec3*       sunColor       = nullptr;
    bool              bindless       = false;
    float             vpWidth        = 0.f;
    float             vpHeight       = 0.f;

    // Transmittance LUT state (owned by SceneRenderer, mutated by atmosphere)
    RHITextureHandle  transmittanceLut;
    uint32_t          transmittanceLutSrv = UINT32_MAX;
    uint32_t          transmittanceLutUav = UINT32_MAX;
    bool*             transmittanceLutDirty  = nullptr;
    bool*             transmittanceLutFilled = nullptr;

    // Multi-scattering LUT state (owned by SceneRenderer, mutated by atmosphere)
    RHITextureHandle  multiScatterLut;
    uint32_t          multiScatterLutSrv = UINT32_MAX;
    uint32_t          multiScatterLutUav = UINT32_MAX;
    bool*             multiScatterLutDirty  = nullptr;
    bool*             multiScatterLutFilled = nullptr;

    // Bruneton 3D Scattering LUT (owned by SceneRenderer)
    RHITextureHandle  scatteringLut3D;
    uint32_t          scatteringLut3DSrv = UINT32_MAX;
    uint32_t          scatteringLut3DUav = UINT32_MAX;
    bool*             scatteringLut3DDirty  = nullptr;
    bool*             scatteringLut3DFilled = nullptr;

    // Delta Rayleigh/Mie/Density 3D textures (temp, owned by SceneRenderer)
    RHITextureHandle  deltaRayleigh3D;
    uint32_t          deltaRayleigh3DSrv = UINT32_MAX;
    uint32_t          deltaRayleigh3DUav = UINT32_MAX;
    RHITextureHandle  deltaMie3D;
    uint32_t          deltaMie3DSrv = UINT32_MAX;
    uint32_t          deltaMie3DUav = UINT32_MAX;
    RHITextureHandle  scatteringDensity3D;
    uint32_t          scatteringDensity3DSrv = UINT32_MAX;
    uint32_t          scatteringDensity3DUav = UINT32_MAX;

    // Irradiance LUT (64x16, owned by SceneRenderer)
    RHITextureHandle  irradianceLut;
    uint32_t          irradianceLutSrv = UINT32_MAX;
    uint32_t          irradianceLutUav = UINT32_MAX;
    RHITextureHandle  deltaIrradiance;
    uint32_t          deltaIrradianceSrv = UINT32_MAX;
    uint32_t          deltaIrradianceUav = UINT32_MAX;

    // Sky cubemap state (owned by SceneRenderer)
    RHITextureHandle  skyCubemap;
    uint32_t          skyCubemapSrv = UINT32_MAX;
    uint32_t          skyCubemapUav = UINT32_MAX;
    bool*             skyIBLDirty   = nullptr;

    // IBL (owned by SceneRenderer)
    IBLBaker*         iblBaker         = nullptr;
    IBLBaker::BakeResult*    iblResult        = nullptr;
    uint32_t          brdfLutSrvIndex  = UINT32_MAX;

    // SH (owned by SceneRenderer)
    SHCoefficients*  skyIrradianceSH = nullptr;

    // Aerial Perspective Volume (owned by SceneRenderer)
    RHITextureHandle  apVolume;
    uint32_t          apVolumeSrv = UINT32_MAX;
    uint32_t          apVolumeUav = UINT32_MAX;
    const float*      invViewProjData = nullptr;  ///< Pointer to 16 floats (row-major invViewProj).
    const Vec3*       cameraPos = nullptr;         ///< Camera world position (cm).

    // GBuffer depth (for sky pass depth test)
    uint32_t          depthSrvIndex = UINT32_MAX;
    RHITextureHandle  depthTexture;

    // HDRI environment override — when active, skip procedural sky passes
    // and render the HDRI cubemap as a skybox background instead.
    bool              hdriActive       = false;          ///< True = HDRI cubemap replaces procedural sky.
    uint32_t          hdriCubemapSrv   = UINT32_MAX;     ///< SRV index for HDRI cubemap (when hdriActive).
    RHITextureHandle  hdriCubemapTex;                    ///< Physical texture handle (SM5 non-bindless path).
};

// ─────────────────────────────────────────────────────────────────────────────
// AddAtmospherePasses
// ─────────────────────────────────────────────────────────────────────────────

/// Adds atmosphere-related render graph passes.
///
/// Iterates SkyAtmosphereComponent from ECS, registers transmittance LUT
/// compute pass (if dirty), sky cubemap capture pass (if IBL dirty), and sky
/// atmosphere fullscreen pass. All passes read/write the HDR scene color.
///
/// Must be called during render graph construction (before Compile/Execute).
void AddAtmospherePasses(
    rg::RgBuilder& rgBuilder,
    rg::RgResourceId hdrRes,
    rg::RgResourceId depthRes,
    const CBAllocation& perFrameAlloc,
    const AtmosphereFrameContext& ctx);

// ─────────────────────────────────────────────────────────────────────────────
// PostFrameAtmosphereBake
// ─────────────────────────────────────────────────────────────────────────────

/// Runs IBL bake and SH projection after render graph execution.
///
/// Must be called AFTER `rgBuilder.Execute()` so the sky cubemap contains
/// valid data. Submits its own GPU work (BakeEnvironment), waits for
/// completion, then projects SH from the cubemap.
///
/// Temporarily ends and re-begins the command list for synchronization.
void PostFrameAtmosphereBake(
    IRHICommandList* cmdList,
    AtmosphereFrameContext& ctx);

} // namespace uz
