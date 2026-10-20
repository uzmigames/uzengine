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
#include "gi/radiance_cache.h"
#include "raytracing/rt_manager.h"
#include "gpu_mesh.h"
#include "vertex.h"
#include <cstring>
#include "world.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "atmosphere_renderer.h"
#include "volumetric_fog_pass.h"
#include "pathtracing/path_tracer_pass.h"
#include "pathtracing/pipeline_path_tracer_pass.h"
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
// r.Fog, r.Clouds and r.SSGI are registered where they are owned (fog_renderer.cpp, cloud_pass.cpp,
// ssgi_renderer.cpp); the copies that lived here were never read (removed 2026-10-08).
// r.SSR.MaxRoughness / NumSteps / StartMipLevel / SlopeToleranceScale are registered where they are read
// (frame_gi_passes.cpp); the copies that lived here were never read (block A.8 list, removed 2026-10-08).
// SSGI
// DFS (Distance Field Shadows)
static auto* cv_DFS              = UZ_CVAR_BOOL("r.DFS", false,
    "Distance Field Shadows (NOT IMPLEMENTED: the dfs_*_cs.hlsl shaders are empty scaffolds and the lighting "
    "shader never reads dfsOutputSrvIndex; left off so the passes are not dispatched)");
// Fog
// Clouds
// Post-process: read into m_casSharpenStrength; no CAS pass is registered (see the end of frame_postprocess.cpp)
static auto* cv_CASSharpness     = UZ_CVAR_FLOAT("r.CAS.Sharpness", 0.2f, "CAS sharpening strength (inert: CAS pass not wired)", 0.f, 1.f);
static auto* cv_LODBias          = UZ_CVAR_FLOAT("r.LOD.Bias", 1.f, "Global LOD bias multiplier", 0.1f, 10.f);
// Debug
static auto* cv_DebugView        = UZ_CVAR_BOOL("r.DebugView", false, "GBuffer debug visualization");
// Path Tracer — runtime mode toggle. When enabled, replaces deferred lighting,
// GI, and shadow passes with a Monte Carlo path tracer pass that writes to
// hdrSceneColor. Bloom/exposure/tonemap downstream remain unchanged.
// @ref Kajiya 1986 "The Rendering Equation" — see docs/analysis/path-tracing/
static auto* cv_PathTracer       = UZ_CVAR_BOOL("r.PathTracer.Enable", false,
    "Compute path tracer runtime mode (replaces deferred lighting + GI + shadows)");
static auto* cv_PipelinePathTracer = UZ_CVAR_BOOL("r.PathTracer.Pipeline", false,
    "DXR pipeline single-bounce reference renderer (--path-trace)");

namespace uz {

void SceneRenderer::RegisterLightingPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    if (m_renderPath != RenderPath::Deferred) return;

    if (cv_PipelinePathTracer->value >= 0.5f) {
        if (!m_rtManager || !m_rtManager->IsEnabled()) {
            UZ_LOG(Renderer, Error, "PipelinePathTracer: --path-trace requires an enabled DXR TLAS");
            return;
        }
        if (!m_pipelinePathTracer) {
            m_pipelinePathTracer = std::make_unique<PipelinePathTracerPass>();
            if (!m_pipelinePathTracer->Init(m_device)) {
                m_pipelinePathTracer.reset();
                return;
            }
        }
        // Update the environment before tracing. The radiance resolve must be
        // the final lighting write, including miss pixels, not a raster sky overlay.
        RegisterAtmosphereFogCloudPasses(rgBuilder, ctx);
        const uint32_t skySrv = m_hdriActive ? m_hdriCubemapSrv
                             : (m_skyCubemapFilled ? m_skyCubemapSrv : UINT32_MAX);
        NrdInputs denoiserGuides{};
        denoiserGuides.depth = ctx.depthRes;
        denoiserGuides.normal = ctx.normalRes;
        denoiserGuides.velocity = ctx.velocityRes;
        denoiserGuides.reset = !ctx.temporal.matrixValid;
        if (!m_pipelinePathTracer->AddPass(rgBuilder, ctx.hdrRes, ctx.perFrameAlloc,
                *m_rtManager, ctx.cmdList, ctx.drawList.GetCommands(), m_meshes, skySrv,
                ctx.frameIndex, static_cast<uint32_t>(ctx.renderWidth),
                static_cast<uint32_t>(ctx.renderHeight), ctx.world,
                ctx.unjitteredViewProj, ctx.updateFlags.flags & ~SceneUpdateFlags::kCamera,
                denoiserGuides)) {
            UZ_LOG(Renderer, Error, "PipelinePathTracer: failed to register single-bounce pass");
            return;
        }
        return;
    }

