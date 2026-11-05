/// Sun-shadow denoiser (r.Shadows.SoftPCSS.Denoise).
///
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows"
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
///
/// Two compute dispatches before the deferred lighting: sun_shadow_raw_cs.hlsl estimates the directional PCSS
/// visibility of every receiver with a kernel rotation that advances every frame, and sun_shadow_denoise_cs.hlsl
/// accumulates it where each surface point was on the previous frame (velocity buffer, geometry-validated,
/// variance-clipped) after an edge-aware pre-filter sized by the penumbra. The UZ_SHADOW_DENOISE permutation of
/// deferred_lighting_ps.hlsl reads the result instead of estimating the visibility itself.
///
/// Textures (render resolution, created on first use and recreated when the resolution changes): the raw
/// estimate (RG16F: visibility, PCSS radius), the denoised visibility pair (R32F, ping-pong) and the geometry
/// history pair (RGBA16F: octahedral normal, view distance, sample count).
///
/// # Lifetime
/// Created by SceneRenderer the first frame the CVar is on; Shutdown before device destruction.

#pragma once

#include "rhi_types.h"
#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
namespace rg { class RgStateRegistry; }

class SunShadowDenoisePass {
public:
    static constexpr uint32_t kThreadGroupSize = 8;

    SunShadowDenoisePass() = default;
    ~SunShadowDenoisePass();

    SunShadowDenoisePass(const SunShadowDenoisePass&) = delete;
    SunShadowDenoisePass& operator=(const SunShadowDenoisePass&) = delete;

    void SetStateRegistry(rg::RgStateRegistry* states) { m_states = states; }

    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr);
    void Shutdown();

    /// Builds the pipelines and (re)creates the textures for this resolution; returns the SRV of the denoised
    /// visibility the next Execute writes, or UINT32_MAX when a pipeline or a texture is missing (the caller keeps
    /// the stock lighting). Called at pass registration, so the lighting can be given the index before execution.
    [[nodiscard]] uint32_t Prepare(uint32_t width, uint32_t height);

    struct DispatchParams {
        uint32_t normalSrvIdx   = UINT32_MAX;
        uint32_t depthSrvIdx    = UINT32_MAX;
        uint32_t positionSrvIdx = UINT32_MAX;
        uint32_t velocitySrvIdx = UINT32_MAX;  ///< The velocity buffer (RG16F, pixels), written this frame.
        /// The frame's PerFrameCB (b1: camera position, sun direction) and ShadowDataCB (b4: the cascades).
        RHIBufferHandle perFrameBuffer{};
        uint64_t        perFrameOffset = 0;
        const void*     perFrameCpu    = nullptr;
        uint32_t        perFrameSize   = 0;
        RHIBufferHandle shadowBuffer{};
        uint64_t        shadowOffset = 0;
        const void*     shadowCpu    = nullptr;
        uint32_t        shadowSize   = 0;
        uint32_t frameIndex      = 0;
        bool     historyReusable = false;           ///< The frame's temporal decision (consecutive frames, no camera cut).
        float    cameraPos[3]    = {0.f, 0.f, 0.f}; ///< This frame's camera position (cm); remembered for the next frame.
        float    pixelAngle      = 0.f;             ///< 2 tan(fovY / 2) / render height.
        bool     rotate          = true;            ///< r.Shadows.SoftPCSS.Denoise.Rotate: advance the kernel rotation per frame.
        uint32_t maxCount        = 32;              ///< r.Shadows.SoftPCSS.Denoise.MaxCount.
        float    spatialScale    = 0.25f;           ///< r.Shadows.SoftPCSS.Denoise.Spatial.
        float    clampSigma      = 1.0f;            ///< r.Shadows.SoftPCSS.Denoise.Clamp.
    };

    /// The raw estimate and the denoise, into the texture Prepare returned. Requires a successful Prepare this frame.
    void Execute(IRHICommandList* cmdList, const DispatchParams& params);

    /// GPU memory held by the textures, in bytes (0 before the first Prepare).
    [[nodiscard]] uint64_t GetTextureBytes() const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    [[nodiscard]] bool EnsurePipelines();
    [[nodiscard]] bool EnsureTextures(uint32_t width, uint32_t height);
    void DestroyTextures();

    IRHIDevice*          m_device    = nullptr;
    rg::RgStateRegistry* m_states    = nullptr;
    ShaderManager*       m_shaderMgr = nullptr;

    RHIShaderHandle   m_rawShaderCS      = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_rawPipeline      = RHIPipelineHandle::Invalid();
    RHIShaderHandle   m_denoiseShaderCS  = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_denoisePipeline  = RHIPipelineHandle::Invalid();
    bool              m_pipelinesFailed  = false;

    RHITextureHandle m_raw    = RHITextureHandle::Invalid();
    uint32_t         m_rawSrv = UINT32_MAX;
    uint32_t         m_rawUav = UINT32_MAX;
    RHITextureHandle m_visibility[2]    = { RHITextureHandle::Invalid(), RHITextureHandle::Invalid() };
    uint32_t         m_visibilitySrv[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t         m_visibilityUav[2] = { UINT32_MAX, UINT32_MAX };
    RHITextureHandle m_geometry[2]      = { RHITextureHandle::Invalid(), RHITextureHandle::Invalid() };
    uint32_t         m_geometrySrv[2]   = { UINT32_MAX, UINT32_MAX };
    uint32_t         m_geometryUav[2]   = { UINT32_MAX, UINT32_MAX };

    uint32_t m_writeIdx     = 0;      ///< Pair element written by the next Execute.
    bool     m_historyValid = false;  ///< The read element was written by the last Execute.
    uint32_t m_lastFrame    = 0;      ///< The frame index of that Execute.
    float    m_prevCameraPos[3] = {0.f, 0.f, 0.f};

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_initialized = false;
};

} // namespace uz
