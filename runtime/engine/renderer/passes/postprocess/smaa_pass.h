/// UzEngine SMAA Pass — Subpixel Morphological Anti-Aliasing.
///
/// Implements SMAA 1x as three fullscreen pixel shader passes:
///
/// 1. **Edge Detection** (smaa_edge_ps.hlsl): Luma-based edge detection on LDR
///    scene color. Outputs edges to an RG8 render target.
/// 2. **Blend Weight Calculation** (smaa_blend_ps.hlsl): Reads edges + precomputed
///    area/search textures, outputs per-pixel blend weights to an RGBA8 RT.
/// 3. **Neighborhood Blending** (smaa_resolve_ps.hlsl): Reads scene color + blend
///    weights, produces the final anti-aliased image.
///
/// The two lookup textures (area: 160x560 RG8, search: 64x16 R8) are uploaded from
/// precomputed C arrays (AreaTex.h, SearchTex.h) at init time.
///
/// # Integration
/// SMAA runs on LDR color AFTER tonemapping and BEFORE sRGB output:
///   ... → Tonemap (LDR linear) → SMAA → sRGB output
///
/// # GPU Resources
/// - Edges RT: RG8_UNORM at full resolution (owned).
/// - Blend weights RT: RGBA8_UNORM at full resolution (owned).
/// - Area texture: 160x560 RG8_UNORM (owned, uploaded once).
/// - Search texture: 64x16 R8_UNORM (owned, uploaded once).
/// - Final output is written to a caller-provided render target.
///
/// @ref Jimenez 2012 "SMAA: Enhanced Subpixel Morphological Antialiasing"
///   — http://www.iryoku.com/smaa/

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;

// ─────────────────────────────────────────────────────────────────────────────
// SMAASettings — runtime-tunable parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime-configurable SMAA settings.
///
/// @ref Jimenez 2012 "SMAA" — SMAA_PRESET_HIGH defaults
struct SMAASettings {
    bool enabled = false; ///< Master toggle. Default: off (CVar r.SMAA).
};

// ─────────────────────────────────────────────────────────────────────────────
// SMAAPass
// ─────────────────────────────────────────────────────────────────────────────

/// GPU SMAA anti-aliasing pass.
///
/// Owns edge/blend render targets, area/search lookup textures, and 3 graphics
/// pipelines (one per SMAA pass). Does NOT own the IRHIDevice or ShaderManager
/// — both must outlive this object.
///
/// # Ownership
/// Owns `RHIPipelineHandle`, `RHIShaderHandle`, and `RHITextureHandle` objects.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class SMAAPass {
public:
    SMAAPass() = default;
    ~SMAAPass();

    SMAAPass(const SMAAPass&) = delete;
    SMAAPass& operator=(const SMAAPass&) = delete;
    SMAAPass(SMAAPass&&) = delete;
    SMAAPass& operator=(SMAAPass&&) = delete;

    /// Initialize GPU resources: lookup textures, intermediate RTs, pipelines.
    ///
    /// # Arguments
    /// * `device`    — Initialized RHI device. Must outlive this object.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this object.
    /// * `width`     — Render target width in pixels.
    /// * `height`    — Render target height in pixels.
    ///
    /// # Returns
    /// `true` on success, `false` if resource creation or shader compilation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t width, uint32_t height);

    /// Upload precomputed area/search texture data to GPU.
    ///
    /// Must be called once after Init(), using a command list that will be
    /// submitted and waited on (GPU must finish before the staging buffers
    /// are released). The caller is responsible for submitting the command list
    /// and calling WaitIdle() before the UploadContext goes out of scope.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    void UploadLookupTextures(IRHICommandList* cmdList);

    /// Execute the full 3-pass SMAA pipeline.
    ///
    /// Input: LDR scene color (post-tonemap) in SRV state.
    /// Output: Anti-aliased result written to `outputRTHandle`.
    ///
    /// # Arguments
    /// * `cmdList`         — Command list in recording state.
    /// * `inputColorSrvIndex` — SRV descriptor index for the LDR scene color input.
    /// * `inputColorTexture`  — Texture handle for the LDR scene color (for barriers).
    /// * `outputRTHandle`     — Render target handle for the final anti-aliased output.
    /// * `width`, `height`    — Render target dimensions.
    void Execute(IRHICommandList* cmdList,
                 uint32_t inputColorSrvIndex,
                 RHITextureHandle inputColorTexture,
                 RHITextureHandle outputRTHandle,
                 uint32_t width, uint32_t height);

    /// Recreate intermediate textures for a new resolution.
    void Resize(uint32_t width, uint32_t height);

    /// Release all GPU resources.
    void Shutdown();

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Settings (modifiable per-frame before Execute).
    SMAASettings settings;

private:
    // ── Pipeline lazy creation ──────────────────────────────────────────
    [[nodiscard]] bool EnsureEdgeDetectionPipeline();
    [[nodiscard]] bool EnsureBlendWeightPipeline();
    [[nodiscard]] bool EnsureNeighborhoodBlendPipeline();

    // ── Resource management ─────────────────────────────────────────────
    [[nodiscard]] bool CreateLookupTextures();
    [[nodiscard]] bool CreateIntermediateTextures(uint32_t width, uint32_t height);
    void DestroyIntermediateTextures();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── Lookup Textures (created once at init) ──────────────────────────
    /// Area texture: 160x560, R8G8_UNORM.
    /// @ref Jimenez 2012 SMAA — AreaTex.h (AREATEX_WIDTH=160, AREATEX_HEIGHT=560)
    RHITextureHandle m_areaTex    = RHITextureHandle::Invalid();
    uint32_t         m_areaSrvIdx = UINT32_MAX;

    /// Search texture: 64x16, R8_UNORM.
    /// @ref Jimenez 2012 SMAA — SearchTex.h (SEARCHTEX_WIDTH=64, SEARCHTEX_HEIGHT=16)
    RHITextureHandle m_searchTex    = RHITextureHandle::Invalid();
    uint32_t         m_searchSrvIdx = UINT32_MAX;

    // ── Intermediate Render Targets (resolution-dependent) ──────────────
    /// Edge detection output: RG8_UNORM at full resolution.
    RHITextureHandle m_edgesTex    = RHITextureHandle::Invalid();
    uint32_t         m_edgesSrvIdx = UINT32_MAX;

    /// Blend weight output: RGBA8_UNORM at full resolution.
    RHITextureHandle m_blendTex    = RHITextureHandle::Invalid();
    uint32_t         m_blendSrvIdx = UINT32_MAX;

    // ── Graphics Pipelines (lazily created) ─────────────────────────────
    RHIPipelineHandle m_edgePipeline     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_blendPipeline    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_resolvePipeline  = RHIPipelineHandle::Invalid();

    // ── Shader Handles ──────────────────────────────────────────────────
    RHIShaderHandle m_fullscreenVS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_edgePS        = RHIShaderHandle::Invalid();
    RHIShaderHandle m_blendPS       = RHIShaderHandle::Invalid();
    RHIShaderHandle m_resolvePS     = RHIShaderHandle::Invalid();

    // ── Dimensions ──────────────────────────────────────────────────────
    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    bool m_initialized     = false;
    bool m_lookupUploaded  = false;  ///< True after UploadLookupTextures() has been called.
};

} // namespace uz
