/// UzEngine Scene Renderer — RenderFrame (main per-frame orchestration).
///
/// This file contains only the top-level `RenderFrame()` method, which
/// delegates to helper methods defined in `frame/*.cpp`:
///   - SetupFrameData()          → frame/frame_setup.cpp
///   - RegisterShadowPasses()    → frame/frame_shadows.cpp
///   - RegisterGeometryPasses()  → frame/frame_geometry.cpp
///   - RegisterLightingPasses()  → frame/frame_lighting.cpp
///   - RegisterPostProcessPasses() → frame/frame_postprocess.cpp
///   - RegisterDecalPasses()     → frame/frame_decals.cpp
///   - RegisterOverlayPasses()   → frame/frame_overlay.cpp

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/asset/vfx_hot_reload.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

// Individual includes needed by post-frame work
#include "camera.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "sh_projection.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"

#include <cstdio>
#include <cstring>

namespace uz {

void SceneRenderer::RenderFrame(IRHICommandList* cmdList, IRHISwapChain* swapChain,
                                 World* world, IVFS* vfs, float dt, uint32_t frameIndex) {
    if (!m_device || !cmdList || !swapChain) return;

    UZ_PROFILE_FUNCTION();

    // ── Begin command list ──────────────────────────────────────────────
    cmdList->Begin();

    // ── GPU Profiler: begin frame ───────────────────────────────────────
    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerBeginFrame(cmdList);
    }

    // ── Build render graph ─────────────────────────────────────────────
    rg::RgBuilder rgBuilder(m_device, m_rgPool.get());
    rgBuilder.SetGpuProfileEnabled(m_gpuProfileEnabled);

    // ── Setup all per-frame state (camera, CBs, draw list, instancing) ──
    FrameContext ctx{};
    ctx.cmdList    = cmdList;
    ctx.swapChain  = swapChain;
    ctx.world      = world;
    ctx.vfs        = vfs;
    ctx.dt         = dt;
    ctx.frameIndex = frameIndex;
    ctx.gizmoModeRaw    = m_gizmoMode;
    ctx.gizmoSpaceRaw   = m_gizmoSpace;
    ctx.gizmoHoveredAxis = m_gizmoHoveredAxis;
    ctx.gizmoActiveAxis  = m_gizmoActiveAxis;
    SetupFrameData(ctx, rgBuilder);

    // ── Volumetric Cloud Compute (inline on main command list) ─────────
    // Noise textures are loaded from disk (precomputed .bin files).
    // Weather/raymarch/temporal run as compute dispatches directly on the
    // main command list before the render graph. The cloud pass manages
    // its own barriers internally (SRV→UAV→SRV on the output texture),
    // so no external synchronization is needed.
    if (m_pendingCloudDispatch && m_cloudPass && m_cloudPass->IsInitialized()
        && m_pendingCloudInput) {

        if (m_gpuProfileEnabled) {
            m_device->GpuProfilerBeginTimestamp(cmdList, "CloudCompute");
        }

        m_cloudPass->SetVFS(vfs);
        m_cloudPass->Execute(cmdList, *m_cbRing, *m_pendingCloudInput);

        if (m_gpuProfileEnabled) {
            m_device->GpuProfilerEndTimestamp(cmdList, "CloudCompute");
        }

        m_cloudOutputSrv = m_cloudPass->GetCloudOutputSrv();
        m_pendingCloudDispatch = false;
    }

    // ── VFX Emitter Sync ───────────────────────────────────────────────
    // Poll VFX hot-reload (.uzfx/.uzvfx file changes) — dev builds only.
#if !defined(NDEBUG)
    if (m_vfxHotReload) m_vfxHotReload->Poll();
#endif

    // Create/update ParticleEmitter instances from VfxEmitterComponent ECS
    // entities. Must run before VFXSystem::Tick() so newly created emitters
    // participate in spawn counting for this frame.
    SyncVfxEmitters(world, vfs);

    // ── VFX Tick ──────────────────────────────────────────────────────────
    // Calculates per-emitter spawn counts for this frame. Must run before
    // RegisterParticlePasses(), which consumes GetActiveEmitters().
    // The override pointer (non-owning) takes precedence over the owned system
    // so that external emitter pools managed by the game layer work correctly.
    {
        VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
        if (activeVFX) {
            activeVFX->Tick(dt);
        }
    }

    // ── Register render graph passes in pipeline order ──────────────────
    RegisterShadowPasses(rgBuilder, ctx);
    RegisterGeometryPasses(rgBuilder, ctx);
    RegisterDecalPasses(rgBuilder, ctx);
    RegisterLightingPasses(rgBuilder, ctx);
    RegisterParticlePasses(rgBuilder, ctx);
    RegisterPostProcessPasses(rgBuilder, ctx);
    RegisterOverlayPasses(rgBuilder, ctx);

    // ── Compile + execute render graph ──────────────────────────────────
    // Per-pass GPU timestamps are handled inside RgBuilder::Execute()
    // when m_gpuProfileEnabled is set (wraps each pass callback).
    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // Shadow Atlas Diagnostic Readback (shadowDebugMode >= 2).
    // Currently disabled — requires render graph pass integration for safe
    // barrier management. Use RenderDoc/PIX to inspect the shadow atlas.
    // The readback infrastructure is in PerformShadowAtlasReadback().

