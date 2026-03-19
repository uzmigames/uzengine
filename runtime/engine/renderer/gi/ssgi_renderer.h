/// UzEngine SSGI Renderer — Screen-Space Global Illumination.
///
/// Dispatches 5 compute passes per frame for one-bounce diffuse indirect lighting:
/// 1. **PrevColorReduce** — Downsample prev frame HDR to half-res (Karis weighted).
/// 2. **SSGITrace** — Cosine-hemisphere ray march in screen space, sample prev color.
/// 3. **SpatialDenoise** — Cross-bilateral spatial filter (depth+normal edge-stopping).
/// 4. **TemporalAccum** — Velocity-reprojected temporal accumulation with variance clip.
/// 5. **BilateralUpsample** — Joint bilateral upsample from half-res to full-res.
///
/// # GPU Resources
/// - Prev scene color copy (full-res RGBA16F)
/// - Reduced prev color (half-res RGBA16F)
/// - Trace output: indirect diffuse (half-res RGBA16F) + AO (half-res R16F)
/// - Denoised output (half-res RGBA16F + R16F)
/// - Temporal history ping-pong (half-res RGBA16F + R16F, x2)
/// - Final result (full-res RGBA16F + R16F)
///
/// # Integration
/// Created and owned by SceneRenderer. The final result SRV indices are passed
/// to the deferred lighting pass via PerFrameCB::ssgiSrvIndex / ssgiAOSrvIndex.
///
/// @source UE5 IndirectLightRendering.cpp:39-45 — r.DynamicGlobalIlluminationMethod=2
/// @source UE5 ScreenSpaceRayTracing.cpp:684-704 — quality-to-ray-count map

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class PipelineLibrary;

// ─────────────────────────────────────────────────────────────────────────────
// SSGISettings — runtime-tunable parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime-configurable SSGI parameters.
///
/// @source UE5 ScreenSpaceRayTracing.cpp:60-83 — SSGI CVars
struct SSGISettings {
    bool     enabled   = true;    ///< Master toggle for SSGI.
    /// Quality level (1-4). Maps to ray count: Q1=4, Q2=8, Q3=16, Q4=32.
    /// @source UE5 ScreenSpaceRayTracing.cpp:684-704
    int      quality   = 1;
    float    intensity = 1.0f;    ///< SSGI indirect diffuse intensity multiplier.
};

// ─────────────────────────────────────────────────────────────────────────────
// SSGIRenderParams — per-frame dispatch parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame parameters needed to dispatch SSGI compute passes.
struct SSGIRenderParams {
    CBRingAllocator* cbRing      = nullptr;
    PipelineLibrary* pipelines   = nullptr;

    // GBuffer SRV indices (full render resolution)
    uint32_t depthSrvIndex       = UINT32_MAX;
    uint32_t normalSrvIndex      = UINT32_MAX;
    uint32_t velocitySrvIndex    = UINT32_MAX;

    // GBuffer texture handles for barriers
    RHITextureHandle depthTexture;
    RHITextureHandle normalTexture;
    RHITextureHandle velocityTexture;

    // HDR scene color for prev-frame copy
    RHITextureHandle hdrSceneColor;
    uint32_t hdrSceneColorSrv    = UINT32_MAX;

    // PerFrameCB allocation (for b1 binding)
    RHIBufferHandle perFrameBuffer;
    uint32_t        perFrameOffset = 0;
    const void*     perFrameCpuPtr = nullptr;
    uint32_t        perFrameSize   = 0;

    // Frame state
    uint32_t frameIndex          = 0;
    uint32_t renderWidth         = 0;  ///< Full render resolution width.
    uint32_t renderHeight        = 0;  ///< Full render resolution height.
};

// ─────────────────────────────────────────────────────────────────────────────
// SSGIRenderer
// ─────────────────────────────────────────────────────────────────────────────

