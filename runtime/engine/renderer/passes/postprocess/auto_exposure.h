/// UzEngine Auto-Exposure — GPU histogram-based eye adaptation.
///
/// Computes scene luminance distribution via a 64-bin histogram compute shader,
/// applies percentile filtering, temporal adaptation, and outputs an exposure
/// multiplier that is applied before tonemapping.
///
/// # Architecture
/// Two compute passes per frame:
/// 1. **Histogram** (exposure_histogram_cs.hlsl): Builds a 64-bin log2 luminance
///    histogram from the HDR backbuffer. Uses 16x16 thread groups with LDS
///    accumulation to minimize global atomic contention.
/// 2. **Average** (exposure_average_cs.hlsl): Reads the histogram, applies
///    percentile filtering (ignoring dark/bright tails), computes weighted
///    average luminance, and applies exponential temporal adaptation with
///    asymmetric up/down speeds. Outputs the final exposure multiplier.
///
/// # GPU Resources
/// - Histogram buffer: 64 x uint32 (256 bytes), UAV+SRV
/// - Exposure buffer: 2 x float (8 bytes), UAV, persistent across frames
///
/// # Root Signature Usage
/// Uses the global root signature (same as IBL baker):
/// - Slot 0: 8 root constants at b0
/// - Slot 4: UAV descriptor table at u0..u3
/// Histogram SRV for the average pass is accessed bindlessly via
/// ResourceDescriptorHeap[].
///
/// # Usage
/// ```cpp
/// AutoExposure autoExp;
/// autoExp.Init(device, &shaderMgr);
/// // Each frame:
/// float exposure = autoExp.Execute(cmdList, hdrSrvIndex, width, height, dt);
/// perFrame.exposure = exposure;
/// ```

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;

/// Auto-exposure parameters (Eye Adaptation defaults).
///
/// Dynamic per-frame parameters are passed via root constants (13 DWORDs of the 16 the
/// signature provides). Adaptation speeds, percentile bounds and the mid-grey target were
/// hardcoded in the average shader until 2026-10-05 while CVars for them were registered and
/// never read (block A.8); they are root constants now.
struct AutoExposureSettings {
    float exposureBias   = 0.0f;     ///< EV offset. 0=neutral, +1=2x brighter.
    float minBrightness  = 0.03f;   ///< Min avg luminance (histogram method default).
    float maxBrightness  = 8.0f;    ///< Max avg luminance (histogram method default).
    float histLogMin     = -10.0f;  ///< Log2 luminance range min (-10).
    float histLogMax     = 20.0f;   ///< Log2 luminance range max (+20).
    float manualExposure = 0.0f;   ///< When > 0, skip auto and use this fixed EV.
    float midGray        = 0.18f;  ///< Target average luminance (Reinhard key).
    float speedUp        = 3.0f;   ///< Bright adaptation speed (EV/s).
    float speedDown      = 1.0f;   ///< Dark adaptation speed (EV/s).
    float lowPercent     = 10.0f;  ///< Histogram percentile that starts the average.
    float highPercent    = 90.0f;  ///< Histogram percentile that ends the average.
    bool  enabled        = true;   ///< Master toggle.
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants — CPU-side structs matching HLSL cbuffer layouts at b0
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the histogram compute shader (5 DWORDs).
///
/// Matches `cbuffer ExposureParams : register(b0)` in
/// exposure_histogram_cs.hlsl. Uses 5 of the 8 available root constant
/// slots; the remaining 3 are unused by the shader.
struct HistogramRootConstants {
    float    histLogMin;     ///< Log2 luminance range minimum.
    float    histLogMax;     ///< Log2 luminance range maximum.
    uint32_t screenWidth;    ///< HDR backbuffer width in pixels.
    uint32_t screenHeight;   ///< HDR backbuffer height in pixels.
    uint32_t hdrSrvIndex;    ///< Bindless SRV index for HDR scene color.
};

static_assert(sizeof(HistogramRootConstants) == 20,
    "HistogramRootConstants must be exactly 5 DWORDs (20 bytes)");

/// Root constants for the average/adaptation compute shader (8 DWORDs).
///
/// Matches `cbuffer ExposureParams : register(b0)` in
/// exposure_average_cs.hlsl. Uses all 8 available root constant slots.
/// Adaptation speeds and percentile bounds are hardcoded in the shader.
struct AverageRootConstants {
    float    histLogMin;       ///< Log2 luminance range minimum.
    float    histLogMax;       ///< Log2 luminance range maximum.
    uint32_t histogramSrvIdx;  ///< Bindless SRV index for the histogram buffer.
    uint32_t padding;          ///< Unused — maintains alignment.
    float    exposureBias;     ///< Artistic EV offset (1.0 = neutral).
    float    minBrightness;    ///< Lower luminance clamp (cd/m2).
    float    maxBrightness;    ///< Upper luminance clamp (cd/m2).
    float    deltaTime;        ///< Frame delta time in seconds.
    float    midGray;          ///< Reinhard key (r.AutoExposure.MidGrayTarget).
    float    speedUp;          ///< EV/s towards brighter (r.AutoExposure.SpeedUp).
    float    speedDown;        ///< EV/s towards darker (r.AutoExposure.SpeedDown).
    float    lowPercent;       ///< Average window start, percent of pixels.
    float    highPercent;      ///< Average window end, percent of pixels.
};

static_assert(sizeof(AverageRootConstants) == 52,
    "AverageRootConstants must be 13 DWORDs (root constant budget 16)");

/// GPU auto-exposure system.
///
/// Owns the histogram buffer, exposure buffer, and compute pipelines.
/// Call `Execute()` each frame to compute the exposure multiplier.
///
/// # Ownership
/// Owns all GPU buffers and pipelines it creates. Does NOT own the
/// `IRHIDevice` or `ShaderManager` — both must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class AutoExposure {
public:
    AutoExposure() = default;
    ~AutoExposure();

