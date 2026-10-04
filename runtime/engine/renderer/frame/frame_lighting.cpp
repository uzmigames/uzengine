/// UzEngine Scene Renderer — Lighting pass orchestrator.
///
/// RegisterLightingPasses() is the top-level entry point that sequences all
/// lighting-related render graph passes. The heavy lifting is in sub-helpers:
///   - frame_ao_passes.cpp         → DBuffer apply, GTAO, DFS
///   - frame_gi_passes.cpp         → SSGI, SDF GI, SSR, SSGI prev-color copy
///   - frame_lighting_pass.cpp     → Deferred lighting / debug view
///   - frame_atmosphere_passes.cpp → Sky atmosphere, height fog, cloud composite

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "gi/ssgi_renderer.h"
#include "gi/mesh_sdf_baker.h"
#include "gpu_mesh.h"
#include "world.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "atmosphere_renderer.h"
#include "volumetric_fog_pass.h"
#include "pathtracing/path_tracer_pass.h"
#include "fog_renderer.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"

#include <cmath>
#include <cstring>

// ── CVars ────────────────────────────────────────────────────────────────
// Shadows
static auto* cv_ContactShadows   = UZ_CVAR_BOOL("r.ContactShadows", true, "Contact shadows");
// GTAO — declared in gtao_pass.cpp as cv_GTAOEnabled. The registry is
// idempotent and CVarRegistry::Find("r.GTAO") returns the same CVar, but
// declaring it in two TUs invites static-init-order confusion where the
// "first" registration's default wins (and empty-arg macro bugs silently
// compile to nonsense). Read via cvars.GetBool("r.GTAO", false) instead.
// SSR — disabled (resolved-history pass produces zero).
static auto* cv_SSR              = UZ_CVAR_BOOL("r.SSR", false, "Screen-Space Reflections (stochastic VNDF + temporal denoise)");
static auto* cv_SSRIntensity     = UZ_CVAR_FLOAT("r.SSR.Intensity", 1.0f, "SSR reflection intensity", 0.f, 2.f);
static auto* cv_SSRMaxRoughness  = UZ_CVAR_FLOAT("r.SSR.MaxRoughness", 0.35f, "Max roughness for SSR (UE5 default)", 0.f, 1.f);
static auto* cv_SSRNumSteps      = UZ_CVAR_INT("r.SSR.NumSteps", 16, "SSR ray march step count", 8, 64);
static auto* cv_SSRStartMip      = UZ_CVAR_FLOAT("r.SSR.StartMipLevel", 1.0f, "SSR initial HZB mip level", 0.f, 3.f);
static auto* cv_SSRSlopeTolerance = UZ_CVAR_FLOAT("r.SSR.SlopeToleranceScale", 4.0f, "SSR slope tolerance scale", 1.f, 16.f);
static auto* cv_SSRCopyPrev      = UZ_CVAR_BOOL("r.SSR.CopyPrev", true, "SSR prev-frame color copy");
// SSGI
static auto* cv_SSGI             = UZ_CVAR_BOOL("r.SSGI", false, "Screen-Space Global Illumination");
// DFS (Distance Field Shadows)
static auto* cv_DFS              = UZ_CVAR_BOOL("r.DFS", true, "Distance Field Shadows");
// Fog
static auto* cv_Fog              = UZ_CVAR_BOOL("r.Fog", true, "Height fog");
// Clouds
static auto* cv_Clouds           = UZ_CVAR_BOOL("r.Clouds", true, "Volumetric clouds");
// Post-process (used by lighting for CAS after TAA)
static auto* cv_CASSharpness     = UZ_CVAR_FLOAT("r.CAS.Sharpness", 0.2f, "CAS sharpening strength", 0.f, 1.f);
static auto* cv_LODBias          = UZ_CVAR_FLOAT("r.LOD.Bias", 1.f, "Global LOD bias multiplier", 0.1f, 10.f);
// Debug
static auto* cv_DebugView        = UZ_CVAR_BOOL("r.DebugView", false, "GBuffer debug visualization");
// Path Tracer — runtime mode toggle. When enabled, replaces deferred lighting,
// GI, and shadow passes with a Monte Carlo path tracer pass that writes to
// hdrSceneColor. Bloom/exposure/tonemap downstream remain unchanged.
// @ref Kajiya 1986 "The Rendering Equation" — see docs/analysis/path-tracing/
static auto* cv_PathTracer       = UZ_CVAR_BOOL("r.PathTracer.Enable", false,
    "Compute path tracer runtime mode (replaces deferred lighting + GI + shadows)");

