/// UzEngine Shadow Depth Pass
///
/// Registers a shadow map rendering pass in the render graph that renders
/// depth-only geometry for each cascade of the Cascaded Shadow Map (CSM)
/// system. The pass writes to a single shadow atlas texture laid out as a
/// 2x2 grid of cascade tiles.
///
/// # Render Graph Integration
/// The pass declares:
/// - Shadow atlas as Write (DepthStencilWrite state)
///
/// Barriers are scheduled automatically by the render graph compiler.
/// The pass execution callback receives resolved physical handles.
///
/// # Atlas Layout
/// A single 2D depth texture of size `(atlasResolution * 2) x (atlasResolution * 2)`.
/// Four cascades are arranged in a 2x2 grid:
/// - Cascade 0: top-left     `(0,   0)   to (res,   res)`
/// - Cascade 1: top-right    `(res, 0)   to (2*res, res)`
/// - Cascade 2: bottom-left  `(0,   res) to (res,   2*res)`
/// - Cascade 3: bottom-right `(res, res) to (2*res, 2*res)`
///
/// # Binding Model
/// Supports both SM6.6 bindless (descriptor index via root constant at slot 0)
/// and SM5 fallback (explicit SetVertexBuffer). Per-object CB bound at root
/// param 2, shadow CB (light viewProj) bound at root param 3.
/// No per-frame CB (slot 1) is bound since the shadow pass does not need
/// camera data.

#pragma once

#include "render_graph_types.h"
#include "cascade.h"
#include "rhi_types.h"

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
// ShadowPassDesc — configuration for the shadow depth pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the shadow depth pass.
///
/// All fields must be set before calling `AddShadowPass`. The pass does
/// not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct ShadowPassDesc {
    /// Render graph resource ID for the shadow atlas (write, DepthStencilWrite state).
    rg::RgResourceId  shadowAtlas;

    /// Physical depth texture handle for the shadow atlas.
    ///
    /// Used to set up the depth-only render pass attachment for each cascade.
    RHITextureHandle   shadowAtlasTexture;

    /// Sorted draw list to iterate. Must not be null.
    const DrawList*    drawList;

    /// Per-object CB ring allocator. Must not be null.
    CBRingAllocator*   cbAllocator;

    /// Pipeline state object for the depth-only shadow shader.
    RHIPipelineHandle  pipeline;

    /// SRV index for bindless vertex fetch (SM6.6 path).
    uint32_t           vertexBufferSrvIndex;

    /// Vertex buffer handle (SM5 fallback path).
    RHIBufferHandle    vertexBuffer;

    /// Index buffer handle.
    RHIBufferHandle    indexBuffer;

    /// Index element format (Uint16 or Uint32).
    RHIIndexFormat     indexFormat;

    /// Total indices per mesh (single-mesh path).
    uint32_t           indexCount;

    /// True if SM6.6 bindless path is active.
    bool               bindless;

    /// Per-cascade data (view-projection matrices and split distances).
    ///
    /// Only the first `cascadeCount` entries are read.
    ShadowCascade      cascades[kMaxShadowCascades];

    /// Number of active cascades to render (1 to kMaxShadowCascades).
    uint32_t           cascadeCount;

    /// Shadow map resolution per cascade in texels (width = height).
    ///
    /// The full atlas is `(atlasResolution * 2) x (atlasResolution * 2)`.
    uint32_t           atlasResolution;
};

/// Registers a shadow depth pass in the render graph.
///
/// The pass writes to the shadow atlas depth texture, rendering depth-only
/// geometry for each cascade. For each cascade the pass sets the viewport
/// and scissor to the cascade's tile in the 2x2 atlas, binds the shadow
/// pipeline, and issues one indexed draw call per draw command with
/// per-object and per-cascade constant buffer binding.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc` — Pass configuration (atlas, draw list, pipeline, cascades, etc.).
///   All referenced resources must have been created or imported in `builder`
///   before calling this function.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Clears the shadow atlas depth texture to 1.0.
/// 2. For each cascade:
///    a. Sets viewport and scissor to the cascade's atlas tile.
///    b. Begins a depth-only render pass (0 color attachments).
///    c. Binds the shadow pipeline, vertex/index buffers.
///    d. For each draw command: allocates PerObjectCB (world matrix) and
///       ShadowCB (light viewProj), binds at root params 2 and 3, issues DrawIndexed.
///    e. Ends the render pass.
/// 3. Logs trace with cascade count and total draw calls.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation (before `RgBuilder::Execute()`).
/// The callback captures state at call time and does not access `builder` after registration.
///
/// # Examples
/// ```cpp
/// ShadowPassDesc desc{};
/// desc.shadowAtlas           = builder.ImportDepthBuffer(shadowAtlasTex);
/// desc.shadowAtlasTexture    = shadowAtlasHandle;
/// desc.drawList              = &sceneDrawList;
/// desc.cbAllocator           = &perObjectAllocator;
/// desc.pipeline              = shadowPSO;
/// desc.vertexBufferSrvIndex  = 100;
/// desc.vertexBuffer          = vertexBufHandle;
/// desc.indexBuffer           = indexBufHandle;
/// desc.indexFormat           = RHIIndexFormat::Uint32;
/// desc.indexCount            = meshIndexCount;
/// desc.bindless              = true;
/// for (uint32_t i = 0; i < cascadeCount; ++i) {
///     desc.cascades[i] = computedCascades[i];  // From ComputeCascades()
/// }
/// desc.cascadeCount          = cascadeCount;
/// desc.atlasResolution       = 2048;
/// AddShadowPass(builder, desc);
/// ```
///
/// # Panics
/// Asserts if `desc.drawList` or `desc.cbAllocator` is null.
/// Asserts if `desc.cascadeCount` is 0 or exceeds `kMaxShadowCascades`.
void AddShadowPass(rg::RgBuilder& builder, const ShadowPassDesc& desc);

} // namespace uz
