/// UzEngine Equirectangular-to-Cubemap Conversion Pass
///
/// Registers a compute pass in the render graph that converts an HDR
/// equirectangular panorama texture into a cubemap for IBL baking.
/// The output cubemap (RGBA16F, NxNx6) can be fed directly into
/// IBLBaker::BakeEnvironment() as the source environment cubemap.
///
/// # Render Graph Integration
/// The pass declares:
/// - cubemapTarget as Write (UnorderedAccess state)
///
/// Barriers are scheduled automatically by the render graph compiler.
///
/// # Pass Ordering
/// Executes BEFORE the IBL baker passes (irradiance, prefilter, BRDF LUT).
/// Should be placed alongside other precompute passes. Unlike the sky
/// cubemap capture pass, this reads from a user-provided HDR texture
/// rather than computing atmosphere scattering.

#pragma once

#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// EquirectToCubemapRootConstants — 4 DWORDs (16 bytes) at root param 0 (b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the equirect-to-cubemap compute shader.
///
/// Matches the HLSL cbuffer RootConstants at b0 in equirect_to_cubemap_cs.hlsl.
/// Uses root constants (not a CBV) because the compute root signature
/// defines slot 0 as root constants — same pattern as the sky cubemap
/// capture and IBL baker compute shaders.
struct EquirectToCubemapRootConstants {
    uint32_t cubemapSize;       ///< Output cubemap face resolution in texels (e.g. 256). DWORD 0.
    uint32_t equirectSrvIndex;  ///< SRV descriptor heap index for the HDR equirect texture. DWORD 1.
    uint32_t _pad[2];           ///< Padding to 16 bytes. DWORD 2-3.
};

static_assert(sizeof(EquirectToCubemapRootConstants) == 16,
    "EquirectToCubemapRootConstants must be exactly 4 DWORDs (16 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// EquirectToCubemapPassDesc
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the equirect-to-cubemap conversion compute pass.
///
/// All fields must be set before calling `AddEquirectToCubemapPass`. The pass
/// does not take ownership of any pointed-to objects.
struct EquirectToCubemapPassDesc {
    rg::RgResourceId  cubemapTarget;    ///< Output texture (write, UnorderedAccess state). RGBA16F NxNx6.
    RHIPipelineHandle pipeline;         ///< Compute PSO for equirect_to_cubemap_cs.hlsl.
    EquirectToCubemapRootConstants rootConstants; ///< Conversion params (4 DWORDs at root param 0).
    uint32_t          cubemapUavIndex;  ///< UAV descriptor heap index for the output cubemap.
    uint32_t          cubemapSize;      ///< Face resolution for dispatch calculation.
};

/// Registers an equirect-to-cubemap conversion compute pass in the render graph.
///
/// Dispatches [cubemapSize/8, cubemapSize/8, 6] thread groups of [8, 8, 1]
/// threads each, converting the equirectangular panorama into a cubemap.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration. All fields must be initialised.
void AddEquirectToCubemapPass(rg::RgBuilder& builder, const EquirectToCubemapPassDesc& desc);

} // namespace uz
