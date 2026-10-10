/// UzEngine Scene Renderer — Frame setup: camera, input, CB uploads, draw list,
/// instancing, and all per-frame state preparation.

#include "scene_renderer.h"
#include "skeletal_mesh_subsystem.h"

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
#include "viewport_toolbar.h"
#include "tech_panel.h"
#include "engine/vfx/editor/vfx_editor_overlay.h"
#include "halton.h"
#include "frame/taa_jitter.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ── CVars ────────────────────────────────────────────────────────────────
// CVars consumed by PerFrameCB population live in frame_constants.cpp.
// CVars below are used by the main SetupFrameData orchestration and ApplyCVars.
// TAA disabled (jitter trembling bug). No anti-aliasing until fixed.
static auto* cv_TAA                = UZ_CVAR_BOOL("r.TAA", false, "TAA anti-aliasing");
static auto* cv_TAAJitterSeqLen    = UZ_CVAR_INT("r.TAA.JitterSequenceLength", 8, "TAA Halton jitter sequence length", 1, 16);
static auto* cv_TAAJitterScale     = UZ_CVAR_FLOAT("r.TAA.JitterScale", 1.f,
    "Diagnostic: scales the Halton jitter offsets (1 = production). 0 gives TAA accumulation with NO "
    "sub-pixel offset, which is not the same as JitterSequenceLength=1 (that is a constant "
    "Halton23(1) = (0, -0.167) px offset). Used by the followup-10 reconstruction experiments.", 0.f, 2.f);
