/// UzEngine Scene Renderer — Frame setup: camera, input, CB uploads, draw list,
/// instancing, and all per-frame state preparation.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

// Individual includes needed by setup logic
#include "camera.h"
#include "fly_camera_controller.h"
#include "gpu_mesh.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "texture_loader.h"
#include "upload_context.h"
#include "vfs.h"
#include "world.h"
#include "material_interface.h"
#include "rhi.h"
#include "dynamic_resolution.h"
#include "input.h"
#include "platform_window.h"
#include "log.h"
#include "editor_grid.h"
#include "stats_overlay.h"
#include "halton.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ── CVars ────────────────────────────────────────────────────────────────
// CVars consumed by PerFrameCB population live in frame_constants.cpp.
// CVars below are used by the main SetupFrameData orchestration and ApplyCVars.
static auto* cv_TAA                = UZ_CVAR_BOOL("r.TAA", false, "TAA anti-aliasing");
static auto* cv_TAACurrentWeight   = UZ_CVAR_FLOAT("r.TAA.CurrentFrameWeight", 0.04f, "TAA current frame blend weight", 0.01f, 0.2f);
static auto* cv_TAAJitterSeqLen    = UZ_CVAR_INT("r.TAA.JitterSequenceLength", 8, "TAA Halton jitter sequence length", 1, 16);
static auto* cv_PBRMinRoughness    = UZ_CVAR_FLOAT("r.PBR.MinRoughness", 0.045f, "Minimum roughness clamp", 0.01f, 0.2f);
static auto* cv_DepthPrepass       = UZ_CVAR_BOOL("r.DepthPrepass", true, "Depth prepass before GBuffer");
static auto* cv_ShadowDebug        = UZ_CVAR_INT("r.ShadowDebug", 0, "Shadow debug grid mode (0=off, 1-8)", 0, 8);

