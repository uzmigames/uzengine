/// UzEngine DBuffer Decal Pass
///
/// Registers two render graph passes for the DBuffer decal system:
///
/// 1. **DecalRender** — Renders all active decal OBB projectors into three
///    DBuffer render targets (albedo, normal, material) with alpha blending.
///    Runs AFTER the depth prepass but BEFORE the GBuffer fill pass.
///
/// 2. **DecalApply** — Fullscreen compute pass that merges the DBuffer into
///    the GBuffer. Runs AFTER the GBuffer fill and BEFORE deferred lighting.
///
/// # DBuffer Layout
/// - DBuffer A (RGBA8Unorm): decal albedo (RGB) + opacity (A)
/// - DBuffer B (RGBA8Unorm): decal normal [0,1] (RGB) + normal blend (A)
/// - DBuffer C (RGBA8Unorm): roughness (R) + metallic (G) + AO (B) + blend (A)
///
/// # Render Graph Integration
/// - DecalRender writes DBuffer A/B/C (RenderTarget state), reads depth (ShaderResource)
/// - DecalApply reads DBuffer A/B/C (ShaderResource), writes GBuffer albedo+normal (UnorderedAccess)
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decals
/// @ref Pesce 2014 "Rendering Decals in Screen Space"

#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class CBRingAllocator;
class IRHIDevice;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DecalObjectCB — per-decal constant buffer (bound at b2)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-decal constant buffer data uploaded per draw call.
///
/// Must match the HLSL `DecalObjectCB` at register b2 in decal_vs.hlsl and
/// decal_ps.hlsl exactly.
struct DecalObjectCB {
    Mat4     world;            ///< Decal world transform (position + rotation + extents).
    Mat4     invWorld;         ///< Inverse of world (for world->local projection in PS).
    float    opacity;          ///< Global opacity [0,1].
    float    normalBlend;      ///< Normal blend intensity [0,1].
    float    roughness;        ///< Roughness value/multiplier.
    float    metallic;         ///< Metallic value/multiplier.
    float    fadeDistance;      ///< Edge fade distance in cm.
    float    angleFade;        ///< Angle fade start in degrees.
    uint32_t albedoSrv;        ///< Albedo texture SRV index (UINT32_MAX = none).
    uint32_t normalSrv;        ///< Normal texture SRV index (UINT32_MAX = none).
    uint32_t materialSrv;      ///< Material texture SRV index (UINT32_MAX = none).
    uint32_t depthSrv;         ///< GBuffer depth SRV index.
    uint32_t normalGbufSrv;    ///< GBuffer normal SRV index (for angle fade).
    float    _pad0 = 0.0f;    ///< Pad to 16-byte boundary.
};

static_assert(sizeof(DecalObjectCB) % 16 == 0, "DecalObjectCB must be 16-byte aligned");

// ─────────────────────────────────────────────────────────────────────────────
// DecalApplyConstants — root constants for the compute apply pass
// ─────────────────────────────────────────────────────────────────────────────

/// Push constants for the DBuffer->GBuffer merge compute shader.
/// Bound as root constants at b0 (same slot as descriptor indices).
struct DecalApplyConstants {
    uint32_t dbufferASrvIndex;      ///< DBuffer A SRV index.
    uint32_t dbufferBSrvIndex;      ///< DBuffer B SRV index.
    uint32_t dbufferCSrvIndex;      ///< DBuffer C SRV index.
    uint32_t gbufferAlbedoUavIndex; ///< GBuffer albedo UAV index.
    uint32_t gbufferNormalUavIndex; ///< GBuffer normal UAV index.
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t _pad2 = 0;
    uint32_t texWidth;              ///< Render target width.
    uint32_t texHeight;             ///< Render target height.
};

static_assert(sizeof(DecalApplyConstants) == 40, "DecalApplyConstants must be 40 bytes (10 uint32)");

// ─────────────────────────────────────────────────────────────────────────────
// DecalInfo — sorted decal data for rendering
// ─────────────────────────────────────────────────────────────────────────────

/// A single decal prepared for rendering. Filled during CPU gathering
/// and sorted by priority before the render pass.
struct DecalInfo {
    DecalObjectCB cb;        ///< Pre-filled constant buffer data for this decal.
    int32_t       priority;  ///< Sort key (ascending — higher priority renders last).
};

