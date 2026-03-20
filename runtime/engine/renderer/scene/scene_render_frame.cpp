/// UzEngine Scene Renderer — RenderFrame (main per-frame orchestration).
///
/// This file contains only the top-level `RenderFrame()` method, which
/// delegates to helper methods defined in `frame/*.cpp`:
///   - SetupFrameData()          → frame/frame_setup.cpp
///   - RegisterShadowPasses()    → frame/frame_shadows.cpp
///   - RegisterGeometryPasses()  → frame/frame_geometry.cpp
///   - RegisterLightingPasses()  → frame/frame_lighting.cpp
///   - RegisterPostProcessPasses() → frame/frame_postprocess.cpp
///   - RegisterOverlayPasses()   → frame/frame_overlay.cpp

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
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

    // ── Build render graph ─────────────────────────────────────────────
    rg::RgBuilder rgBuilder(m_device, m_rgPool.get());

    // ── Setup all per-frame state (camera, CBs, draw list, instancing) ──
    FrameContext ctx{};
    ctx.cmdList    = cmdList;
    ctx.swapChain  = swapChain;
    ctx.world      = world;
    ctx.vfs        = vfs;
    ctx.dt         = dt;
    ctx.frameIndex = frameIndex;
    SetupFrameData(ctx, rgBuilder);

    // ── Volumetric Cloud Compute (dedicated command list) ──────────────
    // Noise textures are now loaded from disk (precomputed .bin files),
    // no GPU TDR risk. Weather/raymarch/temporal run as lightweight 2D
    // compute dispatches on a dedicated command list to isolate barrier
    // state from the render graph.
    if (m_pendingCloudDispatch && m_cloudPass && m_cloudPass->IsInitialized()
        && m_pendingCloudInput && m_cloudCmdList) {
        UZ_PROFILE_SCOPE("CloudPass::DedicatedDispatch");
        m_cloudCmdList->Begin();
        m_cloudPass->Execute(m_cloudCmdList, *m_cbRing, *m_pendingCloudInput);
        m_cloudCmdList->End();
        m_cloudCmdList->Execute();
        m_device->WaitIdle();
        m_cloudOutputSrv = m_cloudPass->GetCloudOutputSrv();
        m_pendingCloudDispatch = false;
    }

    // ── Register render graph passes in pipeline order ──────────────────
    RegisterShadowPasses(rgBuilder, ctx);
    RegisterGeometryPasses(rgBuilder, ctx);
    RegisterLightingPasses(rgBuilder, ctx);
    RegisterPostProcessPasses(rgBuilder, ctx);
    RegisterOverlayPasses(rgBuilder, ctx);

    // ── Compile + execute render graph ──────────────────────────────────
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
        PostFrameAtmosphereBake(cmdList, atmoCtx);
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

    // ── End command list ────────────────────────────────────────────────
    cmdList->End();
    cmdList->Execute();

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
