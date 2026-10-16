/// UzEngine Hi-Z Pyramid Pass
///
/// Registers a compute pass in the render graph that generates a hierarchical
/// Z-buffer (Hi-Z) from the depth prepass output. Each dispatch downsamples
/// the depth buffer by 2x, taking the minimum of 4 depth samples (conservative
/// for reversed-Z occlusion culling where min = closest).
///
/// # Render Graph Integration
/// The pass declares:
/// - Depth source as Read (ShaderResource state)
/// - Hi-Z mip textures as implicit writes (not tracked by the graph since
///   they are persistent external textures; the pass manages UAV barriers
///   internally between mip dispatches).
///
/// # Mip Chain Layout
/// Each mip level is a separate R32_FLOAT texture (no per-mip UAV support in
/// the current RHI). The pass dispatches one compute invocation per mip level,
/// with UAV barriers between each dispatch to ensure read-after-write coherency.
///
/// - Mip 0 input: depth prepass output (full resolution, SRV)
/// - Mip 1 output: half resolution (UAV)
/// - Mip 2 input: mip 1 re-bound as SRV, output: quarter resolution (UAV)
/// - ... continues until 1x1
///
/// # Usage
/// The Hi-Z pyramid is consumed by:
/// - GPU occlusion culling (two-phase HZB test)
/// - SSAO (depth-aware blur)
/// - SSR (Hi-Z ray marching)
/// - Contact shadows (depth hierarchy tracing)
///
/// @ref Wihlidal 2016 "Optimizing the Graphics Pipeline with Compute" — HZB construction
/// Standard min/max depth pyramid for hierarchical occlusion testing.

#pragma once

#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
class RgStateRegistry;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of mip levels in the Hi-Z pyramid.
///
/// For a 16384x16384 source, log2(16384) = 14 mip levels.
/// 12 is sufficient for resolutions up to 4096x4096.
static constexpr uint32_t kHiZMaxMipLevels = 12;

// ─────────────────────────────────────────────────────────────────────────────
// HiZMipLevel — describes one level of the Hi-Z pyramid
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a single mip level of the Hi-Z pyramid.
///
/// Each mip is a separate R32_FLOAT texture since the RHI does not support
/// per-mip UAV descriptors. The pass reads from the previous mip's texture
/// (via its SRV index) and writes to the current mip's texture (via its UAV).
struct HiZMipLevel {
    RHITextureHandle texture;      ///< R32_FLOAT texture for this mip level.
    uint32_t         srvIndex;     ///< SRV descriptor heap index.
    uint32_t         uavIndex;     ///< UAV descriptor heap index.
    uint32_t         width;        ///< Mip width in texels.
    uint32_t         height;       ///< Mip height in texels.
};

// ─────────────────────────────────────────────────────────────────────────────
// HiZPyramidPassDesc — configuration for the Hi-Z compute pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the Hi-Z pyramid compute pass.
///
/// All fields must be set before calling `AddHiZPyramidPass`. The pass does
/// not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct HiZPyramidPassDesc {
    rg::RgStateRegistry* states = nullptr;
    /// Render graph resource ID for the depth prepass output (read, ShaderResource state).
    rg::RgResourceId  depthSource;

    /// Physical depth texture handle (for barrier scheduling only).
    RHITextureHandle   depthSourceTexture;

    /// SRV descriptor heap index for the depth source (mip 0 input).
    uint32_t           depthSourceSrvIndex;

    /// Per-mip level textures for the Hi-Z pyramid output.
    ///
    /// `mipLevels[0]` is the first downsampled mip (half resolution of source).
    /// `mipLevels[mipCount-1]` is the coarsest mip (approaching 1x1).
    /// Each entry must have valid texture, SRV, and UAV indices.
    HiZMipLevel        mipLevels[kHiZMaxMipLevels];

    /// Number of mip levels to generate (1 to kHiZMaxMipLevels).
    ///
    /// Typically `floor(log2(max(width, height)))` for the source resolution.
    uint32_t           mipCount;

    /// Compute PSO for the Hi-Z pyramid downsample shader (hiz_pyramid_cs.hlsl).
    RHIPipelineHandle  pipeline;

    /// Source depth buffer dimensions in texels.
    uint32_t           sourceWidth;
    uint32_t           sourceHeight;

    /// Whether the device uses bindless (SM6.6+) resource access.
    /// When true, the input SRV index is passed via root constants.
    /// When false, the input SRV is bound via descriptor table at slot 5 (t0).
    bool               bindless = false;
};

/// Registers a Hi-Z pyramid compute pass in the render graph.
///
/// Dispatches one compute invocation per mip level, downsampling by 2x each
/// time. Each dispatch reads from the previous mip (SRV at t0) and writes
/// to the current mip (UAV at u0). A UAV barrier is inserted between each
/// dispatch to ensure write completion before the next read.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration (depth source, mip chain, pipeline).
///
/// # Pass Execution
/// During execution:
/// 1. Binds the compute pipeline.
/// 2. For each mip level 0..mipCount-1:
///    a. Sets root constants (output dimensions, input dimensions).
///    b. Binds input SRV at descriptor table slot 2 (t0).
///    c. Binds output UAV at descriptor table slot 4 (u0).
///    d. Dispatches ceil(outW/8) x ceil(outH/8) x 1 thread groups.
///    e. Emits a UAV barrier on the output texture.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if `desc.mipCount` is 0 or exceeds `kHiZMaxMipLevels`.
void AddHiZPyramidPass(rg::RgBuilder& builder, const HiZPyramidPassDesc& desc);

} // namespace uz
