/// UzEngine G-Buffer Pass
///
/// Registers a deferred rendering G-Buffer pass in the render graph that
/// iterates a sorted DrawList and outputs albedo, world-space normals, and
/// metallic/roughness into three render targets plus a depth buffer.
///
/// # Render Graph Integration
/// The pass declares:
/// - Albedo target as Write (RenderTarget state) — RGBA8Unorm
/// - Normal target as Write (RenderTarget state) — RGBA16Float
/// - MetalRough target as Write (RenderTarget state) — RGBA8Unorm
/// - Depth buffer as Write (DepthStencilWrite state) — D32Float
///
/// Barriers are scheduled automatically by the render graph compiler.
/// The pass execution callback receives resolved physical handles.
///
/// # Binding Model
/// Supports both SM6.6 bindless (descriptor index via root constant at slot 0)
/// and SM5 fallback (explicit SetVertexBuffer). Per-frame CB bound at root
/// param 1, per-object CB bound at root param 2.

#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

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
// GBufferPassDesc — configuration for the deferred G-Buffer pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the deferred G-Buffer pass.
///
/// All fields must be set before calling `AddGBufferPass`. The pass does
/// not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct GBufferPassDesc {
    rg::RgResourceId albedoTarget;      ///< Albedo render target (write, RenderTarget state, RGBA8Unorm).
    rg::RgResourceId normalTarget;      ///< World-space normal target (write, RenderTarget state, RGBA16Float).
    rg::RgResourceId metalRoughTarget;  ///< Metallic/roughness target (write, RenderTarget state, RGBA8Unorm).
    rg::RgResourceId depthBuffer;       ///< Depth buffer (write, DepthStencilWrite state, D32Float).
    RHITextureHandle  depthTexture;     ///< Physical depth texture for render pass setup.
    const DrawList*   drawList;         ///< Sorted draw list to iterate. Must not be null.
    CBRingAllocator*  cbAllocator;      ///< Per-object CB ring allocator. Must not be null.
    CBAllocation      perFrameAlloc;    ///< Pre-allocated per-frame CB (already filled by caller).
    RHIPipelineHandle pipeline;         ///< PSO handle for the G-Buffer shader.
    uint32_t          vertexBufferSrvIndex; ///< SRV index for bindless vertex fetch (SM6.6).
    RHIBufferHandle   vertexBuffer;     ///< Vertex buffer handle (SM5 fallback path).
    RHIBufferHandle   indexBuffer;      ///< Index buffer handle.
    RHIIndexFormat    indexFormat;      ///< Index element format (Uint16 or Uint32).
    uint32_t          indexCount;       ///< Total indices per mesh (single-mesh path).
    bool              bindless;         ///< True if SM6.6 bindless path is active.
    float             viewportWidth;    ///< Viewport width in pixels.
    float             viewportHeight;   ///< Viewport height in pixels.
};

/// Registers a deferred G-Buffer pass in the render graph.
///
/// The pass writes albedo, normals, metallic/roughness, and depth, iterates
/// the sorted DrawList, and issues one indexed draw call per command with
/// per-object constant buffer binding.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc` — Pass configuration (resources, draw list, pipeline, etc.).
///   All referenced resources must have been created or imported in `builder`
///   before calling this function.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Clears all three G-Buffer render targets and the depth buffer.
/// 2. Begins a render pass with three color attachments and depth.
/// 3. Binds the pipeline, vertex/index buffers, and per-frame CB.
/// 4. For each draw command: allocates a PerObjectCB, fills model/normal/material,
///    binds at root param 2, and issues DrawIndexed.
/// 5. Ends the render pass.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation (before `RgBuilder::Execute()`).
/// The callback captures state at call time and does not access `builder` after registration.
///
/// # Examples
/// ```cpp
/// GBufferPassDesc desc{};
/// desc.albedoTarget    = builder.ImportRenderTarget(albedoTex);
/// desc.normalTarget    = builder.ImportRenderTarget(normalTex);
/// desc.metalRoughTarget = builder.ImportRenderTarget(metalRoughTex);
/// desc.depthBuffer     = builder.ImportDepthBuffer(depthTex);
/// desc.depthTexture    = depthHandle;
/// desc.drawList        = &sceneDrawList;
/// desc.cbAllocator     = &perObjectAllocator;
/// desc.perFrameAlloc   = perFrameCBAlloc;
/// desc.pipeline        = gbufferPSO;
/// desc.vertexBufferSrvIndex = 100;  // Bindless SRV heap index
/// desc.vertexBuffer    = vertexBufHandle;
/// desc.indexBuffer     = indexBufHandle;
/// desc.indexFormat     = RHIIndexFormat::Uint32;
/// desc.indexCount      = meshIndexCount;
/// desc.bindless        = true;       // Enable SM6.6 bindless path
/// desc.viewportWidth   = 1920.0f;
/// desc.viewportHeight  = 1080.0f;
/// AddGBufferPass(builder, desc);
/// ```
///
/// # Panics
/// Asserts if `desc.drawList` or `desc.cbAllocator` is null.
void AddGBufferPass(rg::RgBuilder& builder, const GBufferPassDesc& desc);

} // namespace uz
