/// UzEngine Legacy TAA Upscaler
///
/// Single-pass compute shader temporal anti-aliasing: reprojects history via
/// motion vectors, applies YCoCg neighborhood clamping, and blends with an
/// HDR-weighted exponential moving average. This is the lightweight fallback
/// upscaler — no velocity dilation, no shading rejection, no spatial filter.
///
/// Screen percentage is fixed at 100% (no upscaling). For sub-native
/// rendering with temporal upscale, use TsrUpscaler instead.
///

#pragma once

#include "temporal_upscaler.h"

namespace uz {

class PipelineLibrary;
class CBRingAllocator;

/// Legacy temporal anti-aliasing upscaler.
///
/// Wraps the existing `taa_resolve_cs.hlsl` compute shader behind the
/// `ITemporalUpscaler` interface. Dispatches 8x8 thread groups at display
/// resolution. Constant buffer layout matches `cbuffer TaaCB` at slot b2.
///
/// Init/Shutdown/OnResize are trivial because TAA owns no intermediate
/// resources — the history texture ping-pong is managed by SceneRenderer
/// and the compute pipeline is owned by PipelineLibrary.
///
class TaaUpscaler final : public ITemporalUpscaler {
public:
    TaaUpscaler() = default;
    ~TaaUpscaler() override;

    // --- Queries ---

    /// Returns UpscalerType::TAA.
    [[nodiscard]] UpscalerType GetType() const noexcept override { return UpscalerType::TAA; }
    /// Returns "TAA".
    [[nodiscard]] const char*  GetName() const noexcept override { return "TAA"; }
    [[nodiscard]] bool IsAvailable() const noexcept override { return true; }

    /// TAA operates at native resolution only — no upscaling support.
    [[nodiscard]] float GetMinScreenPercentage() const noexcept override { return 100.f; }
    /// TAA operates at native resolution only — always 100%.
    [[nodiscard]] float GetMaxScreenPercentage() const noexcept override { return 100.f; }

    // --- Lifecycle ---

    /// Stores pointers to the pipeline library and CB ring allocator.
    /// Validates the shared resolve pipeline before accepting the backend.
    [[nodiscard]] bool Init(IRHIDevice* device, PipelineLibrary* pipelines, CBRingAllocator* cbRing) override;

    /// No-op: TAA owns no GPU resources to release.
    void Shutdown() override;

    /// No-op: TAA has no resolution-dependent intermediate buffers.
    /// History textures are managed externally by SceneRenderer.
    void OnResize(uint32_t displayWidth, uint32_t displayHeight) override;

    // --- Execution ---

    /// Dispatches the TAA resolve compute shader for one frame.
    ///
    /// Fills the `TaaCB` constant buffer with jitter, texel size, blend
    /// weight, and bindless descriptor indices, then dispatches 8x8 groups
    /// covering the full display resolution.
    ///
    /// @param cmd   Command list to record the compute dispatch on.
    /// @param input Per-frame textures, jitter, and resolution data.
    bool Execute(IRHICommandList* cmd, const UpscalerInput& input) override;

private:
    IRHIDevice*      m_device    = nullptr;
    PipelineLibrary* m_pipelines = nullptr;
    CBRingAllocator* m_cbRing    = nullptr;
};

} // namespace uz
