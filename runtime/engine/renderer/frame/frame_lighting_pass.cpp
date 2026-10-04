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

/// Classifies the current debug view mode as one of three dispatch strategies:
/// - kGBufferOnly: replace the lighting pass entirely (needs only GBuffer + PerFrame).
/// - kPostLighting: run lighting first, then overlay the debug view on top of HDR.
/// - kWireframeOverlay: run lighting normally, add wireframe overlay in the overlay pass.
/// - kNone: no debug mode active — run normal deferred lighting.
static bool IsGBufferOnlyMode(DebugViewMode m) {
    switch (m) {
        case DebugViewMode::Albedo:
        case DebugViewMode::WorldNormal:
        case DebugViewMode::Metallic:
        case DebugViewMode::Roughness:
        case DebugViewMode::AO:
        case DebugViewMode::Depth:
        case DebugViewMode::WorldPosition:
        case DebugViewMode::Unlit:
        case DebugViewMode::Tangent:
        case DebugViewMode::Faceted:
        case DebugViewMode::DetailLighting:
            return true;
        default:
            return false;
    }
}
static bool IsPostLightingMode(DebugViewMode m) {
    return m == DebugViewMode::Lighting
        || m == DebugViewMode::Shadows
        || m == DebugViewMode::LightingOnly
        || m == DebugViewMode::Reflections;
}

