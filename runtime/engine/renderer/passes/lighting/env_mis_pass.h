/// Environment MIS Temporal Accumulation Pass.
///
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" — MIS
/// @ref Karis 2014 "High Quality Temporal Supersampling"
///
/// Dispatches a compute shader that estimates the specular environment term of
/// smooth metals with two sampling strategies (the environment through the alias
/// table, the BRDF through its visible normals) combined by the balance heuristic,
/// and accumulates the estimate over time with an exponential moving average
/// (ping-pong history).
///
/// The accumulated result is an RGBA16F texture: rgb the estimate, alpha its
/// coverage; the deferred lighting blends it over the split-sum specular by that
/// coverage (block A.3: it replaces, never adds; block B.5b: by coverage, so the
/// pixels the estimate does not cover keep the split-sum term).
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
namespace rg { class RgStateRegistry; }

/// GPU resources and dispatch logic for the env MIS temporal accumulation pass.
class EnvMISPass {
public:
    static constexpr uint32_t kThreadGroupSize = 8;

    EnvMISPass() = default;
    ~EnvMISPass();

    EnvMISPass(const EnvMISPass&) = delete;
    EnvMISPass& operator=(const EnvMISPass&) = delete;

    void SetStateRegistry(rg::RgStateRegistry* states) { m_states = states; }

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
        /// The frame's PerFrameCB allocation, bound at b1 (the shader reads the camera position and
        /// the sky light tint from it). Block B.5b: the pass never bound it; the compute root slot
        /// held whatever the previous compute pass left there.
        RHIBufferHandle perFrameBuffer{};
        uint64_t        perFrameOffset = 0;
        const void*     perFrameCpu    = nullptr;
        uint32_t        perFrameSize   = 0;
        /// r.EnvMIS.Reproject: run the UZ_ENVMIS_REPROJECT=1 permutation, which fetches the history where each
        /// surface point was on the previous frame (the velocity buffer) and keeps it only where the geometry
        /// history says it was accumulated on that surface. Off = the screen-locked fetch, unchanged.
        bool     reproject        = false;
        uint32_t velocitySrvIdx   = UINT32_MAX;  ///< The velocity buffer (RG16F, pixels), written this frame.
        bool     historyReusable  = false;       ///< The frame's temporal decision (consecutive frames, no camera cut).
        float    cameraPos[3]     = {0.f, 0.f, 0.f}; ///< This frame's camera position (cm); remembered for the next frame.
        /// r.EnvMIS.Denoise (on top of reproject): the estimate is written alone into a raw texture and
        /// env_mis_resolve_cs.hlsl resolves it over the neighbouring pixels whose lobes see the same environment
        /// before the same reprojected accumulation. Off = the accumulation above, unchanged.
        bool     denoise             = false;
        uint32_t denoiseRadius       = 3;        ///< Kernel radius in pixels (r.EnvMIS.Denoise.Radius).
        float    denoiseLobeFraction = 0.5f;     ///< Normal-weight sigma / GGX alpha (r.EnvMIS.Denoise.LobeFraction).
        /// r.EnvMIS.Temporal (on top of denoise): the UZ_ENVMIS_TEMPORAL=1 permutation of the resolve keeps an
        /// fp32 accumulator (the running average of the output up to temporalMaxCount samples, and the fast
        /// reference: the denoise accumulation's luminance and the estimate's noise) and clamps the output every
        /// frame to that reference +- temporalClamp x its noise. Off = the resolve above, unchanged.
        bool     temporal            = false;
        uint32_t temporalMaxCount    = 256;      ///< Sample cap of the running average (r.EnvMIS.Temporal.MaxCount).
        float    temporalClamp       = 2.5f;     ///< Clamp width in fast-reference standard deviations (r.EnvMIS.Temporal.Clamp).
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
    [[nodiscard]] bool EnsureReprojectPipeline();
    [[nodiscard]] bool EnsureGeometryTextures();
    void DestroyGeometryTextures();
    [[nodiscard]] bool EnsureResolvePipeline();
    [[nodiscard]] bool EnsureRawTexture();
    void DestroyRawTexture();
    [[nodiscard]] bool EnsureTemporalPipeline();
    [[nodiscard]] bool EnsureAccumulatorTextures();
    void DestroyAccumulatorTextures();

    IRHIDevice*    m_device    = nullptr;
    rg::RgStateRegistry* m_states = nullptr;
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

    // ── r.EnvMIS.Reproject: the permutation and its geometry history (allocated on first use) ──
    RHIShaderHandle   m_reprojectShaderCS = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_reprojectPipeline = RHIPipelineHandle::Invalid();
    bool              m_reprojectPipelineFailed = false;
    RHITextureHandle  m_geometry[2]    = { RHITextureHandle::Invalid(), RHITextureHandle::Invalid() };
    uint32_t          m_geometrySrv[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t          m_geometryUav[2] = { UINT32_MAX, UINT32_MAX };
    bool              m_reprojectHistoryValid = false; ///< The geometry history was written by the last Execute.
    uint32_t          m_reprojectFrame = 0;            ///< The frame index of that Execute.
    float             m_prevCameraPos[3] = {0.f, 0.f, 0.f};

    // ── r.EnvMIS.Denoise: the resolve pipeline and the raw estimate (allocated on first use) ──
    RHIShaderHandle   m_resolveShaderCS = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_resolvePipeline = RHIPipelineHandle::Invalid();
    bool              m_resolvePipelineFailed = false;
    RHITextureHandle  m_raw    = RHITextureHandle::Invalid();
    uint32_t          m_rawSrv = UINT32_MAX;
    uint32_t          m_rawUav = UINT32_MAX;

    // ── r.EnvMIS.Temporal: the permutation and the accumulator pair (RGBA32F, allocated on first use) ──
    RHIShaderHandle   m_temporalShaderCS = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_temporalPipeline = RHIPipelineHandle::Invalid();
    bool              m_temporalPipelineFailed = false;
    RHITextureHandle  m_accum[2]    = { RHITextureHandle::Invalid(), RHITextureHandle::Invalid() };
    uint32_t          m_accumSrv[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t          m_accumUav[2] = { UINT32_MAX, UINT32_MAX };
    bool              m_temporalHistoryValid = false;  ///< The accumulator was written by the last Execute.

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_initialized = false;
};

} // namespace uz
