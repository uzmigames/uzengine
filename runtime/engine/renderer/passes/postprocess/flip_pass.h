/// UzEngine FLIP Perceptual Quality Pass
///
/// @ref Andersson et al. 2020 "FLIP: A Difference Evaluator for Alternating Images"
///
/// Computes the FLIP perceptual difference between a reference and test image.
/// Returns a heatmap texture (magma colormap) and mean/max FLIP scores.
///
/// # Usage
/// ```cpp
/// FlipPass flip;
/// flip.Init(device, shaderMgr);
/// auto result = flip.Evaluate(cmdList, refSrv, testSrv, width, height);
/// if (result.meanFlip > 0.05f) { /* regression detected */ }
/// flip.Shutdown();
/// ```

#pragma once

#include "rhi_types.h"
#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;

/// Result of a FLIP evaluation.
struct FlipResult {
    RHITextureHandle heatmap;  ///< RGBA16F heatmap (RGB=magma color, A=FLIP score).
    float meanFlip = 0.0f;    ///< Mean FLIP score across all pixels.
    float maxFlip  = 0.0f;    ///< Maximum FLIP score.
    bool  pass     = true;    ///< True if meanFlip < threshold.
};

/// GPU compute pass for FLIP perceptual image comparison.
class FlipPass {
public:
    static constexpr uint32_t kThreadGroupSize = 8;

    FlipPass() = default;
    ~FlipPass();

    FlipPass(const FlipPass&) = delete;
    FlipPass& operator=(const FlipPass&) = delete;

    /// Initialize with device and shader manager.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr);

    /// Evaluate FLIP between two images.
    /// @param refSrv   SRV index of reference image (sRGB).
    /// @param testSrv  SRV index of test image (sRGB).
    /// @param width    Image width in pixels.
    /// @param height   Image height in pixels.
    /// @param threshold FLIP threshold for pass/fail (default 0.05).
    /// @return FlipResult with heatmap texture and scores.
    [[nodiscard]] FlipResult Evaluate(IRHICommandList* cmdList,
                                       uint32_t refSrv, uint32_t testSrv,
                                       uint32_t width, uint32_t height,
                                       float threshold = 0.05f);

    /// Release all GPU resources.
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    [[nodiscard]] bool EnsurePipeline();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    RHIShaderHandle   m_shaderCS   = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_pipeline   = RHIPipelineHandle::Invalid();

    // Reusable heatmap + stats buffer (recreated on size change)
    RHITextureHandle m_heatmap    = RHITextureHandle::Invalid();
    uint32_t         m_heatmapUav = UINT32_MAX;
    uint32_t         m_heatmapSrv = UINT32_MAX;
    RHIBufferHandle  m_statsBuffer;
    uint32_t         m_statsUav   = UINT32_MAX;
    uint32_t         m_lastWidth  = 0;
    uint32_t         m_lastHeight = 0;

    bool m_initialized = false;
};

} // namespace uz