namespace uz {

void SceneRenderer::SetupFrameData(FrameContext& ctx, rg::RgBuilder& rgBuilder) {
    UZ_PROFILE_FUNCTION();

    ctx.bindless = m_device->IsBindless();
    m_totalTime += ctx.dt;

    // ── Pass Inspector: reset per-frame target registrations ─────────────
    m_passInspector.BeginFrame();

    // ── Deferred StatsOverlay + fallback texture init (first frame only) ─
    if (!m_statsOverlayInitialized && m_statsOverlay) {
        UploadContext upload(m_device);
        if (m_statsOverlay->Init(m_device, ctx.cmdList, upload,
                m_shaderMgr->GetPipeline(), RHIFormat::RGBA8Unorm,
                m_width, m_height, ctx.bindless)) {
            m_statsOverlayInitialized = true;
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to init stats overlay");
        }

        // Upload 1x1 fallback textures for SM5 unconditional sampling.
        {
            const uint8_t whitePixel[4]  = {255, 255, 255, 255};
            const uint8_t blackPixel[4]  = {  0,   0,   0, 255}; // metallic=0 fallback
            const uint8_t normalPixel[4] = {128, 128, 255, 255}; // flat tangent-space normal

            upload.UploadTexture(ctx.cmdList, m_fallbackWhite,  whitePixel,  sizeof(whitePixel));
            upload.UploadTexture(ctx.cmdList, m_fallbackBlack,  blackPixel,  sizeof(blackPixel));
            upload.UploadTexture(ctx.cmdList, m_fallbackNormal, normalPixel, sizeof(normalPixel));
        }

        // Flush the upload commands
        ctx.cmdList->End();
        ctx.cmdList->Execute();
        m_device->WaitIdle();
        upload.ReleaseStaging();
        ctx.cmdList->Begin();
    }

    // ── Destroy GPU buffers deferred from previous frame ────────────────
    // Previous frame's fence has completed by now (Application::WaitForFrame),
    // so it's safe to destroy buffers that were referenced by GPU commands.
    for (auto& buf : m_deferredDestroyBuffers) {
        if (buf.IsValid()) {
            m_device->DestroyBuffer(buf);
        }
    }
    m_deferredDestroyBuffers.clear();

    // ── Cache VFS for QueueTextureLoad (called from Lua outside RenderFrame) ──
    m_lastVfs = ctx.vfs;

    // ── Flush deferred asset loads (queued from Lua callbacks) ─────────
    FlushPendingLoads(ctx.cmdList, ctx.vfs);

    // ── Apply CVar changes ─────────────────────────────────────────────
    ApplyCVars();

    // ── Dynamic Resolution update ────────────────────────────────────────
    // Uses CPU frame time as GPU time proxy until RHI exposes GPU timestamp queries.
    if (m_dynRes && m_dynRes->IsEnabled()) {
        const float newPct = m_dynRes->Update(ctx.dt);
        if (std::abs(newPct - m_screenPercentage) > 0.5f) {
            SetScreenPercentage(newPct);
        }
    }

    // ── CB Ring begin frame ─────────────────────────────────────────────
    m_cbRing->BeginFrame(ctx.frameIndex);

    // ── Stats begin frame ───────────────────────────────────────────────
    if (m_statsOverlayInitialized) {
        m_statsOverlay->BeginFrame(ctx.dt);
    }

    // ── Input ───────────────────────────────────────────────────────────
    m_input->BeginFrame();

    // ── Debug mode toggle: '0' = off, '1'-'7' = G-Buffer channels ──────
    if (m_input->IsKeyJustPressed(Key::Num0)) {
        m_debugMode = GBufferDebugMode::Count;
    }
    for (uint32_t k = 0; k < static_cast<uint32_t>(GBufferDebugMode::Count); ++k) {
        if (m_input->IsKeyJustPressed(static_cast<Key>(static_cast<uint32_t>(Key::Num1) + k))) {
            m_debugMode = static_cast<GBufferDebugMode>(k);
        }
    }

    // ── Render path toggle: F ───────────────────────────────────────────
    if (m_input->IsKeyJustPressed(Key::F)) {
        m_renderPath = (m_renderPath == RenderPath::Deferred)
            ? RenderPath::Forward : RenderPath::Deferred;
        UZ_LOG(Renderer, Info, "Render path switched to: {}",
               RenderPathToString(m_renderPath));
    }

    // ── Camera: ECS camera (game mode) or fly controller (editor mode) ──
    bool cameraFromECS = false;

    if (m_editorMode && !m_editorCameraInitialized && ctx.world) {
        // First frame in editor mode: copy ECS camera pose to the fly controller
        // so the editor starts at the scene-defined camera position, then hand
        // off control to the fly controller (with WASD gated behind RMB).
        auto cameraView = ctx.world->View<CameraComponent>();
        for (auto [enttEntity, cam] : cameraView.each()) {
            if (cam.isActive) {
                const Vec3 camPos = cam.GetWorldPosition();
                m_camera->SetPosition(camPos);
                if (cam.hasLookTarget) {
                    m_camera->LookAt(cam.lookTarget);
                } else {
                    const Vec3 fwd = (cam.GetRotation() * Vec3::Forward()).Normalized();
                    m_camera->LookAt(camPos + fwd * 1000.f);
                }
                m_camera->SetPerspective(
                    cam.fov * (3.14159265f / 180.f),
                    static_cast<float>(m_width) / static_cast<float>(m_height),
                    cam.nearPlane, cam.farPlane);
                // Initialize fly controller yaw/pitch from this camera pose.
                m_cameraCtrl->InitFromCamera(*m_camera);
                m_editorCameraInitialized = true;
                break;
            }
        }
    }

    if (!m_editorMode && ctx.world) {
        // Game mode: ECS camera driven by Lua scripts every frame.
        auto cameraView = ctx.world->View<CameraComponent>();
        for (auto [enttEntity, cam] : cameraView.each()) {
            if (cam.isActive) {
                const Vec3 camPos = cam.GetWorldPosition();
                m_camera->SetPosition(camPos);
                if (cam.hasLookTarget) {
                    m_camera->LookAt(cam.lookTarget);
                } else {
                    const Vec3 fwd = (cam.GetRotation() * Vec3::Forward()).Normalized();
                    m_camera->LookAt(camPos + fwd * 1000.f);
                }
                m_camera->SetPerspective(
                    cam.fov * (3.14159265f / 180.f),
                    static_cast<float>(m_width) / static_cast<float>(m_height),
                    cam.nearPlane, cam.farPlane);
                cameraFromECS = true;
                break;
            }
        }
    }

    if (!cameraFromECS) {
        // Fly controller: in editor mode, only runs when RMB is held.
        const bool cameraAllowed = !m_editorMode
                                 || m_input->IsMouseDown(MouseButton::Right);
        if (cameraAllowed) {
            m_cameraCtrl->Update(*m_camera, *m_input, ctx.dt);
        }
    }

    auto& camera = *m_camera;

    // ── TAA Jitter — Halton(2,3) sub-pixel offset on the projection matrix ──
    // TAA jitter: Halton(2,3) sub-pixel offset applied to the projection matrix.
    // Jitter = SampleXY * 2/Resolution, flipped Y for D3D convention.

    ctx.jitterX = 0.f;
    ctx.jitterY = 0.f;

    ctx.taaActive = m_taaEnabled
                 && m_taaHistory[0].IsValid()
                 && m_taaHistory[1].IsValid()
                 && m_device->IsBindless()
                 && m_passInspector.IsPassEnabled("taa");

    if (ctx.taaActive) {
        // 1-based index into Halton(2,3) sequence, cycled every 8 samples.
        // 8-sample Halton sequence (standard TAA jitter length).
        const int32_t jitterIdx = static_cast<int32_t>((ctx.frameIndex % kDefaultJitterSequenceLength) + 1);
        Halton23(jitterIdx, ctx.jitterX, ctx.jitterY);
    }

    // Store unjittered viewProj for reprojection and velocity computation.
    ctx.unjitteredViewProj = camera.GetViewProjection();

    // Apply jitter to the projection matrix as a sub-pixel translation in clip space.
    // The jitter is applied as an offset to the projection matrix's [2][0] and [2][1]
    // elements, which translates the clip-space output by a sub-pixel amount.
    // Uses RENDER resolution (not display) because the jitter is in pixel units of the
    // rendered image, which is the viewport the geometry is rasterized into.
    ctx.jitteredViewProj = ctx.unjitteredViewProj;
    if (ctx.taaActive) {
        const float renderW = static_cast<float>(m_renderWidth);
        const float renderH = static_cast<float>(m_renderHeight);
        // Equivalent: SampleX * 2.0 / ViewRect.Width(), SampleY * -2.0 / ViewRect.Height()
        // The *2 converts from pixel units to NDC (-1 to 1 range = 2 units per viewport dim).
        // Y is negated because clip-space Y increases upward but pixel Y increases downward.
        ctx.jitteredViewProj.m.m[2][0] += ctx.jitterX * 2.0f / renderW;
        ctx.jitteredViewProj.m.m[2][1] += ctx.jitterY * -2.0f / renderH;
    }

    // ── Per-frame constants: PerFrameCB, shadows, local light shadows ───
    PopulatePerFrameConstants(ctx);

    // ── Draw list, instancing, GPU occlusion culling ────────────────────
    SyncDrawListAndInstances(ctx);

    // ── Resolve viewport dimensions ────────────────────────────────────
    ctx.vpWidth  = static_cast<float>(ctx.swapChain->GetWidth());
    ctx.vpHeight = static_cast<float>(ctx.swapChain->GetHeight());
    ctx.renderWidth  = static_cast<float>(m_renderWidth);
    ctx.renderHeight = static_cast<float>(m_renderHeight);
    ctx.pipelines = m_pipelines.get();

    // ── Import render graph resources ──────────────────────────────────
    RHITextureHandle backBuffer = ctx.swapChain->GetCurrentBackBuffer();
    ctx.bbRes       = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);
    ctx.hdrRes      = rgBuilder.ImportTexture("HDR_SceneColor", m_hdrSceneColor, RHIResourceState::RenderTarget);
    ctx.albedoRes   = rgBuilder.ImportTexture("GBuf_Albedo",    m_gbuffer.albedo,     RHIResourceState::ShaderResource);
    ctx.normalRes   = rgBuilder.ImportTexture("GBuf_Normal",    m_gbuffer.normal,     RHIResourceState::ShaderResource);
    ctx.depthRes    = rgBuilder.ImportTexture("GBuf_Depth",     m_gbuffer.depth,      RHIResourceState::ShaderResource);
    ctx.positionRes = rgBuilder.ImportTexture("GBuf_Position",  m_gbuffer.positionWS, RHIResourceState::ShaderResource);
    ctx.emissiveRes = rgBuilder.ImportTexture("GBuf_Emissive",  m_gbuffer.emissive,   RHIResourceState::ShaderResource);
    ctx.velocityRes = rgBuilder.ImportTexture("GBuf_Velocity",  m_gbuffer.velocity,   RHIResourceState::ShaderResource);
    ctx.shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", m_shadowAtlas.GetTexture(), RHIResourceState::ShaderResource);

