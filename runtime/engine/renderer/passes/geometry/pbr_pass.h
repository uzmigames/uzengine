/// UzEngine PBR Pass
///
/// Registers a forward PBR rendering pass in the render graph that iterates
/// a sorted DrawList and issues GPU draw calls with Cook-Torrance PBR
/// lighting. Handles per-object constant buffer binding and sort-key-based
/// state management (no switch-on-type).
///
/// # Render Graph Integration
/// The pass declares:
/// - Backbuffer as Write (RenderTarget state)
/// - Depth buffer as Write (DepthStencilWrite state)
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
// PBRPassDesc — configuration for the forward PBR pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the forward PBR pass.
///
/// All fields must be set before calling `AddPBRPass`. The pass does
/// not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct PBRPassDesc {
    rg::RgResourceId backbuffer;       ///< Render target (write, RenderTarget state).
    rg::RgResourceId depthBuffer;      ///< Depth buffer (write, DepthStencilWrite state).
    RHITextureHandle  depthTexture;    ///< Physical depth texture for render pass setup.
    const DrawList*   drawList;        ///< Sorted draw list to iterate. Must not be null.
    CBRingAllocator*  cbAllocator;     ///< Per-object CB ring allocator. Must not be null.
    CBAllocation      perFrameAlloc;   ///< Pre-allocated per-frame CB (already filled by caller).
    RHIPipelineHandle pipeline;        ///< PSO handle for the PBR shader.

    /// Array of loaded GPU meshes, indexed by `DrawCommand::meshId`.
    ///
    /// Each draw command selects its mesh by `DrawCommand::meshId`. Commands
    /// whose `meshId >= meshCount` or whose mesh `!IsValid()` are skipped.
    /// Must not be null when `meshCount > 0`. The caller retains ownership;
    /// the array must remain valid until `RgBuilder::Execute()` completes.
    const GPUMesh*    meshes = nullptr;

    /// Number of meshes in the `meshes` array.
    uint32_t          meshCount = 0;

    bool              bindless;        ///< True if SM6.6 bindless path is active.
    float             viewportWidth;   ///< Viewport width in pixels.
    float             viewportHeight;  ///< Viewport height in pixels.

    // ── Binder path (optional — set perFrameParams to enable) ──────────
    // When perFrameParams is non-null, the pass uses ShaderParameterBinder
    // to upload the per-frame CB and bind IBL textures automatically.

    const PerFrameParams* perFrameParams   = nullptr; ///< If set, binder uploads this instead of perFrameAlloc.
    CBRingAllocator*      cbRingAllocator   = nullptr; ///< Required when perFrameParams is set. Used by binder for per-object CB too.
};

/// Registers a forward PBR pass in the render graph.
///
/// The pass writes to the backbuffer and depth buffer, iterates the sorted
/// DrawList, and issues one indexed draw call per command with per-object
/// constant buffer binding.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc` — Pass configuration (resources, draw list, pipeline, etc.).
///   All referenced resources must have been created or imported in `builder`
///   before calling this function.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Clears the backbuffer and depth buffer.
/// 2. Begins a render pass with one color attachment and depth.
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
/// PBRPassDesc desc{};
/// desc.backbuffer           = builder.ImportRenderTarget(backbufferTex);
/// desc.depthBuffer          = builder.ImportDepthBuffer(depthTex);
/// desc.depthTexture         = depthHandle;
/// desc.drawList             = &sceneDrawList;
/// desc.cbAllocator          = &perObjectAllocator;
/// desc.perFrameAlloc        = perFrameCBAlloc;
/// desc.pipeline             = pbrPSO;
/// desc.meshes               = loadedMeshes.data();
/// desc.meshCount            = static_cast<uint32_t>(loadedMeshes.size());
/// desc.bindless             = true;
/// desc.viewportWidth        = 1920.0f;
/// desc.viewportHeight       = 1080.0f;
/// AddPBRPass(builder, desc);
/// ```
///
/// # Panics
/// Asserts if `desc.drawList` or `desc.cbAllocator` is null.
void AddPBRPass(rg::RgBuilder& builder, const PBRPassDesc& desc);

} // namespace uz
