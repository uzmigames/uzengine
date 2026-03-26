/// UzEngine GTAO Pass — Ground Truth Ambient Occlusion.
///
/// Dispatches two compute passes per frame:
/// 1. **GTAO** (gtao_cs.hlsl): Horizon-based AO computation, outputs raw AO.
/// 2. **Denoise** (gtao_denoise_cs.hlsl): Bilateral spatial filter, outputs
///    clean AO texture for the deferred lighting pass.
///
/// # GPU Resources
/// - AO texture: R8_UNORM at full resolution.
/// - Filtered AO: R8_UNORM, same dimensions.
///
/// # Integration
/// Created and owned by SceneRenderer. The filtered AO SRV index is passed
/// to the deferred lighting pass via PerFrameCB::gtaoSrvIndex.
///
/// @ref Jimenez 2016 "GTAO — Ground Truth Ambient Occlusion"

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class CBRingAllocator;

// ─────────────────────────────────────────────────────────────────────────────
// GTAOQuality — resolution and sample count presets
// ─────────────────────────────────────────────────────────────────────────────

/// GTAO quality preset — controls resolution scale, angle count, and tap count.
///
/// @ref Jimenez 2016 "GTAO" — quality presets from SHADER_QUALITY levels
enum class GTAOQuality : uint8_t {
    Low    = 0,  ///< Half-res, 1 angle, 4 taps — fastest, minimal quality loss.
    Medium = 1,  ///< Half-res, 2 angles, 6 taps — best quality/perf balance.
    High   = 2,  ///< Full-res, 2 angles, 8 taps — current default behavior.
    Ultra  = 3,  ///< Full-res, 3 angles, 12 taps — maximum quality.
};

/// Returns true if the given quality preset uses half-resolution rendering.
inline bool GTAOQualityIsHalfRes(GTAOQuality q) noexcept {
    return q == GTAOQuality::Low || q == GTAOQuality::Medium;
}

// ─────────────────────────────────────────────────────────────────────────────
// GTAOSettings — runtime-tunable parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime-configurable GTAO parameters.
///
/// Default values match "High" quality preset.
struct GTAOSettings {
    float    worldRadius     = 200.0f;   ///< AO search radius in cm. Default: 200.
    float    intensity       = 1.0f;     ///< AO strength multiplier.
    float    thicknessBlend  = 0.5f;     ///< Thickness heuristic. 0=thin, 1=thick.
    float    fadeStartDist   = 50000.0f; ///< Distance where AO fades (cm).
    float    fadeEndDist     = 100000.0f;///< Distance where AO disappears (cm).
    float    maxFadeDepth    = 100000.0f;///< Skip AO beyond this depth (cm).
    bool     enabled         = true;     ///< Master toggle.
    GTAOQuality quality      = GTAOQuality::High;  ///< Quality preset. Default: High (full-res).

    /// AO power curve exponent. Default: 2.0.
    /// In GTAO path, shader multiplies by 0.5 -> effective 1.0.
    /// Currently hardcoded in gtao_cs.hlsl as GTAO_AO_POWER.
    float    aoPower         = 2.0f;

    /// Enable multi-bounce colored AO approximation in deferred lighting.
    /// Uses Jimenez 2016 polynomial fit to prevent over-darkening of colored
    /// surfaces under occlusion. Currently hardcoded ON in deferred_lighting_ps.hlsl.
    /// @ref Jimenez 2016 — AOMultiBounce polynomial approximation
    bool     multiBounce     = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants — b0 (16 DWORDs)
// ─────────────────────────────────────────────────────────────────────────────

/// Matches `cbuffer GTAORootCB : register(b0)` in gtao_cs.hlsl.
/// GTAO root constants — exactly 16 DWORDs (64 bytes).
/// DWORDs 0-7: per-frame dynamic values (SRV indices, frame index, camera).
/// DWORDs 8-15: algorithm parameters driven by GTAOSettings / CVars.
///
/// @ref Jimenez 2016 "GTAO" — GTAOParams layout
struct GTAORootConstants {
    // ── Per-frame dynamic values (DWORDs 0-7) ──
    uint32_t depthSrvIndex;     // DWORD 0: depth buffer SRV
    uint32_t normalSrvIndex;    // DWORD 1: normal buffer SRV
    uint32_t outputWidth;       // DWORD 2: output texture width
    uint32_t outputHeight;      // DWORD 3: output texture height
    float    invTanHalfFov;     // DWORD 4: 1/tan(fovY/2) for screen→view
    float    nearPlane;         // DWORD 5: camera near plane (cm)
    float    farPlane;          // DWORD 6: camera far plane (cm)
    uint32_t frameAndHiz;       // DWORD 7: bits[0:15]=frameIndex, bits[16:31]=hizSrvIndex (0xFFFF=disabled)