    // ── Path Tracer runtime mode override ──────────────────────────────
    // When r.PathTracer.Enable=1, the path tracer replaces the entire
    // deferred lighting + GI + shadow chain. The pass writes the lit HDR
    // result directly to ctx.hdrRes; bloom/exposure/tonemap downstream
    // consume it unchanged.
    // @ref Kajiya 1986 "The Rendering Equation"
    if (cv_PathTracer->value >= 0.5f) {
        const uint32_t resetGeneration = GetPathTracerResetGeneration();
        const bool accumulate = CVarRegistry::Get().GetBool("r.PathTracer.Accumulate", true);
        if (resetGeneration != m_ptResetGeneration || !accumulate) {
            m_ptAccumSamples = 0;
            m_ptHistoryValid = false;
            m_ptResetGeneration = resetGeneration;
        }
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

        // Track both ping-pong textures, including the previous accumulation's bindless read.
        rg::RgResourceId accumCurrRes = rgBuilder.ImportTrackedTexture("PT_AccumCurr",
            m_ptAccum[curr]);
        rg::RgResourceId accumPrevRes = rgBuilder.ImportTrackedTexture("PT_AccumPrev",
            m_ptAccum[prev]);

        // The reference path tracer consumes GDF clipmaps for bounce-hit
        // normals — keep the SDF GI chain (GDF composite + probe traces)
        // running in PT mode so the clipmaps hold real distances.
        RegisterSDFGIPasses(rgBuilder, ctx);

        PathTracerPassDesc ptDesc{};
        ptDesc.hdrTarget         = ctx.hdrRes;
        ptDesc.accumCurr         = accumCurrRes;
        ptDesc.accumPrev         = accumPrevRes;
        ptDesc.normalGBuffer     = ctx.normalRes;
        ptDesc.depthGBuffer      = ctx.depthRes;
        ptDesc.albedoGBuffer     = ctx.albedoRes;
        ptDesc.positionGBuffer   = ctx.positionRes;
        ptDesc.emissiveGBuffer   = ctx.emissiveRes;
        ptDesc.shadowAtlas       = ctx.shadowAtlasRes;
        ptDesc.perFrameAlloc     = ctx.perFrameAlloc;
        ptDesc.pipeline          = ctx.pipelines->GetPathTracer();
        ptDesc.normalSrvIndex    = m_gbuffer.normalSRV;
        ptDesc.depthSrvIndex     = m_gbuffer.depthSRV;
        ptDesc.albedoSrvIndex    = m_gbuffer.albedoSRV;
        ptDesc.positionSrvIndex  = m_gbuffer.positionWSSRV;
        ptDesc.emissiveSrvIndex  = m_gbuffer.emissiveSRV;
        // One sky SRV for every PT sky read (RT-off fallback, specular miss, bounce miss):
        // HDRI when active, else the atmosphere capture once filled, else the shader's flat-
        // skylight sentinel. Set independent of the TLAS. The m_skyCubemapFilled guard keeps
        // frame 0 (pre-capture, allocator-recycled memory) on the sentinel (phase0
        // frame-allocation-layout-dependence).
        const uint32_t ptSkySrv  = m_hdriActive ? m_hdriCubemapSrv
                                 : (m_skyCubemapFilled ? m_skyCubemapSrv : UINT32_MAX);
        ptDesc.skyCubeSrvIndex   = ptSkySrv;
        ptDesc.skyCubemapSrv     = ptSkySrv;
        // Raw equirect HDRI for direct env sampling (Falcor-equivalent single bilinear tap);
        // UINT32_MAX = none, PT falls back to the sky cubemap.
        ptDesc.equirectSrvIndex  = m_hdriActive ? m_hdriEquirectSrv : UINT32_MAX;
        // Bind only the active HDRI's built sampler; otherwise retain the BSDF-only defaults.
        if (m_hdriActive && m_envMapSampler && m_envMapSampler->IsReady()) {
            ptDesc.envAliasSrv = m_envMapSampler->GetAliasSrv();
            ptDesc.envLumMip0Srv = m_envMapSampler->GetLuminanceMip0Srv();
            const float totalLuminance = m_envMapSampler->GetTotalLuminance();
            std::memcpy(&ptDesc.envTotalLumBits, &totalLuminance, sizeof(uint32_t));
            ptDesc.envLumWidth = m_envMapSampler->GetLuminanceWidth();
            ptDesc.envLumHeight = m_envMapSampler->GetLuminanceHeight();
        }
        ptDesc.accumPrevSrvIndex = m_ptAccumSrv[prev];
        ptDesc.sampleCount       = m_ptAccumSamples;
        ptDesc.frameIndex        = ctx.frameIndex;
        auto& ptCvars = CVarRegistry::Get();
        ptDesc.maxBounces        = static_cast<uint32_t>(ptCvars.GetInt("r.PathTracer.MaxBounces", 4));
        ptDesc.samplesPerFrame   = static_cast<uint32_t>(ptCvars.GetInt("r.PathTracer.SamplesPerFrame", 4));
        ptDesc.clampFirefly      = ptCvars.GetFloat("r.PathTracer.ClampFirefly", 64.f);
        ptDesc.envIntensity      = ptCvars.GetFloat("r.PathTracer.EnvIntensity", 1.f);
        ptDesc.rrEnabled         = ptCvars.GetBool("r.PathTracer.RussianRoulette", true) ? 1u : 0u;
        ptDesc.rrMinBounce       = static_cast<uint32_t>(ptCvars.GetInt("r.PathTracer.RussianRouletteMinBounce", 2));
        ptDesc.shadowAlloc       = ctx.shadowAlloc;
        ptDesc.bindless          = ctx.bindless;

        // The shared b2 trace constants (GDF hit normals and albedo, sun, the
        // skylight colour × intensity every sky read is scaled by) are bound on
        // every bindless frame — the shader reads the skylight scale even
        // without a TLAS. The CB is built inside the pass execution callback:
        // at registration time the PerFrameCB is not populated yet.
        if (ctx.bindless) {
            ptDesc.giCbRing = m_cbRing.get();
            ptDesc.gdf      = &m_globalDistanceField;
        }
        // Real multi-bounce transport: TLAS + the material table. Without a
        // TLAS the shader falls back to the occlusion-free sky estimate.
        // `IsReady()` alone admits an EMPTY TLAS: with the `r.RayTracing`
        // master switch off RTManager never rebuilds it, so every ray would
        // miss (the DDGI writer gates the same way).
        if (m_rtManager && m_rtManager->IsReady() && m_rtManager->IsEnabled()
            && m_rtManager->GetTLASInstanceCount() > 0u) {
            ptDesc.tlasSrvIndex     = m_rtManager->GetTLASSRVIndex();
            ptDesc.materialTableSrv = UpdateGIMaterialTable(ctx);
        }
        ptDesc.viewportWidth     = static_cast<float>(ctx.renderWidth);
        ptDesc.viewportHeight    = static_cast<float>(ctx.renderHeight);
        AddPathTracerPass(rgBuilder, ptDesc);

        // Advance ping-pong + sample counter for next frame.
        m_ptAccumIdx     = prev;
        m_ptAccumSamples = accumulate ? std::min(m_ptAccumSamples + 1u, 65535u) : 0u;

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
    RegisterRTReflectionPass(rgBuilder, ctx);   // opt-in HWRT fill of the SSR holes, or the reflections on their own with SSR off (r.RayTracing.Reflections)
    
    // ── Env MIS temporal accumulation (render graph compute pass) ───────
    if (ctx.perFrame.sdfReflectionsEnabled == 0 && m_envMisEnabled
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
                misParams.perFrameBuffer   = ctx.perFrameAlloc.bufferHandle;
                misParams.perFrameOffset   = ctx.perFrameAlloc.offsetBytes;
                misParams.perFrameCpu      = ctx.perFrameAlloc.cpuPtr;
                misParams.perFrameSize     = ctx.perFrameAlloc.sizeBytes;

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
        ctx.apVolumeRes = rgBuilder.ImportTrackedTexture("APVolume", m_apVolume);
    }

    RegisterCloudShadowPasses(rgBuilder, ctx);
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

// ── GI material table (reference path tracer bounce-hit albedo) ──

uint32_t SceneRenderer::UpdateGIMaterialTable(const FrameContext& ctx) {
    // Indexed by TLAS InstanceID (= meshId). Refreshed every frame from the
    // draw list. Raw buffer aliases preserve the structured views used by
    // raster vertex fetch while exposing byte-exact triangle attributes.
    const uint32_t meshCap = static_cast<uint32_t>(m_meshes.size());
    if (meshCap > 0
        && (!m_giMaterialTable.IsValid()
            || m_giMaterialTableCapacity < meshCap)) {
        if (m_giMaterialTable.IsValid()) {
            m_device->DestroyBuffer(m_giMaterialTable);
            m_giMaterialTable = {};
            m_giMaterialTableSrv = UINT32_MAX;
        }
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(meshCap)
                       * sizeof(GIMeshMaterialGPU);
        desc.stride    = sizeof(GIMeshMaterialGPU);
        desc.usage     = RHIBufferUsage::StructuredBuffer;
        desc.memory    = RHIMemoryType::Upload;
        desc.debugName = "GI_MaterialTable";
        m_giMaterialTable = m_device->CreateBuffer(desc);
        m_giMaterialTableSrv = m_giMaterialTable.IsValid()
            ? m_device->GetBufferSRVIndex(m_giMaterialTable)
            : UINT32_MAX;
        m_giMaterialTableCapacity = meshCap;
    }
    if (m_giMaterialTable.IsValid()) {
        auto* entries = static_cast<GIMeshMaterialGPU*>(
            m_device->MapBuffer(m_giMaterialTable));
        if (entries) {
            for (uint32_t e = 0; e < meshCap; ++e) {
                entries[e] = {};
            }
            for (const auto& dc : ctx.drawList.GetCommands()) {
                if (dc.meshId >= meshCap) continue;
                auto& entry = entries[dc.meshId];
                entry.tintR = dc.baseColorTintR;
                entry.tintG = dc.baseColorTintG;
                entry.tintB = dc.baseColorTintB;
                // Slot 0 doubles as "unset" in DrawCommand.
                entry.albedoSrv =
                    (dc.albedoSrvIndex != 0
                     && dc.albedoSrvIndex != UINT32_MAX)
                        ? dc.albedoSrvIndex : UINT32_MAX;
                const auto& mesh = m_meshes[dc.meshId];
                const bool packed = mesh.vertexStride == sizeof(VertexPacked);
                if (mesh.vbSrvIndex != UINT32_MAX && mesh.IsIndexed()
                    && (packed || mesh.vertexStride == sizeof(Vertex)
                               || mesh.vertexStride == sizeof(SkinnedVertex))) {
                    entry.vbSrv = m_device->GetBufferRawSRVIndex(mesh.vertexBuffer);
                    entry.ibSrv = m_device->GetBufferRawSRVIndex(mesh.indexBuffer);
                    entry.vertexStride = mesh.vertexStride;
                    entry.uvOffset = packed ? offsetof(VertexPacked, texcoord)
                                            : offsetof(Vertex, texcoord);
                    entry.normalOffset = packed ? offsetof(VertexPacked, packedNormal)
                                                : offsetof(Vertex, normal);
                    entry.indexFormat = mesh.indexFormat == RHIIndexFormat::Uint16 ? 2u : 4u;
                    entry.normalFormat = packed ? 1u : 0u;
                }
            }
            m_device->UnmapBuffer(m_giMaterialTable);
        }
    }
    return m_giMaterialTableSrv;
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
    // RGB statistics: RT3.a packs specular + 16 x shading model (gbuffer_ps.hlsl), not an emissive quantity.
    m_passInspector.RegisterTarget("gbuffer.emissive",  m_gbuffer.emissive,   m_gbuffer.emissiveSRV,   VisDisplayMode::Color,     rw, rh,
                                    RHIResourceState::ShaderResource, DumpChannel::RGB);
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
                                        VisDisplayMode::Grayscale, m_shadowAtlas.GetResolution(), m_shadowAtlas.GetResolution(),
                                        RHIResourceState::DepthStencilWrite);
    }