/// GPU SSGI subsystem — owns all textures and dispatches 5 compute passes.
///
/// # Ownership
/// Owns all GPU textures. Does NOT own IRHIDevice, PipelineLibrary, or
/// CBRingAllocator — all must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class SSGIRenderer {
public:
    SSGIRenderer() = default;
    ~SSGIRenderer();

    SSGIRenderer(const SSGIRenderer&) = delete;
    SSGIRenderer& operator=(const SSGIRenderer&) = delete;
    SSGIRenderer(SSGIRenderer&&) noexcept = default;
    SSGIRenderer& operator=(SSGIRenderer&&) noexcept = default;

    /// Create all SSGI GPU textures at the given render resolution.
    /// Called from SceneRenderer::Init().
    [[nodiscard]] bool CreateResources(IRHIDevice* device, uint32_t width, uint32_t height);

    /// Destroy all SSGI GPU textures. Called from SceneRenderer::Shutdown().
    void DestroyResources();

    /// Recreate textures at a new resolution. Called from SceneRenderer::Resize().
    void OnResize(uint32_t width, uint32_t height);

    /// Dispatch all 5 SSGI compute passes for the current frame.
    ///
    /// Must be called after GBuffer + GTAO and before deferred lighting.
    /// The prev scene color copy is performed at the END of the previous
    /// frame's HDR rendering (before tonemap).
    void Render(IRHICommandList* cmdList, const SSGIRenderParams& params);

    /// Copy current HDR scene color to prev-frame buffer for next frame's use.
    /// Call this AFTER deferred lighting, BEFORE tonemap/post-process.
    void CopyPrevSceneColor(IRHICommandList* cmdList,
                            RHITextureHandle hdrSceneColor,
                            uint32_t width, uint32_t height);

    /// Returns the SRV index of the final upsampled SSGI indirect diffuse result.
    [[nodiscard]] uint32_t GetResultSrvIndex() const noexcept { return m_ssgiResultSrv; }

    /// Returns the SRV index of the final upsampled SSGI AO result.
    [[nodiscard]] uint32_t GetAOResultSrvIndex() const noexcept { return m_ssgiResultAOSrv; }

    /// Returns true if SSGI is enabled and resources are valid.
    [[nodiscard]] bool IsReady() const noexcept {
        return m_initialized && settings.enabled;
    }

    // ── PassInspector accessors for intermediate textures ────────────────

    /// Returns the previous frame color texture handle (full-res copy).
    [[nodiscard]] RHITextureHandle GetPrevColorTexture() const noexcept { return m_prevSceneColor; }
    /// Returns the SRV index for the previous frame color texture.
    [[nodiscard]] uint32_t GetPrevColorSrvIndex() const noexcept { return m_prevSceneColorSrv; }

    /// Returns the raw trace output texture handle (half-res indirect diffuse).
    [[nodiscard]] RHITextureHandle GetTraceTexture() const noexcept { return m_ssgiTrace; }
    /// Returns the SRV index for the raw trace output.
    [[nodiscard]] uint32_t GetTraceSrvIndex() const noexcept { return m_ssgiTraceSrv; }

    /// Returns the spatially denoised output texture handle (half-res).
    [[nodiscard]] RHITextureHandle GetDenoisedTexture() const noexcept { return m_ssgiDenoised; }
    /// Returns the SRV index for the denoised output.
    [[nodiscard]] uint32_t GetDenoisedSrvIndex() const noexcept { return m_ssgiDenoisedSrv; }

    /// Returns the final upsampled result texture handle (full-res).
    [[nodiscard]] RHITextureHandle GetResultTexture() const noexcept { return m_ssgiResult; }

    /// Returns the final upsampled AO result texture handle (full-res).
    [[nodiscard]] RHITextureHandle GetAOResultTexture() const noexcept { return m_ssgiResultAO; }
    /// Returns the SRV index for the AO result.
    [[nodiscard]] uint32_t GetAOResultSrvForInspector() const noexcept { return m_ssgiResultAOSrv; }

    /// Returns the half-res width used by SSGI.
    [[nodiscard]] uint32_t GetHalfWidth() const noexcept { return m_halfWidth; }
    /// Returns the half-res height used by SSGI.
    [[nodiscard]] uint32_t GetHalfHeight() const noexcept { return m_halfHeight; }

    /// Public settings — modifiable per-frame before Render().
    SSGISettings settings;

