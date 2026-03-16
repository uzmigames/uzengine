/// UzEngine Sky Cubemap Capture Pixel Shader Pass — SM5 fallback.
///
/// Registers a graphics pass in the render graph that captures the sky
/// atmosphere into a 64x64x6 RGBA16F cubemap for IBL. Unlike the compute
/// shader path (sky_cubemap_capture_pass.h), this uses a fullscreen pixel
/// shader with explicit register(t0) binding — no bindless required.
///
/// Renders 6 fullscreen triangles (one per cubemap face), each targeting
/// a different array slice of the cubemap texture via per-slice RTVs.
///
/// # Render Graph Integration
/// The pass declares:
/// - cubemapTarget as Write (RenderTarget state)
/// - transmittanceLut as Read (ShaderResource state)
///
/// # When to Use
/// Use this path when `!device->IsBindless()` (SM5 / DX11 / older GPUs).
/// The compute path (sky_cubemap_capture_pass.h) is preferred when bindless
/// is available because it dispatches all 6 faces in a single call.

#pragma once

#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ---------------------------------------------------------------------------
// SkyCubemapCapturePSRootConstants — 8 DWORDs (32 bytes) at root param 0 (b0)
// ---------------------------------------------------------------------------

/// Root constants for the sky cubemap capture pixel shader.
///
/// Matches the HLSL cbuffer SkyCubemapFaceParams at b0 in sky_cubemap_ps.hlsl.
/// DWORD 6 is `cubeFaceIndex` (not a bindless SRV index like the compute path).
/// The renderer updates cubeFaceIndex before each of the 6 draw calls.
struct SkyCubemapCapturePSRootConstants {
    float    bottomRadiusKm;        ///< Planet surface radius (km). DWORD 0.
    float    topRadiusKm;           ///< Top of atmosphere radius (km). DWORD 1.
    float    sunDirX;               ///< Sun direction X component (normalised). DWORD 2.
    float    sunDirY;               ///< Sun direction Y component (normalised). DWORD 3.
    float    sunDirZ;               ///< Sun direction Z component (normalised). DWORD 4.
    float    sunIntensity;          ///< Sun illuminance (uniform RGB). DWORD 5.
    uint32_t cubeFaceIndex;         ///< Current cubemap face [0-5]. DWORD 6.
    uint32_t packedScatterScales;   ///< Packed f16 pair: low16 = rayleighScale, high16 = mieScale. DWORD 7.
};

static_assert(sizeof(SkyCubemapCapturePSRootConstants) == 32,
    "SkyCubemapCapturePSRootConstants must be exactly 8 DWORDs (32 bytes)");

// ---------------------------------------------------------------------------
// SkyCubemapCapturePSPassDesc
// ---------------------------------------------------------------------------

/// Configuration for the sky cubemap capture pixel shader pass.
struct SkyCubemapCapturePSPassDesc {
    rg::RgResourceId  cubemapTarget;       ///< Output cubemap (write, RenderTarget state). RGBA16F 64x64x6.
    rg::RgResourceId  transmittanceLut;    ///< Transmittance LUT (read, ShaderResource state).
    RHIPipelineHandle  pipeline;            ///< Graphics PSO (fullscreen_vs + sky_cubemap_ps).
    RHITextureHandle   transmittanceLutTex; ///< Transmittance LUT texture handle (for SetFragmentTexture).
    SkyCubemapCapturePSRootConstants rootConstants; ///< Atmosphere params (8 DWORDs).
};

/// Registers a sky cubemap capture pixel shader pass in the render graph.
///
/// Draws 6 fullscreen triangles (one per cubemap face), with the render
/// target set to each array slice in sequence. Root constants are updated
/// per-face to set cubeFaceIndex.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration.
void AddSkyCubemapCapturePSPass(rg::RgBuilder& builder, const SkyCubemapCapturePSPassDesc& desc);

} // namespace uz
