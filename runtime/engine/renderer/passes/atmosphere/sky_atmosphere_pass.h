/// UzEngine Sky Atmosphere Pass
///
/// Registers a fullscreen pass in the render graph that renders a physical
/// sky using Hillaire 2020 single-scatter atmospheric model. Only fills
/// pixels where depth == 1.0 (no geometry), rendering behind the lit scene.
///
/// # Render Graph Integration
/// The pass declares:
/// - Backbuffer as Write (RenderTarget state)
/// - Depth G-Buffer as Read (ShaderResource state) — for sky/geometry masking
///
/// Barriers are scheduled automatically by the render graph compiler.
///
/// # Pass Ordering
/// Executes AFTER the lighting pass (scene is already lit in the backbuffer)
/// and BEFORE the fog pass. Sky fills only empty sky pixels.

#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmospherePassDesc
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the sky atmosphere pass.
///
/// All fields must be set before calling `AddSkyAtmospherePass`. The pass
/// does not take ownership of any pointed-to objects.
struct SkyAtmospherePassDesc {
    rg::RgResourceId backbuffer;        ///< Output RT (write, RenderTarget state).
    rg::RgResourceId depthGBuffer;      ///< Depth input for sky masking (read, ShaderResource state).

    CBAllocation      perFrameAlloc;    ///< PerFrameCB with invViewProj and lightDir (bound at b1).
    CBAllocation      skyAtmosphereAlloc; ///< SkyAtmosphereCB (bound at b2).
    RHIPipelineHandle pipeline;         ///< Sky atmosphere PSO (fullscreen triangle + sky_atmosphere_ps).

    uint32_t          depthSrvIndex;    ///< SRV heap index for depth G-Buffer (SM6.6 bindless).
    RHITextureHandle  depthTexture;     ///< Physical depth texture handle (SM5 fallback).

    uint32_t          transmittanceLutSrvIndex = UINT32_MAX; ///< SRV index for precomputed transmittance LUT (UINT32_MAX = not available).
    RHITextureHandle  transmittanceLutTexture;  ///< Physical transmittance LUT texture handle (SM5 fallback, bound at t1).
    rg::RgResourceId  transmittanceLutRes;      ///< Render graph resource handle for the LUT (triggers UAV→SRV barrier when valid).

    uint32_t          multiScatterLutSrvIndex = UINT32_MAX; ///< SRV index for multi-scattering LUT.
    RHITextureHandle  multiScatterLutTexture;   ///< Physical multi-scattering LUT texture handle (SM5 fallback, bound at t2).
    rg::RgResourceId  multiScatterLutRes;       ///< Render graph resource handle for the multi-scatter LUT.

    uint32_t          apVolumeSrvIndex = UINT32_MAX; ///< SRV index for AP volume 3D texture (UINT32_MAX = none).

    uint32_t          scatteringLut3DSrvIndex = UINT32_MAX; ///< SRV index for Bruneton 3D scattering LUT.
    RHITextureHandle  scatteringLut3DTexture;  ///< Physical 3D scattering texture handle (SM5 fallback).
    rg::RgResourceId  scatteringLut3DRes;      ///< Render graph resource for barrier tracking.

    uint32_t          irradianceLutSrvIndex = UINT32_MAX; ///< SRV index for 64x16 irradiance LUT.
    rg::RgResourceId  irradianceLutRes;        ///< Render graph resource for barrier tracking.

    bool              bindless;         ///< True if SM6.6 bindless path is active.
    float             viewportWidth;    ///< Viewport width in pixels.
    float             viewportHeight;   ///< Viewport height in pixels.
};

/// Registers a sky atmosphere pass in the render graph.
///
/// The pass renders a physical sky into backbuffer pixels where depth == 1.0
/// (far plane). Geometry pixels are preserved. Uses the fullscreen triangle
/// technique (Draw(3), no vertex buffer).
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration.
void AddSkyAtmospherePass(rg::RgBuilder& builder, const SkyAtmospherePassDesc& desc);

} // namespace uz