static auto* cv_PBRMinRoughness    = UZ_CVAR_FLOAT("r.PBR.MinRoughness", 0.045f, "Minimum roughness clamp", 0.01f, 0.2f);
static auto* cv_DepthPrepass       = UZ_CVAR_BOOL("r.DepthPrepass", false,
    "Depth prepass before the GBuffer (block B.4c: the GBuffer pass in depth-only mode, the colour pass then tests Equal). "
    "Off by default: on Sponza prepass + GBuffer costs the same as the GBuffer alone (0.524 vs 0.514 ms at 1080p); "
    "it pays where the GBuffer is overdrawn (a 48-wall stack: 0.180 -> 0.054 + 0.019 ms).");
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
            // Disable instead of retrying every frame (each retry ran End / Execute / WaitIdle / Begin
            // and re-uploaded the fallback textures).
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to init stats overlay — disabling");
            m_statsOverlay->Shutdown();
            m_statsOverlay.reset();
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
    // The frame fence only covers N-3; these buffers may belong to newer
    // graphics or copy work (SDF bake staging in particular, which is queued
    // from the copy queue). Drain all queues before releasing their storage.
    if (!m_deferredDestroyBuffers.empty()) {
        m_device->WaitIdle();
    }
    for (auto& buf : m_deferredDestroyBuffers) {
        if (buf.IsValid()) {
            m_device->DestroyBuffer(buf);
        }
    }
    m_deferredDestroyBuffers.clear();

    // ── Retire mesh-upload staging buffers (frames-in-flight aged) ──────
    // Stall-free mesh flushes submit copies with no CPU wait, so their staging
    // buffers must outlive the full frames-in-flight depth. Rotating into a
    // bucket that is kStagingRetireFrames passes old guarantees the frame
    // fence has covered the submission that referenced these buffers.
    m_stagingRetireIndex = (m_stagingRetireIndex + 1) % kStagingRetireFrames;
    for (auto& buf : m_stagingRetireBuckets[m_stagingRetireIndex]) {
        if (buf.IsValid()) {
            m_device->DestroyBuffer(buf);
        }
    }
    m_stagingRetireBuckets[m_stagingRetireIndex].clear();

    // ── Cache VFS for QueueTextureLoad (called from Lua outside RenderFrame) ──
    m_lastVfs = ctx.vfs;

    // ── Flush deferred asset loads (queued from Lua callbacks) ─────────
    FlushPendingLoads(ctx.cmdList, ctx.vfs);

    // ── Skeletal: GPU skinning compute pass ──────────────────────────────
    // Runs gpu_skinning_cs.hlsl per skeletal instance: reads SkinnedVertex[]
    // SRV + bone palette SRV, writes Vertex[] into the procedural mesh's VB
    // UAV. The GBuffer pass then samples the freshly-skinned VB via its
    // bindless SRV. Falls back to the CPU `UploadAll` path (which uploads the
    // CPU-skinned `m_outputVertexData` produced by `SkinCpu` in TickAll) if
    // the GPU skinning PSO is invalid (downlevel feature levels, shader
    // compile failure, etc.).
    if (m_pipelines && m_pipelines->GetGpuSkinning().IsValid()) {
        uz::SkeletalMeshSubsystem::Get().DispatchSkinningGpu(
            m_device, ctx.cmdList, *this, *m_pipelines);
    } else {
        uz::SkeletalMeshSubsystem::Get().UploadAll(m_device, ctx.cmdList, *this);
    }

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

    // ── VFX Editor Overlay deferred init ─────────────────────────────────
    if (!m_vfxEditorOverlayInitialized && m_vfxEditorOverlay) {
        UploadContext vfxUpload(m_device);
        if (m_vfxEditorOverlay->Init(m_device, ctx.cmdList, vfxUpload,
                m_shaderMgr->GetPipeline(), RHIFormat::RGBA8Unorm,
                m_width, m_height, ctx.bindless)) {
            m_vfxEditorOverlayInitialized = true;
            UZ_LOG(Renderer, Info, "VfxEditorOverlay: initialized OK");
        } else {
            UZ_LOG(Renderer, Error, "VfxEditorOverlay: init FAILED");
        }
        ctx.cmdList->End();
        ctx.cmdList->Execute();
        m_device->WaitIdle();
        vfxUpload.ReleaseStaging();
        ctx.cmdList->Begin();
    }

    // ── VFX Editor Overlay begin frame ───────────────────────────────────
    if (m_vfxEditorOverlayInitialized && m_vfxEditorOverlay->IsVisible()) {
        m_vfxEditorOverlay->BeginFrame(ctx.frameIndex);
    }

    // ── Viewport Toolbar deferred init ───────────────────────────────────
    if (!m_viewportToolbarInitialized && m_viewportToolbar) {
        UploadContext tbUpload(m_device);
        if (m_viewportToolbar->Init(m_device, ctx.cmdList, tbUpload,
                m_shaderMgr->GetPipeline(), RHIFormat::RGBA8Unorm,
                m_width, m_height, ctx.bindless)) {
            m_viewportToolbarInitialized = true;
            UZ_LOG(Renderer, Info, "ViewportToolbar: initialized OK");
        } else {
            UZ_LOG(Renderer, Warning, "ViewportToolbar: init FAILED — toolbar disabled");
            m_viewportToolbar->Shutdown();
            m_viewportToolbar.reset();
        }
        ctx.cmdList->End();
        ctx.cmdList->Execute();
        m_device->WaitIdle();
        tbUpload.ReleaseStaging();
        ctx.cmdList->Begin();
    }

    // ── Tech Panel deferred init (same pattern; not gated by editor mode) ─
    if (!m_techPanelInitialized && m_techPanel) {
        UploadContext tpUpload(m_device);
        if (m_techPanel->Init(m_device, ctx.cmdList, tpUpload,
                m_shaderMgr->GetPipeline(), RHIFormat::RGBA8Unorm,
                m_width, m_height, ctx.bindless)) {
            m_techPanelInitialized = true;
        } else {
            UZ_LOG(Renderer, Warning, "TechPanel: init FAILED — panel disabled");
            m_techPanel.reset();
        }
        ctx.cmdList->End();
        ctx.cmdList->Execute();
        m_device->WaitIdle();
        tpUpload.ReleaseStaging();
        ctx.cmdList->Begin();
    }

    // ── Shader hot-reload (poll every frame, recompile if .hlsl changed) ──
    if (m_shaderMgr) {
        auto recompiled = m_shaderMgr->GetPipeline().PollHotReload();
        if (!recompiled.empty()) {
            UZ_LOG(Renderer, Info, "Shader hot-reload: {} shader(s) recompiled", recompiled.size());
            // PSO cache invalidation happens lazily — next GetBytecode() returns new code
        }
    }

    // ── Input ───────────────────────────────────────────────────────────
    // Capture scroll delta BEFORE BeginFrame clears it — the VFX overlay
    // reads scroll in HandleMouse which runs after BeginFrame.
    const float preFrameScrollDelta = m_input->GetScrollDelta();
    // Snapshot only. The host pumps window messages BEFORE RenderFrame, so the
    // press/release edges and the scroll/raw deltas recorded by that pump belong
    // to THIS frame: they are dropped by `EndFrame()` at the end of the overlay
    // pass, after the last UI consumer. `BeginFrame()` here (= EndFrame +
    // PrepareFrame) discarded them before anyone read them, which is why no
    // `IsKeyJustPressed` / `IsMouseJustPressed` fired in the standalone runner.
    m_input->PrepareFrame();

    // ── VFX Editor Overlay: handle mouse AFTER BeginFrame ────────────────
    // The overlay tracks just-pressed internally from IsMouseDown,
    // so it doesn't depend on InputSystem's BeginFrame timing.
    // Inject the pre-BeginFrame scroll delta so the panel can scroll.
    bool vfxOverlayWantsMouse = false;
    if (m_vfxEditorOverlayInitialized && m_vfxEditorOverlay &&
        m_vfxEditorOverlay->IsVisible()) {
        m_vfxEditorOverlay->InjectScrollDelta(preFrameScrollDelta);
        vfxOverlayWantsMouse = m_vfxEditorOverlay->HandleMouse(m_input.get(), ctx.dt);
    }

    // ── Viewport Toolbar: update + hit-test (editor mode only) ───────────
    if (m_editorMode && m_viewportToolbarInitialized && m_viewportToolbar) {
        m_viewportToolbar->Update(m_input.get(), m_editorInputHandler, this);
    }

    // ── Tech Panel: F3 toggle + hit-test, every frame, any mode ─────────
    if (m_techPanelInitialized && m_techPanel) {
        m_techPanel->Update(m_input.get(), this);
    }

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

    // ── Game view toggle: G — hides all editor overlays ──────────────────
    if (m_editorMode && m_input->IsKeyJustPressed(Key::G)) {
        m_gameViewMode = !m_gameViewMode;
        auto& cvars = CVarRegistry::Get();
        // Grid and toolbar CVars reflect game-view state.
        cvars.Set("r.Toolbar.Show", m_gameViewMode ? 0.f : 1.f);
        cvars.Set("r.Show.Grid",    m_gameViewMode ? 0.f : 1.f);
        cvars.Set("r.Gizmo.Show",   m_gameViewMode ? 0.f : 1.f);
        UZ_LOG(Renderer, Info, "Game view: {}", m_gameViewMode ? "ON (overlays hidden)" : "OFF (overlays visible)");
    }

    // ── Editor Free-Cam Toggle (F5) ──
    if (m_input && m_input->IsKeyJustPressed(Key::F5)) {
        if (m_editorFreeCamId == kInvalidCameraId) {
            // Push: create a free-cam at the current camera's position
            m_editorFreeCamId = m_cameraManager->CreateCamera("EditorFreeCam");
            Camera* freeCam = m_cameraManager->GetCamera(m_editorFreeCamId);
            if (freeCam) {
                const Camera& current = m_cameraManager->GetActiveCamera();
                *freeCam = current; // Copy position, orientation, projection
                m_cameraCtrl->InitFromCamera(*freeCam);   // yaw / pitch from the copied camera
            }
            m_cameraManager->PushCamera(m_editorFreeCamId);
        } else {
            // Pop: restore previous camera, destroy free-cam
            m_cameraManager->PopCamera();
            m_cameraManager->DestroyCamera(m_editorFreeCamId);
            m_editorFreeCamId = kInvalidCameraId;
            m_cameraCtrl->InitFromCamera(m_cameraManager->GetActiveCamera());
        }
    }

    // ── Camera: editor fly cam (editor mode) or ECS camera (play mode) ──
    //
    // In editor mode the native FlyCameraController is the authoritative
    // driver — matches the industry convention (UE5 PIE / Unity Scene view /
    // Godot editor viewport): the scene's authored ECS cameras are read only
    // once, to seed the fly cam's initial pose, then WASD+RMB flies freely.
    // No Lua script is required; the Controller/fly_camera.lua pattern is
    // therefore obsolete.
    //
    // In play mode the original flow applies: ECS cameras drive the view and
    // the FlyCameraController is a fallback when nothing is active.
    bool cameraFromECS = false;
    const bool editorMode = IsEditorMode();

    if (editorMode && m_cameraManager->GetActiveCameraId() == 0) {
        // One-shot seed from the first active ECS camera on editor entry /
        // scene reload. After that, the user is flying — authored ECS
        // positions are ignored until the next editor-mode transition.
        if (!m_editorFlyCamInitialized && ctx.world) {
            auto cameraView = ctx.world->View<CameraComponent>();
            for (auto [enttEntity, cam] : cameraView.each()) {
                if (!cam.isActive) continue;
                auto& activeCam = m_cameraManager->GetActiveCamera();
                const Vec3 camPos = cam.GetWorldPosition();
                activeCam.SetPosition(camPos);
                if (cam.hasLookTarget) {
                    activeCam.LookAt(cam.lookTarget);
                } else {
                    const Vec3 fwd = (cam.GetRotation() * Vec3::Forward()).Normalized();
                    activeCam.LookAt(camPos + fwd * 1000.f);
                }
                activeCam.SetPerspective(
                    cam.fov * (3.14159265f / 180.f),
                    static_cast<float>(m_width) / static_cast<float>(m_height),
                    cam.nearPlane, cam.farPlane);
                m_cameraCtrl->InitFromCamera(activeCam);
                m_editorFlyCamInitialized = true;
                break;
            }
        }

        if (!vfxOverlayWantsMouse) {
            m_cameraCtrl->Update(m_cameraManager->GetActiveCamera(), *m_input, ctx.dt);
        }
    } else {
        // Play mode — sync camera from ECS CameraComponent every frame, but
        // only while the default Main camera (id=0) is active. Non-default
        // cameras are driven by Lua via mgr_set_position/mgr_look_at.
        if (ctx.world && m_cameraManager->GetActiveCameraId() == 0) {
            auto cameraView = ctx.world->View<CameraComponent>();
            for (auto [enttEntity, cam] : cameraView.each()) {
                if (cam.isActive) {
                    const Vec3 camPos = cam.GetWorldPosition();
                    m_cameraManager->GetActiveCamera().SetPosition(camPos);
                    if (cam.hasLookTarget) {
                        m_cameraManager->GetActiveCamera().LookAt(cam.lookTarget);
                    } else {
                        const Vec3 fwd = (cam.GetRotation() * Vec3::Forward()).Normalized();
                        m_cameraManager->GetActiveCamera().LookAt(camPos + fwd * 1000.f);
                    }
                    m_cameraManager->GetActiveCamera().SetPerspective(
                        cam.fov * (3.14159265f / 180.f),
                        static_cast<float>(m_width) / static_cast<float>(m_height),
                        cam.nearPlane, cam.farPlane);
                    cameraFromECS = true;
                    break;
                }
            }
        }

        // Fallback fly cam when no ECS camera is active (typical for empty
        // scenes or demos without a CameraComponent) AND the active camera is
        // the default Main camera (id=0) or the editor free camera (F5) —
        // which reaches this branch in editor mode too, since its id is not 0.
        // Other non-default cameras are controlled by Lua scripts via
        // mgr_set_position/mgr_look_at. Until the parity-06 close-out the F5
        // free camera was moved by no controller.
        const bool editorFreeCamActive = m_editorFreeCamId != kInvalidCameraId
            && m_cameraManager->GetActiveCameraId() == m_editorFreeCamId;
        if (!cameraFromECS && !vfxOverlayWantsMouse
                && (m_cameraManager->GetActiveCameraId() == 0 || editorFreeCamActive)) {
            m_cameraCtrl->Update(m_cameraManager->GetActiveCamera(), *m_input, ctx.dt);
        }
    }

    auto& camera = m_cameraManager->GetActiveCamera();

    // ── TAA Jitter — Halton(2,3) sub-pixel offset on the projection matrix ──
    // TAA jitter: Halton(2,3) sub-pixel offset applied to the projection matrix.
    // Jitter = SampleXY * 2/Resolution, flipped Y for D3D convention.

    ctx.jitterX = 0.f;
    ctx.jitterY = 0.f;

    // TAA runs on both bindless (DX12/SM6.6) and non-bindless (DX11/SM5) backends.
    // On SM5 the TAA upscaler binds textures via SetDescriptorTable/SetFragmentTexture
    // instead of ResourceDescriptorHeap indexing.
    // DX11: disable TAA — compute SRV/UAV resource conflicts on DX11's
    // immediate context cause the HDR scene color to go black after frame 1.
    // DX11 SM5.1 is below SM6.0, so use shader model as the backend check.
    const bool taaBackendOk = (m_device->GetShaderModel() >= ShaderModel::SM6_0);
    ctx.taaActive = m_taaEnabled
                 && m_taaHistory[0].IsValid()
                 && m_taaHistory[1].IsValid()
                 && taaBackendOk
                 && m_passInspector.IsPassEnabled("taa");

    if (ctx.taaActive) {
        // 1-based index into the Halton(2,3) sequence, cycled every
        // `r.TAA.JitterSequenceLength` frames (default kDefaultJitterSequenceLength = 8;
        // the CVar was registered but the constant was used until 2026-09-09).
        // Length 1 = TAA accumulation with a constant jitter (no sub-pixel offset).
        const int32_t seqLen = std::max(1, static_cast<int32_t>(cv_TAAJitterSeqLen->value));
        const int32_t jitterIdx = static_cast<int32_t>((ctx.frameIndex % static_cast<uint32_t>(seqLen)) + 1);
        Halton23(jitterIdx, ctx.jitterX, ctx.jitterY);
        ctx.jitterX *= cv_TAAJitterScale->value;
        ctx.jitterY *= cv_TAAJitterScale->value;
        ctx.jitterIndex = static_cast<uint32_t>(jitterIdx);
    }

    // Store unjittered viewProj for reprojection and velocity computation.
    ctx.unjitteredViewProj = camera.GetViewProjection();

    // Apply the jitter as an NDC translation of the combined view-projection
    // (see taa_jitter.h): every vertex moves by exactly (jitterX, jitterY)
    // render pixels regardless of depth. Adding the offset to VP[2][0]/[2][1]
    // — correct only for a pure projection matrix — made the shift depend on
    // world z / depth and shook near geometry by many pixels (followup-07).
    // Uses RENDER resolution (not display) because the jitter is in pixel units of the
    // rendered image, which is the viewport the geometry is rasterized into.
    ctx.jitteredViewProj = ctx.unjitteredViewProj;
    if (ctx.taaActive) {
        ctx.jitteredViewProj = ApplyTaaJitter(ctx.unjitteredViewProj, ctx.jitterX, ctx.jitterY,
                                              static_cast<float>(m_renderWidth),
                                              static_cast<float>(m_renderHeight));
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
    // Import with tracked end-of-frame states from the previous frame.
    // On frame 0, warmup barriers transition from Common to the expected state.
    auto hdrState   = m_hdrEndState   != RHIResourceState::Undefined ? m_hdrEndState   : RHIResourceState::RenderTarget;
    auto depthState = m_depthEndState != RHIResourceState::Undefined ? m_depthEndState : RHIResourceState::DepthStencilWrite;
    auto albState   = m_albedoEndState!= RHIResourceState::Undefined ? m_albedoEndState: RHIResourceState::RenderTarget;
    auto nrmState   = m_normalEndState!= RHIResourceState::Undefined ? m_normalEndState: RHIResourceState::RenderTarget;

    ctx.bbRes       = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);
    ctx.hdrRes      = rgBuilder.ImportTexture("HDR_SceneColor", m_hdrSceneColor, hdrState);
    ctx.albedoRes   = rgBuilder.ImportTexture("GBuf_Albedo",    m_gbuffer.albedo,     albState);
    ctx.normalRes   = rgBuilder.ImportTexture("GBuf_Normal",    m_gbuffer.normal,     nrmState);
    ctx.depthRes    = rgBuilder.ImportTexture("GBuf_Depth",     m_gbuffer.depth,      depthState);
    ctx.positionRes = rgBuilder.ImportTexture("GBuf_Position",  m_gbuffer.positionWS, albState);
    ctx.emissiveRes = rgBuilder.ImportTexture("GBuf_Emissive",  m_gbuffer.emissive,   albState);
    ctx.velocityRes = rgBuilder.ImportTexture("GBuf_Velocity",  m_gbuffer.velocity,   albState);
    ctx.shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", m_shadowAtlas.GetTexture(), RHIResourceState::ShaderResource);

    // Lighting split targets (SV_Target1..3 of the deferred pass). Imported so the graph can
    // schedule the RenderTarget -> ShaderResource transition the `lighting` / `shadows` /
    // `lightingonly` debug views need; without it those views read a target still bound as an RT.
    if (m_debugDirectLight.IsValid() && m_debugIndirectLight.IsValid() && m_debugShadowMask.IsValid()) {
        ctx.debugDirectRes   = rgBuilder.ImportTexture("Light_Direct",   m_debugDirectLight,   RHIResourceState::RenderTarget);
        ctx.debugIndirectRes = rgBuilder.ImportTexture("Light_Indirect", m_debugIndirectLight, RHIResourceState::RenderTarget);
        ctx.debugShadowRes   = rgBuilder.ImportTexture("Light_Shadow",   m_debugShadowMask,    RHIResourceState::RenderTarget);
    }

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
