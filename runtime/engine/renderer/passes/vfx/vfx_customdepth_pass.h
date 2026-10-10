#pragma once

/// VFX CustomDepth + stencil render pass (phase 9.12.0).
///
/// Registers a render-graph pass that draws particle emitters into the
/// engine's CustomDepth render target for reveal/outline/DoF masking,
/// optionally writing a stencil ref for post-process stencil masking.
///
/// # Pipeline permutations
///
/// Three PSO variants cover the `VfxStencilWriteMode` enum:
///
/// | `writeMode`  | Depth | Stencil |
/// |-------------|-------|---------|
/// | `Disabled`  | write | keep    |
/// | `Replace`   | write | replace (ref = `stencilRef`) |
/// | `Increment` | write | incrSat |
///
/// The caller pre-creates these PSOs and stores them in the pass descriptor.
/// The pass selects the correct one per-emitter at draw time.
///
/// # Shader
///
/// Uses `vfx_customdepth_ps.hlsl` (phase 8.17.0) — depth-only render with
/// alpha-test discard at the same threshold as the billboard PS so
/// silhouettes agree.
///
/// @ref engine/vfx/modules/vfx_stencil.h
/// @ref shaders/runtime/vfx/vfx_customdepth_ps.hlsl
/// @ref docs/engine/25-vfx/34-stencil-customdepth.md

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"
#include "engine/vfx/modules/vfx_stencil.h"

#include <cstdint>
#include <vector>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Per-emitter custom-depth data
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter data for the CustomDepth pass.
struct CustomDepthEmitterData {
    /// Stencil configuration for this emitter.
    VfxStencilConfig stencilConfig;

    /// Indirect draw args buffer handle.
    RHIBufferHandle indirectDrawArgs;

    /// Render-graph resource ID for the indirect draw args (barrier).
    rg::RgResourceId rgIndirectDrawArgs;

    /// Per-emitter CB allocation (billboard VS constants — positions, sizes).
    CBAllocation renderCB = {};

    /// Bindless SRV indices for the particle attribute buffers.
    uint32_t positionSrvIdx  = UINT32_MAX;
    uint32_t sizeSrvIdx      = UINT32_MAX;
    uint32_t rotationSrvIdx  = UINT32_MAX;

    /// Sprite texture SRV index (for alpha-test in the CustomDepth PS).
    uint32_t spriteSrvIdx = UINT32_MAX;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pass descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the CustomDepth + stencil render pass.
struct VfxCustomDepthPassDesc {
    /// PSO variants indexed by `VfxStencilWriteMode`.
    ///   [0] = Disabled (depth-only, stencil keep)
    ///   [1] = Replace (depth + stencil replace)
    ///   [2] = Increment (depth + stencil incrSat)
    RHIPipelineHandle pipelines[3] = {};

    /// CustomDepth render target (depth-only, no colour attachment).
    rg::RgResourceId customDepthTarget;
    RHITextureHandle customDepthTexture;

    /// Per-frame CB (b1: viewProj, camera).
    CBAllocation perFrameCB = {};

    /// Viewport dimensions.
    uint32_t renderWidth  = 0;
    uint32_t renderHeight = 0;

    /// Emitters with `customDepthEnabled == true`.
    std::vector<CustomDepthEmitterData> emitters;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pass registration
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the CustomDepth + stencil render-graph pass.
///
/// For each emitter, selects the PSO variant matching its
/// `stencilConfig.writeMode`, sets the stencil reference via
/// `SetStencilRef`, and issues `DrawIndexedIndirect`.
///
/// Skipped (no-op) when `desc.emitters` is empty.
void AddVfxCustomDepthPass(rg::RgBuilder& builder,
                            const VfxCustomDepthPassDesc& desc);

} // namespace uz
