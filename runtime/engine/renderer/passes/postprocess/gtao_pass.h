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
/// @source UE5 PostProcessAmbientOcclusion.usf — GTAO algorithm
/// @source UE5 PostProcessAmbientOcclusionCommon.ush — shared helpers

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class CBRingAllocator;

// ─────────────────────────────────────────────────────────────────────────────
// GTAOSettings — runtime-tunable parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime-configurable GTAO parameters.
///
/// Default values match UE5's "High" quality preset.
/// @source UE5 PostProcessAmbientOcclusion.usf:553-557 — SHADER_QUALITY==3
struct GTAOSettings {
    float    worldRadius     = 200.0f;   ///< AO search radius in cm. UE5 default: 200.
    float    intensity       = 1.0f;     ///< AO strength multiplier.
    float    thicknessBlend  = 0.5f;     ///< Thickness heuristic. 0=thin, 1=thick.
    float    fadeStartDist   = 50000.0f; ///< Distance where AO fades (cm).
    float    fadeEndDist     = 100000.0f;///< Distance where AO disappears (cm).
    float    maxFadeDepth    = 100000.0f;///< Skip AO beyond this depth (cm).
    bool     enabled         = false;    ///< Master toggle. DISABLED — GPU hangs on first dispatch.
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants — b0 (8 DWORDs)
// ─────────────────────────────────────────────────────────────────────────────

/// Matches `cbuffer GTAORootCB : register(b0)` in gtao_cs.hlsl.
/// All GTAO parameters packed into 16 DWORDs (64 bytes) for compute root constants.
/// Compute root sig only supports root constants at slot 0 — no CBV slots available.
struct GTAORootConstants {
    uint32_t depthSrvIndex;     // DWORD 0
    uint32_t normalSrvIndex;    // DWORD 1
    uint32_t frameIndex;        // DWORD 2
    uint32_t outputWidth;       // DWORD 3
    float    outputHeight;      // DWORD 4 (as float for shader)
    float    invWidth;          // DWORD 5
    float    invHeight;         // DWORD 6
    float    worldRadius;       // DWORD 7
    float    invTanHalfFov;     // DWORD 8
    float    nearPlane;         // DWORD 9
    float    farPlane;          // DWORD 10
    float    aoIntensity;       // DWORD 11
    float    temporalCos;       // DWORD 12
    float    temporalSin;       // DWORD 13
    float    temporalOffset;    // DWORD 14
    float    thicknessBlend;    // DWORD 15
};

static_assert(sizeof(GTAORootConstants) == 64,
    "GTAORootConstants must be 16 DWORDs (64 bytes) — compute root constant limit");

// ─────────────────────────────────────────────────────────────────────────────
// CBV at b2 — bulk GTAO parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Matches `cbuffer GTAOParams : register(b2)` in gtao_cs.hlsl.
/// Must be 256-byte aligned for D3D12 CBV.
struct GTAOParamsCBV {
    // Temporal jitter (4 floats)
    float    temporalCos;
    float    temporalSin;
    float    temporalOffset;
    float    thicknessBlend;

    // Target size (4 floats)
    float    targetSizeX;
    float    targetSizeY;
    float    invTargetSizeX;
    float    invTargetSizeY;

    // World radius + view (4 floats)
    float    worldRadius;
    float    invTanHalfFov;
    float    nearPlane;
    float    farPlane;

    // Fade (4 floats)
    float    fadeMul;
    float    fadeAdd;
    float    maxFadeDepth;
    float    aoIntensity;

    // View matrix (16 floats)
    float    viewMatrix[16];

    // Pad to 256 bytes (current: 32 floats = 128 bytes, need 128 more)
    float    _pad[32];
};

static_assert(sizeof(GTAOParamsCBV) == 256,
    "GTAOParamsCBV must be 256 bytes (D3D12 CB alignment)");

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

    /// Execute GTAO + denoise for this frame.
    ///
    /// Returns the SRV index of the filtered AO texture, or UINT32_MAX if disabled.
    uint32_t Execute(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                     uint32_t depthSrvIndex, uint32_t normalSrvIndex,
                     RHITextureHandle depthTexture, RHITextureHandle normalTexture,
                     const float* viewMatrix,
                     float invTanHalfFov, float aspectRatio,
                     float nearPlane, float farPlane,
                     uint32_t width, uint32_t height,
                     uint32_t frameIndex);

    /// Shutdown and release all GPU resources.
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    [[nodiscard]] uint32_t GetFilteredAOSrvIndex() const noexcept { return m_filteredAOSrvIndex; }

    /// Settings (modifiable per-frame before Execute).
    GTAOSettings settings;

private:
    [[nodiscard]] bool EnsureGTAOPipeline();
    [[nodiscard]] bool EnsureDenoisePipeline();
    [[nodiscard]] bool CreateTextures(uint32_t width, uint32_t height);
    void DestroyTextures();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── GPU Textures ─────────────────────────────────────────────────────
    RHITextureHandle m_rawAOTexture      = RHITextureHandle::Invalid();
    RHITextureHandle m_filteredAOTexture = RHITextureHandle::Invalid();

    // ── Compute Pipelines ────────────────────────────────────────────────
    RHIPipelineHandle m_gtaoPipeline    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_denoisePipeline = RHIPipelineHandle::Invalid();

    // ── Shader Handles ───────────────────────────────────────────────────
    RHIShaderHandle m_gtaoCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_denoiseCS = RHIShaderHandle::Invalid();

    // ── Descriptor Indices ───────────────────────────────────────────────
    uint32_t m_rawAOSrvIndex       = UINT32_MAX;
    uint32_t m_rawAOUavIndex       = UINT32_MAX;
    uint32_t m_filteredAOSrvIndex  = UINT32_MAX;
    uint32_t m_filteredAOUavIndex  = UINT32_MAX;

    // ── Dimensions ───────────────────────────────────────────────────────
    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    bool m_initialized = false;

    // ── Constants ────────────────────────────────────────────────────────
    static constexpr uint32_t kThreadGroupSize = 8;
};

} // namespace uz
