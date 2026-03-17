/// UzEngine Bloom Pass — GPU Compute Bloom (Threshold + Downsample + Upsample)
///
/// Implements a physically-based bloom effect using compute shaders:
/// 1. Threshold pass: Extract bright pixels from HDR scene color with Karis average
/// 2. Downsample chain: 13-tap Jimenez 2014 progressive downsampling (5 levels)
/// 3. Upsample chain: 9-tap tent filter upsampling with additive blend (5 levels)
/// 4. Composite: Adds bloom result to HDR scene color before tonemapping
///
/// The mip chain starts at half-resolution of the HDR target and goes down to
/// 1/64 resolution, providing a wide bloom radius without excessive blur passes.
///
/// # UE5 Equivalent
/// PostProcessBloom.cpp — FRCPassPostProcessBloomSetup + FRCPassPostProcessBloomDown
/// PostProcessBloom.usf — BloomSetupCS, BloomDownCS, BloomUpCS
///
/// @source UE5 PostProcessBloom.cpp:148-320 — Bloom dispatch pattern
///
/// # Usage
/// ```cpp
/// BloomPass bloom;
/// bloom.Init(device, &shaderMgr);
/// // Each frame, after lighting, before tonemap:
/// bloom.Execute(cmdList, hdrSrvIndex, hdrUavIndex, width, height, exposure);
/// ```

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;

/// GPU compute bloom settings.
///
/// @source UE5 PostProcessSettings.h — bloom default values
/// @source UE5 Scene.cpp:426-437 — default per-stage tint weights
struct BloomSettings {
    float threshold     = -1.0f;  ///< Luminance threshold. -1.0 = no threshold (UE5 default). @source UE5 Scene.cpp:423
    float intensity     = 0.675f; ///< Bloom contribution to final image. UE5 default: 0.675.
    float radius        = 1.0f;   ///< Upsample filter radius scale. UE5 default: 1.0.
    float sizeScale     = 4.0f;   ///< Global bloom size scale (affects mip blur radius). UE5 default: 4.0.
    bool  enabled       = true;   ///< Master toggle.

    /// Per-stage size multipliers — controls each mip's relative blur width.
    /// @source UE5 PostProcessSettings.h — Bloom1Size..Bloom6Size
    /// @constants Size1=0.3 (1/2 res), Size2=1.0 (1/4), Size3=2.0 (1/8),
    ///   Size4=10.0 (1/16), Size5=30.0 (1/32), Size6=64.0 (1/64)
    float stageSizes[6] = { 0.3f, 1.0f, 2.0f, 10.0f, 30.0f, 64.0f };

    /// Per-stage tint weights applied during upsample accumulation.
    /// Each stage's contribution is multiplied by its tint before additive blend.
    /// Index 0 = stage 1 (1/2 res), ..., index 5 = stage 6 (1/64 res).
    ///
    /// @source UE5 Scene.cpp:426-437 — FPostProcessSettings constructor defaults
    /// @constants Stage1=0.3465 (Scene.cpp:426), Stage2=0.138 (Scene.cpp:428),
    ///   Stage3=0.1176 (Scene.cpp:430), Stage4=0.066 (Scene.cpp:432),
    ///   Stage5=0.066 (Scene.cpp:434), Stage6=0.061 (Scene.cpp:436)
    /// @deviations NONE — scalar weights match UE5 default tints (R=G=B)
    float stageTints[6] = { 0.3465f, 0.138f, 0.1176f, 0.066f, 0.066f, 0.061f };
};

/// GPU compute bloom pass.
///
/// Owns all compute pipelines and intermediate mip textures. Textures are
/// created at Init() based on the provided resolution and recreated on Resize().
///
/// # Ownership
/// Owns all GPU textures and pipelines. Does NOT own the IRHIDevice or
/// ShaderManager — both must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class BloomPass {
public:
    BloomPass() = default;
    ~BloomPass();

    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;
    BloomPass(BloomPass&&) = delete;
    BloomPass& operator=(BloomPass&&) = delete;

    /// Initialize GPU resources and compile compute shaders.
    ///
    /// Creates the 6-level mip chain (half-res through 1/64 res) as RGBA16F
    /// textures with SRV+UAV access, and compiles the 3 compute pipelines
    /// (threshold, downsample, upsample).
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this object.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this object.
    /// * `width` — HDR scene color width in pixels.
    /// * `height` — HDR scene color height in pixels.
    ///
    /// # Returns
    /// `true` on success, `false` if resource creation or shader compilation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t width, uint32_t height);

    /// Execute the bloom pipeline for this frame.
    ///
    /// Dispatches threshold + 5x downsample + 5x upsample compute passes.
    /// After this call, the final bloom result is in mip[0] in SRV state,
    /// ready for the tonemap pass to read and composite.
    ///
    /// The HDR scene color must be in SRV state on entry (for threshold read).
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `hdrSrvIndex` — SRV descriptor index for the HDR scene color texture.
    /// * `width`, `height` — HDR scene color dimensions.
    /// * `exposure` — Current exposure multiplier (from auto-exposure).
    void Execute(IRHICommandList* cmdList,
                 uint32_t hdrSrvIndex,
                 uint32_t width, uint32_t height,
                 float exposure);

    /// Recreate mip chain textures for a new resolution.
    ///
    /// Call when the render target size changes (e.g., window resize).
    /// Destroys existing textures and creates new ones at the new resolution.
    void Resize(uint32_t width, uint32_t height);

    /// Release all GPU resources.
    void Shutdown();

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Returns the SRV index of the final bloom result (mip[0]).
    /// Valid only after Execute() has been called at least once.
    [[nodiscard]] uint32_t GetBloomResultSrvIndex() const noexcept { return m_mipSrvIndices[0]; }

    /// Settings (can be modified per-frame before calling Execute).
    BloomSettings settings;

private:
    /// Ensures the threshold compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureThresholdPipeline();

    /// Ensures the downsample compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureDownsamplePipeline();

    /// Ensures the upsample compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureUpsamplePipeline();

    /// Creates the mip chain textures for the given resolution.
    void CreateMipChain(uint32_t width, uint32_t height);

    /// Destroys all mip chain textures.
    void DestroyMipChain();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── Mip Chain Textures ──────────────────────────────────────────────
    // Mip 0 = half-res, Mip 1 = quarter-res, ..., Mip 5 = 1/64 res
    static constexpr uint32_t kMipCount = 6;

    RHITextureHandle m_mipTextures[kMipCount]  = {};
    uint32_t         m_mipSrvIndices[kMipCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                                    UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t         m_mipUavIndices[kMipCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                                    UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t         m_mipWidths[kMipCount]  = {};
    uint32_t         m_mipHeights[kMipCount] = {};

    // ── Compute Pipelines (lazily created) ───────────────────────────────
    RHIPipelineHandle m_thresholdPipeline  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_downsamplePipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_upsamplePipeline   = RHIPipelineHandle::Invalid();

    // ── Shader Handles ───────────────────────────────────────────────────
    RHIShaderHandle m_thresholdCS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_downsampleCS = RHIShaderHandle::Invalid();
    RHIShaderHandle m_upsampleCS   = RHIShaderHandle::Invalid();

    uint32_t m_fullWidth  = 0;  ///< Full HDR scene color width.
    uint32_t m_fullHeight = 0;  ///< Full HDR scene color height.

    bool m_initialized = false;

    // ── Constants ────────────────────────────────────────────────────────
    static constexpr uint32_t kGroupSize = 8;  ///< Compute thread group size (8x8x1).
};

} // namespace uz
