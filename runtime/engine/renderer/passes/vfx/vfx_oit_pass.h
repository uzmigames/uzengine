#pragma once

/// VFX Weighted Blended OIT render passes — accumulation + resolve.
///
/// Phase 9.11.0 — render-graph wiring for the phase 8.16.0 OIT shaders.
///
/// Two passes:
///
/// 1. **VfxOITAccumulation** (graphics): emitters with `blendMode=WeightedOIT`
///    draw into two MRT targets:
///      - RT0 (RGBA16F, accum): `color·α·w` / `α·w` via additive blend (ONE/ONE)
///      - RT1 (R8, revealage): `(1−α)` via multiplicative blend (ZERO/INV_SRC_ALPHA)
///    Depth: read-only (same scene depth as the regular particle pass).
///
/// 2. **VfxOITResolve** (fullscreen PS): reads accum+revealage SRVs, composites
///    onto the HDR scene colour RT. Blend: `dst×srcAlpha + src×(1−srcAlpha)`.
///
/// Both transient RTs (accum, revealage) are allocated via the render graph
/// at screen resolution and released after resolve.
///
/// @ref McGuire & Bavoil 2013 "Weighted Blended Order-Independent Transparency"
/// @ref engine/vfx/modules/vfx_oit.h (CPU math helpers)
/// @ref shaders/runtime/vfx/vfx_oit_resolve_ps.hlsl
/// @ref docs/engine/25-vfx/33-weighted-oit.md

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

struct GPUParticleBuffer;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Per-emitter OIT render data
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter data for the OIT accumulation pass. Mirrors
/// `EmitterRenderData` but only carries fields relevant to the OIT path.
struct OITEmitterRenderData {
    /// GPU particle buffer SRV indices for position, size, color, rotation.
    uint32_t positionSrvIdx    = UINT32_MAX;
    uint32_t sizeSrvIdx        = UINT32_MAX;
    uint32_t colorSrvIdx       = UINT32_MAX;
    uint32_t rotationSrvIdx    = UINT32_MAX;

    /// Indirect draw args buffer handle.
    RHIBufferHandle indirectDrawArgs;

    /// Per-emitter CB allocation (billboard constants).
    CBAllocation renderCB = {};

    /// Distance to camera for optional rough back-to-front ordering
    /// (improves convergence quality even though OIT is order-independent).
    float distanceToCamera = 0.0f;

    /// Render-graph resource ID for the indirect draw args (for barrier).
    rg::RgResourceId rgIndirectDrawArgs;
};

// ─────────────────────────────────────────────────────────────────────────────
// Accumulation pass descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the OIT accumulation pass.
struct VfxOITAccumPassDesc {
    /// Pipeline with MRT blend state:
    ///   RT0 (RGBA16F): SrcBlend=One, DstBlend=One (additive)
    ///   RT1 (R8):      SrcBlend=Zero, DstBlend=InvSrcAlpha (multiplicative)
    RHIPipelineHandle oitAccumPipeline;

    /// Transient RGBA16F accumulation target (RG-allocated, screen-sized).
    rg::RgResourceId accumTarget;
    RHITextureHandle accumTexture;

    /// Transient R8 revealage target (RG-allocated, screen-sized).
    rg::RgResourceId revealageTarget;
    RHITextureHandle revealageTexture;

    /// Scene depth (read-only for depth testing, no depth write).
    rg::RgResourceId depthTarget;
    RHITextureHandle depthTexture;

    /// Per-frame CB (b1: viewProj, camera, etc.).
    CBAllocation perFrameCB = {};

    /// Viewport dimensions.
    uint32_t renderWidth  = 0;
    uint32_t renderHeight = 0;

    /// OIT-tagged emitters to render.
    std::vector<OITEmitterRenderData> emitters;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resolve pass descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the OIT resolve fullscreen pass.
struct VfxOITResolvePassDesc {
    /// Pipeline for the resolve fullscreen shader.
    /// Blend: dst×srcAlpha + src×(1−srcAlpha) on the scene color RT.
    RHIPipelineHandle resolvePipeline;

    /// Accumulation RT (SRV read).
    rg::RgResourceId accumTarget;

    /// Revealage RT (SRV read).
    rg::RgResourceId revealageTarget;

    /// SRV descriptor indices for the two textures (bindless path).
    uint32_t accumSrvIndex     = UINT32_MAX;
    uint32_t revealageSrvIndex = UINT32_MAX;

    /// HDR scene colour target (read-modify-write via composite blend).
    rg::RgResourceId colorTarget;
    RHITextureHandle colorTexture;

    /// Viewport dimensions.
    uint32_t renderWidth  = 0;
    uint32_t renderHeight = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pass registration
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the OIT accumulation pass. Emitters draw into the
/// accum + revealage MRT with order-independent blend state.
///
/// Skipped (no-op) when `desc.emitters` is empty.
void AddVfxOITAccumPass(rg::RgBuilder& builder,
                         const VfxOITAccumPassDesc& desc);

/// Registers the OIT resolve composite pass. Reads accum + revealage
/// and composites onto the HDR scene colour RT.
///
/// Skipped (no-op) when the accumulation target is invalid (no OIT
/// emitters this frame).
void AddVfxOITResolvePass(rg::RgBuilder& builder,
                            const VfxOITResolvePassDesc& desc);

} // namespace uz
