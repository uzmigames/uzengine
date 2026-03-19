/// UzEngine G-Buffer Pass
///
/// Registers a deferred rendering G-Buffer pass in the render graph that
/// iterates a sorted DrawList and outputs albedo+AO and packed
/// normal+metallic+roughness into two render targets plus a depth buffer.
///
/// # Render Graph Integration
/// The pass declares:
/// - Albedo target as Write (RenderTarget state) — RGBA8Unorm (RGB=albedo, A=AO)
/// - Normal target as Write (RenderTarget state) — RGBA8Unorm (RG=octNormal, B=metallic, A=roughness)
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
#include "instance_batcher.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"
#include "mesh/gpu_mesh.h"

#include <cstdint>

namespace uz {

class DrawList;
class CBRingAllocator;
struct PerFrameParams;

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
    rg::RgResourceId albedoTarget;      ///< Albedo+AO render target (write, RenderTarget state, RGBA8Unorm).
    rg::RgResourceId normalTarget;      ///< Packed normal+metallic+roughness target (write, RenderTarget state, RGBA8Unorm).
    rg::RgResourceId positionTarget;    ///< World-space position target (write, RenderTarget state, RGBA32Float).
    rg::RgResourceId emissiveTarget;    ///< HDR emissive color target (write, RenderTarget state, RGBA16Float).
    rg::RgResourceId depthBuffer;       ///< Depth buffer (write, DepthStencilWrite state, D32Float).
    RHITextureHandle  depthTexture;     ///< Physical depth texture for render pass setup.
    const DrawList*   drawList;         ///< Sorted draw list to iterate. Must not be null.
    CBRingAllocator*  cbAllocator;      ///< Per-object CB ring allocator. Must not be null.
    CBAllocation      perFrameAlloc;    ///< Pre-allocated per-frame CB (already filled by caller).
    RHIPipelineHandle pipeline;         ///< PSO handle for the standard opaque G-Buffer shader.
    RHIPipelineHandle maskedPipeline;   ///< PSO handle for masked (alpha-test, two-sided) G-Buffer path. Invalid = no masked support.

    /// Array of loaded GPU meshes, indexed by `DrawCommand::meshId`.
    ///
    /// Each draw command selects its mesh by `DrawCommand::meshId`. Commands
    /// whose `meshId >= meshCount` or whose mesh `!IsValid()` are skipped.
    /// Must not be null when `meshCount > 0`. The caller retains ownership;
    /// the array must remain valid until `RgBuilder::Execute()` completes.
    const GPUMesh*    meshes = nullptr;

    /// Number of meshes in the `meshes` array.
    uint32_t          meshCount = 0;

    RHITextureHandle  fallbackWhite;   ///< 1x1 white texture for missing albedo/AO/roughness maps (SM5 fallback).
    RHITextureHandle  fallbackBlack;   ///< 1x1 black texture for missing metallic/emissive maps (SM5 fallback — zero=off).
    RHITextureHandle  fallbackNormal;  ///< 1x1 flat normal texture (128,128,255) for missing normal maps (SM5 fallback).
    bool              bindless;         ///< True if SM6.6 bindless path is active.
    float             viewportWidth;    ///< Viewport width in pixels.
    float             viewportHeight;   ///< Viewport height in pixels.

    // ── Binder path (optional — set perFrameParams to enable) ──────────
    // When perFrameParams is non-null, the pass uses ShaderParameterBinder
    // to upload the per-frame CB and bind IBL textures automatically.
    // When null, the legacy perFrameAlloc path is used.

    const PerFrameParams* perFrameParams   = nullptr; ///< If set, binder uploads this instead of perFrameAlloc.
    CBRingAllocator*      cbRingAllocator   = nullptr; ///< Required when perFrameParams is set. Used by binder for per-object CB too.

    // ── GPU Instancing (optional — set instanced=true to enable) ──────────
    bool              instanced = false;       ///< True to use instanced draw path.
    RHIPipelineHandle instancedPipeline;       ///< PSO compiled with UZ_INSTANCED=1.
    uint32_t          instanceBufferSrvIndex = UINT32_MAX; ///< SRV index for StructuredBuffer<InstanceData>.
    const InstanceBatch* batchData = nullptr;  ///< Pointer to instanced draw batches array.
    uint32_t          batchCount = 0;          ///< Number of batches in the array.
};

/// Registers a deferred G-Buffer pass in the render graph.
///
/// The pass writes albedo+AO and packed normal+metallic+roughness into two
/// color targets plus depth, iterates the sorted DrawList, and issues one
/// indexed draw call per command with per-object constant buffer binding.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc` — Pass configuration (resources, draw list, pipeline, etc.).
///   All referenced resources must have been created or imported in `builder`
///   before calling this function.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Clears both G-Buffer render targets and the depth buffer.
/// 2. Begins a render pass with two color attachments and depth.
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
/// desc.depthBuffer     = builder.ImportDepthBuffer(depthTex);
/// desc.depthTexture    = depthHandle;
/// desc.drawList        = &sceneDrawList;
/// desc.cbAllocator     = &perObjectAllocator;
/// desc.perFrameAlloc   = perFrameCBAlloc;
/// desc.pipeline        = gbufferPSO;
/// desc.meshes          = loadedMeshes.data();
/// desc.meshCount       = static_cast<uint32_t>(loadedMeshes.size());
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