namespace uz {

void SceneRenderer::RegisterLightingPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    if (m_renderPath != RenderPath::Deferred) return;

    // ── Path Tracer runtime mode override ──────────────────────────────
    // When r.PathTracer.Enable=1, the path tracer replaces the entire
    // deferred lighting + GI + shadow chain. The pass writes the lit HDR
    // result directly to ctx.hdrRes; bloom/exposure/tonemap downstream
    // consume it unchanged.
    // @ref Kajiya 1986 "The Rendering Equation"
    if (cv_PathTracer->value >= 0.5f) {
        // ── Camera-cut detection: reset accumulation when view changes ───
        // We compare the current view matrix (first 16 floats of viewProj
        // would also work but view is more stable across projection toggles).
        // Any non-trivial diff resets sampleCount=0.
        const float* curView = reinterpret_cast<const float*>(&ctx.perFrame.viewProj);
        float maxDiff = 0.0f;
        for (int i = 0; i < 16; ++i) {
            const float diff = std::fabs(curView[i] - m_ptPrevView[i]);
            if (diff > maxDiff) maxDiff = diff;
        }
        if (!m_ptHistoryValid || maxDiff > 1e-5f) {
            m_ptAccumSamples = 0;
            m_ptHistoryValid = true;
        }
        for (int i = 0; i < 16; ++i) m_ptPrevView[i] = curView[i];

        // Ping-pong: this frame writes to m_ptAccum[curr], reads from prev.
        const uint32_t curr = m_ptAccumIdx;
        const uint32_t prev = curr ^ 1u;

        // Import both accumulation textures into the render graph each frame
        // so the graph tracks state across frames. They start as ShaderResource
        // since the previous frame left them in RT then transitioned at end of pass.
        const RHIResourceState currImportState = (m_ptAccumSamples == 0)
            ? RHIResourceState::Common
            : RHIResourceState::ShaderResource;
        rg::RgResourceId accumCurrRes = rgBuilder.ImportTexture("PT_AccumCurr",
            m_ptAccum[curr], currImportState);
        // Prev is consumed only via the bindless SRV — no need to import as
        // a render graph resource since we don't declare it as a Read (the
        // shader reads it via ResourceDescriptorHeap[]).

        PathTracerPassDesc ptDesc{};
        ptDesc.hdrTarget         = ctx.hdrRes;
        ptDesc.accumCurr         = accumCurrRes;
        ptDesc.normalGBuffer     = ctx.normalRes;
        ptDesc.depthGBuffer      = ctx.depthRes;
        ptDesc.albedoGBuffer     = ctx.albedoRes;
        ptDesc.positionGBuffer   = ctx.positionRes;
        ptDesc.emissiveGBuffer   = ctx.emissiveRes;
        ptDesc.perFrameAlloc     = ctx.perFrameAlloc;
        ptDesc.pipeline          = ctx.pipelines->GetPathTracer();
        ptDesc.normalSrvIndex    = m_gbuffer.normalSRV;
        ptDesc.depthSrvIndex     = m_gbuffer.depthSRV;
        ptDesc.albedoSrvIndex    = m_gbuffer.albedoSRV;
        ptDesc.positionSrvIndex  = m_gbuffer.positionWSSRV;
        ptDesc.emissiveSrvIndex  = m_gbuffer.emissiveSRV;
        ptDesc.skyCubeSrvIndex   = m_hdriActive ? m_hdriCubemapSrv : m_skyCubemapSrv;
        ptDesc.accumPrevSrvIndex = m_ptAccumSrv[prev];
        ptDesc.sampleCount       = m_ptAccumSamples;
        ptDesc.frameIndex        = ctx.frameIndex;
        ptDesc.shadowAlloc       = ctx.shadowAlloc;
        ptDesc.bindless          = ctx.bindless;
        ptDesc.viewportWidth     = static_cast<float>(ctx.renderWidth);
        ptDesc.viewportHeight    = static_cast<float>(ctx.renderHeight);
        AddPathTracerPass(rgBuilder, ptDesc);

        // Advance ping-pong + sample counter for next frame.
        m_ptAccumIdx     = prev;
        m_ptAccumSamples = std::min(m_ptAccumSamples + 1u, 65535u);

        // Atmosphere fog/cloud passes still composite into hdrRes after PT.
        RegisterAtmosphereFogCloudPasses(rgBuilder, ctx);
        return;
    }

