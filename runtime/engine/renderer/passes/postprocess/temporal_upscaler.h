/// UzEngine Temporal Upscaler Interface
///
/// Abstracts temporal upscaling algorithms (TAA, TSR, FSR2, DLSS, XeSS)
/// behind a common interface. Each upscaler receives the current frame's
/// scene color, depth, velocity, and history textures, then writes the
/// upscaled result into the history write texture.
///
/// The existing TAA resolve in frame_postprocess.cpp is the first concrete
/// implementation. TSR (multi-pass, production-quality) is the primary target.
/// Vendor-specific upscalers (FSR2, DLSS, XeSS) will be added as plugins.
///
/// @deviations The reference uses FRDGBuilder for resource management; we pass a raw
///             IRHICommandList and bindless SRV/UAV indices. The reference FInputs
///             bundles SceneTextures with dozens of fields; we use a minimal
///             UpscalerInput struct with only the textures temporal upscaling
///             needs. History textures are managed externally and passed in
///             via the UpscalerInput struct.

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <memory>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class PipelineLibrary;
class CBRingAllocator;

// ─────────────────────────────────────────────────────────────────────────────
// Upscaler Type
// ─────────────────────────────────────────────────────────────────────────────

/// Identifies the temporal upscaling algorithm in use.
///
/// `None` disables temporal upscaling entirely (scene color passes through).
/// `TAA` is the lightweight single-pass fallback. `TSR` is the multi-pass
/// algorithm targeting production-quality. Vendor upscalers are reserved for future
/// plugin integration.
enum class UpscalerType : uint32_t {
    None = 0,  ///< No temporal upscaling (raw scene color passed through)
    TAA  = 1,  ///< Legacy temporal anti-aliasing (single-pass, lightweight)
    TSR  = 2,  ///< Temporal Super Resolution (multi-pass, production-quality)
    FSR2 = 3,  ///< AMD FidelityFX Super Resolution 2 (future plugin)
    DLSS = 4,  ///< NVIDIA Deep Learning Super Sampling (future plugin)
    XeSS = 5,  ///< Intel Xe Super Sampling (future plugin)
};

// ─────────────────────────────────────────────────────────────────────────────
// Upscaler Input
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame input data for the temporal upscaler.
///
/// The caller fills this struct before calling `ITemporalUpscaler::Execute()`.
/// SRV/UAV indices are bindless heap indices. Physical texture handles are
/// provided separately so the caller can issue resource barriers before and
/// after the upscaler dispatch.
struct UpscalerInput {
    // --- Bindless descriptor indices ---

    uint32_t sceneColorSrv   = UINT32_MAX; ///< HDR scene color at render resolution (RGBA16F)
    uint32_t depthSrv        = UINT32_MAX; ///< Depth buffer at render resolution (R32F, reversed-Z)
    uint32_t velocitySrv     = UINT32_MAX; ///< Velocity buffer at render resolution (RG16F, pixels)
    uint32_t historyReadSrv  = UINT32_MAX; ///< Previous frame's history texture SRV
    uint32_t historyWriteUav = UINT32_MAX; ///< Current frame's history texture UAV (output)

    // --- Physical texture handles (for barriers and vendor upscalers) ---

    RHITextureHandle sceneColorTexture;    ///< HDR scene color texture (for vendor upscalers needing native resource)
    RHITextureHandle depthTexture;         ///< Depth buffer texture (for vendor upscalers needing native resource)
    RHITextureHandle velocityTexture;      ///< Velocity buffer texture (for vendor upscalers needing native resource)
    RHITextureHandle historyReadTexture;   ///< Previous frame's history texture
    RHITextureHandle historyWriteTexture;  ///< Current frame's history texture (write target)

    // --- Sub-pixel jitter ---

    float jitterX  = 0.f;     ///< Sub-pixel jitter X in pixels [-0.5, 0.5]
    float jitterY  = 0.f;     ///< Sub-pixel jitter Y in pixels [-0.5, 0.5]

    // --- Resolution ---

    float renderWidth  = 0.f;  ///< Scene color width (render resolution)
    float renderHeight = 0.f;  ///< Scene color height (render resolution)
    float outputWidth  = 0.f;  ///< Output/history width (display resolution)
    float outputHeight = 0.f;  ///< Output/history height (display resolution)

    // --- Frame state ---

    bool  firstFrame = true;   ///< True on first frame (no valid history available)
    float exposure   = 1.0f;   ///< Auto-exposure value (1.0 if disabled)