    // GTAO — use IsEnabledCVar() (live CVar query) instead of stale settings.enabled.
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->IsEnabledCVar()) {
        const uint32_t gtaoSrv = m_gtaoPass->GetFilteredAOSrvIndex();
        if (gtaoSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("gtao.result", m_gtaoPass->GetFilteredAOTexture(), gtaoSrv,
                                            VisDisplayMode::Grayscale, rw, rh);
        }
    } else {
        // Keep the target visible in --diag as SKIPPED instead of absent.
        m_passInspector.RegisterDisabledTarget("gtao.result");
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
    } else {
        m_passInspector.RegisterDisabledTarget("ssgi.result");
    }

    // ── World-space GI probe systems (radiance cache + DDGI atlases) ─────
    // Registered unconditionally so `--diag` distinguishes "disabled" from
    // "absent" — the entire GI stack being silently off is exactly the bug
    // class this visibility exists to catch.
    {
        auto& cvars = CVarRegistry::Get();

        // Radiance cache stores L2 SH in a StructuredBuffer — registered as a
        // BUFFER target so --diag reads real min/max/avg probe statistics
        // (all-zero probes = trace not converging/running).
        if (cvars.GetBool("r.RadianceCache", false)
            && m_radianceCache.IsInitialized()
            && m_radianceCache.GetProbeSHBuffer().IsValid()) {
            m_passInspector.RegisterBufferTarget("rc.probes",
                                                  m_radianceCache.GetProbeSHBuffer(),
                                                  m_radianceCache.GetProbeSHBufferSize());
        } else {
            m_passInspector.RegisterDisabledTarget("rc.probes");
        }

        const bool ddgiActive = cvars.GetBool("r.RayTracing.GI", false)
                              && m_ddgiSystem.IsInitialized();
        if (ddgiActive) {
            m_passInspector.RegisterTarget("ddgi.irradiance",
                                            m_ddgiSystem.GetIrradianceTexture(),
                                            m_ddgiSystem.GetIrradianceSrv(),
                                            VisDisplayMode::HDR,
                                            m_ddgiSystem.GetIrradianceAtlasWidth(),
                                            m_ddgiSystem.GetIrradianceAtlasHeight());
            m_passInspector.RegisterTarget("ddgi.depth",
                                            m_ddgiSystem.GetDepthTexture(),
                                            m_ddgiSystem.GetDepthSrv(),
                                            VisDisplayMode::Grayscale,
                                            m_ddgiSystem.GetDepthAtlasWidth(),
                                            m_ddgiSystem.GetDepthAtlasHeight());
        } else {
            m_passInspector.RegisterDisabledTarget("ddgi.irradiance");
        }
    }

    // SSR
    if (ctx.ssrActive) {
        m_passInspector.RegisterTarget("ssr.trace", m_ssrTraceOutput, m_ssrTraceOutputSrv,
                                        VisDisplayMode::Color, rw, rh);
    }
    // The resolved buffer: the SSR chain's output, or (stage 3, SSR off) the ray-traced reflections'.
    if (ctx.ssrActive || RTReflectionsStandalone()) {
        m_passInspector.RegisterTarget("ssr.resolve", m_ssrResolvedOutput, m_ssrResolvedOutputSrv,
                                        VisDisplayMode::Color, rw, rh);
    }
    if (ctx.ssrActive) {
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
                                        VisDisplayMode::HDR, rw, rh, RHIResourceState::ShaderResource, DumpChannel::RGB);
    }
    if (m_debugIndirectLight.IsValid() && m_debugIndirectLightSrv != UINT32_MAX) {
        m_passInspector.RegisterTarget("lighting.indirect", m_debugIndirectLight, m_debugIndirectLightSrv,
                                        VisDisplayMode::HDR, rw, rh, RHIResourceState::ShaderResource, DumpChannel::RGB);
    }
    if (m_debugShadowMask.IsValid() && m_debugShadowMaskSrv != UINT32_MAX) {
        // float4(shadow, ao, 0, 1): the statistics read the shadow alone.
        m_passInspector.RegisterTarget("lighting.shadow", m_debugShadowMask, m_debugShadowMaskSrv,
                                        VisDisplayMode::Grayscale, rw, rh, RHIResourceState::ShaderResource, DumpChannel::R);
    }

    // HDR scene color (after lighting + atmosphere, before postprocess)
    // RGB statistics: the alpha is a constant 1 and pinned every max at 1.000 (followup-06 4.3).
    m_passInspector.RegisterTarget("hdr", m_hdrSceneColor, m_hdrSceneColorSrv,
                                    VisDisplayMode::HDR, rw, rh, RHIResourceState::ShaderResource, DumpChannel::RGB);

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

    // Cloud output (half-res RGBA16F).
    if (m_cloudPass && m_cloudPass->IsInitialized() && m_cloudPass->GetCloudOutputSrv() != UINT32_MAX) {
        m_passInspector.RegisterTarget("cloud.output", m_cloudPass->GetCloudOutputTexture(),
                                        m_cloudPass->GetCloudOutputSrv(), VisDisplayMode::HDR,
                                        (rw + 1) / 2, (rh + 1) / 2);
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
