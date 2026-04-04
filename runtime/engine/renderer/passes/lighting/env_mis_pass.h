/// Environment MIS Temporal Accumulation Pass.
///
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" — MIS
/// @ref Karis 2014 "High Quality Temporal Supersampling"
///
/// Dispatches a compute shader that importance-samples the environment map
/// via the alias table, evaluates GGX BRDF with MIS balance heuristic, and
/// accumulates results over time using exponential moving average (ping-pong).
///
/// The accumulated result is a clean RGBA16F texture that deferred lighting
/// reads as an additive specular contribution for metallic surfaces.
///
/// # Lifetime
/// Created and owned by SceneRenderer. Init after EnvMapSampler, Shutdown
/// before device destruction.

#pragma once

#include "rhi_types.h"
#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class EnvMapSampler;

/// GPU resources and dispatch logic for the env MIS temporal accumulation pass.
class EnvMISPass {
public:
    static constexpr uint32_t kThreadGroupSize = 8;

    EnvMISPass() = default;
    ~EnvMISPass();

    EnvMISPass(const EnvMISPass&) = delete;
    EnvMISPass& operator=(const EnvMISPass&) = delete;

    /// Initialize pipeline and resources.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t width, uint32_t height);

    /// Release all GPU resources.
    void Shutdown();

    /// Recreate textures on resize.
    void OnResize(uint32_t width, uint32_t height);

    /// Input parameters for a single dispatch.
    struct DispatchParams {
        uint32_t albedoSrvIdx     = UINT32_MAX;
        uint32_t normalSrvIdx     = UINT32_MAX;
        uint32_t depthSrvIdx      = UINT32_MAX;
        uint32_t positionSrvIdx   = UINT32_MAX;
        uint32_t envCubemapSrvIdx = UINT32_MAX; ///< Environment cubemap SRV.
        uint32_t frameIndex       = 0;
        float    temporalAlpha    = 0.05f;      ///< EMA blend: 0.05 = 95% history.
    };

    /// Dispatch the env MIS compute + temporal accumulation.
    /// The EnvMapSampler must be ready (IsReady() == true).
    void Execute(IRHICommandList* cmdList, const EnvMapSampler& sampler,
                 const DispatchParams& params);

    /// SRV of the temporally-accumulated result (for deferred lighting to read).
    /// Returns UINT32_MAX if not initialized or not yet executed.
    [[nodiscard]] uint32_t GetAccumulatedSrv() const noexcept;

    /// Returns true if Init() succeeded.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    bool CreateTextures(uint32_t width, uint32_t height);
    void DestroyTextures();
    [[nodiscard]] bool EnsurePipeline();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── Compute pipeline ─────────────────────────────────────────────────
    RHIShaderHandle   m_shaderCS   = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_pipeline   = RHIPipelineHandle::Invalid();

    // ── Ping-pong history textures (full-res RGBA16F) ────────────────────
    RHITextureHandle m_history[2]    = { RHITextureHandle::Invalid(), RHITextureHandle::Invalid() };
    uint32_t         m_historySrv[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t         m_historyUav[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t         m_writeIdx      = 0;     ///< Current write target (flips each frame).
    bool             m_historyValid  = false;  ///< False on first frame / after resize.

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_initialized = false;
};

} // namespace uz