    // Diagnostic clamp-box storage (r.TAA.ClampFreezeAfterFrame, ledger H-25).
    uint32_t clampBoxSrv[2] = {UINT32_MAX, UINT32_MAX};  ///< Stored clip box, min and max.
    uint32_t clampBoxUav[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t frameIndex     = 0;    ///< Absolute frame index, for the freeze threshold.
    uint32_t clampFreezeEnabled = 0; ///< 1 only while the diagnostic is on; otherwise the shader must not touch the storage.

    // Region dump (diagnostics) — TAA resolve writes per-pixel intermediates for this rectangle.
    uint32_t debugRegionUav = UINT32_MAX;   ///< Dump buffer UAV; UINT32_MAX = off.
    uint32_t debugRegionSrv = UINT32_MAX;   ///< Rect table SRV.
    uint32_t debugRegionCount = 0;          ///< Rect count (0 = off).
    uint32_t debugRegionStamp = 0;          ///< Frame stamp written into each rect footer.
    uint32_t debugRegionFields = 0;         ///< Field-group bitmask.
};

// ─────────────────────────────────────────────────────────────────────────────
// Temporal Upscaler Interface
// ─────────────────────────────────────────────────────────────────────────────

/// Abstract interface for temporal upscaling algorithms.
///
/// Concrete implementations (TAA, TSR) create their own compute pipelines
/// and intermediate resources at `Init()` time, then record dispatches in
/// `Execute()`. The caller owns the history texture ping-pong and issues
/// resource barriers around the `Execute()` call.
///
/// Lifecycle:
///   1. `CreateTemporalUpscaler(type)` --- factory creates the instance
///   2. `Init(device, pipeline)` --- allocate GPU resources
///   3. Per frame: `Execute(cmd, input)`
///   4. On resize: `OnResize(width, height)`
///   5. `Shutdown()` --- release GPU resources (also called by destructor)
///
/// @deviations Reference passes FRDGBuilder + FSceneTextures; we pass raw
///             IRHICommandList + UpscalerInput with bindless indices.
class ITemporalUpscaler {
public:
    virtual ~ITemporalUpscaler() = default;

    // --- Queries ---

    /// Returns the upscaler type identifier.
    [[nodiscard]] virtual UpscalerType GetType() const noexcept = 0;

    /// Returns a human-readable name (e.g. "TSR", "TAA", "DLSS").
    /// The returned pointer is valid for the lifetime of the upscaler.
    [[nodiscard]] virtual const char* GetName() const noexcept = 0;

    /// True when this build has an implemented backend. No device is needed;
    /// Init validates whether it can run on the actual device.
    [[nodiscard]] virtual bool IsAvailable() const noexcept = 0;

    /// Returns the minimum supported screen percentage.
    /// For TSR this is typically 50% (2x upscale). For TAA it is 100% (no upscale).
    [[nodiscard]] virtual float GetMinScreenPercentage() const noexcept = 0;

    /// Returns the maximum supported screen percentage.
    /// For TSR this is typically 200% (supersampling). For TAA it is 100%.
    [[nodiscard]] virtual float GetMaxScreenPercentage() const noexcept = 0;

    // --- Lifecycle ---

    /// Initializes GPU resources (compute pipelines, intermediate buffers).
    /// Called once after the RHI device is fully initialized.
    /// Returns false on failure, leaving the object in the Shutdown state.
    ///
    /// @param device    The RHI device for resource creation.
    /// @param pipelines The pipeline library for compute shader compilation.
    /// @param cbRing    The constant buffer ring allocator for per-frame CB uploads.
    [[nodiscard]] virtual bool Init(IRHIDevice* device, PipelineLibrary* pipelines, CBRingAllocator* cbRing) = 0;

    /// Releases all GPU resources. Called during engine shutdown.
    /// Safe to call multiple times (subsequent calls are no-ops).
    virtual void Shutdown() = 0;

    /// Called when the display resolution changes. The upscaler recreates
    /// any resolution-dependent intermediate resources.
    ///
    /// @param displayWidth  New display width in pixels.
    /// @param displayHeight New display height in pixels.
    virtual void OnResize(uint32_t displayWidth, uint32_t displayHeight) = 0;

    // --- Execution ---

    /// Executes the upscaler for one frame.
    ///
    /// Records compute dispatches on the command list. The caller is
    /// responsible for resource state transitions on history textures
    /// (SRV <-> UAV) before and after this call.
    ///
    /// @param cmd   The command list to record dispatches on.
    /// @param input Per-frame input data (textures, jitter, resolution).
    /// Returns true iff a history write was recorded.
    virtual bool Execute(IRHICommandList* cmd, const UpscalerInput& input) = 0;

    // --- Non-copyable (prevent slicing) ---

    ITemporalUpscaler() = default;
    ITemporalUpscaler(const ITemporalUpscaler&) = delete;
    ITemporalUpscaler& operator=(const ITemporalUpscaler&) = delete;
    ITemporalUpscaler(ITemporalUpscaler&&) = default;
    ITemporalUpscaler& operator=(ITemporalUpscaler&&) = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a temporal upscaler of the specified type.
///
/// Returns nullptr for `UpscalerType::None`, XeSS, or unknown types.
/// SDK-less FSR2 and DLSS instances report IsAvailable() == false.
///
/// @param type The upscaler algorithm to instantiate.
/// @return Owning pointer to the upscaler, or nullptr if the type is
///         None or not yet implemented.
///
[[nodiscard]] std::unique_ptr<ITemporalUpscaler> CreateTemporalUpscaler(UpscalerType type);

} // namespace uz