    AutoExposure(const AutoExposure&) = delete;
    AutoExposure& operator=(const AutoExposure&) = delete;
    AutoExposure(AutoExposure&&) = delete;
    AutoExposure& operator=(AutoExposure&&) = delete;

    /// Initialize GPU resources and compile compute shaders.
    ///
    /// Creates the histogram buffer (64 x uint32, UAV+SRV), exposure buffer
    /// (2 x float, UAV), and compiles both compute pipelines via ShaderManager.
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this object.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this object.
    ///
    /// # Returns
    /// `true` on success, `false` if resource creation or shader compilation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr);

    /// Execute the auto-exposure pipeline for this frame.
    ///
    /// Dispatches histogram + average compute shaders on the HDR backbuffer.
    /// Returns the exposure multiplier to apply before tonemapping.
    ///
    /// When auto-exposure is disabled (`settings.enabled == false`), returns
    /// `settings.exposureBias` without dispatching any GPU work.
    ///
    /// When manual exposure is set (`settings.manualExposure > 0`), computes
    /// the exposure from the fixed EV value without GPU dispatch.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state (between Begin/End).
    /// * `hdrSrvIndex` — SRV descriptor index for the HDR scene color texture.
    /// * `width`, `height` — HDR backbuffer dimensions in pixels.
    /// * `dt` — Frame delta time in seconds (for temporal adaptation).
    ///
    /// # Returns
    /// Exposure multiplier (typically 0.01 to 10.0). 1.0 = no change.
    float Execute(IRHICommandList* cmdList, uint32_t hdrSrvIndex,
                  uint32_t width, uint32_t height, float dt);

    /// Shutdown and release all GPU resources.
    ///
    /// Destroys buffers and pipelines. Safe to call multiple times or
    /// without prior `Init()`. Called automatically by the destructor.
    void Shutdown();

    /// Clears the adaptation history (EV + exposure) on the GPU so the next
    /// pass seeds itself from the current frame. Call once after a scene has
    /// fully streamed in and one full-HDR frame has been rendered.
    void ResetHistory(IRHICommandList* cmdList);

    /// Returns true if `Init()` has been called and `Shutdown()` has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Returns the exposure value from the most recent `Execute()` call.
    [[nodiscard]] float GetLastExposure() const noexcept { return m_lastExposure; }

    /// Settings (can be modified per-frame before calling Execute).
    AutoExposureSettings settings;

private:
    /// Ensures the histogram compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureHistogramPipeline();

    /// Ensures the average compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureAveragePipeline();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── GPU Buffers ──────────────────────────────────────────────────────
    RHIBufferHandle m_histogramBuffer = RHIBufferHandle::Invalid(); ///< 64 x uint32 histogram (UAV+SRV, Default heap).
    RHIBufferHandle m_exposureBuffer  = RHIBufferHandle::Invalid(); ///< 2 x float: [0]=currentEV, [4]=exposure (UAV, Default heap).
    RHIBufferHandle m_zeroBuffer      = RHIBufferHandle::Invalid(); ///< 256 bytes of zeros (Upload heap, copy source for histogram clear).
    RHIBufferHandle m_readbackBuffer  = RHIBufferHandle::Invalid(); ///< 8 bytes (Readback heap, CPU reads exposure from here).

    // ── Compute Pipelines (lazily created) ───────────────────────────────
    RHIPipelineHandle m_histogramPipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_averagePipeline   = RHIPipelineHandle::Invalid();

    // ── Shader Handles ───────────────────────────────────────────────────
    RHIShaderHandle m_histogramCS = RHIShaderHandle::Invalid();
    RHIShaderHandle m_averageCS   = RHIShaderHandle::Invalid();

    // ── Descriptor Indices ───────────────────────────────────────────────
    uint32_t m_histogramUavIndex = UINT32_MAX; ///< UAV for histogram buffer (u0 in histogram pass).
    uint32_t m_histogramSrvIndex = UINT32_MAX; ///< SRV for histogram buffer (t0 in average pass).
    uint32_t m_exposureUavIndex  = UINT32_MAX; ///< UAV for exposure buffer (u0 in average pass).

    float m_lastExposure  = 1.0f;  ///< Cached exposure from last frame.
    bool  m_initialized   = false;
    bool  m_pipelineFailed = false; ///< Latched on pipeline creation failure to avoid per-frame retry spam.

    // ── Constants ────────────────────────────────────────────────────────
    static constexpr uint32_t kNumBins          = 64;
    static constexpr uint32_t kHistogramBytes   = kNumBins * sizeof(uint32_t); ///< 256 bytes.
    static constexpr uint32_t kExposureBytes    = 2 * sizeof(float);           ///< 8 bytes.
    static constexpr uint32_t kHistGroupSize    = 16;                          ///< [16, 16, 1] thread group.
    static constexpr float    kExposureNorm     = 1.2f;                        ///< ISO 12232 normalization.
};

} // namespace uz