    RegisterDBufferApplyPass(rgBuilder, ctx);
    RegisterGTAOPass(rgBuilder, ctx);
    RegisterDFSPasses(rgBuilder, ctx);
    RegisterSDFGIPasses(rgBuilder, ctx);
    RegisterSSGIPasses(rgBuilder, ctx);
    RegisterBrixelizerGIPasses(rgBuilder, ctx);
    RegisterSSRPasses(rgBuilder, ctx);
    
    // ── Env MIS temporal accumulation (render graph compute pass) ───────
    if (ctx.perFrame.sdfReflectionsEnabled == 0
        && m_envMISPass && m_envMISPass->IsInitialized()
        && m_envMapSampler && m_envMapSampler->IsReady()) {

        rgBuilder.AddPass("EnvMIS_Temporal", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.albedoRes,   RHIResourceState::ShaderResource);
            pb.Read(ctx.normalRes,   RHIResourceState::ShaderResource);
            pb.Read(ctx.depthRes,    RHIResourceState::ShaderResource);
            pb.Read(ctx.positionRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("EnvMIS::Execute");

                EnvMISPass::DispatchParams misParams;
                misParams.albedoSrvIdx     = m_gbuffer.albedoSRV;
                misParams.normalSrvIdx     = m_gbuffer.normalSRV;
                misParams.depthSrvIdx      = m_gbuffer.depthSRV;
                misParams.positionSrvIdx   = m_gbuffer.positionWSSRV;
                misParams.envCubemapSrvIdx = m_hdriActive ? m_hdriCubemapSrv : m_skyCubemapSrv;
                misParams.frameIndex       = ctx.frameIndex;
                misParams.temporalAlpha    = 0.05f;

                m_envMISPass->Execute(cmd, *m_envMapSampler, misParams);
            });
        });
    }

    // ── Import AP volume for render graph tracking ───────────────────
    // The AP volume is a persistent 32x32x16 3D texture. Deferred lighting
    // reads the PREVIOUS frame's data (1-frame latency, standard practice).
    // The atmosphere passes later in this frame fill the CURRENT frame's data.
    // Render graph handles UAV→SRV→UAV barriers automatically.
    if (m_apVolume.IsValid() && m_apVolumeSrv != UINT32_MAX) {
        ctx.apVolumeRes = rgBuilder.ImportTexture("APVolume",
            m_apVolume, RHIResourceState::UnorderedAccess);
    }

    RegisterDeferredLightingPass(rgBuilder, ctx);
    RegisterAtmosphereFogCloudPasses(rgBuilder, ctx);
    RegisterSSGIPrevColorCopy(rgBuilder, ctx);

    // Copy HDR scene color for SSR next-frame input (UE5 pattern: read prev TAA output).
    // Runs AFTER lighting+atmosphere so the copy contains the complete lit scene.
    // SSR trace next frame reads m_ssrPrevSceneColorSrv instead of m_hdrSceneColorSrv.
    // @ref UE5 DeferredShadingRenderer.cpp — QueueTextureExtraction for SSR input
    // CopyPrevColor moved to frame_postprocess.cpp (post-TAA) to avoid
    // barrier transitions on hdrSceneColor that cause TAA noise.
    if (false) {
        rgBuilder.AddPass("SSR_CopyPrevColor", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::RenderTarget);

            pb.SetExecutionCallback([this](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("SSR::CopyPrevColor");

                RHIBarrierDesc barriers[2]{};
                barriers[0].texture     = m_hdrSceneColor;
                barriers[0].stateBefore = RHIResourceState::RenderTarget;
                barriers[0].stateAfter  = RHIResourceState::CopySrc;
                barriers[1].texture     = m_ssrPrevSceneColor;
                barriers[1].stateBefore = RHIResourceState::ShaderResource;
                barriers[1].stateAfter  = RHIResourceState::CopyDst;
                cmd->ResourceBarrier(barriers, 2);

                cmd->CopyTexture(m_hdrSceneColor, m_ssrPrevSceneColor);

                RHIBarrierDesc restore[2]{};
                restore[0].texture     = m_hdrSceneColor;
                restore[0].stateBefore = RHIResourceState::CopySrc;
                restore[0].stateAfter  = RHIResourceState::RenderTarget;
                restore[1].texture     = m_ssrPrevSceneColor;
                restore[1].stateBefore = RHIResourceState::CopyDst;
                restore[1].stateAfter  = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(restore, 2);

                m_ssrHasPrevColor = true;
            });
        });
    }

    // ── PassInspector: register intermediate targets for visualization ──
    RegisterPassInspectorTargets(ctx);
}

