/// UzEngine DLSS Temporal Upscaler
///
/// Integrates NVIDIA's Deep Learning Super Sampling (DLSS) as an
/// ITemporalUpscaler implementation. DLSS uses a deep neural network
/// trained on thousands of games to produce high-quality temporal
/// upscaling from a lower-resolution render target to display resolution.
///
/// DLSS supports five quality presets controlling the upscale ratio:
///   - DLAA:              1.0x (native AA, no upscaling)
///   - Quality:           1.5x per-dimension (render at 67% of display)
///   - Balanced:          1.7x per-dimension (render at 59% of display)
///   - Performance:       2.0x per-dimension (render at 50% of display)
///   - Ultra Performance: 3.0x per-dimension (render at 33% of display)
///
/// The implementation uses the NVIDIA NGX SDK (nvsdk_ngx.h) with the
/// DX12 backend to create and evaluate the DLSS feature. On non-NVIDIA
/// hardware or when the SDK is unavailable, selection chooses a fallback.
///
/// @ref NVIDIA 2020 "Deep Learning Super Sampling" — AI temporal upscaling
///      with motion vectors, depth, and exposure inputs.

#pragma once

#include "temporal_upscaler.h"

#include <cstdint>
#include <memory>

namespace uz {

/// DLSS quality mode controlling render resolution vs display resolution ratio.
///
/// @ref NVIDIA DLSS SDK — quality presets table.
enum class DlssQuality : uint32_t {
    UltraPerformance = 0, ///< 3.0x upscale (render at 33% of display)
    Performance      = 1, ///< 2.0x upscale (render at 50% of display)
    Balanced         = 2, ///< 1.7x upscale (render at 59% of display)
    Quality          = 3, ///< 1.5x upscale (render at 67% of display)
    DLAA             = 4, ///< 1.0x native AA (no upscaling, best quality)
};

/// NVIDIA DLSS temporal upscaler implementing ITemporalUpscaler.
///
/// Uses the NVIDIA NGX SDK with the DX12 backend and the nvngx_dlss.dll
/// runtime. Internally manages the NGX context and DLSS feature handle.
///
/// # Quality Modes
/// Quality preset   | Scale factor | Min screen %
/// ---------------- | ------------ | ------------
/// DLAA             | 1.0x         | 100%
/// Quality          | 1.5x         | 67%
/// Balanced         | 1.7x         | 59%
/// Performance      | 2.0x         | 50%
/// Ultra Perf.      | 3.0x         | 33%
///
/// # Lifecycle
///   1. `CreateTemporalUpscaler(UpscalerType::DLSS)` via factory
///   2. `Init(device, pipelines, cbRing)` — initializes NGX + creates DLSS feature
///   3. Per frame: `Execute(cmd, input)` — evaluates DLSS with current frame data
///   4. On resize: `OnResize(w, h)` — destroys + recreates DLSS feature
///   5. `Shutdown()` — destroys NGX context and releases all resources
///
/// # Graceful Fallback
/// If DLSS is not supported (non-NVIDIA GPU, old driver, missing DLL), Init()
/// returns false. The caller selects a supported fallback.
class DlssUpscaler final : public ITemporalUpscaler {
public:
    DlssUpscaler();
    ~DlssUpscaler() override;

    // --- Queries ---

    /// Returns `UpscalerType::DLSS`.
    [[nodiscard]] UpscalerType GetType() const noexcept override;

    /// Returns `"DLSS"`.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// DLSS supports rendering at 33% of display resolution (Ultra Performance 3x).
    [[nodiscard]] float GetMinScreenPercentage() const noexcept override;

    /// DLSS supports native AA mode at 100% (DLAA preset).
    [[nodiscard]] float GetMaxScreenPercentage() const noexcept override;

    /// Returns build capability before Init; device support is checked by Init.
    [[nodiscard]] bool IsAvailable() const noexcept override;

    // --- Lifecycle ---

    /// Initializes the NGX SDK and queries DLSS capability.
    ///
    /// Calls NVSDK_NGX_D3D12_Init with the application directory to locate
    /// nvngx_dlss.dll. Queries optimal render resolution for the current
    /// quality preset. If DLSS is not supported, logs a warning and returns false
    /// in the Shutdown state. The feature is created lazily on the first frame.
    ///
    /// @param device    The RHI device (must be DX12 backend).
    /// @param pipelines Not used by DLSS (it has its own internal pipelines).
    /// @param cbRing    Not used by DLSS (it manages its own constant buffers).
    [[nodiscard]] bool Init(IRHIDevice* device, PipelineLibrary* pipelines, CBRingAllocator* cbRing) override;

    /// Destroys the DLSS feature and shuts down the NGX SDK.
    /// Safe to call multiple times (subsequent calls are no-ops).
    void Shutdown() override;

    /// Destroys and recreates the DLSS feature at the new display resolution.
    ///
    /// @param displayWidth  New display width in pixels.
    /// @param displayHeight New display height in pixels.
    void OnResize(uint32_t displayWidth, uint32_t displayHeight) override;

    // --- Execution ---

    /// Evaluates DLSS for one frame.
    ///
    /// Wraps UzEngine's RHI texture handles into NGX resource structs via
    /// the DX12 backend, then calls NVSDK_NGX_D3D12_EvaluateFeature. DLSS
    /// internally records its compute passes onto the provided command list.
    ///
    /// @param cmd   Command list to record DLSS dispatches on.
    /// @param input Per-frame textures, jitter, and resolution data.
    bool Execute(IRHICommandList* cmd, const UpscalerInput& input) override;

    // --- Non-copyable, movable ---

    DlssUpscaler(const DlssUpscaler&) = delete;
    DlssUpscaler& operator=(const DlssUpscaler&) = delete;
    DlssUpscaler(DlssUpscaler&&) noexcept;
    DlssUpscaler& operator=(DlssUpscaler&&) noexcept;

private:
    /// PIMPL — hides NGX SDK types from consumers.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace uz