    // ── Algorithm parameters from GTAOSettings (DWORDs 8-15) ──
    uint32_t numAngles;         // DWORD 8:  direction search count (quality preset)
    uint32_t numTaps;           // DWORD 9:  steps per direction (quality preset)
    float    worldRadius;       // DWORD 10: AO search radius in cm
    float    aoIntensity;       // DWORD 11: AO strength multiplier
    float    thicknessBlend;    // DWORD 12: thickness heuristic [0,1]
    float    fadeStart;         // DWORD 13: distance where AO fades (cm)
    float    fadeEnd;           // DWORD 14: distance where AO disappears (cm)
    float    aoPower;           // DWORD 15: AO power curve exponent
};

static_assert(sizeof(GTAORootConstants) == 64,
    "GTAORootConstants must be exactly 16 DWORDs (64 bytes)");

/// Matches `cbuffer DenoiseParams : register(b0)` in gtao_denoise_cs.hlsl.
/// Packed into 8 DWORDs for root constants at slot 0.
struct GTAODenoiseRootConstants {
    float    texelSizeX;
    float    texelSizeY;
    float    nearPlane;
    float    farPlane;
    uint32_t rawAOSrvIndex;
    uint32_t depthSrvIndex;
    uint32_t _pad[2];
};

static_assert(sizeof(GTAODenoiseRootConstants) == 32,
    "GTAODenoiseRootConstants must be 8 DWORDs (32 bytes)");

/// Matches `cbuffer GTAOTemporalRootCB : register(b0)` in gtao_temporal_cs.hlsl.
/// Packed into 8 DWORDs for root constants at slot 0.
///
/// @ref Jimenez 2016 "GTAO" — temporal accumulation parameters
struct GTAOTemporalRootConstants {
    uint32_t currentAOSrvIndex;   ///< DWORD 0: current frame denoised AO SRV.
    uint32_t historyAOSrvIndex;   ///< DWORD 1: previous frame accumulated AO SRV.
    uint32_t velocitySrvIndex;    ///< DWORD 2: velocity buffer SRV (RG16F, pixels).
    uint32_t depthSrvIndex;       ///< DWORD 3: depth buffer SRV (R32_FLOAT).
    uint32_t outputWidth;         ///< DWORD 4: render target width.
    uint32_t outputHeight;        ///< DWORD 5: render target height.
    float    blendWeight;         ///< DWORD 6: temporal blend weight (default 0.1).
    uint32_t _pad0;               ///< DWORD 7: padding.
};

static_assert(sizeof(GTAOTemporalRootConstants) == 32,
    "GTAOTemporalRootConstants must be 8 DWORDs (32 bytes)");

/// Matches `cbuffer GTAOUpsampleRootCB : register(b0)` in gtao_upsample_ps.hlsl.
/// Packed into 8 DWORDs for root constants at slot 0.
struct GTAOUpsampleRootConstants {
    uint32_t halfResAOSrvIndex;     ///< DWORD 0: half-res filtered AO SRV.
    uint32_t halfResDepthSrvIndex;  ///< DWORD 1: half-res depth SRV.
    uint32_t fullResDepthSrvIndex;  ///< DWORD 2: full-res depth SRV.
    uint32_t _pad0;                 ///< DWORD 3: padding for alignment.
    float    halfResTexelSizeX;     ///< DWORD 4: 1.0 / halfWidth.
    float    halfResTexelSizeY;     ///< DWORD 5: 1.0 / halfHeight.
    float    fullResTexelSizeX;     ///< DWORD 6: 1.0 / fullWidth.
    float    fullResTexelSizeY;     ///< DWORD 7: 1.0 / fullHeight.
};

static_assert(sizeof(GTAOUpsampleRootConstants) == 32,
    "GTAOUpsampleRootConstants must be 8 DWORDs (32 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// GTAOPass
// ─────────────────────────────────────────────────────────────────────────────

/// GPU GTAO ambient occlusion system.
///
/// Owns AO textures, compute pipelines, and dispatches GTAO + denoise passes.
///
/// # Ownership
/// Owns all GPU textures and pipelines. Does NOT own IRHIDevice or
/// ShaderManager — both must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class GTAOPass {
public:
    GTAOPass() = default;
    ~GTAOPass();

    GTAOPass(const GTAOPass&) = delete;
    GTAOPass& operator=(const GTAOPass&) = delete;
    GTAOPass(GTAOPass&&) = delete;
    GTAOPass& operator=(GTAOPass&&) = delete;

    /// Initialize GPU resources and compile compute shaders.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t width, uint32_t height);

    /// Recreate AO textures at a new resolution.
    [[nodiscard]] bool Resize(uint32_t width, uint32_t height);

    /// Execute GTAO + denoise + temporal accumulation for this frame.
    ///
    /// @param velocitySrvIndex SRV index for the velocity buffer (RG16F, pixels).
    ///        Pass UINT32_MAX if velocity is unavailable — temporal pass will be skipped.
    /// @param hizSrvIndex SRV index for Hi-Z mip 0 (half-res min-depth R32F).
    ///        Pass UINT32_MAX to disable Hi-Z accelerated horizon search.
    ///
    /// Returns the SRV index of the final AO texture, or UINT32_MAX if disabled.
    uint32_t Execute(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                     uint32_t depthSrvIndex, uint32_t normalSrvIndex,
                     RHITextureHandle depthTexture, RHITextureHandle normalTexture,
                     const float* viewMatrix,
                     float invTanHalfFov, float aspectRatio,
                     float nearPlane, float farPlane,
                     uint32_t width, uint32_t height,
                     uint32_t frameIndex,
                     uint32_t velocitySrvIndex = UINT32_MAX,
                     uint32_t hizSrvIndex = UINT32_MAX);

    /// Shutdown and release all GPU resources.
    void Shutdown();

    /// Returns true if Init() has been called and GPU resources are allocated.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Returns the SRV descriptor index of the filtered AO texture.
    [[nodiscard]] uint32_t GetFilteredAOSrvIndex() const noexcept { return m_filteredAOSrvIndex; }

    /// Returns the filtered AO texture handle (for PassInspector registration).
    [[nodiscard]] RHITextureHandle GetFilteredAOTexture() const noexcept { return m_filteredAOTexture; }

    /// Settings (modifiable per-frame before Execute).
    GTAOSettings settings;

private:
    [[nodiscard]] bool EnsureGTAOPipeline();
    [[nodiscard]] bool EnsureDenoisePipeline();
    [[nodiscard]] bool EnsureUpsamplePipeline();
    [[nodiscard]] bool EnsureTemporalPipeline();
    [[nodiscard]] bool CreateTextures(uint32_t width, uint32_t height);
    [[nodiscard]] bool CreateHalfResTextures(uint32_t halfWidth, uint32_t halfHeight);
    void DestroyHalfResTextures();
    void DestroyTextures();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── GPU Textures ─────────────────────────────────────────────────────
    RHITextureHandle m_rawAOTexture      = RHITextureHandle::Invalid();
    RHITextureHandle m_filteredAOTexture = RHITextureHandle::Invalid();

    // ── Half-Resolution Textures (for Low/Medium quality) ────────────
    RHITextureHandle m_rawAOHalf      = RHITextureHandle::Invalid();
    RHITextureHandle m_filteredAOHalf = RHITextureHandle::Invalid();

    // ── Compute Pipelines ────────────────────────────────────────────────
    RHIPipelineHandle m_gtaoPipeline      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_denoisePipeline   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_upsamplePipeline  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_temporalPipeline  = RHIPipelineHandle::Invalid();

    // ── Shader Handles ───────────────────────────────────────────────────
    RHIShaderHandle m_gtaoCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_denoiseCS   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_upsamplePS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_temporalCS  = RHIShaderHandle::Invalid();

    // ── Descriptor Indices ───────────────────────────────────────────────
    uint32_t m_rawAOSrvIndex       = UINT32_MAX;
    uint32_t m_rawAOUavIndex       = UINT32_MAX;
    uint32_t m_filteredAOSrvIndex  = UINT32_MAX;
    uint32_t m_filteredAOUavIndex  = UINT32_MAX;

    // ── Half-Res Descriptor Indices ───────────────────────────────────
    uint32_t m_rawAOHalfSrvIndex      = UINT32_MAX;
    uint32_t m_rawAOHalfUavIndex      = UINT32_MAX;
    uint32_t m_filteredAOHalfSrvIndex = UINT32_MAX;
    uint32_t m_filteredAOHalfUavIndex = UINT32_MAX;

    // ── Temporal History (ping-pong) ─────────────────────────────────────
    RHITextureHandle  m_aoHistory[2]        = { RHITextureHandle::Invalid(), RHITextureHandle::Invalid() };
    uint32_t          m_aoHistorySrvIndex[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t          m_aoHistoryUavIndex[2] = { UINT32_MAX, UINT32_MAX };
    RHIResourceState  m_aoHistoryState[2]   = { RHIResourceState::Common, RHIResourceState::Common };
    uint32_t          m_historyIndex        = 0;   ///< Current write target (flips each frame).
    bool              m_historyValid        = false; ///< False on first frame / after resize.

    // ── Dimensions ───────────────────────────────────────────────────────
    uint32_t m_width      = 0;
    uint32_t m_height     = 0;
    uint32_t m_halfWidth  = 0;
    uint32_t m_halfHeight = 0;

    bool m_initialized = false;
    bool m_initialBarriersDone = false;

    /// Tracks the current resource state of m_filteredAOTexture across frames.
    /// After initial barriers: UAV. After each Execute(): SRV.
    /// The half-res upsample path needs this to transition SRV/UAV → RenderTarget correctly.
    RHIResourceState m_filteredAOCurrentState = RHIResourceState::Common;

    // ── Constants ────────────────────────────────────────────────────────
    static constexpr uint32_t kThreadGroupSize = 8;
};

} // namespace uz
