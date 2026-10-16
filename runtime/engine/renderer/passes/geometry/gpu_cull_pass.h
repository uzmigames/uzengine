/// UzEngine GPU Frustum + HiZ Occlusion Culling Pass
///
/// Registers a compute pass in the render graph that tests per-instance bounding
/// spheres against the view frustum and optionally against the Hi-Z depth pyramid.
/// Instances that survive both tests are appended to an output buffer for indirect
/// draw calls.
///
/// # Two-Phase Culling
/// 1. **Frustum cull**: 6-plane Gribb-Hartmann test against bounding spheres.
/// 2. **HiZ occlusion cull** (optional, requires SM6.6 bindless): projects each
///    sphere to a screen-space rect (Mara & Morgan 2013), selects the appropriate
///    Hi-Z mip level, loads 2x2 texels via bindless, and rejects spheres behind
///    the existing depth.
///
/// # Render Graph Integration
/// The pass is registered as a compute pass with no explicit resource tracking
/// (buffers are external persistent resources). The pass emits UAV barriers
/// on the output buffers after dispatch.
///
/// # Output Format
/// - `AppendStructuredBuffer<uint>` of visible instance indices
/// - `RWByteAddressBuffer` with atomically-incremented instance count at offset 4
///   (compatible with `DrawIndexedIndirect` / `ExecuteIndirect`).
///

#pragma once

#include "cb_ring_allocator.h"     // CBAllocation
#include "render_graph_types.h"
#include "postprocess/hiz_pyramid_pass.h"  // HiZMipLevel, kHiZMaxMipLevels
#include "engine/rhi/rhi_types.h"
#include "mat4.h"

#include <cstdint>
#include <cstring>

namespace uz::rg {
class RgBuilder;
class RgStateRegistry;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// GpuCullConstants — CPU-side mirror of CullConstants cbuffer (gpu_cull_cs.hlsl)
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of Hi-Z mip SRV indices packed into the constant buffer.
/// Must match the g_HiZMipSrvIndices[3] array in the shader (12 uints).
static constexpr uint32_t kGpuCullMaxHiZMips = 12;

/// CPU-side mirror of the shader's CullConstants constant buffer at register(b1).
/// Must be byte-identical to the HLSL cbuffer layout.
///
/// Total size: 288 bytes (16-byte aligned for CBV binding).
struct GpuCullConstants {
    Mat4     viewProjection;            ///< 0:   Combined view-projection (world → clip).
    Mat4     viewMatrix;                ///< 64:  View matrix (world → view).
    Mat4     viewToClip;                ///< 128: Projection matrix (view → clip).
    uint32_t instanceCount = 0;         ///< 192: Number of bounding spheres to cull.
    uint32_t hizEnabled    = 0;         ///< 196: 0 = frustum only, 1 = frustum + HiZ.
    uint32_t hizWidth      = 0;         ///< 200: Hi-Z mip 0 width in texels.
    uint32_t hizHeight     = 0;         ///< 204: Hi-Z mip 0 height in texels.
    uint32_t hizMipCount   = 0;         ///< 208: Number of Hi-Z mip levels.
    uint32_t _pad0[3]      = {};        ///< 212: Padding to 16-byte boundary.
    uint32_t hizMipSrvIndices[12] = {}; ///< 224: Per-mip SRV indices for bindless access.
    uint32_t _pad1[4]      = {};        ///< 272: Padding to 288 bytes.
};

static_assert(sizeof(GpuCullConstants) == 288,
              "GpuCullConstants must match CullConstants cbuffer layout (288 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// GpuCullPassDesc — configuration for the GPU culling compute pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the GPU frustum + HiZ occlusion culling compute pass.
///
/// All fields must be set before calling `AddGpuCullPass`. The pass does not
/// take ownership of any pointed-to objects — the caller must ensure they
/// remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct GpuCullPassDesc {
    rg::RgStateRegistry* states = nullptr;
    // ── Pipeline ────────────────────────────────────────────────────────

    /// Compute PSO for the gpu_cull_cs.hlsl shader.
    RHIPipelineHandle  pipeline;

    // ── CB allocation (pre-filled by caller) ────────────────────────────

    /// Pre-allocated CB ring allocation containing the CullConstants struct.
    /// The caller must fill this with the GpuCullConstants data before
    /// registering the pass. Bound at root parameter slot 1 (b1).
    CBAllocation       cullCBAlloc;

    // ── Bounding sphere input ───────────────────────────────────────────

    /// SRV descriptor heap index for the bounding sphere buffer.
    /// The buffer is a StructuredBuffer<float4> with xyz = center, w = radius.
    uint32_t           boundingSphereSrvIndex = UINT32_MAX;

    // ── Output buffers ──────────────────────────────────────────────────

    /// UAV descriptor heap index for the visible index buffer.
    /// Must be an AppendStructuredBuffer<uint>.
    uint32_t           visibleIndexUavIndex = UINT32_MAX;

    /// UAV descriptor heap index for the indirect args buffer.
    /// Must be a RWByteAddressBuffer.
    uint32_t           indirectArgsUavIndex = UINT32_MAX;

    /// Buffer handles for UAV barrier emission after dispatch.
    RHIBufferHandle    visibleIndexBuffer;
    RHIBufferHandle    indirectArgsBuffer;

    // ── Dispatch dimensions ─────────────────────────────────────────────

    /// Number of instances (bounding spheres) to process.
    /// Determines thread group count: ceil(instanceCount / 64).
    uint32_t           instanceCount = 0;

    /// Whether the device uses bindless (SM6.6+) resource access.
    bool               bindless = false;
};

/// Registers a GPU frustum + HiZ occlusion culling compute pass in the render graph.
///
/// Dispatches ceil(instanceCount / 64) thread groups with [64,1,1] threads each.
/// Each thread tests one bounding sphere against the frustum and optionally
/// against the Hi-Z pyramid.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration (CB allocation, buffer SRV/UAV indices).
///
/// # Pass Execution
/// During execution:
/// 1. Binds the compute pipeline.
/// 2. Binds CullConstants CBV at root parameter slot 1 (b1).
/// 3. Binds bounding sphere SRV via descriptor table at slot 5 (SM5).
/// 4. Binds visible index + indirect args UAVs via descriptor table at slot 4.
/// 5. Dispatches ceil(instanceCount / 64) x 1 x 1 thread groups.
/// 6. Emits UAV barriers on output buffers.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if `desc.instanceCount` is 0 or `desc.pipeline` is invalid.
void AddGpuCullPass(rg::RgBuilder& builder, const GpuCullPassDesc& desc);

} // namespace uz