    // HDR target now contains valid scene color for next frame's auto-exposure readback.
    m_hdrHasContent = true;

    // ── IBL bake + SH projection — delegated to atmosphere_renderer ────
    // Must run after the render graph has executed (sky cubemap capture is
    // a render graph pass, but the IBL bake runs as a standalone compute
    // dispatch afterward).
    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerBeginTimestamp(cmdList, "AtmosphereBake");
    }

    {
        AtmosphereFrameContext atmoCtx{};
        atmoCtx.device              = m_device;
        atmoCtx.world               = world;
        atmoCtx.cbRing              = m_cbRing.get();
        atmoCtx.pipelines           = ctx.pipelines;
        atmoCtx.sunDir              = &ctx.sunDir;
        atmoCtx.sunColor            = &ctx.sunColor;
        atmoCtx.bindless            = ctx.bindless;
        atmoCtx.vpWidth             = ctx.vpWidth;
        atmoCtx.vpHeight            = ctx.vpHeight;
        atmoCtx.transmittanceLut    = m_transmittanceLut;
        atmoCtx.transmittanceLutSrv = m_transmittanceLutSrv;
        atmoCtx.transmittanceLutUav = m_transmittanceLutUav;
        atmoCtx.transmittanceLutDirty  = &m_transmittanceLutDirty;
        atmoCtx.transmittanceLutFilled = &m_transmittanceLutFilled;
        atmoCtx.multiScatterLut     = m_multiScatterLut;
        atmoCtx.multiScatterLutSrv  = m_multiScatterLutSrv;
        atmoCtx.multiScatterLutUav  = m_multiScatterLutUav;
        atmoCtx.multiScatterLutDirty  = &m_multiScatterLutDirty;
        atmoCtx.multiScatterLutFilled = &m_multiScatterLutFilled;
        atmoCtx.skyCubemap          = m_skyCubemap;
        atmoCtx.skyCubemapSrv       = m_skyCubemapSrv;
        atmoCtx.skyCubemapUav       = m_skyCubemapUav;
        atmoCtx.skyIBLDirty         = &m_skyIBLDirty;
        atmoCtx.iblBaker            = m_iblBaker.get();
        atmoCtx.iblResult           = &m_iblResult;
        atmoCtx.brdfLutSrvIndex     = m_brdfLutSrvIndex;
        atmoCtx.skyIrradianceSH     = &m_skyIrradianceSH;
        atmoCtx.depthSrvIndex       = m_gbuffer.depthSRV;
        atmoCtx.depthTexture        = m_gbuffer.depth;
        atmoCtx.hdriActive          = m_hdriActive;
        atmoCtx.hdriCubemapSrv      = m_hdriActive ? m_hdriCubemapSrv : UINT32_MAX;
        atmoCtx.hdriCubemapTex      = m_hdriCubemap;
        PostFrameAtmosphereBake(cmdList, atmoCtx);
    }

    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerEndTimestamp(cmdList, "AtmosphereBake");
    }

    // ── Screenshot capture (after all passes, before End) ───────────────
    // Capture directly from the backbuffer after the render graph executed
    // all passes. The backbuffer is in Present state after the render graph,
    // so CaptureFromPresent handles the Present→CopySrc→Present transitions.
    const bool doScreenshot = m_screenshotPending;
    if (doScreenshot) {
        if (!m_screenshotCapture.IsInitialized()) {
            m_screenshotCapture.Init(m_device, m_width, m_height);
        }
        m_screenshotCapture.CaptureFromPresent(cmdList, swapChain);
        m_screenshotPending = false;
    }

    // ── GPU Profiler: end frame (resolve timestamps to readback buffer) ──
    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerEndFrame(cmdList);
    }

    // ── End command list ────────────────────────────────────────────────
    cmdList->End();
    cmdList->Execute();

    // ── GPU Profiler: resolve results from previous frame (N-2) ─────────
    // After GPU execution, resolve the triple-buffered readback. Results
    // lag by 2 frames due to the profiler's triple-buffered design. The
    // first 2 frames will produce empty/zero results — this is expected.
    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerResolve();

        const auto& results = m_device->GpuProfilerGetResults();
        if (!results.empty()) {
            UZ_LOG(Renderer, Info, "── GPU Profile (frame {}) ──", frameIndex);
            for (const auto& r : results) {
                UZ_LOG(Renderer, Info, "  {:30s}  {:7.3f} ms", r.name ? r.name : "?", r.ms);
            }
            UZ_LOG(Renderer, Info, "  {:30s}  {:7.3f} ms", "TOTAL", m_device->GpuProfilerGetFrameTimeMs());
        }
    }

    // ── Save screenshot immediately after GPU finishes ──────────────────
    if (doScreenshot) {
        m_device->WaitIdle();
        if (m_screenshotBase64) {
            std::string b64 = m_screenshotCapture.ToBase64PNG();
            if (!b64.empty()) {
                std::printf("SCREENSHOT_BASE64:%s\n", b64.c_str());
                std::fflush(stdout);
            }
            m_screenshotBase64 = false;
        } else {
            m_screenshotCapture.SaveToPNG(m_screenshotPath);
        }
        m_screenshotPath.clear();
    }
}


} // namespace uz
