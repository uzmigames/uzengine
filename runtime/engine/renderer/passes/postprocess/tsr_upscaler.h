/// UzEngine TSR (Temporal Super Resolution) Upscaler
///
/// Multi-pass compute-based temporal upscaler. Uses three sequential compute
/// dispatches compiled from `tsr_resolve_cs.hlsl`
/// with different preprocessor defines:
///
///   Pass 1: `TSR_PASS_DILATE_VELOCITY` — 3x3 closest-depth velocity dilation
///   Pass 2: `TSR_PASS_REJECT_SHADING`  — luminance-based shading rejection
///   Pass 3: `TSR_PASS_UPDATE_HISTORY`  — temporal resolve with YCoCg clamping
///
/// Supports screen percentages from 50% (2x upscale) to 200% (supersampling),
/// matching the supported screen percentage range. Intermediate textures
/// (dilated velocity and rejection mask) are owned by this class and
/// recreated on resolution change.
///
/// @ref Yang et al. 2020 "Temporal Super Resolution"
/// @deviations Consolidated from 12+ passes to 3 core passes. See
///             tsr_resolve_cs.hlsl @deviations for the full list.

#pragma once

#include "temporal_upscaler.h"

namespace uz {

class PipelineLibrary;
class CBRingAllocator;

/// Temporal Super Resolution upscaler (multi-pass).
///
/// Owns two intermediate textures between passes and three compute pipelines
/// obtained from PipelineLibrary. The caller manages history texture ping-pong
/// and external resource barriers (scene color, depth, velocity → SRV before
/// Execute; history write → UAV before Execute, SRV after).
///
/// # Lifecycle
///   1. `Init()` — store device/pipeline/CB pointers
///   2. `Execute()` — lazily creates intermediates on first call or resize
///   3. `OnResize()` — destroys and recreates intermediates at new resolution
///   4. `Shutdown()` — releases all GPU resources
///
class TsrUpscaler final : public ITemporalUpscaler {
public:
    TsrUpscaler() = default;
    ~TsrUpscaler() override;

    // --- Queries ---

    /// Returns UpscalerType::TSR.
    [[nodiscard]] UpscalerType GetType() const noexcept override { return UpscalerType::TSR; }
    /// Returns "TSR".
    [[nodiscard]] const char*  GetName() const noexcept override { return "TSR"; }

    /// TSR supports 50% to 200% screen percentage (2x upscale to 2x supersample).
    [[nodiscard]] float GetMinScreenPercentage() const noexcept override { return 50.f; }
    /// Returns 200% — TSR supports up to 2x supersampling.
    [[nodiscard]] float GetMaxScreenPercentage() const noexcept override { return 200.f; }

    // --- Lifecycle ---

    /// Stores pointers to the RHI device, pipeline library, and CB ring.
    /// No GPU resource allocation here — intermediates are created lazily in
    /// Execute() when the render resolution is first known.
    ///
    /// @param device    The RHI device for resource creation.
    /// @param pipelines The pipeline library providing TSR compute pipelines.
    /// @param cbRing    The constant buffer ring allocator for per-frame CB uploads.
    void Init(IRHIDevice* device, PipelineLibrary* pipelines, CBRingAllocator* cbRing) override;

    /// Releases intermediate textures and pipeline references.
    /// Safe to call multiple times (subsequent calls are no-ops).
    void Shutdown() override;

    /// Destroys and recreates intermediate textures at the new display resolution.
    /// Called by SceneRenderer when the window or screen percentage changes.
    ///
    /// @param displayWidth  New display width in pixels.
    /// @param displayHeight New display height in pixels.
    void OnResize(uint32_t displayWidth, uint32_t displayHeight) override;

    // --- Execution ---

    /// Executes the three TSR compute passes for one frame.
    ///
    /// 1. Dilate Velocity — 3x3 closest-depth search, outputs dilated velocity.
    /// 2. Reject Shading  — compares reprojected history luminance to current
    ///    frame, outputs per-pixel rejection weight.
    /// 3. Update History  — blends current frame with clamped history using
    ///    HDR-weighted exponential moving average. Outputs to historyWriteUav.
    ///
    /// Resource barriers between passes are issued internally. The caller is
    /// responsible for external barriers on scene color, depth, velocity,
    /// and history textures.
    ///
    /// @param cmd   Command list to record compute dispatches on.
    /// @param input Per-frame textures, jitter, and resolution data.
    void Execute(IRHICommandList* cmd, const UpscalerInput& input) override;

    // --- Settings ---

    /// Sets RCAS sharpening intensity applied during the final resolve pass.
    /// 0.0 = no sharpening (default), 1.0 = maximum sharpening.
    void SetSharpness(float s) noexcept { m_sharpness = (s < 0.f) ? 0.f : (s > 1.f ? 1.f : s); }

    /// Returns the current RCAS sharpness value [0, 1].
    [[nodiscard]] float GetSharpness() const noexcept { return m_sharpness; }

private:
    /// Creates the two intermediate textures (dilated velocity RG16F,
    /// rejection mask R16F) at the current render resolution.
    void CreateIntermediateResources();

    /// Destroys intermediate textures and resets SRV/UAV indices.
    void DestroyIntermediateResources();

    // --- External references (not owned) ---
    IRHIDevice*      m_device    = nullptr;
    PipelineLibrary* m_pipelines = nullptr;
    CBRingAllocator* m_cbRing    = nullptr;

    // --- Intermediate textures (owned) ---
    RHITextureHandle m_dilatedVelocity;             ///< RG16F at render resolution
    uint32_t         m_dilatedVelocitySrv = UINT32_MAX;
    uint32_t         m_dilatedVelocityUav = UINT32_MAX;

    RHITextureHandle m_rejectionMask;               ///< R16F at render resolution
    uint32_t         m_rejectionMaskSrv   = UINT32_MAX;
    uint32_t         m_rejectionMaskUav   = UINT32_MAX;

    // --- Settings ---
    float    m_sharpness    = 0.f;   ///< RCAS sharpness [0, 1], 0 = disabled
    uint32_t m_renderWidth  = 0;     ///< Current render resolution width
    uint32_t m_renderHeight = 0;     ///< Current render resolution height
    bool     m_firstDispatch = true; ///< True until first Execute() — controls initial barrier states
};

} // namespace uz
