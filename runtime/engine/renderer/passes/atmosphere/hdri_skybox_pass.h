/// UzEngine HDRI Skybox Pass
///
/// Registers a fullscreen pass in the render graph that renders an HDRI
/// cubemap as the sky background. Used when an external HDRI environment
/// map is active, replacing the procedural sky atmosphere ray-march pass.
///
/// Only fills pixels where depth == 0.0 (reversed-Z far plane = no geometry).
///
/// # Render Graph Integration
/// The pass declares:
/// - Backbuffer as Write (RenderTarget state)
/// - Depth G-Buffer as Read (ShaderResource state) — for sky/geometry masking
///
/// Barriers are scheduled automatically by the render graph compiler.
///
/// # Pass Ordering
/// Executes at the same point as the SkyAtmosphere pass — AFTER the lighting
/// pass (scene is already lit in the backbuffer) and BEFORE the fog pass.

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
// r.HDRISkybox
// ─────────────────────────────────────────────────────────────────────────────

/// Values of `r.HDRISkybox` (registered once, in frame_constants.cpp; read by
/// `AddAtmospherePasses` every frame and by the SkyLight auto-load once).
enum class HdriSkyboxMode : int32_t {
    /// The HdriSkybox pass is skipped. The procedural sky passes run when a
    /// SkyAtmosphereComponent exists; the HDRI still drives the IBL, the SH,
    /// the fog colour and the env-MIS sampler (the capture and the re-bake
    /// stay suppressed while the HDRI is active).
    Off = 0,
    /// The pass renders the IBL cubemap, converted at
    /// `SkyLightComponent::cubemapResolution` (128 by default). The shipped
    /// behaviour before the CVar existed.
    Cubemap = 1,
    /// As `Cubemap`, but the auto-loaded panorama is converted at its own
    /// resolution (`HdriSkyboxCubeResolution`), so the visible sky and the
    /// prefiltered mip 0 are not upsampled from a 128-texel cube.
    SourceResolution = 2,
};

/// Largest cube face `HdriSkyboxCubeResolution` returns (texels).
inline constexpr uint32_t kHdriSkyboxMaxCubeResolution = 1024;

/// Cube face size for an equirect panorama `equirectWidth` texels wide under
/// `HdriSkyboxMode::SourceResolution`: one face spans 90 of the panorama's
/// 360 degrees, so width / 4, rounded down to a power of two, capped at
/// `kHdriSkyboxMaxCubeResolution`, and never below the authored
/// `SkyLightComponent::cubemapResolution` (the value the other modes use).
/// A 2048-wide panorama (the loader's cap) gives 512.
[[nodiscard]] constexpr uint32_t HdriSkyboxCubeResolution(uint32_t equirectWidth,
                                                          uint32_t authoredResolution) noexcept
{
    uint32_t res = 1;
    while (res * 2 <= equirectWidth / 4 && res * 2 <= kHdriSkyboxMaxCubeResolution) {
        res *= 2;
    }
    return res > authoredResolution ? res : authoredResolution;
}

// ─────────────────────────────────────────────────────────────────────────────
// HdriSkyboxPassDesc
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the HDRI skybox pass.
///
/// All fields must be set before calling `AddHdriSkyboxPass`. The pass
/// does not take ownership of any pointed-to objects.
struct HdriSkyboxPassDesc {
    rg::RgResourceId backbuffer;        ///< Output RT (write, RenderTarget state).
    rg::RgResourceId depthGBuffer;      ///< Depth input for sky masking (read, ShaderResource state).

    CBAllocation      perFrameAlloc;    ///< PerFrameCB with invViewProj and cameraPos (bound at b1).
    RHIPipelineHandle pipeline;         ///< HDRI skybox PSO (fullscreen triangle + skybox_cubemap_ps).

    uint32_t          depthSrvIndex;    ///< SRV heap index for depth G-Buffer (SM6.6 bindless).
    RHITextureHandle  depthTexture;     ///< Physical depth texture handle (SM5 fallback).

    uint32_t          cubemapSrvIndex;  ///< SRV heap index for HDRI cubemap (SM6.6 bindless).
    RHITextureHandle  cubemapTexture;   ///< Physical cubemap texture handle (SM5 fallback, bound at t1).

    bool              bindless;         ///< True if SM6.6 bindless path is active.
    float             viewportWidth;    ///< Viewport width in pixels.
    float             viewportHeight;   ///< Viewport height in pixels.
};

/// Registers an HDRI skybox pass in the render graph.
///
/// The pass renders an HDRI cubemap into backbuffer pixels where depth == 0.0
/// (far plane). Geometry pixels are preserved. Uses the fullscreen triangle
/// technique (Draw(3), no vertex buffer).
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration.
void AddHdriSkyboxPass(rg::RgBuilder& builder, const HdriSkyboxPassDesc& desc);

} // namespace uz
