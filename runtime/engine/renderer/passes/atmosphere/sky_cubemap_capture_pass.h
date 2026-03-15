/// UzEngine Sky Cubemap Capture Pass
///
/// Registers a compute pass in the render graph that captures the sky
/// atmosphere into a 64x64x6 RGBA16F cubemap for IBL (Image-Based Lighting).
/// The cubemap encodes sky radiance from six face directions and is used
/// as input for the IBL baker (irradiance + prefiltered specular).
///
/// This pass reads the transmittance LUT (produced by the transmittance LUT
/// pass) and writes a cubemap UAV, dispatching 6 face slices in a single
/// Dispatch(8, 8, 6) call.
///
/// # Render Graph Integration
/// The pass declares:
/// - cubemapTarget as Write (UnorderedAccess state)
///
/// Barriers are scheduled automatically by the render graph compiler.
/// The IBL baker reads the cubemap SRV after the render graph executes.
///
/// # Pass Ordering
/// Executes AFTER the transmittance LUT pass (which provides the LUT SRV
/// index via root constants). Should be placed alongside other precompute
/// passes before the main lighting pass.

#pragma once

#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SkyCubemapCaptureRootConstants — 8 DWORDs (32 bytes) at root param 0 (b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the sky cubemap capture compute shader.
///
/// Matches the HLSL cbuffer SkyCubemapParams at b0 in sky_cubemap_cs.hlsl.
/// Uses root constants (not a CBV) because the compute root signature
/// defines slot 0 as 8x 32-bit root constants — same pattern as the
/// transmittance LUT and IBL baker compute shaders.
struct SkyCubemapCaptureRootConstants {
    float    bottomRadiusKm;        ///< Planet surface radius (km). DWORD 0.
    float    topRadiusKm;           ///< Top of atmosphere radius (km). DWORD 1.
    float    sunDirX;               ///< Sun direction X component (normalised). DWORD 2.
    float    sunDirY;               ///< Sun direction Y component (normalised). DWORD 3.
    float    sunDirZ;               ///< Sun direction Z component (normalised). DWORD 4.
    float    sunIntensity;          ///< Sun illuminance (uniform RGB). DWORD 5.
    uint32_t transmittanceLutIdx;   ///< SRV index for transmittance LUT texture. DWORD 6.
    uint32_t packedScatterScales;   ///< Packed f16 pair: low16 = rayleighScatteringScale, high16 = mieScatteringScale. DWORD 7.
};

static_assert(sizeof(SkyCubemapCaptureRootConstants) == 32,
    "SkyCubemapCaptureRootConstants must be exactly 8 DWORDs (32 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// SkyCubemapCapturePassDesc
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the sky cubemap capture compute pass.
///
/// All fields must be set before calling `AddSkyCubemapCapturePass`. The pass
/// does not take ownership of any pointed-to objects.
struct SkyCubemapCapturePassDesc {
    rg::RgResourceId  cubemapTarget;    ///< Output texture (write, UnorderedAccess state). RGBA16F 64x64x6.
    RHIPipelineHandle pipeline;         ///< Compute PSO for sky_cubemap_cs.hlsl.
    SkyCubemapCaptureRootConstants rootConstants; ///< Atmosphere params (8 DWORDs at root param 0).
    uint32_t          cubemapUavIndex;  ///< UAV descriptor heap index for the output cubemap.
};

/// Registers a sky cubemap capture compute pass in the render graph.
///
/// Dispatches [8, 8, 6] thread groups of [8, 8, 1] threads each,
/// producing a 64x64x6 RGBA16F cubemap encoding sky radiance.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration.
void AddSkyCubemapCapturePass(rg::RgBuilder& builder, const SkyCubemapCapturePassDesc& desc);

} // namespace uz