// ── PassInspector Target Registration ───────────────────────────────────────
// Registers intermediate render targets so the PassInspector can visualize
// them in the debug UI. Must be called after all passes are registered since
// it needs to know which passes are active.

void SceneRenderer::RegisterPassInspectorTargets(FrameContext& ctx) {
    const uint32_t rw = static_cast<uint32_t>(ctx.renderWidth);
    const uint32_t rh = static_cast<uint32_t>(ctx.renderHeight);

    // GBuffer targets
    m_passInspector.RegisterTarget("gbuffer.albedo",    m_gbuffer.albedo,     m_gbuffer.albedoSRV,     VisDisplayMode::Color,     rw, rh);
    m_passInspector.RegisterTarget("gbuffer.normal",    m_gbuffer.normal,     m_gbuffer.normalSRV,     VisDisplayMode::Normal,    rw, rh);
    // GBuffer depth ends the frame in DepthStencilWrite (the render graph
    // does not emit restore barriers for imported depth resources).
    m_passInspector.RegisterTarget("gbuffer.depth",     m_gbuffer.depth,      m_gbuffer.depthSRV,      VisDisplayMode::Depth,     rw, rh,
                                    RHIResourceState::DepthStencilWrite);
    m_passInspector.RegisterTarget("gbuffer.velocity",  m_gbuffer.velocity,   m_gbuffer.velocitySRV,   VisDisplayMode::Velocity,  rw, rh);
    m_passInspector.RegisterTarget("gbuffer.emissive",  m_gbuffer.emissive,   m_gbuffer.emissiveSRV,   VisDisplayMode::Color,     rw, rh);
    // World-space position GBuffer (RGBA32F or RGBA16F xyz).
    if (m_gbuffer.positionWS.IsValid() && m_gbuffer.positionWSSRV != UINT32_MAX) {
        m_passInspector.RegisterTarget("gbuffer.position", m_gbuffer.positionWS, m_gbuffer.positionWSSRV,
                                        VisDisplayMode::Color, rw, rh);
    }

    // ── Channel splits of packed GBuffer RTs ─────────────────────────────
    // Single-channel views authored from packed RTs so the dump shows them
    // as distinct grayscale targets:
    //   gbuffer.ao        ← albedoAO.a
    //   gbuffer.metallic  ← normalMatPbr.b
    //   gbuffer.roughness ← normalMatPbr.a
    // Display unaffected — these alias the same texture handles.
    m_passInspector.RegisterTarget("gbuffer.ao",        m_gbuffer.albedo, m_gbuffer.albedoSRV,
                                    VisDisplayMode::Grayscale, rw, rh,
                                    RHIResourceState::ShaderResource, DumpChannel::A);
    m_passInspector.RegisterTarget("gbuffer.metallic",  m_gbuffer.normal, m_gbuffer.normalSRV,
                                    VisDisplayMode::Grayscale, rw, rh,
                                    RHIResourceState::ShaderResource, DumpChannel::B);
    m_passInspector.RegisterTarget("gbuffer.roughness", m_gbuffer.normal, m_gbuffer.normalSRV,
                                    VisDisplayMode::Grayscale, rw, rh,
                                    RHIResourceState::ShaderResource, DumpChannel::A);

    // Shadow atlas (tile resolution used as approximate display dimensions)
    if (m_shadowAtlas.GetTexture().IsValid() && m_shadowAtlas.GetSrvIndex() != UINT32_MAX) {
        // Shadow atlas ends the frame in DepthStencilWrite (the render graph
        // does not emit restore barriers for imported depth resources).
        m_passInspector.RegisterTarget("shadow.atlas", m_shadowAtlas.GetTexture(), m_shadowAtlas.GetSrvIndex(),
                                        VisDisplayMode::Grayscale, kShadowResolution, kShadowResolution,
                                        RHIResourceState::DepthStencilWrite);
    }

    // GTAO — use IsEnabledCVar() (live CVar query) instead of stale settings.enabled.
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->IsEnabledCVar()) {
        const uint32_t gtaoSrv = m_gtaoPass->GetFilteredAOSrvIndex();
        if (gtaoSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("gtao.result", m_gtaoPass->GetFilteredAOTexture(), gtaoSrv,
                                            VisDisplayMode::Grayscale, rw, rh);
        }
    }

    // SSGI intermediate textures
    const bool ssgiActive = m_ssgiRenderer.IsReady()
                          && ctx.perFrame.ssgiEnabled != 0
                          && m_device->IsBindless()
                          && m_passInspector.IsPassEnabled("ssgi");
    if (ssgiActive) {
        const uint32_t hw = m_ssgiRenderer.GetHalfWidth();
        const uint32_t hh = m_ssgiRenderer.GetHalfHeight();

        if (ctx.perFrame.sdfDiffuseSamples == 0 && m_ssgiRenderer.HasPreviousSceneColor()) {
            m_passInspector.RegisterTarget("ssgi.prev_color", m_ssgiRenderer.GetPrevColorTexture(),
                                        m_ssgiRenderer.GetPrevColorSrvIndex(), VisDisplayMode::Color, rw, rh);
        }
        m_passInspector.RegisterTarget("ssgi.trace", m_ssgiRenderer.GetTraceTexture(),
                                        m_ssgiRenderer.GetTraceSrvIndex(), VisDisplayMode::HDR, hw, hh);
        m_passInspector.RegisterTarget("ssgi.denoised", m_ssgiRenderer.GetDenoisedTexture(),
                                        m_ssgiRenderer.GetDenoisedSrvIndex(), VisDisplayMode::HDR, hw, hh);
        m_passInspector.RegisterTarget("ssgi.result", m_ssgiRenderer.GetResultTexture(),
                                        m_ssgiRenderer.GetResultSrvIndex(), VisDisplayMode::HDR, rw, rh);
        m_passInspector.RegisterTarget("ssgi.ao", m_ssgiRenderer.GetAOResultTexture(),
                                        m_ssgiRenderer.GetAOResultSrvForInspector(), VisDisplayMode::Grayscale, rw, rh);
    }

    // SSR
    if (ctx.ssrActive) {
        m_passInspector.RegisterTarget("ssr.trace", m_ssrTraceOutput, m_ssrTraceOutputSrv,
                                        VisDisplayMode::Color, rw, rh);
        m_passInspector.RegisterTarget("ssr.resolve", m_ssrResolvedOutput, m_ssrResolvedOutputSrv,
                                        VisDisplayMode::Color, rw, rh);
        // Register the temporal history write target (the one lighting reads from).
        // After the temporal pass flips the index, the *previous* write idx is
        // what was written this frame. At this point m_ssrHistoryIndex has been
        // flipped already (flip happens during pass registration), so the
        // texture that was WRITTEN this frame is at (1 - m_ssrHistoryIndex).
        const uint32_t ssrWrittenIdx = 1 - m_ssrHistoryIndex;
        if (m_ssrHistory[ssrWrittenIdx].IsValid()
            && m_ssrHistorySrv[ssrWrittenIdx] != UINT32_MAX) {
            m_passInspector.RegisterTarget("ssr.temporal", m_ssrHistory[ssrWrittenIdx],
                                            m_ssrHistorySrv[ssrWrittenIdx],
                                            VisDisplayMode::Color, rw, rh);
        }
    }

    // Brixelizer GI outputs
    if (m_brixelizerGI && m_brixelizerGI->IsInitialized()
        && m_brixelizerGI->GetDiffuseGISrv() != UINT32_MAX) {
        m_passInspector.RegisterTarget("brixelizer.diffuse",
                                        m_brixelizerGI->GetDiffuseGITexture(),
                                        m_brixelizerGI->GetDiffuseGISrv(),
                                        VisDisplayMode::HDR, rw, rh);
        m_passInspector.RegisterTarget("brixelizer.specular",
                                        m_brixelizerGI->GetSpecularGITexture(),
                                        m_brixelizerGI->GetSpecularGISrv(),
                                        VisDisplayMode::HDR, rw, rh);
    }

    // ── Lighting debug MRT targets (direct / indirect / shadow+AO) ────
    if (m_debugDirectLight.IsValid() && m_debugDirectLightSrv != UINT32_MAX) {
        m_passInspector.RegisterTarget("lighting.direct", m_debugDirectLight, m_debugDirectLightSrv,
                                        VisDisplayMode::HDR, rw, rh);
    }
    if (m_debugIndirectLight.IsValid() && m_debugIndirectLightSrv != UINT32_MAX) {
        m_passInspector.RegisterTarget("lighting.indirect", m_debugIndirectLight, m_debugIndirectLightSrv,
                                        VisDisplayMode::HDR, rw, rh);
    }
    if (m_debugShadowMask.IsValid() && m_debugShadowMaskSrv != UINT32_MAX) {
        m_passInspector.RegisterTarget("lighting.shadow", m_debugShadowMask, m_debugShadowMaskSrv,
                                        VisDisplayMode::Grayscale, rw, rh);
    }

    // HDR scene color (after lighting + atmosphere, before postprocess)
    m_passInspector.RegisterTarget("hdr", m_hdrSceneColor, m_hdrSceneColorSrv,
                                    VisDisplayMode::HDR, rw, rh);

    // DFS output
    const bool dfsEnabled = m_dfsConfig.enabled
                          && m_sdfAtlas && m_sdfAtlas->IsInitialized()
                          && m_sdfAtlas->GetRegisteredObjectCount() > 0
                          && m_dfsOutputTexture.IsValid()
                          && m_dfsCulledIndicesBuffer.IsValid()
                          && m_dfsIndirectArgsBuffer.IsValid()
                          && m_passInspector.IsPassEnabled("dfs");
    if (dfsEnabled && m_dfsOutputSrvIndex != UINT32_MAX) {
        const uint32_t dfsW = m_dfsConfig.fullResolution ? rw : rw / 2;
        const uint32_t dfsH = m_dfsConfig.fullResolution ? rh : rh / 2;
        m_passInspector.RegisterTarget("dfs.result", m_dfsOutputTexture, m_dfsOutputSrvIndex,
                                        VisDisplayMode::Grayscale, dfsW, dfsH);
    }

    // Atmosphere LUTs
    if (m_transmittanceLut.IsValid() && m_transmittanceLutSrv != UINT32_MAX && m_transmittanceLutFilled) {
        m_passInspector.RegisterTarget("atmo.transmittance", m_transmittanceLut, m_transmittanceLutSrv,
                                        VisDisplayMode::Color, 256, 64);
    }
    if (m_multiScatterLut.IsValid() && m_multiScatterLutSrv != UINT32_MAX && m_multiScatterLutFilled) {
        m_passInspector.RegisterTarget("atmo.multiscatter", m_multiScatterLut, m_multiScatterLutSrv,
                                        VisDisplayMode::Color, 32, 32);
    }
    if (m_apVolume.IsValid() && m_apVolumeSrv != UINT32_MAX) {
        m_passInspector.RegisterTarget("atmo.ap_volume", m_apVolume, m_apVolumeSrv,
                                        VisDisplayMode::HDR, 32, 32);
    }
    if (m_skyCubemap.IsValid() && m_skyCubemapSrv != UINT32_MAX) {
        m_passInspector.RegisterTarget("atmo.sky_cubemap", m_skyCubemap, m_skyCubemapSrv,
                                        VisDisplayMode::HDR, 64, 64);
    }

    // Volumetric fog (godrays / light shafts) — froxel inscattering volume
    // and the integrated march. Lets the dump show whether volumetric scatter
    // is producing the expected light shaft pattern from the directional sun.
    if (m_volFogPass && m_volFogPass->GetInscatteringSrv() != UINT32_MAX) {
        m_passInspector.RegisterTarget("volfog.inscattering",
                                        m_volFogPass->GetInscatteringTexture(),
                                        m_volFogPass->GetInscatteringSrv(),
                                        VisDisplayMode::HDR, 160, 90);
    }
    if (m_volFogPass && m_volFogPass->GetIntegratedSrv() != UINT32_MAX) {
        m_passInspector.RegisterTarget("volfog.integrated",
                                        m_volFogPass->GetIntegratedTexture(),
                                        m_volFogPass->GetIntegratedSrv(),
                                        VisDisplayMode::HDR, 160, 90);
    }

    // Cloud output (half-res RGBA16F) — register both ping-pong buffers for debugging
    if (m_cloudPass && m_cloudPass->IsInitialized() && m_cloudOutputSrv != UINT32_MAX) {
        m_passInspector.RegisterTarget("cloud.output", m_cloudPass->GetCloudOutputTexture(),
                                        m_cloudOutputSrv, VisDisplayMode::HDR,
                                        rw / 2, rh / 2);
        // DEBUG: also register both raw ping-pong textures
        m_passInspector.RegisterTarget("cloud.raw0", m_cloudPass->GetDebugRaymarchOutput(0),
                                        m_cloudPass->GetDebugRaymarchOutputSrv(0), VisDisplayMode::HDR,
                                        rw / 2, rh / 2);
        m_passInspector.RegisterTarget("cloud.raw1", m_cloudPass->GetDebugRaymarchOutput(1),
                                        m_cloudPass->GetDebugRaymarchOutputSrv(1), VisDisplayMode::HDR,
                                        rw / 2, rh / 2);
    }

    // HiZ pyramid mip 0 (half-res R32F depth min)
    if (m_hizMipCount > 0 && m_hizMipLevels[0].srvIndex != UINT32_MAX) {
        m_passInspector.RegisterTarget("hiz.mip0", m_hizMipLevels[0].texture, m_hizMipLevels[0].srvIndex,
                                        VisDisplayMode::Grayscale, m_hizMipLevels[0].width, m_hizMipLevels[0].height);
    }

    // TAA history (current write target)
    if (m_taaHistory[m_taaHistoryIndex].IsValid() && m_taaHistorySrv[m_taaHistoryIndex] != UINT32_MAX) {
        m_passInspector.RegisterTarget("taa.history", m_taaHistory[m_taaHistoryIndex],
                                        m_taaHistorySrv[m_taaHistoryIndex],
                                        VisDisplayMode::Color, rw, rh);
    }

    // DDGI octahedral atlases (DXR probe update). Registered only once the
    // first full atlas write completed — before that they hold their clear
    // value and would report a misleading all-zero range.
    if (m_ddgiSystem.IsInitialized() && m_ddgiSystem.HasCompletedFirstUpdate()) {
        if (m_ddgiSystem.GetIrradianceTexture().IsValid()
            && m_ddgiSystem.GetIrradianceSrv() != UINT32_MAX) {
            m_passInspector.RegisterTarget("gi.ddgi_irradiance",
                                           m_ddgiSystem.GetIrradianceTexture(),
                                           m_ddgiSystem.GetIrradianceSrv(),
                                           VisDisplayMode::HDR,
                                           m_ddgiSystem.GetIrradianceAtlasWidth(),
                                           m_ddgiSystem.GetIrradianceAtlasHeight());
        }
        if (m_ddgiSystem.GetDepthTexture().IsValid()
            && m_ddgiSystem.GetDepthSrv() != UINT32_MAX) {
            m_passInspector.RegisterTarget("gi.ddgi_depth",
                                           m_ddgiSystem.GetDepthTexture(),
                                           m_ddgiSystem.GetDepthSrv(),
                                           VisDisplayMode::Grayscale,
                                           m_ddgiSystem.GetDepthAtlasWidth(),
                                           m_ddgiSystem.GetDepthAtlasHeight());
        }
    }

    // Global Distance Field cascade 0
    if (m_sdfGiEnabled) {
        const auto& clip = m_globalDistanceField.GetClipmap(0);
        if (clip.hasData && clip.texture.IsValid() && clip.srvIndex != UINT32_MAX) {
            const auto desc = m_device->GetTextureDesc(clip.texture);
            m_passInspector.RegisterTarget("gi.gdf_cascade0", clip.texture, clip.srvIndex,
                                            VisDisplayMode::Grayscale, desc.width, desc.height);
            if (clip.rawTexture.IsValid() && clip.rawSrvIndex != UINT32_MAX) {
                const auto rawDesc = m_device->GetTextureDesc(clip.rawTexture);
                m_passInspector.RegisterTarget("gi.gdf_raw_cascade0", clip.rawTexture, clip.rawSrvIndex,
                                               VisDisplayMode::Grayscale, rawDesc.width, rawDesc.height);
            }
        }
    }
}

} // namespace uz
