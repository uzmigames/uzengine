/// UzEngine Scene Renderer — Deferred lighting pass registration.
///
/// Selects between full deferred lighting, debug view mode, and GBuffer debug
/// visualization depending on the active debug flags.
///
/// Extracted from frame_lighting.cpp. All functions are private methods of
/// SceneRenderer, called by RegisterLightingPasses().

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"

#include <cstring>

namespace uz {

void SceneRenderer::RegisterDeferredLightingPass(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    // ── Deferred Lighting / Debug View / GBuffer Debug ──────────────────
    const bool debugActive = false; // DEBUG: force off to isolate
    const bool debugViewActive = false; // DEBUG: force off to isolate

    if (debugViewActive) {
        // ── Debug View Mode — replaces lighting with channel visualization ──
        DebugViewPassDesc dvDesc{};
        dvDesc.backbuffer        = ctx.hdrRes;
        dvDesc.albedoGBuffer     = ctx.albedoRes;
        dvDesc.normalGBuffer     = ctx.normalRes;
        dvDesc.depthGBuffer      = ctx.depthRes;
        dvDesc.positionGBuffer   = ctx.positionRes;
        dvDesc.perFrameAlloc     = ctx.perFrameAlloc;
        dvDesc.pipeline          = ctx.pipelines->GetDebugView();
        dvDesc.mode              = m_debugViewMode;
        dvDesc.albedoSrvIndex    = m_gbuffer.albedoSRV;
        dvDesc.normalSrvIndex    = m_gbuffer.normalSRV;
        dvDesc.depthSrvIndex     = m_gbuffer.depthSRV;
        dvDesc.positionSrvIndex  = m_gbuffer.positionWSSRV;
        dvDesc.albedoTexture     = m_gbuffer.albedo;
        dvDesc.normalTexture     = m_gbuffer.normal;
        dvDesc.depthTexture      = m_gbuffer.depth;
        dvDesc.positionTexture   = m_gbuffer.positionWS;
        dvDesc.bindless          = ctx.bindless;
        dvDesc.viewportWidth     = ctx.renderWidth;
        dvDesc.viewportHeight    = ctx.renderHeight;
        AddDebugViewPass(rgBuilder, dvDesc);
    } else if (!debugActive) {
        LightingPassDesc lightDesc{};
        lightDesc.backbuffer          = ctx.hdrRes;
        lightDesc.albedoGBuffer       = ctx.albedoRes;
        lightDesc.normalGBuffer       = ctx.normalRes;
        lightDesc.depthGBuffer        = ctx.depthRes;
        lightDesc.positionGBuffer     = ctx.positionRes;
        lightDesc.emissiveGBuffer     = ctx.emissiveRes;
        lightDesc.perFrameAlloc       = ctx.perFrameAlloc;
        lightDesc.lightingAlloc       = ctx.lightingAlloc;
        lightDesc.pipeline            = ctx.pipelines->GetLighting();
        lightDesc.albedoSrvIndex      = m_gbuffer.albedoSRV;
        lightDesc.normalSrvIndex      = m_gbuffer.normalSRV;
        lightDesc.depthSrvIndex       = m_gbuffer.depthSRV;
        lightDesc.positionSrvIndex    = m_gbuffer.positionWSSRV;
        lightDesc.emissiveSrvIndex    = m_gbuffer.emissiveSRV;
        lightDesc.envMisSrvIndex      = (m_envMISPass && m_envMISPass->IsInitialized())
                                        ? m_envMISPass->GetAccumulatedSrv() : UINT32_MAX;
        lightDesc.albedoTexture       = m_gbuffer.albedo;
        lightDesc.normalTexture       = m_gbuffer.normal;
        lightDesc.depthTextureForSrv  = m_gbuffer.depth;
        lightDesc.positionTexture     = m_gbuffer.positionWS;
        lightDesc.emissiveTexture     = m_gbuffer.emissive;
        lightDesc.shadowAlloc         = ctx.shadowAlloc;
        lightDesc.shadowAtlasResource = ctx.shadowAtlasRes;
        lightDesc.shadowAtlasTexture  = m_shadowAtlas.GetTexture();
        // IBL texture handles for SM5 fallback path (t5-t7) — kept for
        // backward compat; the binder path reads from perFrameParams instead.
        if (m_iblBaker) {
            lightDesc.iblIrradiance   = m_iblBaker->GetIrradianceTexture();
            lightDesc.iblPrefiltered  = m_iblBaker->GetPrefilteredTexture();
            lightDesc.iblBrdfLut      = m_iblBaker->GetBRDFLutTexture();
        }
        // Transmittance LUT for SM5 fallback (t8) — aerial perspective
        if (m_transmittanceLut.IsValid() && m_transmittanceLutFilled) {
            lightDesc.transmittanceLutTexture = m_transmittanceLut;
        }
        // GTAO filtered AO texture for SM5 fallback (t9)
        if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled) {
            lightDesc.gtaoTexture = m_gtaoPass->GetFilteredAOTexture();
        }
        lightDesc.bindless            = ctx.bindless;
        lightDesc.viewportWidth       = ctx.renderWidth;
        lightDesc.viewportHeight      = ctx.renderHeight;
        // Binder path — ShaderParameterBinder uploads per-frame CB and
        // binds IBL textures via metadata. On SM5, BindResources calls
        // SetFragmentTexture for each texture member automatically.
        lightDesc.perFrameParams    = &ctx.perFrame;
        lightDesc.shadowDataParams  = &ctx.shadowDataParams;
        lightDesc.cbRingAllocator   = m_cbRing.get();

        AddLightingPass(rgBuilder, lightDesc);
    } else {
        GBufferDebugPassDesc dbgDesc{};
        dbgDesc.backbuffer         = ctx.hdrRes;
        dbgDesc.albedoGBuffer      = ctx.albedoRes;
        dbgDesc.normalGBuffer      = ctx.normalRes;
        dbgDesc.depthGBuffer       = ctx.depthRes;
        dbgDesc.perFrameAlloc      = ctx.perFrameAlloc;

        struct DebugCBData { uint32_t mode; uint32_t pad[3]; };
        CBAllocation debugAlloc = m_cbRing->Allocate(sizeof(DebugCBData));
        DebugCBData debugCBData{static_cast<uint32_t>(m_debugMode), {0, 0, 0}};
        std::memcpy(debugAlloc.cpuPtr, &debugCBData, sizeof(DebugCBData));
        dbgDesc.debugAlloc         = debugAlloc;

        dbgDesc.pipeline           = ctx.pipelines->GetGBufferDebug();
        dbgDesc.mode               = m_debugMode;
        dbgDesc.albedoSrvIndex     = m_gbuffer.albedoSRV;
        dbgDesc.normalSrvIndex     = m_gbuffer.normalSRV;
        dbgDesc.depthSrvIndex      = m_gbuffer.depthSRV;
        dbgDesc.albedoTexture      = m_gbuffer.albedo;
        dbgDesc.normalTexture      = m_gbuffer.normal;
        dbgDesc.depthTextureForSrv = m_gbuffer.depth;
        dbgDesc.bindless           = ctx.bindless;
        dbgDesc.viewportWidth      = ctx.renderWidth;
        dbgDesc.viewportHeight     = ctx.renderHeight;
        AddGBufferDebugPass(rgBuilder, dbgDesc);
    }
}

} // namespace uz
