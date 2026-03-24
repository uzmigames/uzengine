/// UzEngine FSR 2.x Temporal Upscaler
///
/// Integrates AMD's FidelityFX Super Resolution 2 as an ITemporalUpscaler
/// implementation. FSR2 is a temporal upscaling algorithm that uses motion
/// vectors, depth, and color history to reconstruct a high-quality output
/// image at display resolution from a lower-resolution render target.
///
/// FSR2 supports four quality presets controlling the upscale ratio:
///   - Quality:          1.5x per-dimension (render at 67% of display)
///   - Balanced:         1.7x per-dimension (render at 59% of display)
///   - Performance:      2.0x per-dimension (render at 50% of display)
///   - Ultra Performance: 3.0x per-dimension (render at 33% of display)
///
/// The implementation uses the FSR2 C API directly (ffx_fsr2.h) with the
/// DX12 backend (ffx_dx12.h) to create the FfxInterface and wrap native
/// DX12 resources into FfxApiResource descriptors for the dispatch call.
///
/// @ref AMD 2022 "FidelityFX Super Resolution 2" — temporal upscaling with
///      Lanczos resampling, luminance lock, depth clipping, and RCAS sharpening.

#pragma once

#include "temporal_upscaler.h"

#include <cstdint>
#include <memory>

namespace uz {

/// FSR 2.x temporal upscaler implementing ITemporalUpscaler.
///
/// Uses AMD's FidelityFX SDK with the DX12 backend and precompiled shader
/// blobs. Internally manages the FfxFsr2Context and its scratch memory.
///
/// # Quality Modes
/// Quality preset  | Scale factor | Min screen %
/// --------------- | ------------ | ------------
/// Quality         | 1.5x         | 67%
/// Balanced        | 1.7x         | 59%
/// Performance     | 2.0x         | 50%
/// Ultra Perf.     | 3.0x         | 33%
///
/// # Lifecycle
///   1. `CreateTemporalUpscaler(UpscalerType::FSR2)` via factory
///   2. `Init(device, pipelines, cbRing)` — creates FFX backend + FSR2 context
///   3. Per frame: `Execute(cmd, input)` — dispatches FSR2 compute passes
///   4. On resize: `OnResize(w, h)` — destroys + recreates context
///   5. `Shutdown()` — destroys FFX context and frees scratch memory
class FSR2Upscaler final : public ITemporalUpscaler {
public:
    FSR2Upscaler();
    ~FSR2Upscaler() override;

    // --- Queries ---

    /// Returns `UpscalerType::FSR2`.
    [[nodiscard]] UpscalerType GetType() const noexcept override;

    /// Returns `"FSR2"`.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// FSR2 supports rendering at 33% of display resolution (Ultra Performance 3x).
    [[nodiscard]] float GetMinScreenPercentage() const noexcept override;

    /// FSR2 supports native AA mode at 100% (NativeAA quality preset).
    [[nodiscard]] float GetMaxScreenPercentage() const noexcept override;

    // --- Lifecycle ---

    /// Creates the FFX DX12 backend interface and FSR2 context.
    ///
    /// Extracts the native ID3D12Device from the RHI device to initialize
    /// the FfxInterface. Allocates scratch memory for the backend. The FSR2
    /// context is created lazily on the first Execute() call when the display
    /// resolution is known.
    ///
    /// @param device    The RHI device (must be DX12 backend).
    /// @param pipelines Not used by FSR2 (it has its own internal pipelines).
    /// @param cbRing    Not used by FSR2 (it manages its own constant buffers).
    void Init(IRHIDevice* device, PipelineLibrary* pipelines, CBRingAllocator* cbRing) override;

    /// Destroys the FSR2 context and frees all FFX resources.
    /// Safe to call multiple times (subsequent calls are no-ops).
    void Shutdown() override;

    /// Destroys and recreates the FSR2 context at the new display resolution.
    ///
    /// @param displayWidth  New display width in pixels.
    /// @param displayHeight New display height in pixels.
    void OnResize(uint32_t displayWidth, uint32_t displayHeight) override;

    // --- Execution ---

    /// Dispatches the FSR2 upscaling passes for one frame.
    ///
    /// Wraps UzEngine's RHI texture handles into FfxApiResource structs via
    /// the DX12 backend helper, then calls ffxFsr2ContextDispatch. FSR2
    /// internally records its compute passes onto the provided command list.
    ///
    /// The caller is responsible for resource state transitions on the
    /// history textures. Scene color, depth, and velocity must be in
    /// SRV-compatible state before this call. The output (historyWriteTexture)
    /// must be in UAV-compatible state.
    ///
    /// @param cmd   Command list to record FSR2 dispatches on.
    /// @param input Per-frame textures, jitter, and resolution data.
    void Execute(IRHICommandList* cmd, const UpscalerInput& input) override;

    // --- Non-copyable, movable ---

    FSR2Upscaler(const FSR2Upscaler&) = delete;
    FSR2Upscaler& operator=(const FSR2Upscaler&) = delete;
    FSR2Upscaler(FSR2Upscaler&&) noexcept;
    FSR2Upscaler& operator=(FSR2Upscaler&&) noexcept;

private:
    /// PIMPL — hides FFX SDK types from consumers.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace uz