    // ── DBuffer resource imports (decal system) ──────────────────────────
    if (m_dbufferA.IsValid() && m_dbufferB.IsValid() && m_dbufferC.IsValid()) {
        ctx.dbufferARes = rgBuilder.ImportTexture("DBuffer_A", m_dbufferA, RHIResourceState::RenderTarget);
        ctx.dbufferBRes = rgBuilder.ImportTexture("DBuffer_B", m_dbufferB, RHIResourceState::RenderTarget);
        ctx.dbufferCRes = rgBuilder.ImportTexture("DBuffer_C", m_dbufferC, RHIResourceState::RenderTarget);
    }

    // ── Compute post-process activation flags ─────────────────────────
    ctx.bloomEnabled = m_bloomPass && m_bloomPass->IsInitialized()
                     && m_bloomPass->settings.enabled
                     && m_shadowDebugMode != 1
                     && ctx.bindless
                     && m_passInspector.IsPassEnabled("bloom");

    ctx.ssrActive = m_ssrEnabled
                  && m_ssrTraceOutput.IsValid()
                  && m_ssrResolvedOutput.IsValid()
                  && m_hdrHasContent
                  && m_hizMipCount > 0
                  && m_shadowDebugMode != 1
                  && m_passInspector.IsPassEnabled("ssr");

