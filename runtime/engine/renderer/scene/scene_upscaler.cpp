/// UzEngine Scene Renderer — High-level configuration methods (upscaler, screen
/// percentage, HDRI environment).

#include "scene_renderer.h"

#include "rhi.h"
#include "log.h"
#include "sh_projection.h"
#include "cvar.h"

#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Screen Percentage
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::SetUpscalerType(UpscalerType type) {
    if (type == m_upscalerType && m_upscaler) return;

    // Shut down the old upscaler
    if (m_upscaler) {
        m_upscaler->Shutdown();
        m_upscaler.reset();
    }

    m_upscalerType = type;

    // Sync CVar so ApplyCVars() doesn't overwrite this value next frame
    CVarRegistry::Get().Set("r.AntiAliasingMethod", static_cast<float>(type));

    // Create and initialize the new upscaler
    m_upscaler = CreateTemporalUpscaler(type);
    if (m_upscaler && m_device) {
        m_upscaler->Init(m_device, m_pipelines.get(), m_cbRing.get());
        if (m_width > 0 && m_height > 0) {
            m_upscaler->OnResize(m_width, m_height);
        }
    }

    // Enable/disable TAA jitter based on upscaler presence
    m_taaEnabled = (type != UpscalerType::None);

    UZ_LOG(Renderer, Info, "SceneRenderer: upscaler set to {}",
           m_upscaler ? m_upscaler->GetName() : "None");
}

void SceneRenderer::SetScreenPercentage(float pct) {
    // Clamp to valid range (10-200).
    pct = std::max(10.f, std::min(200.f, pct));

    if (std::abs(pct - m_screenPercentage) < 0.01f) return;

    m_screenPercentage = pct;

    // Sync CVar so ApplyCVars() doesn't overwrite this value next frame
    CVarRegistry::Get().Set("r.ScreenPercentage", pct);

    UZ_LOG(Renderer, Info, "SceneRenderer: screen percentage set to {:.1f}%", pct);

    // Rebuild all render targets at the new render resolution.
    // Resize() recomputes m_renderWidth/Height from m_screenPercentage.
    if (m_device && m_width > 0 && m_height > 0) {
        Resize(m_width, m_height);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SetHDRIEnvironment
// ─────────────────────────────────────────────────────────────────────────────

bool SceneRenderer::SetHDRIEnvironment(uint32_t equirectSrvIndex, uint32_t cubemapSize) {
    if (!m_iblBaker || !m_iblBaker->IsInitialized()) {
        UZ_LOG(Renderer, Error, "SceneRenderer::SetHDRIEnvironment: IBL baker not initialized");
        return false;
    }

    if (equirectSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "SceneRenderer::SetHDRIEnvironment: invalid equirectSrvIndex");
        return false;
    }

    // Convert equirectangular panorama to cubemap
    auto equirectResult = m_iblBaker->EquirectToCubemap(equirectSrvIndex, cubemapSize);
    if (!equirectResult.cubemap.IsValid() || equirectResult.srvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "SceneRenderer::SetHDRIEnvironment: equirect-to-cubemap conversion failed");
        return false;
    }

    // Destroy previous HDRI cubemap if one was set
    if (m_hdriCubemap.IsValid()) {
        m_device->DestroyTexture(m_hdriCubemap);
    }

    m_hdriCubemap = equirectResult.cubemap;
    m_hdriCubemapSrv = equirectResult.srvIndex;

    // Destroy previous IBL textures before re-baking (caller-owned since baker
    // no longer auto-destroys — fixes reflection probe bake invalidating HDRI IBL).
    if (m_iblResult.irradianceTexture.IsValid()) {
        m_device->DestroyTexture(m_iblResult.irradianceTexture);
    }
    if (m_iblResult.prefilteredTexture.IsValid()) {
        m_device->DestroyTexture(m_iblResult.prefilteredTexture);
    }

    // Bake IBL from the new cubemap
    auto bakeResult = m_iblBaker->BakeEnvironment(m_hdriCubemapSrv, m_hdriCubemap);
    if (bakeResult.irradianceSrvIndex == UINT32_MAX || bakeResult.prefilteredSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "SceneRenderer::SetHDRIEnvironment: IBL bake failed");
        return false;
    }

    m_iblResult = bakeResult;
    m_hdriActive = true;

    // Rebuild env map importance sampler from HDRI cubemap
    if (m_envMapSampler) {
        m_envMapSampler->Rebuild(m_hdriCubemapSrv);
    }

    // Project SH irradiance from the HDRI cubemap for diffuse lighting + fog color.
    // Without this, m_skyIrradianceSH retains stale procedural sky values.
    {
        auto* cmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
        if (cmdList) {
            cmdList->Begin();
            auto shResult = ProjectCubemapToSH(m_device, cmdList,
                                                m_hdriCubemap, cubemapSize);
            cmdList->End();
            m_device->DestroyCommandList(cmdList);

            bool shValid = (std::abs(shResult.coeffs[0].w) > 1e-6f ||
                           std::abs(shResult.coeffs[1].w) > 1e-6f);
            if (shValid) {
                m_skyIrradianceSH = shResult;
                UZ_LOG(Renderer, Info, "SceneRenderer: HDRI SH projected (L0_R={:.4f})",
                       shResult.coeffs[0].x);
            }
        }
    }

    // Suppress procedural sky IBL re-capture — the HDRI cubemap is authoritative
    m_skyIBLDirty = false;

    // Overwrite the sky cubemap SRV so the skybox pass renders the HDRI
    // instead of the procedural sky atmosphere. The sky cubemap is used in the
    // PerFrameCB as the environment cubemap SRV index for both the skybox
    // background and IBL sampling.
    m_skyCubemapSrv = m_hdriCubemapSrv;

    UZ_LOG(Renderer, Info,
           "SceneRenderer: HDRI environment set (cubemap={}x{}, SRV={}, "
           "irrad={}, prefilt={})",
           cubemapSize, cubemapSize, m_hdriCubemapSrv,
           bakeResult.irradianceSrvIndex, bakeResult.prefilteredSrvIndex);

    return true;
}

} // namespace uz
