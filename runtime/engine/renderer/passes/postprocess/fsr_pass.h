/// UzEngine FSR 1.0 Pass — FidelityFX Super Resolution (EASU + RCAS)
///
/// Implements AMD FSR 1.0 spatial upscaling as two compute shader passes:
///   1. **EASU** (Edge Adaptive Spatial Upscaling) — reads low-res HDR color,
///      writes high-res output using an anisotropic Lanczos2-like filter.
///   2. **RCAS** (Robust Contrast-Adaptive Sharpening) — optional sharpening
///      pass that restores fine detail softened by spatial upscaling.
///
/// # Pipeline Integration
/// FSR runs after tonemapping (or before, depending on pipeline config):
///   Scene render at low-res → EASU upscale → RCAS sharpen → final output.
///
/// # GPU Resources
/// - EASU: reads low-res input SRV, writes high-res output UAV.
/// - RCAS: reads EASU output SRV, writes final output UAV.
/// - No intermediate textures owned by this pass — caller provides all.
///
/// # CVars
/// - `r.FSR` (bool) — Master toggle. Default: false.
/// - `r.FSR.Sharpness` (float) — RCAS sharpness. 0.0=max sharp, 2.0=soft.
///   Default: 0.2 (AMD recommended for quality mode).
///
/// @ref AMD 2021 "FidelityFX Super Resolution" — GPUOpen published algorithm

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;

// ─────────────────────────────────────────────────────────────────────────────
// FSRInput — Per-frame execution parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Parameters for a single FSR execution.
///
/// The caller is responsible for creating and managing all textures.
/// FSRPass only dispatches compute work and manages pipeline state.
struct FSRInput {
    uint32_t inputSrvIndex;         ///< Bindless SRV index for low-res input texture.
    uint32_t outputUavIndex;        ///< Bindless UAV index for EASU output (intermediate if RCAS enabled).
    uint32_t finalUavIndex;         ///< Bindless UAV index for final output (RCAS output, or same as outputUavIndex if RCAS disabled).
    uint32_t intermediateSrvIndex;  ///< Bindless SRV index for EASU output (used as RCAS input). Ignored if RCAS disabled.
    RHITextureHandle inputTexture;        ///< Low-res input texture handle (for barriers).
    RHITextureHandle intermediateTexture; ///< Intermediate texture handle (EASU output / RCAS input). Ignored if RCAS disabled.
    RHITextureHandle outputTexture;       ///< Final output texture handle (for barriers).
    uint32_t inputWidth;            ///< Input texture width in pixels.
    uint32_t inputHeight;           ///< Input texture height in pixels.
    uint32_t outputWidth;           ///< Output texture width in pixels.
    uint32_t outputHeight;          ///< Output texture height in pixels.
    float    sharpness;             ///< RCAS sharpness (0.0=max sharp, 2.0=soft). Default: 0.2.
    bool     enableRCAS;            ///< If false, only EASU is dispatched. Default: true.
};

// ─────────────────────────────────────────────────────────────────────────────
// CPU-side Constant Setup Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Packs RCAS constant from sharpness parameter.
///
/// Computes the uint4 constant needed by the RCAS shader.
/// Packs exp2(-sharpness) as a float-as-uint into con0.x.
///
/// @ref AMD ffx_fsr1.h — FsrRcasCon
///
/// @param con0      Output: RCAS constant 0 (4 uints). Only x is used.
/// @param sharpness Sharpness in [0, 2]. 0=max sharpness, 2=minimal.
void FsrRcasCon(uint32_t con0[4], float sharpness);

// ─────────────────────────────────────────────────────────────────────────────
// FSRPass
// ─────────────────────────────────────────────────────────────────────────────

/// GPU FSR 1.0 spatial upscaling pass.
///
/// Owns compute pipelines and shader handles. Does NOT own any textures —
/// the caller provides all input/output/intermediate textures.
///
/// # Ownership
/// Owns `RHIPipelineHandle` and `RHIShaderHandle` objects. Does NOT own
/// `IRHIDevice` or `ShaderManager` — both must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class FSRPass {
public:
    FSRPass() = default;
    ~FSRPass();

    FSRPass(const FSRPass&) = delete;
    FSRPass& operator=(const FSRPass&) = delete;
    FSRPass(FSRPass&&) = delete;
    FSRPass& operator=(FSRPass&&) = delete;

    /// Initialize compute pipelines.
    ///
    /// Pipelines are created lazily on first Execute() call, so Init()
    /// mainly stores device/shaderMgr references.
    ///
    /// # Arguments
    /// * `device`    — Initialized RHI device. Must outlive this object.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this object.
    ///
    /// # Returns
    /// `true` on success, `false` if arguments are null.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr);

    /// Execute FSR upscaling for this frame.
    ///
    /// Dispatches EASU (always) and optionally RCAS based on `input.enableRCAS`.
    /// Caller must ensure:
    /// - Input texture is in SRV state on entry.
    /// - Output/intermediate textures are in UAV state on entry.
    /// - After Execute(), output texture is in UAV state.
    ///
    /// Resource barriers between EASU and RCAS are handled internally.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `input`   — Execution parameters (textures, dimensions, sharpness).
    void Execute(IRHICommandList* cmdList, const FSRInput& input);

    /// Release all GPU resources.
    void Shutdown();

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Lazily compiles the EASU compute pipeline.
    [[nodiscard]] bool EnsureEasuPipeline();

    /// Lazily compiles the RCAS compute pipeline.
    [[nodiscard]] bool EnsureRcasPipeline();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── Compute Pipelines (lazily created) ───────────────────────────────
    RHIPipelineHandle m_easuPipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_rcasPipeline = RHIPipelineHandle::Invalid();

    // ── Shader Handles ───────────────────────────────────────────────────
    RHIShaderHandle m_easuShader = RHIShaderHandle::Invalid();
    RHIShaderHandle m_rcasShader = RHIShaderHandle::Invalid();

    bool m_initialized = false;

    // ── Constants ────────────────────────────────────────────────────────
    static constexpr uint32_t kGroupSize = 8;  ///< Compute thread group size (8x8x1).
};

} // namespace uz