// ─────────────────────────────────────────────────────────────────────────────
// DecalRenderPassDesc — configuration for the DBuffer render pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the DBuffer decal render pass.
///
/// All fields must be set before calling `AddDecalRenderPass`. The pass does
/// not take ownership of any pointed-to objects.
struct DecalRenderPassDesc {
    // ── Render targets (DBuffer) ─────────────────────────────────────────
    rg::RgResourceId dbufferARes;     ///< DBuffer A resource (write, RenderTarget).
    rg::RgResourceId dbufferBRes;     ///< DBuffer B resource (write, RenderTarget).
    rg::RgResourceId dbufferCRes;     ///< DBuffer C resource (write, RenderTarget).
    RHITextureHandle dbufferATex;     ///< Physical DBuffer A texture.
    RHITextureHandle dbufferBTex;     ///< Physical DBuffer B texture.
    RHITextureHandle dbufferCTex;     ///< Physical DBuffer C texture.

    // ── Dependencies ─────────────────────────────────────────────────────
    rg::RgResourceId depthRes;        ///< Depth buffer resource (read, ShaderResource).
    rg::RgResourceId normalRes;       ///< GBuffer normal resource (read, ShaderResource — for angle fade).

    // ── Pipeline ─────────────────────────────────────────────────────────
    RHIPipelineHandle pipeline;        ///< PSO for decal rendering (decal_vs + decal_ps).
    CBRingAllocator*  cbAllocator;     ///< CB ring for per-decal constant buffers.

    // ── Decal data ───────────────────────────────────────────────────────
    const DecalInfo*  decals;          ///< Sorted array of decals to render.
    uint32_t          decalCount;      ///< Number of decals in the array.

    // ── Viewport ─────────────────────────────────────────────────────────
    float viewportWidth;               ///< Viewport width in pixels.
    float viewportHeight;              ///< Viewport height in pixels.
};

/// Registers the DBuffer decal render pass in the render graph.
///
/// Renders all decals into three DBuffer render targets with alpha blending.
/// Decals are rendered as unit cubes transformed by each decal's world matrix.
/// The pixel shader reconstructs world position from depth, projects into
/// decal space, and samples decal textures.
///
/// # Pass Execution
/// 1. Clears all three DBuffer targets to (0,0,0,0).
/// 2. Sets render targets (3 color, no depth write).
/// 3. Sets alpha-blend state (SrcAlpha / InvSrcAlpha).
/// 4. For each decal: uploads DecalObjectCB, binds at b2, draws 36 vertices.
/// 5. Ends render pass.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
void AddDecalRenderPass(rg::RgBuilder& builder, const DecalRenderPassDesc& desc);

// ─────────────────────────────────────────────────────────────────────────────
// DecalApplyPassDesc — configuration for the DBuffer->GBuffer merge pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the DBuffer application compute pass.
struct DecalApplyPassDesc {
    // ── DBuffer inputs (read, ShaderResource) ────────────────────────────
    rg::RgResourceId dbufferARes;
    rg::RgResourceId dbufferBRes;
    rg::RgResourceId dbufferCRes;
    uint32_t         dbufferASrvIndex;
    uint32_t         dbufferBSrvIndex;
    uint32_t         dbufferCSrvIndex;

    // ── GBuffer outputs (read-write, UnorderedAccess) ────────────────────
    rg::RgResourceId albedoRes;
    rg::RgResourceId normalRes;
    uint32_t         albedoUavIndex;
    uint32_t         normalUavIndex;

    // ── Pipeline ─────────────────────────────────────────────────────────
    RHIPipelineHandle pipeline;        ///< Compute PSO for decal_apply_cs.

    // ── Dimensions ───────────────────────────────────────────────────────
    uint32_t width;                    ///< Render target width.
    uint32_t height;                   ///< Render target height.
};

/// Registers the DBuffer application compute pass in the render graph.
///
/// Dispatches a fullscreen compute shader that reads the three DBuffer
/// targets and modifies the GBuffer albedo and normal textures in-place
/// via alpha-weighted lerp.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
void AddDecalApplyPass(rg::RgBuilder& builder, const DecalApplyPassDesc& desc);

} // namespace uz