private:
    [[nodiscard]] bool CreateTextures(uint32_t width, uint32_t height);
    void DestroyTextures();

    void DispatchPrevColorReduce(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchTrace(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchSpatialDenoise(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchTemporalAccum(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchUpsample(IRHICommandList* cmdList, const SSGIRenderParams& params);

    IRHIDevice* m_device = nullptr;

    // ── Textures ───────────────────────────────────────────────────────────

    /// Full-res copy of previous frame's HDR scene color (for reduction input).
    RHITextureHandle m_prevSceneColor;
    uint32_t m_prevSceneColorSrv = UINT32_MAX;

    /// Half-res reduced prev color (mip 0, Karis weighted).
    RHITextureHandle m_prevColorReduced;
    uint32_t m_prevColorReducedSrv = UINT32_MAX;
    uint32_t m_prevColorReducedUav = UINT32_MAX;

    /// Half-res raw trace output: indirect diffuse (RGBA16F).
    RHITextureHandle m_ssgiTrace;
    uint32_t m_ssgiTraceSrv = UINT32_MAX;
    uint32_t m_ssgiTraceUav = UINT32_MAX;

    /// Half-res raw trace output: AO (R16F).
    RHITextureHandle m_ssgiTraceAO;
    uint32_t m_ssgiTraceAOSrv = UINT32_MAX;
    uint32_t m_ssgiTraceAOUav = UINT32_MAX;

    /// Half-res spatially denoised: indirect diffuse (RGBA16F).
    RHITextureHandle m_ssgiDenoised;
    uint32_t m_ssgiDenoisedSrv = UINT32_MAX;
    uint32_t m_ssgiDenoisedUav = UINT32_MAX;

    /// Half-res spatially denoised: AO (R16F).
    RHITextureHandle m_ssgiDenoisedAO;
    uint32_t m_ssgiDenoisedAOSrv = UINT32_MAX;
    uint32_t m_ssgiDenoisedAOUav = UINT32_MAX;

    /// Half-res temporal history ping-pong: indirect diffuse (RGBA16F) x2.
    RHITextureHandle m_ssgiHistory[2];
    uint32_t m_ssgiHistorySrv[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t m_ssgiHistoryUav[2] = {UINT32_MAX, UINT32_MAX};

    /// Half-res temporal history ping-pong: AO (R16F) x2.
    RHITextureHandle m_ssgiHistoryAO[2];
    uint32_t m_ssgiHistoryAOSrv[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t m_ssgiHistoryAOUav[2] = {UINT32_MAX, UINT32_MAX};

    /// Full-res final upsampled result: indirect diffuse (RGBA16F).
    RHITextureHandle m_ssgiResult;
    uint32_t m_ssgiResultSrv = UINT32_MAX;
    uint32_t m_ssgiResultUav = UINT32_MAX;

    /// Full-res final upsampled result: AO (R16F).
    RHITextureHandle m_ssgiResultAO;
    uint32_t m_ssgiResultAOSrv = UINT32_MAX;
    uint32_t m_ssgiResultAOUav = UINT32_MAX;

    // ── State ──────────────────────────────────────────────────────────────

    int      m_historyIndex = 0;   ///< Ping-pong index (0 or 1).
    uint32_t m_halfWidth    = 0;
    uint32_t m_halfHeight   = 0;
    uint32_t m_fullWidth    = 0;
    uint32_t m_fullHeight   = 0;
    bool     m_initialized  = false;
    bool     m_hasPrevColor = false; ///< False on first frame (no valid prev scene color).

    static constexpr uint32_t kThreadGroupSize = 8;
};

} // namespace uz