    // Gate motion blur on m_prevViewProjValid to avoid reading uninitialized velocity on frame 0.
    // The velocity pass is skipped on the first frame (no prevViewProj yet), so velocity data is garbage.
    ctx.motionBlurActive = m_motionBlurEnabled && m_prevViewProjValid
                         && m_motionBlurOutput.IsValid()
                         && m_motionBlurTileMax.IsValid() && m_motionBlurNeighborMax.IsValid()
                         && m_gbuffer.velocity.IsValid() && ctx.bindless && m_shadowDebugMode != 1
                         && m_passInspector.IsPassEnabled("motionblur");

    ctx.dofActive = m_dofEnabled && m_dofFocusDistance > 0.0f
                  && m_dofCocTexture.IsValid() && m_dofOutputTexture.IsValid()
                  && ctx.bindless && m_shadowDebugMode != 1
                  && m_passInspector.IsPassEnabled("dof");

    ctx.fxaaActive = !ctx.taaActive && ctx.bindless
                   && m_shadowDebugMode != 1
                   && m_fxaaLdrIntermediate.IsValid()
                   && m_fxaaLdrIntermediateSrv != UINT32_MAX
                   && m_passInspector.IsPassEnabled("fxaa");

    // CAS (Contrast Adaptive Sharpening) — runs after tonemap, before FXAA.
    // Enabled by default. Requires the LDR intermediate texture (shared with FXAA).
    ctx.casActive = m_casEnabled && ctx.bindless
                  && m_shadowDebugMode != 1
                  && m_fxaaLdrIntermediate.IsValid()
                  && m_fxaaLdrIntermediateSrv != UINT32_MAX
                  && m_passInspector.IsPassEnabled("cas");

    // Initialize SRV routing chain — each post-process pass may override these.
    ctx.taaSrvForTonemap = m_hdrSceneColorSrv;
    ctx.motionBlurSrvForTonemap = ctx.taaSrvForTonemap;
    ctx.postDofSrvForTonemap = ctx.motionBlurSrvForTonemap;

    // TAA ping-pong indices
    ctx.taaWriteIdx = m_taaHistoryIndex;
    ctx.taaReadIdx  = 1 - m_taaHistoryIndex;
}

} // namespace uz
