/// UzEngine SVGF Denoiser Pass — Spatiotemporal Variance-Guided Filtering
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
///
/// 5-pass denoiser pipeline for noisy indirect lighting (SSGI, RT shadows, etc.):
/// 1. Temporal reprojection + moment accumulation
/// 2. Spatial moments filter (3x3 bilateral on variance)
/// 3-7. À-trous wavelet filter (5 iterations, step sizes 1/2/4/8/16)
///
/// Input: noisy half-res or full-res color texture + GBuffer (depth, normals, velocity)
/// Output: denoised color texture
///
/// # Integration
/// Wire as optional denoiser for SSGI output via SSGIRenderer.
/// Can also be used for denoising ray-traced shadows or reflections.

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class PipelineLibrary;

/// Per-frame parameters for SVGF dispatch.
struct SVGFRenderParams {
    CBRingAllocator* cbRing      = nullptr;
    PipelineLibrary* pipelines   = nullptr;

    // Input: noisy color texture
    uint32_t noisyColorSrv       = UINT32_MAX;

    // GBuffer SRV indices
    uint32_t depthSrvIndex       = UINT32_MAX;
    uint32_t normalSrvIndex      = UINT32_MAX;
    uint32_t velocitySrvIndex    = UINT32_MAX;

    // GBuffer texture handles for barriers
    RHITextureHandle depthTexture;
    RHITextureHandle normalTexture;
    RHITextureHandle velocityTexture;

    // PerFrameCB for b1 binding
    RHIBufferHandle perFrameBuffer;
    uint32_t        perFrameOffset = 0;
    const void*     perFrameCpuPtr = nullptr;
    uint32_t        perFrameSize   = 0;

    // Frame state
    uint32_t frameIndex          = 0;
    uint32_t width               = 0;
    uint32_t height              = 0;
};

/// SVGF denoiser — owns history buffers and dispatches the 5-pass pipeline.
class SVGFPass {
public:
    SVGFPass() = default;
    ~SVGFPass();

    SVGFPass(const SVGFPass&) = delete;
    SVGFPass& operator=(const SVGFPass&) = delete;

    /// Create all SVGF GPU textures at the given resolution.
    [[nodiscard]] bool CreateResources(IRHIDevice* device, uint32_t width, uint32_t height);

    /// Destroy all GPU resources.
    void DestroyResources();

    /// Recreate textures at a new resolution.
    void OnResize(uint32_t width, uint32_t height);

    /// Dispatch the full SVGF pipeline (reproject → filter moments → 5× à-trous).
    /// Returns the SRV index of the final denoised color texture.
    [[nodiscard]] uint32_t Denoise(IRHICommandList* cmdList, const SVGFRenderParams& params);

    /// Returns the SRV index of the last denoised result.
    [[nodiscard]] uint32_t GetResultSrvIndex() const noexcept { return m_resultSrv; }

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    IRHIDevice* m_device = nullptr;

    // ── Ping-pong color buffers for à-trous iterations ──
    RHITextureHandle m_colorA;          ///< Color buffer A (input/output alternation)
    uint32_t         m_colorASrv = UINT32_MAX;
    uint32_t         m_colorAUav = UINT32_MAX;

    RHITextureHandle m_colorB;          ///< Color buffer B
    uint32_t         m_colorBSrv = UINT32_MAX;
    uint32_t         m_colorBUav = UINT32_MAX;

    // ── Moments textures (ping-pong) ──
    RHITextureHandle m_momentsA;        ///< Moments buffer A (μ, σ²)
    uint32_t         m_momentsASrv = UINT32_MAX;
    uint32_t         m_momentsAUav = UINT32_MAX;

    RHITextureHandle m_momentsB;
    uint32_t         m_momentsBSrv = UINT32_MAX;
    uint32_t         m_momentsBUav = UINT32_MAX;

    // ── History for temporal reprojection ──
    RHITextureHandle m_historyColor;    ///< Previous frame denoised color
    uint32_t         m_historyColorSrv = UINT32_MAX;

    RHITextureHandle m_historyMoments;  ///< Previous frame moments
    uint32_t         m_historyMomentsSrv = UINT32_MAX;

    // ── State ──
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    uint32_t m_resultSrv = UINT32_MAX;
    int      m_historyIdx = 0;
    bool     m_initialized = false;
    bool     m_hasHistory  = false;

    static constexpr uint32_t kThreadGroupSize = 8;
    static constexpr uint32_t kAtrousIterations = 5;
};

} // namespace uz
