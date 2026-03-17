/// UzEngine Depth Prepass
///
/// Registers a depth-only prepass in the render graph that renders all opaque
/// meshes to the GBuffer depth buffer before the main GBuffer pass. The prepass
/// populates the depth buffer so the GBuffer pass can use an Equal depth test
/// to eliminate overdraw (early-Z optimization).
///
/// # Render Graph Integration
/// The pass declares:
/// - Depth buffer as Write (DepthStencilWrite state) — D32Float
///
/// Barriers are scheduled automatically by the render graph compiler.
/// The GBuffer pass should also Write the same depth buffer; the graph
/// compiler will ensure the prepass executes first (registered first).
///
/// # Binding Model
/// Supports both SM6.6 bindless (descriptor index via root constant at slot 0)
/// and SM5 fallback (explicit SetVertexBuffer). Per-frame CB bound at root
/// param 1, per-object CB (model matrix only) bound at root param 2.
///
/// # Differences from Shadow Pass
/// - NO depth bias (no slope-scaled bias, no constant bias).
/// - Uses GBuffer depth format (D32Float) with reversed-Z (kDepthFar=0.0 clear).
/// - Single viewport covering the full render target.
/// - No cascade iteration, no atlas tiling.
///
/// @source UE5 DepthRendering.cpp — FDepthPassMeshProcessor / RenderPrePass

#pragma once

#include "constant_buffers.h"
#include "instance_batcher.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"
#include "mesh/gpu_mesh.h"

#include <cstdint>

namespace uz {

class DrawList;
class CBRingAllocator;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DepthPrepassDesc — configuration for the depth-only prepass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the depth-only prepass.
///
/// All fields must be set before calling `AddDepthPrepass`. The pass does
/// not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct DepthPrepassDesc {
    /// Render graph resource ID for the depth buffer (write, DepthStencilWrite state).
    rg::RgResourceId  depthBuffer;

    /// Physical depth texture handle for render pass setup.
    ///
    /// Used to configure the depth attachment in the render pass descriptor.
    /// Must be the same texture referenced by `depthBuffer`.
    RHITextureHandle   depthTexture;

    /// Sorted draw list to iterate. Must not be null.
    const DrawList*    drawList;

    /// Per-object CB ring allocator. Must not be null.
    CBRingAllocator*   cbAllocator;

    /// Pre-allocated per-frame CB (already filled by caller).
    ///
    /// Contains the camera's view-projection matrix at minimum. The depth
    /// prepass vertex shader reads `viewProj` from b1.
    CBAllocation       perFrameAlloc;

    /// Pipeline state object for the depth-only prepass shader.
    ///
    /// Must be created with 0 render targets, D32Float depth, no depth bias,
    /// and LessEqual depth comparison (reversed-Z).
    RHIPipelineHandle  pipeline;

    /// Array of loaded GPU meshes, indexed by `DrawCommand::meshId`.
    ///
    /// Each draw command selects its mesh by `DrawCommand::meshId`. Commands
    /// whose `meshId >= meshCount` or whose mesh `!IsValid()` are skipped.
    /// Must not be null when `meshCount > 0`. The caller retains ownership;
    /// the array must remain valid until `RgBuilder::Execute()` completes.
    const GPUMesh*     meshes = nullptr;

    /// Number of meshes in the `meshes` array.
    uint32_t           meshCount = 0;

    /// True if SM6.6 bindless path is active.
    bool               bindless;

    /// Viewport width in pixels.
    float              viewportWidth;

    /// Viewport height in pixels.
    float              viewportHeight;

    // ── Binder path (optional — set cbRingAllocator to enable) ──────────
    // When cbRingAllocator is non-null, the pass uses ShaderParameterBinder
    // to upload per-object CBs via metadata. Legacy path is used when null.

    CBRingAllocator*   cbRingAllocator = nullptr; ///< If set, binder uploads CBs via metadata.

    // ── GPU Instancing (optional — set instanced=true to enable) ──────────
    bool               instanced = false;         ///< True to use instanced draw path.
    RHIPipelineHandle  instancedPipeline;          ///< PSO compiled with UZ_INSTANCED=1.
    uint32_t           instanceBufferSrvIndex = UINT32_MAX; ///< SRV index for StructuredBuffer<InstanceData>.
    const InstanceBatch* batchData = nullptr;      ///< Pointer to instanced draw batches array.
    uint32_t           batchCount = 0;             ///< Number of batches in the array.
};

/// Registers a depth-only prepass in the render graph.
///
/// The pass writes depth-only geometry for all opaque meshes in the draw list.
/// It uses the same vertex shader and mesh binding pattern as the shadow pass
/// but targets the GBuffer depth buffer with no depth bias.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc` — Pass configuration (depth buffer, draw list, pipeline, etc.).
///   All referenced resources must have been created or imported in `builder`
///   before calling this function.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Clears the depth buffer to `kDepthFar` (reversed-Z far plane = 0.0).
/// 2. Begins a depth-only render pass (0 color attachments).
/// 3. Binds the pipeline and per-frame CB at root slot 1.
/// 4. For each draw command: allocates a per-object CB (model matrix only),
///    binds at root slot 2, and issues DrawIndexed.
/// 5. Ends the render pass.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation (before
/// `RgBuilder::Execute()`). The callback captures state at call time and
/// does not access `builder` after registration.
///
/// # Examples
/// ```cpp
/// DepthPrepassDesc desc{};
/// desc.depthBuffer      = rgBuilder.ImportTexture("GBuf_Depth", depthTex, ...);
/// desc.depthTexture     = depthHandle;
/// desc.drawList         = &sceneDrawList;
/// desc.cbAllocator      = &perObjectAllocator;
/// desc.perFrameAlloc    = perFrameCBAlloc;
/// desc.pipeline         = pipelines.GetDepthPrepass(bindless);
/// desc.meshes           = loadedMeshes.data();
/// desc.meshCount        = static_cast<uint32_t>(loadedMeshes.size());
/// desc.bindless         = true;
/// desc.viewportWidth    = 1920.0f;
/// desc.viewportHeight   = 1080.0f;
/// AddDepthPrepass(builder, desc);
/// ```
///
/// # Panics
/// Asserts if `desc.drawList` or `desc.cbAllocator` is null.
void AddDepthPrepass(rg::RgBuilder& builder, const DepthPrepassDesc& desc);

} // namespace uz
