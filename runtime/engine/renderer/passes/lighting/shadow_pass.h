/// UzEngine Shadow Depth Pass
///
/// Registers a shadow map rendering pass in the render graph that renders
/// depth-only geometry for each cascade of the Cascaded Shadow Map (CSM)
/// system. The pass writes to a single shadow atlas texture with a dynamic
/// NxM grid layout that scales with cascade count.
///
/// # Render Graph Integration
/// The pass declares:
/// - Shadow atlas as Write (DepthStencilWrite state)
///
/// Barriers are scheduled automatically by the render graph compiler.
/// The pass execution callback receives resolved physical handles.
///
/// # Atlas Layout
/// A single 2D depth texture with a dynamic grid layout based on cascade count:
/// - 1-4 cascades: 2x2 grid (backward compatible with original layout)
/// - 5-6 cascades: 3x2 grid
/// - 7-8 cascades: 4x2 grid
///
/// Atlas texture size is `(cols * atlasResolution) x (rows * atlasResolution)`.
/// The number of columns is determined by `GetAtlasColumns(cascadeCount)`.
///
/// # Binding Model
/// Supports both SM6.6 bindless (descriptor index via root constant at slot 0)
/// and SM5 fallback (explicit SetVertexBuffer). Per-object CB bound at root
/// param 2, shadow CB (light viewProj) bound at root param 3.
/// No per-frame CB (slot 1) is bound since the shadow pass does not need
/// camera data.

#pragma once

#include "render_graph_types.h"
#include "shadow_cascade.h"
#include "shadow_cache.h"
#include "instance_batcher.h"
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
    /// The full atlas is `(atlasResolution * cols) x (atlasResolution * rows)`
    /// where cols/rows depend on cascadeCount via `GetAtlasColumns()`.
    uint32_t           atlasResolution;

    /// Cache mode for this cascade's shadow depth rendering.
    ///
    /// When `Uncached`, behavior is identical to pre-cache (backward compatible).
    /// When `StaticPrimitivesOnly`, the pass renders only static geometry into
    /// the cached shadow texture. When `MovablePrimitivesOnly`, the pass renders
    /// only dynamic geometry (the static cache is composited in the pixel shader).
    ///
    /// Default: `Uncached` — no behavioral change unless explicitly set.
    ShadowCacheMode    cacheMode = ShadowCacheMode::Uncached;

    /// LOD bias for far shadow cascades. Applied to meshes rendered in cascades
    /// where `bFarShadowCascade == true`. Higher values = coarser geometry.
    /// 0 = no bias (same LOD as main cascades). Only effective when the engine
    /// has a LOD selection system.
    /// @source UE5 DirectionalLightComponent.cpp:68 — GFarShadowStaticMeshLODBias
    int32_t            farShadowLodBias = 0;

    // ── GPU Instancing (optional — set instanced=true to enable) ──────────
    bool               instanced = false;       ///< True to use instanced draw path.
    RHIPipelineHandle  instancedPipeline;       ///< PSO compiled with UZ_INSTANCED=1.
    uint32_t           instanceBufferSrvIndex = UINT32_MAX; ///< SRV index for StructuredBuffer<InstanceData>.
    const InstanceBatch* batchData = nullptr;  ///< Pointer to instanced draw batches array.
    uint32_t           batchCount = 0;         ///< Number of batches in the array.
};

/// Computes the number of columns in the shadow atlas grid for a given cascade count.
///
/// - 1-4 cascades: 2 columns (2x2 grid, backward compatible)
/// - 5-6 cascades: 3 columns (3x2 grid)
/// - 7-8 cascades: 4 columns (4x2 grid)
///
/// # Arguments
/// * `totalCascades` — Total number of cascades (main + far), 1 to kMaxShadowCascades.
///
/// # Returns
/// Number of columns in the atlas grid layout.
[[nodiscard]] inline constexpr uint32_t GetAtlasColumns(uint32_t totalCascades) {
    if (totalCascades <= 4) return 2;
    if (totalCascades <= 6) return 3;
    return 4;
}

/// Computes the number of rows in the shadow atlas grid for a given cascade count.
///
/// # Arguments
/// * `totalCascades` — Total number of cascades (main + far), 1 to kMaxShadowCascades.
///
/// # Returns
/// Number of rows in the atlas grid layout.
[[nodiscard]] inline constexpr uint32_t GetAtlasRows(uint32_t totalCascades) {
    const uint32_t cols = GetAtlasColumns(totalCascades);
    return (totalCascades + cols - 1) / cols;
}

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
/// Asserts if `desc.cascadeCount` is 0 or exceeds `kMaxShadowCascades` (8).
void AddShadowPass(rg::RgBuilder& builder, const ShadowPassDesc& desc);

} // namespace uz