void SceneRenderer::RegisterDeferredLightingPass(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    // ── Deferred Lighting / Debug View / GBuffer Debug ──────────────────
    const bool debugActive = false; // Legacy GBufferDebug force-off — kept for safety

    const DebugViewMode dvm = m_debugViewMode;
    const bool debugViewActive = (dvm != DebugViewMode::Lit && dvm != DebugViewMode::LitWireframe);
    const bool gbufferOnly = debugViewActive && IsGBufferOnlyMode(dvm);
    const bool postLighting = debugViewActive && IsPostLightingMode(dvm);
    const bool needsLightingFirst = postLighting;

    // Helper to fill the common DebugViewPassDesc fields.
    auto makeDebugViewDesc = [&]() {
        DebugViewPassDesc dvDesc{};
        dvDesc.backbuffer        = ctx.hdrRes;
        dvDesc.albedoGBuffer     = ctx.albedoRes;
        dvDesc.normalGBuffer     = ctx.normalRes;
        dvDesc.depthGBuffer      = ctx.depthRes;
        dvDesc.positionGBuffer   = ctx.positionRes;
        dvDesc.perFrameAlloc     = ctx.perFrameAlloc;
        dvDesc.pipeline          = ctx.pipelines->GetDebugView();
        dvDesc.mode              = dvm;
        dvDesc.albedoSrvIndex    = m_gbuffer.albedoSRV;
        dvDesc.normalSrvIndex    = m_gbuffer.normalSRV;
        dvDesc.depthSrvIndex     = m_gbuffer.depthSRV;
        dvDesc.positionSrvIndex  = m_gbuffer.positionWSSRV;
        dvDesc.albedoTexture     = m_gbuffer.albedo;
        dvDesc.normalTexture     = m_gbuffer.normal;
        dvDesc.depthTexture      = m_gbuffer.depth;
        dvDesc.positionTexture   = m_gbuffer.positionWS;
        dvDesc.hdrSrvIndex       = m_hdrSceneColorSrv;
        dvDesc.ssrSrvIndex       = m_ssrResolvedOutputSrv;
        // Lighting split targets — what `lighting`, `shadows` and `lightingonly` show.
        dvDesc.directGBuffer     = ctx.debugDirectRes;
        dvDesc.indirectGBuffer   = ctx.debugIndirectRes;
        dvDesc.shadowGBuffer     = ctx.debugShadowRes;
        dvDesc.directSrvIndex    = m_debugDirectLightSrv;
        dvDesc.indirectSrvIndex  = m_debugIndirectLightSrv;
        dvDesc.shadowSrvIndex    = m_debugShadowMaskSrv;
        // GTAO for the `ao` view; UINT32_MAX falls back to the G-buffer alpha.
        dvDesc.gtaoSrvIndex      = (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->IsEnabledCVar())
                                 ? m_gtaoPass->GetFilteredAOSrvIndex() : UINT32_MAX;
        dvDesc.bindless          = ctx.bindless;
        dvDesc.viewportWidth     = ctx.renderWidth;
        dvDesc.viewportHeight    = ctx.renderHeight;
        return dvDesc;
    };

    if (gbufferOnly) {
        // ── GBuffer-only debug view — replaces the lighting pass entirely ──
        auto dvDesc = makeDebugViewDesc();
        AddDebugViewPass(rgBuilder, dvDesc);
        return;
    }

    if (!debugActive) {
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
        // The env-MIS accumulation is the prefiltered-cubemap reflection path;
        // when the SDF trace supplies reflections it must stay out, or the
        // deferred pass adds a second specular term on top of the traced one.
        lightDesc.envMisSrvIndex      = (ctx.perFrame.sdfReflectionsEnabled == 0
                                        && m_envMISPass && m_envMISPass->IsInitialized())
                                        ? m_envMISPass->GetAccumulatedSrv() : UINT32_MAX;
        lightDesc.iblPrefilteredSrvIndex = m_iblResult.prefilteredSrvIndex;
        lightDesc.iblIrradianceSrvIndex  = m_iblResult.irradianceSrvIndex;
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
        // GTAO filtered AO texture for SM5 fallback (t9). See
        // frame_constants.cpp:555 — must gate on IsEnabledCVar().
        if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->IsEnabledCVar()) {
            lightDesc.gtaoTexture = m_gtaoPass->GetFilteredAOTexture();
        }
        // SSR temporal output for SM5 fallback (t11)
        if (ctx.ssrActive) {
            const uint32_t ssrWriteIdx = m_ssrHistoryIndex;
            if (m_ssrHistory[ssrWriteIdx].IsValid()) {
                lightDesc.ssrTexture = m_ssrHistory[ssrWriteIdx];
            } else if (m_ssrResolvedOutput.IsValid()) {
                lightDesc.ssrTexture = m_ssrResolvedOutput;
            }
        }
        // SSGI upsampled indirect diffuse for SM5 fallback (t12)
        if (m_ssgiRenderer.IsReady()) {
            lightDesc.ssgiTexture = m_ssgiRenderer.GetResultTexture();
        }
        lightDesc.apVolumeResource    = ctx.apVolumeRes;  // AP volume (read prev frame's data)
        // Debug MRT targets — lighting split visualization for --pass-dump
        lightDesc.debugDirectResource   = ctx.debugDirectRes;
        lightDesc.debugIndirectResource = ctx.debugIndirectRes;
        lightDesc.debugShadowResource   = ctx.debugShadowRes;
        lightDesc.debugDirectLight    = m_debugDirectLight;
        lightDesc.debugIndirectLight  = m_debugIndirectLight;
        lightDesc.debugShadowMask     = m_debugShadowMask;
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

        // ── Post-lighting debug view — overlays debug visualization on top of HDR ──
        // LightingOnly and Reflections need the filled HDR buffer, so they run AFTER
        // the lighting pass has written hdrRes, then overwrite it with the debug view.
        if (needsLightingFirst) {
            auto dvDesc = makeDebugViewDesc();
            // The debug view WRITES this same HDR resource, and clears it first, so it must not
            // also read it: `lightingonly` rendered black that way. The post-lighting modes now
            // read the lighting split targets (Lighting, Shadows, LightingOnly) or the SSR result
            // (Reflections), never their own output.
            // After debug view writes the HDR resource, the rest of the pipeline
            // (tonemap, TAA) reads it as before.  No extra transitions needed.
            AddDebugViewPass(rgBuilder, dvDesc);
        }
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
