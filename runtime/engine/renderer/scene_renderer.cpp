/// UzEngine Scene Renderer — Implementation.
///
/// Bridges the ECS World to the GPU render pipeline. Follows the same patterns
/// as `DemoApplication::InitInfrastructure()` and `PBRTestDemo::OnFrame()` but
/// reads scene data from ECS components instead of hardcoded positions.

#include "scene_renderer.h"

#include "camera.h"
#include "fps_camera_controller.h"
#include "cb_ring_allocator.h"
#include "constant_buffers.h"
#include "pipeline_library.h"
#include "shader_manager.h"
#include "mesh.h"
#include "gpu_mesh.h"
#include "gbuffer_pass.h"
#include "gbuffer_debug_pass.h"
#include "lighting_pass.h"
#include "debug_view_pass.h"
#include "pbr_pass.h"
#include "shadow_pass.h"
#include "sky_atmosphere_pass.h"
#include "transmittance_lut_pass.h"
#include "sky_cubemap_capture_pass.h"
#include "height_fog_pass.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "shadow_cascade.h"
#include "shadow_renderer.h"
#include "light_renderer.h"
#include "atmosphere_renderer.h"
#include "fog_renderer.h"
#include "sh_projection.h"
#include "root_slots.h"
#include "profiler.h"
#include "instance_batcher.h"
#include "texture_loader.h"
#include "upload_context.h"
#include "vfs.h"
#include "world.h"
#include "camera_component.h"
#include "light_component.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "sky_atmosphere_component.h"
#include "sky_light_component.h"
#include "height_fog_component.h"
#include "color_utils.h"
#include "rhi.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "engine_assert.h"
#include "input.h"
#include "platform_window.h"
#include "log.h"
#include "editor_grid.h"
#include "stats_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

SceneRenderer::SceneRenderer() = default;

SceneRenderer::~SceneRenderer() {
    Shutdown();
}

SceneRenderer::SceneRenderer(SceneRenderer&&) noexcept = default;
SceneRenderer& SceneRenderer::operator=(SceneRenderer&&) noexcept = default;

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

bool SceneRenderer::Init(IRHIDevice* device, PlatformWindow* window,
                          uint32_t width, uint32_t height) {
    if (!device || !window) {
        UZ_LOG(Renderer, Error, "SceneRenderer::Init: null device or window");
        return false;
    }

    m_device = device;
    m_width = width;
    m_height = height;

    // ── Input ────────────────────────────────────────────────────────────
    m_input = std::make_unique<InputSystem>();
    m_input->Init();
    window->SetInputSystem(m_input.get());

    // ── Shader Manager ──────────────────────────────────────────────────
    m_shaderMgr = std::make_unique<ShaderManager>();
    if (!m_shaderMgr->Init(device)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to init shader manager");
        return false;
    }

    // ── Pipeline Library ────────────────────────────────────────────────
    m_pipelines = std::make_unique<PipelineLibrary>();
    if (!m_pipelines->Init(device, m_shaderMgr.get())) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to init pipeline library");
        return false;
    }

    // ── CB Ring Allocator ───────────────────────────────────────────────
    m_cbRing = std::make_unique<CBRingAllocator>();
    m_cbRing->Init(device, kCBRingFrames, kCBRingBytesPerFrame);

    // ── Editor Grid ─────────────────────────────────────────────────────
    m_editorGrid = std::make_unique<EditorGrid>();
    if (!m_editorGrid->Init(device, m_shaderMgr->GetPipeline(),
                            RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to init editor grid");
    }

    // ── Stats Overlay (deferred init — needs cmdList in recording state) ─
    m_statsOverlay = std::make_unique<StatsOverlay>();
    m_statsOverlayInitialized = false;

    // ── Render Graph Resource Pool ──────────────────────────────────────
    m_rgPool = std::make_unique<rg::RgResourcePool>(device);

    // ── Camera ──────────────────────────────────────────────────────────
    m_camera = std::make_unique<Camera>();
    m_cameraCtrl = std::make_unique<FPSCameraController>();

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    m_camera->SetHorizontalFOV(90.f, aspect, 10.f, 100000.f);
    m_camera->SetPosition({0.f, 200.f, -500.f});
    m_camera->LookAt({0.f, 0.f, 0.f}, Vec3::Up());
    m_cameraCtrl->InitFromCamera(*m_camera);
    m_cameraCtrl->SetMoveSpeed(500.f);  // 500 cm/s = 5 m/s (UE5 parity)

    // ── GBuffer ─────────────────────────────────────────────────────────
    if (!m_gbuffer.Create(device, width, height)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create GBuffer");
        return false;
    }

    // ── HDR Scene Color (RGBA16F — intermediate before tonemap) ────────
    {
        RHITextureDesc hdrDesc{};
        hdrDesc.width     = width;
        hdrDesc.height    = height;
        hdrDesc.format    = RHIFormat::RGBA16Float;
        hdrDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
        hdrDesc.debugName = "HDR_SceneColor";
        m_hdrSceneColor = device->CreateTexture(hdrDesc);
        if (!m_hdrSceneColor.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create HDR scene color texture");
            return false;
        }
        m_hdrSceneColorSrv = device->GetTextureSRVIndex(m_hdrSceneColor);
        UZ_LOG(Renderer, Info, "SceneRenderer: HDR scene color created ({}x{}, RGBA16F, SRV={})",
               width, height, m_hdrSceneColorSrv);
    }

    // ── Shadow Atlas ────────────────────────────────────────────────────
    m_shadowAtlas.Init(m_device);

    // ── Fallback Textures (SM5 unconditional sampling) ──────────────
    // The SM5 shader path samples all PBR textures unconditionally (no
    // index guards), so we must always bind valid textures. These 1x1
    // fallbacks produce neutral defaults: white = identity for albedo/
    // roughness/metallic/AO, flat normal = no perturbation.
    {
        RHITextureDesc fbDesc{};
        fbDesc.width     = 1;
        fbDesc.height    = 1;
        fbDesc.format    = RHIFormat::RGBA8Unorm;
        fbDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;

        fbDesc.debugName = "FallbackWhite";
        m_fallbackWhite  = m_device->CreateTexture(fbDesc);

        fbDesc.debugName = "FallbackBlack";
        m_fallbackBlack  = m_device->CreateTexture(fbDesc);

        fbDesc.debugName = "FallbackNormal";
        m_fallbackNormal = m_device->CreateTexture(fbDesc);
    }

    // ── Transmittance LUT texture (256x64 RGBA16F) ──────────────────────────
    // Created here; filled by the compute pass on first frame with sky atmosphere.
    {
        RHITextureDesc lutDesc{};
        lutDesc.width     = 256;
        lutDesc.height    = 64;
        lutDesc.format    = RHIFormat::RGBA16Float;
        lutDesc.usage     = RHITextureUsage::ShaderResource
                          | RHITextureUsage::UnorderedAccess;
        lutDesc.debugName = "TransmittanceLUT";

        m_transmittanceLut = m_device->CreateTexture(lutDesc);
        if (!m_transmittanceLut.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create TransmittanceLUT texture");
        } else {
            m_transmittanceLutSrv = m_device->GetTextureSRVIndex(m_transmittanceLut);
            m_transmittanceLutUav = m_device->GetTextureUAVIndex(m_transmittanceLut);
            UZ_LOG(Renderer, Info, "SceneRenderer: TransmittanceLUT created (256x64 RGBA16F)");
        }
    }

    // ── IBL Baker (bake BRDF LUT once) ────────────────────────────────
    m_iblBaker = std::make_unique<IBLBaker>();
    if (m_iblBaker->Init(device, m_shaderMgr.get())) {
        m_brdfLutSrvIndex = m_iblBaker->BakeBRDFLut();
        UZ_LOG(Renderer, Info, "SceneRenderer: BRDF LUT baked (SRV={})", m_brdfLutSrvIndex);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to init IBL baker");
    }

    // ── Auto-Exposure ────────────────────────────────────────────────
    m_autoExposure = std::make_unique<AutoExposure>();
    if (!m_autoExposure->Init(device, m_shaderMgr.get())) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: auto-exposure init failed (will use exposure=1.0)");
    }

    // ── Sky cubemap (64x64x6 RGBA16F) for IBL capture ──────────────────
    {
        RHITextureDesc cubeDesc{};
        cubeDesc.width     = 64;
        cubeDesc.height    = 64;
        cubeDesc.depthOrArraySize = 6;
        cubeDesc.format    = RHIFormat::RGBA16Float;
        cubeDesc.usage     = RHITextureUsage::ShaderResource
                           | RHITextureUsage::UnorderedAccess
                           | RHITextureUsage::CopySrc;
        cubeDesc.debugName = "SkyCubemap";

        m_skyCubemap = m_device->CreateTexture(cubeDesc);
        if (m_skyCubemap.IsValid()) {
            m_skyCubemapSrv = m_device->GetTextureSRVIndex(m_skyCubemap);
            m_skyCubemapUav = m_device->GetTextureUAVIndex(m_skyCubemap);
            UZ_LOG(Renderer, Info, "SceneRenderer: Sky cubemap created (64x64x6 RGBA16F)");
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create sky cubemap texture");
        }
    }

    UZ_LOG(Renderer, Info, "SceneRenderer initialized ({}x{}, {})",
           width, height, RenderPathToString(m_renderPath));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;

    m_width = width;
    m_height = height;

    if (m_gbuffer.IsValid()) {
        [[maybe_unused]] bool ok = m_gbuffer.Resize(m_device, width, height);
        UZ_CHECK(ok);
    }

    // Recreate HDR scene color at new size
    if (m_hdrSceneColor.IsValid()) {
        m_device->DestroyTexture(m_hdrSceneColor);
        RHITextureDesc hdrDesc{};
        hdrDesc.width     = width;
        hdrDesc.height    = height;
        hdrDesc.format    = RHIFormat::RGBA16Float;
        hdrDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
        hdrDesc.debugName = "HDR_SceneColor";
        m_hdrSceneColor = m_device->CreateTexture(hdrDesc);
        m_hdrSceneColorSrv = m_device->GetTextureSRVIndex(m_hdrSceneColor);
    }

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    m_camera->SetAspectRatio(aspect);

    if (m_statsOverlay) {
        m_statsOverlay->SetScreenSize(width, height);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::Shutdown() {
    if (!m_device) return;

    DestroyAssets();

    if (m_fallbackWhite.IsValid()) {
        m_device->DestroyTexture(m_fallbackWhite);
        m_fallbackWhite = RHITextureHandle{};
    }
    if (m_fallbackBlack.IsValid()) {
        m_device->DestroyTexture(m_fallbackBlack);
        m_fallbackBlack = RHITextureHandle{};
    }
    if (m_fallbackNormal.IsValid()) {
        m_device->DestroyTexture(m_fallbackNormal);
        m_fallbackNormal = RHITextureHandle{};
    }

    if (m_gbuffer.IsValid()) {
        m_gbuffer.Destroy(m_device);
    }

    m_shadowAtlas.Shutdown(m_device);

    // ── IBL resources ──────────────────────────────────────────────────
    if (m_iblBaker) m_iblBaker->Shutdown();
    if (m_skyCubemap.IsValid()) {
        m_device->DestroyTexture(m_skyCubemap);
        m_skyCubemap = RHITextureHandle{};
        m_skyCubemapSrv = UINT32_MAX;
        m_skyCubemapUav = UINT32_MAX;
    }

    // ── HDR Scene Color cleanup ─────────────────────────────────
    if (m_hdrSceneColor.IsValid()) {
        m_device->DestroyTexture(m_hdrSceneColor);
        m_hdrSceneColor = RHITextureHandle{};
    }

    // ── Auto-Exposure cleanup ─────────────────────────────────────
    if (m_autoExposure) m_autoExposure->Shutdown();

    // ── Instance buffer cleanup ─────────────────────────────────────
    if (m_instanceBuffer.IsValid()) {
        m_device->DestroyBuffer(m_instanceBuffer);
        m_instanceBuffer = RHIBufferHandle{};
    }

    if (m_statsOverlay)  m_statsOverlay->Shutdown();
    if (m_editorGrid)    m_editorGrid->Shutdown();
    if (m_cbRing)        m_cbRing->Shutdown();
    if (m_pipelines)     m_pipelines->Shutdown();
    if (m_shaderMgr)     m_shaderMgr->Shutdown();

    m_rgPool.reset();
    m_cameraCtrl.reset();
    m_camera.reset();
    m_statsOverlay.reset();
    m_editorGrid.reset();
    m_cbRing.reset();
    m_pipelines.reset();
    m_shaderMgr.reset();
    m_input.reset();

    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// RenderFrame
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::RenderFrame(IRHICommandList* cmdList, IRHISwapChain* swapChain,
                                 World* world, IVFS* vfs, float dt, uint32_t frameIndex) {
    if (!m_device || !cmdList || !swapChain) return;

    const bool bindless = m_device->IsBindless();
    m_totalTime += dt;

    // ── Begin command list ──────────────────────────────────────────────
    cmdList->Begin();

    // ── Deferred StatsOverlay + fallback texture init (first frame only) ─
    if (!m_statsOverlayInitialized && m_statsOverlay) {
        UploadContext upload(m_device);
        if (m_statsOverlay->Init(m_device, cmdList, upload,
                m_shaderMgr->GetPipeline(), RHIFormat::RGBA8Unorm,
                m_width, m_height, bindless)) {
            m_statsOverlayInitialized = true;
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to init stats overlay");
        }

        // Upload 1x1 fallback textures for SM5 unconditional sampling.
        {
            const uint8_t whitePixel[4]  = {255, 255, 255, 255};
            const uint8_t blackPixel[4]  = {  0,   0,   0, 255}; // metallic=0 fallback
            const uint8_t normalPixel[4] = {128, 128, 255, 255}; // flat tangent-space normal

            upload.UploadTexture(cmdList, m_fallbackWhite,  whitePixel,  sizeof(whitePixel));
            upload.UploadTexture(cmdList, m_fallbackBlack,  blackPixel,  sizeof(blackPixel));
            upload.UploadTexture(cmdList, m_fallbackNormal, normalPixel, sizeof(normalPixel));
        }

        // Flush the upload commands
        cmdList->End();
        cmdList->Execute();
        m_device->WaitIdle();
        upload.ReleaseStaging();
        cmdList->Begin();
    }

    // ── Flush deferred asset loads (queued from Lua callbacks) ─────────
    FlushPendingLoads(cmdList, vfs);

    // ── CB Ring begin frame ─────────────────────────────────────────────
    m_cbRing->BeginFrame(frameIndex);

    // ── Stats begin frame ───────────────────────────────────────────────
    if (m_statsOverlayInitialized) {
        m_statsOverlay->BeginFrame(dt);
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

    // ── Camera: prefer ECS CameraComponent, fallback to FPS controller ──
    bool cameraFromECS = false;
    if (world) {
        auto cameraView = world->View<CameraComponent>();
        for (auto [enttEntity, cam] : cameraView.each()) {
            if (cam.isActive) {
                // Sync ECS camera into our Camera object.
                // Camera::LookAt handles Z-up → Y-up conversion internally.
                const Vec3 camPos = cam.GetWorldPosition();
                m_camera->SetPosition(camPos);
                if (cam.hasLookTarget) {
                    m_camera->LookAt(cam.lookTarget);
                } else {
                    // Fallback: use rotation quaternion to derive forward direction.
                    // Note: FromEuler produces Y-up quaternions, so we convert via
                    // the same yaw/pitch extraction that FPSCameraController uses.
                    const Vec3 fwd = (cam.GetRotation() * Vec3::Forward()).Normalized();
                    m_camera->LookAt(camPos + fwd * 1000.f);  // 10m ahead in cm
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
        m_cameraCtrl->Update(*m_camera, *m_input, dt);
    }

    auto& camera = *m_camera;

    // ── Build PerFrameCB ────────────────────────────────────────────────
    PerFrameCB perFrame{};
    perFrame.viewProj    = camera.GetViewProjection();
    perFrame.invViewProj = perFrame.viewProj.Inverse();
    Vec3 camPos          = camera.GetPosition();
    perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams  = {m_totalTime, dt, static_cast<float>(frameIndex), 0.f};
    perFrame.nearPlane   = camera.GetNearPlane();
    perFrame.farPlane    = camera.GetFarPlane();

    // ── IBL SRV indices (set once after first bake, UINT32_MAX = no IBL) ──
    perFrame.irradianceSrvIndex  = m_iblResult.irradianceSrvIndex;
    perFrame.prefilteredSrvIndex = m_iblResult.prefilteredSrvIndex;
    perFrame.brdfLutSrvIndex     = m_brdfLutSrvIndex;
    perFrame.prefilteredMipLevels = 5;

    // Upload SH coefficients for sky diffuse irradiance (UE5 parity)
    for (int i = 0; i < 7; ++i) {
        perFrame.skyIrradianceSH[i] = m_skyIrradianceSH.coeffs[i];
    }

    // ── Extract SkyLight from SkyLightComponent (UE5 parity) ──────────
    if (world) {
        auto skyLightView = world->GetRegistry().view<SkyLightComponent>();
        for (auto enttEntity : skyLightView) {
            auto& sl = skyLightView.get<SkyLightComponent>(enttEntity);
            if (!sl.enabled) continue;
            perFrame.skyLightIntensity  = sl.intensity;
            perFrame.skyLightColorR     = sl.lightColor.x;
            perFrame.skyLightColorG     = sl.lightColor.y;
            perFrame.skyLightColorB     = sl.lightColor.z;
            perFrame.skyLightMinAmbient = sl.minOcclusion;
            break;  // First SkyLightComponent only
        }
    }

    // ── Extract lighting from ECS (directional + point lights) ─────────
    LightFrameData lightData = ExtractLightingFromECS(world, *m_cbRing);
    const Vec3& sunDir      = lightData.sunDir;
    const Vec3& sunColor    = lightData.sunColor;
    const Vec3& ambientColor = lightData.ambientColor;
    CBAllocation lightingAlloc = lightData.lightingCBAlloc;

    perFrame.lightDir     = {sunDir.x, sunDir.y, sunDir.z, 0.f};
    perFrame.lightColor   = {sunColor.x, sunColor.y, sunColor.z, 0.f};
    perFrame.ambientColor = {ambientColor.x, ambientColor.y, ambientColor.z, 0.f};

    // ── Auto-Exposure ──────────────────────────────────────────────────
    // Run GPU histogram-based auto-exposure on the HDR scene color.
    // On the first few frames before HDR is filled, use a sensible default.
    if (m_autoExposure && m_hdrSceneColorSrv != UINT32_MAX && m_hdrHasContent) {
        float autoExp = m_autoExposure->Execute(cmdList, m_hdrSceneColorSrv,
            m_width, m_height, dt);
        perFrame.exposure = autoExp;
        // Log only on first adaptation (avoid per-frame spam)
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Info, "AutoExposure: initial exposure={:.4f}", perFrame.exposure);
            loggedOnce = true;
        }
    } else {
        perFrame.exposure = 1.0f;
    }

    CBAllocation perFrameAlloc = m_cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── Shadow cascades — delegated to shadow_renderer ─────────────────
    ShadowFrameData shadowFrame = SetupShadowFrame(
        camera, sunDir,
        lightData.perLightShadowBias, lightData.perLightShadowSlopeBias,
        lightData.lightSourceAngleDeg,
        m_shadowAtlas.GetSrvIndex(), kShadowResolution, *m_cbRing,
        lightData.farShadowCascadeCount,
        lightData.farShadowDistance,
        lightData.farShadowLodBias);
    const auto& shadowCfg  = shadowFrame.config;
    const auto* cascades    = shadowFrame.cascades;
    const uint32_t totalCascadeCount = shadowFrame.totalCascadeCount;
    CBAllocation shadowAlloc = shadowFrame.cbAllocation;

    // Resize shadow atlas if cascade count changed (e.g., far shadows toggled)
    m_shadowAtlas.Resize(m_device, totalCascadeCount);

    // ── Build draw list from ECS ────────────────────────────────────────
    DrawList drawList;
    drawList.Clear();
    SyncWorldToDrawList(world, vfs, cmdList, drawList);
    drawList.Sort();

    // ── GPU Instancing — batch identical mesh+material draw commands ────
    std::vector<InstanceBatch> instanceBatches;
    std::vector<InstanceData> instanceDataVec;
    BuildInstanceBatches(drawList, instanceBatches, instanceDataVec);

    // Ensure instance buffer has enough capacity (recreate only when too small)
    const auto instanceCount = static_cast<uint32_t>(instanceDataVec.size());
    const bool useInstancing = bindless && instanceCount > 0;

    if (useInstancing && instanceCount > m_instanceBufferCapacity) {
        // Need a bigger buffer — wait for GPU idle before destroying the old one
        if (m_instanceBuffer.IsValid()) {
            m_device->WaitIdle();
            m_device->DestroyBuffer(m_instanceBuffer);
            m_instanceBuffer = RHIBufferHandle{};
        }

        // Round up to next power of two for stable allocation
        uint32_t newCapacity = instanceCount;
        newCapacity |= (newCapacity >> 1);
        newCapacity |= (newCapacity >> 2);
        newCapacity |= (newCapacity >> 4);
        newCapacity |= (newCapacity >> 8);
        newCapacity |= (newCapacity >> 16);
        newCapacity++;
        if (newCapacity < 16) newCapacity = 16;

        RHIBufferDesc ibDesc{};
        ibDesc.sizeBytes = newCapacity * sizeof(InstanceData);
        ibDesc.stride    = sizeof(InstanceData);
        ibDesc.usage     = RHIBufferUsage::StructuredBuffer;
        ibDesc.memory    = RHIMemoryType::Upload;
        ibDesc.debugName = "InstanceBuffer";

        m_instanceBuffer = m_device->CreateBuffer(ibDesc);
        if (m_instanceBuffer.IsValid()) {
            m_instanceBufferCapacity = newCapacity;
            m_instanceBufferSrvIndex = m_device->GetBufferSRVIndex(m_instanceBuffer);
        } else {
            m_instanceBufferCapacity = 0;
            m_instanceBufferSrvIndex = UINT32_MAX;
        }
    }

    // Upload instance data — must wait for GPU to finish reading from previous frame.
    // TODO(perf): Replace with per-frame ring offset or triple-buffered instance buffers.
    uint32_t instanceBufferSrvIndex = UINT32_MAX;
    if (useInstancing && m_instanceBuffer.IsValid()) {
        m_device->WaitIdle();
        void* mapped = m_device->MapBuffer(m_instanceBuffer);
        if (mapped) {
            std::memcpy(mapped, instanceDataVec.data(),
                        instanceCount * sizeof(InstanceData));
            m_device->UnmapBuffer(m_instanceBuffer);
        }
        instanceBufferSrvIndex = m_instanceBufferSrvIndex;
    }

    // ── Record frame via render graph ───────────────────────────────────
    const float vpWidth = static_cast<float>(swapChain->GetWidth());
    const float vpHeight = static_cast<float>(swapChain->GetHeight());

    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
    rg::RgBuilder rgBuilder(m_device, m_rgPool.get());
    auto bbRes  = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);
    auto hdrRes = rgBuilder.ImportTexture("HDR_SceneColor", m_hdrSceneColor, RHIResourceState::RenderTarget);

    auto albedoRes   = rgBuilder.ImportTexture("GBuf_Albedo",    m_gbuffer.albedo,     RHIResourceState::ShaderResource);
    auto normalRes   = rgBuilder.ImportTexture("GBuf_Normal",    m_gbuffer.normal,     RHIResourceState::ShaderResource);
    auto depthRes    = rgBuilder.ImportTexture("GBuf_Depth",     m_gbuffer.depth,      RHIResourceState::ShaderResource);
    auto positionRes = rgBuilder.ImportTexture("GBuf_Position",  m_gbuffer.positionWS, RHIResourceState::ShaderResource);

    auto* pipelines = m_pipelines.get();

    // ── Determine mesh buffers for rendering ────────────────────────────
    // For MVP: use the first loaded mesh's buffers, or skip if no meshes loaded
    GPUMesh renderMesh{};
    if (!m_meshes.empty()) {
        renderMesh = m_meshes[0];
    }

    // ── Shadow pass ─────────────────────────────────────────────────────
    auto shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", m_shadowAtlas.GetTexture(), RHIResourceState::ShaderResource);
    if (renderMesh.IsValid()) {
        ShadowPassDesc shadowDesc{};
        shadowDesc.shadowAtlas          = shadowAtlasRes;
        shadowDesc.shadowAtlasTexture   = m_shadowAtlas.GetTexture();
        shadowDesc.drawList             = &drawList;
        shadowDesc.cbAllocator          = m_cbRing.get();
        shadowDesc.pipeline             = pipelines->GetShadowDepth(bindless);
        shadowDesc.vertexBufferSrvIndex = renderMesh.vbSrvIndex;
        shadowDesc.vertexBuffer         = renderMesh.vertexBuffer;
        shadowDesc.indexBuffer          = renderMesh.indexBuffer;
        shadowDesc.indexFormat          = renderMesh.indexFormat;
        shadowDesc.indexCount           = renderMesh.indexCount;
        shadowDesc.bindless             = bindless;
        for (uint32_t i = 0; i < totalCascadeCount; ++i) {
            shadowDesc.cascades[i] = cascades[i];
        }
        shadowDesc.cascadeCount     = totalCascadeCount;
        shadowDesc.atlasResolution  = kShadowResolution;
        shadowDesc.farShadowLodBias = shadowFrame.config.farShadowLodBias;
        // TEMP: disable instancing for shadows to debug flickering
        // if (useInstancing) {
        //     shadowDesc.instanced              = true;
        //     shadowDesc.instancedPipeline      = pipelines->GetShadowDepthInstanced();
        //     shadowDesc.instanceBufferSrvIndex = instanceBufferSrvIndex;
        //     shadowDesc.batchData              = instanceBatches.data();
        //     shadowDesc.batchCount             = static_cast<uint32_t>(instanceBatches.size());
        // }
        AddShadowPass(rgBuilder, shadowDesc);
    }

    if (m_renderPath == RenderPath::Deferred) {
        // ── Deferred: GBuffer + lighting/debug ──────────────────────────
        if (renderMesh.IsValid()) {
            GBufferPassDesc gbDesc{};
            gbDesc.albedoTarget         = albedoRes;
            gbDesc.normalTarget         = normalRes;
            gbDesc.positionTarget       = positionRes;
            gbDesc.depthBuffer          = depthRes;
            gbDesc.depthTexture         = m_gbuffer.depth;
            gbDesc.drawList             = &drawList;
            gbDesc.cbAllocator          = m_cbRing.get();
            gbDesc.perFrameAlloc        = perFrameAlloc;
            gbDesc.pipeline             = pipelines->GetGBuffer(bindless);
            gbDesc.vertexBufferSrvIndex = renderMesh.vbSrvIndex;
            gbDesc.vertexBuffer         = renderMesh.vertexBuffer;
            gbDesc.indexBuffer          = renderMesh.indexBuffer;
            gbDesc.indexFormat          = renderMesh.indexFormat;
            gbDesc.indexCount           = renderMesh.indexCount;
            gbDesc.fallbackWhite        = m_fallbackWhite;
            gbDesc.fallbackBlack        = m_fallbackBlack;
            gbDesc.fallbackNormal       = m_fallbackNormal;
            gbDesc.bindless             = bindless;
            gbDesc.viewportWidth        = vpWidth;
            gbDesc.viewportHeight       = vpHeight;
            if (useInstancing) {
                gbDesc.instanced              = true;
                gbDesc.instancedPipeline      = pipelines->GetGBufferInstanced();
                gbDesc.instanceBufferSrvIndex = instanceBufferSrvIndex;
                gbDesc.batchData              = instanceBatches.data();
                gbDesc.batchCount             = static_cast<uint32_t>(instanceBatches.size());
            }
            AddGBufferPass(rgBuilder, gbDesc);
        }

        const bool debugActive = (m_debugMode != GBufferDebugMode::Count);
        const bool debugViewActive = (m_debugViewMode != DebugViewMode::Lit);

        if (debugViewActive) {
            // ── Debug View Mode — replaces lighting with channel visualization ──
            DebugViewPassDesc dvDesc{};
            dvDesc.backbuffer        = hdrRes;
            dvDesc.albedoGBuffer     = albedoRes;
            dvDesc.normalGBuffer     = normalRes;
            dvDesc.depthGBuffer      = depthRes;
            dvDesc.positionGBuffer   = positionRes;
            dvDesc.perFrameAlloc     = perFrameAlloc;
            dvDesc.pipeline          = pipelines->GetDebugView();
            dvDesc.mode              = m_debugViewMode;
            dvDesc.albedoSrvIndex    = m_gbuffer.albedoSRV;
            dvDesc.normalSrvIndex    = m_gbuffer.normalSRV;
            dvDesc.depthSrvIndex     = m_gbuffer.depthSRV;
            dvDesc.positionSrvIndex  = m_gbuffer.positionWSSRV;
            dvDesc.albedoTexture     = m_gbuffer.albedo;
            dvDesc.normalTexture     = m_gbuffer.normal;
            dvDesc.depthTexture      = m_gbuffer.depth;
            dvDesc.positionTexture   = m_gbuffer.positionWS;
            dvDesc.bindless          = bindless;
            dvDesc.viewportWidth     = vpWidth;
            dvDesc.viewportHeight    = vpHeight;
            AddDebugViewPass(rgBuilder, dvDesc);
        } else if (!debugActive) {
            LightingPassDesc lightDesc{};
            lightDesc.backbuffer          = hdrRes;
            lightDesc.albedoGBuffer       = albedoRes;
            lightDesc.normalGBuffer       = normalRes;
            lightDesc.depthGBuffer        = depthRes;
            lightDesc.positionGBuffer     = positionRes;
            lightDesc.perFrameAlloc       = perFrameAlloc;
            lightDesc.lightingAlloc       = lightingAlloc;
            lightDesc.pipeline            = pipelines->GetLighting();
            lightDesc.albedoSrvIndex      = m_gbuffer.albedoSRV;
            lightDesc.normalSrvIndex      = m_gbuffer.normalSRV;
            lightDesc.depthSrvIndex       = m_gbuffer.depthSRV;
            lightDesc.positionSrvIndex    = m_gbuffer.positionWSSRV;
            lightDesc.albedoTexture       = m_gbuffer.albedo;
            lightDesc.normalTexture       = m_gbuffer.normal;
            lightDesc.depthTextureForSrv  = m_gbuffer.depth;
            lightDesc.positionTexture     = m_gbuffer.positionWS;
            lightDesc.shadowAlloc         = shadowAlloc;
            lightDesc.shadowAtlasResource = shadowAtlasRes;
            lightDesc.shadowAtlasTexture  = m_shadowAtlas.GetTexture();
            lightDesc.bindless            = bindless;
            lightDesc.viewportWidth       = vpWidth;
            lightDesc.viewportHeight      = vpHeight;
            AddLightingPass(rgBuilder, lightDesc);
        } else {
            GBufferDebugPassDesc dbgDesc{};
            dbgDesc.backbuffer         = hdrRes;
            dbgDesc.albedoGBuffer      = albedoRes;
            dbgDesc.normalGBuffer      = normalRes;
            dbgDesc.depthGBuffer       = depthRes;
            dbgDesc.perFrameAlloc      = perFrameAlloc;

            struct DebugCBData { uint32_t mode; uint32_t pad[3]; };
            CBAllocation debugAlloc = m_cbRing->Allocate(sizeof(DebugCBData));
            DebugCBData debugCBData{static_cast<uint32_t>(m_debugMode), {0, 0, 0}};
            std::memcpy(debugAlloc.cpuPtr, &debugCBData, sizeof(DebugCBData));
            dbgDesc.debugAlloc         = debugAlloc;

            dbgDesc.pipeline           = pipelines->GetGBufferDebug();
            dbgDesc.mode               = m_debugMode;
            dbgDesc.albedoSrvIndex     = m_gbuffer.albedoSRV;
            dbgDesc.normalSrvIndex     = m_gbuffer.normalSRV;
            dbgDesc.depthSrvIndex      = m_gbuffer.depthSRV;
            dbgDesc.albedoTexture      = m_gbuffer.albedo;
            dbgDesc.normalTexture      = m_gbuffer.normal;
            dbgDesc.depthTextureForSrv = m_gbuffer.depth;
            dbgDesc.bindless           = bindless;
            dbgDesc.viewportWidth      = vpWidth;
            dbgDesc.viewportHeight     = vpHeight;
            AddGBufferDebugPass(rgBuilder, dbgDesc);
        }
    } else {
        // ── Forward: single-pass PBR ────────────────────────────────────
        if (renderMesh.IsValid()) {
            PBRPassDesc fwdDesc{};
            fwdDesc.backbuffer           = hdrRes;
            fwdDesc.depthBuffer          = depthRes;
            fwdDesc.depthTexture         = m_gbuffer.depth;
            fwdDesc.drawList             = &drawList;
            fwdDesc.cbAllocator          = m_cbRing.get();
            fwdDesc.perFrameAlloc        = perFrameAlloc;
            fwdDesc.pipeline             = pipelines->GetForwardPBR(bindless);
            fwdDesc.vertexBufferSrvIndex = renderMesh.vbSrvIndex;
            fwdDesc.vertexBuffer         = renderMesh.vertexBuffer;
            fwdDesc.indexBuffer          = renderMesh.indexBuffer;
            fwdDesc.indexFormat          = renderMesh.indexFormat;
            fwdDesc.indexCount           = renderMesh.indexCount;
            fwdDesc.bindless             = bindless;
            fwdDesc.viewportWidth        = vpWidth;
            fwdDesc.viewportHeight       = vpHeight;
            AddPBRPass(rgBuilder, fwdDesc);
        }
    }

    // ── Sky Atmosphere passes — delegated to atmosphere_renderer ────────
    AtmosphereFrameContext atmoCtx{};
    atmoCtx.device              = m_device;
    atmoCtx.world               = world;
    atmoCtx.cbRing              = m_cbRing.get();
    atmoCtx.pipelines           = pipelines;
    atmoCtx.sunDir              = &sunDir;
    atmoCtx.sunColor            = &sunColor;
    atmoCtx.bindless            = bindless;
    atmoCtx.vpWidth             = vpWidth;
    atmoCtx.vpHeight            = vpHeight;
    atmoCtx.transmittanceLut    = m_transmittanceLut;
    atmoCtx.transmittanceLutSrv = m_transmittanceLutSrv;
    atmoCtx.transmittanceLutUav = m_transmittanceLutUav;
    atmoCtx.transmittanceLutDirty  = &m_transmittanceLutDirty;
    atmoCtx.transmittanceLutFilled = &m_transmittanceLutFilled;
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

    AddAtmospherePasses(rgBuilder, hdrRes, depthRes, perFrameAlloc, atmoCtx);

    // ── Height Fog pass — delegated to fog_renderer ─────────────────────
    AddHeightFogFromECS(rgBuilder, hdrRes, depthRes, perFrameAlloc,
                        world, camera, pipelines, *m_cbRing,
                        m_gbuffer.depthSRV, m_gbuffer.depth,
                        bindless, vpWidth, vpHeight);

    // (atmosphere and fog passes registered above via AddAtmospherePasses + AddHeightFogFromECS)

    // ── Tonemap pass (HDR → LDR backbuffer) ────────────────────────────
    // Reads HDR scene color (RGBA16F), applies auto-exposure + ACES filmic
    // tonemap + sRGB gamma, writes LDR to backbuffer (RGBA8Unorm).
    // This is the ONLY place tonemapping happens in the pipeline.
    rgBuilder.AddPass("Tonemap", [&](rg::RgPassBuilder& pb) {
        pb.Read(hdrRes, RHIResourceState::ShaderResource);
        pb.Write(bbRes, RHIResourceState::RenderTarget);

        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("Tonemap::Execute");

            auto bb = res.GetTexture(bbRes);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            cmd->BeginRenderPass(passDesc);

            cmd->SetPipeline(pipelines->GetTonemap());

            RHIViewport vp{};
            vp.width    = vpWidth;
            vp.height   = vpHeight;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
            cmd->SetScissor(sc);

            // Root constants: HDR scene color SRV index
            uint32_t indices[4] = { m_hdrSceneColorSrv, 0, 0, 0 };
            cmd->SetRootConstants(kSlotRootConstants, indices, 4);

            // PerFrameCB at b1 — contains exposure from auto-exposure
            cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.bufferHandle,
                            perFrameAlloc.offsetBytes, perFrameAlloc.cpuPtr,
                            perFrameAlloc.sizeBytes);

            cmd->Draw(3); // fullscreen triangle

            cmd->EndRenderPass();
        });
    });

    // ── Overlay pass (editor grid + stats overlay) ──────────────────────
    rgBuilder.AddPass("OverlayPass", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::RenderTarget);
        pb.Write(depthRes, RHIResourceState::DepthStencilWrite);
        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto bb    = res.GetTexture(bbRes);
            auto depth = res.GetTexture(depthRes);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth                    = true;
            passDesc.depthAttachment.texture     = depth;
            passDesc.depthAttachment.clear       = false;
            cmd->BeginRenderPass(passDesc);

            RHIViewport vp{};
            vp.width    = vpWidth;
            vp.height   = vpHeight;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
            cmd->SetScissor(sc);

            m_editorGrid->Draw(cmd, *m_cbRing, camera);

            if (m_statsOverlayInitialized) {
                const uint32_t reportedDrawCalls = useInstancing
                    ? static_cast<uint32_t>(instanceBatches.size())
                    : static_cast<uint32_t>(drawList.Count());
                m_statsOverlay->SetDrawCalls(reportedDrawCalls);
                m_statsOverlay->Render(cmd, *m_cbRing, frameIndex);
            }

            cmd->EndRenderPass();
        });
    });

    // ── Present transition ──────────────────────────────────────────────
    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // HDR target now contains valid scene color for next frame's auto-exposure readback.
    m_hdrHasContent = true;

    // ── IBL bake + SH projection — delegated to atmosphere_renderer ────
    PostFrameAtmosphereBake(cmdList, atmoCtx);

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
        m_screenshotCapture.SaveToPNG(m_screenshotPath);
        m_screenshotPath.clear();
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// RequestScreenshot
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::RequestScreenshot(const std::string& path) {
    m_screenshotPath    = path;
    m_screenshotPending = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SyncWorldToDrawList
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::SyncWorldToDrawList(World* world, IVFS* vfs,
                                         IRHICommandList* cmdList,
                                         DrawList& outDrawList) {
    if (!world) return;

    uint32_t drawIndex = 0;

    // Use mutable registry view — we need to write meshLoaded/materialLoaded flags
    // back into the components after lazy-loading assets.
    auto meshView = world->GetRegistry().view<StaticMeshComponent, SceneComponent>();

    for (auto [enttEntity, meshComp, sceneComp] : meshView.each()) {

        // ── Lazy-load mesh from path ────────────────────────────────────────
        if (!meshComp.meshLoaded && !meshComp.meshPath.empty()) {
            const uint32_t loaded = LoadMesh(meshComp.meshPath, vfs, cmdList);
            if (loaded != UINT32_MAX) {
                meshComp.meshId     = loaded;
                meshComp.meshLoaded = true;
            } else {
                // Mark loaded to avoid retrying every frame
                meshComp.meshLoaded = true;
            }
        }

        // ── Lazy-load material textures from paths ──────────────────────────
        if (!meshComp.materialLoaded && !meshComp.albedoPath.empty()) {
            auto tryLoad = [&](const std::string& path) -> uint32_t {
                if (path.empty()) return UINT32_MAX;
                return LoadTexture(path, vfs, cmdList);
            };
            tryLoad(meshComp.albedoPath);
            tryLoad(meshComp.normalPath);
            tryLoad(meshComp.roughnessPath);
            tryLoad(meshComp.metallicPath);
            tryLoad(meshComp.aoPath);
            meshComp.materialLoaded = true;
        }

        const uint32_t meshId = meshComp.meshId;

        // Validate mesh index is within loaded meshes
        if (m_meshes.empty()) continue;
        if (meshId >= static_cast<uint32_t>(m_meshes.size())) continue;
        if (!m_meshes[meshId].IsValid()) continue;

        DrawCommand cmd{};
        cmd.sortKey      = SortKey::Opaque(0, 0, 0.f, drawIndex);
        cmd.worldMatrix  = sceneComp.GetWorldMatrix();
        cmd.normalMatrix = cmd.worldMatrix.NormalMatrix();
        cmd.meshId       = meshId;

        // Map materialId to texture SRV indices if textures are loaded
        if (!m_textures.empty() && meshComp.materialId < static_cast<uint32_t>(m_textures.size())) {
            const auto& tex = m_textures[meshComp.materialId];
            cmd.albedoSrvIndex = tex.srvIndex;
            cmd.albedoTexture  = tex.handle;
        }

        // Also check path-based texture loading results for all PBR channels.
        auto bindTexFromPath = [&](const std::string& path, uint32_t& srvIndex, RHITextureHandle& handle) {
            if (path.empty()) return;
            auto texIt = m_textureMap.find(path);
            if (texIt != m_textureMap.end() && texIt->second < static_cast<uint32_t>(m_textures.size())) {
                const auto& tex = m_textures[texIt->second];
                srvIndex = tex.srvIndex;
                handle   = tex.handle;
            }
        };
        bindTexFromPath(meshComp.albedoPath,    cmd.albedoSrvIndex,    cmd.albedoTexture);
        bindTexFromPath(meshComp.normalPath,    cmd.normalSrvIndex,    cmd.normalTexture);
        bindTexFromPath(meshComp.roughnessPath, cmd.roughnessSrvIndex, cmd.roughnessTexture);
        bindTexFromPath(meshComp.metallicPath,  cmd.metallicSrvIndex,  cmd.metallicTexture);
        bindTexFromPath(meshComp.aoPath,        cmd.aoSrvIndex,        cmd.aoTexture);

        outDrawList.Add(cmd);
        drawIndex++;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Loading
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::LoadMesh(const std::string& path, IVFS* vfs,
                                  IRHICommandList* cmdList) {
    // Check cache
    auto it = m_meshMap.find(path);
    if (it != m_meshMap.end()) {
        return it->second;
    }

    // Built-in primitives: "builtin:cube", "builtin:sphere"
    Mesh cpuMesh{};
    if (path.size() > 8 && path.compare(0, 8, "builtin:") == 0) {
        const std::string primitive = path.substr(8);
        if (primitive == "cube") {
            cpuMesh = MeshBuilder::CreateCube(0.5f);
        } else if (primitive == "sphere") {
            cpuMesh = MeshBuilder::CreateSphere(0.5f, 32, 32);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: unknown builtin mesh: '{}'", primitive);
            return UINT32_MAX;
        }
    } else {
        // Resolve via VFS for relative paths so demos using VFS-relative asset names work.
        const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                                 (path.size() > 1 && path[1] == ':'));
        const std::string resolvedPath = (vfs && !isAbsolute)
            ? vfs->ResolvePath(path)
            : path;
        cpuMesh = uz::LoadMesh(resolvedPath);
    }
    if (!cpuMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to load mesh: '{}'", path);
        return UINT32_MAX;
    }

    GPUMesh gpuMesh = UploadMesh(m_device, cmdList, cpuMesh);
    if (!gpuMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to upload mesh: '{}'", path);
        return UINT32_MAX;
    }

    const auto index = static_cast<uint32_t>(m_meshes.size());
    m_meshes.push_back(gpuMesh);
    m_meshMap[path] = index;

    UZ_LOG(Renderer, Info, "SceneRenderer: loaded mesh '{}' (id={}, {} vertices, {} indices)",
           path, index, gpuMesh.vertexCount, gpuMesh.indexCount);
    return index;
}

uint32_t SceneRenderer::LoadTexture(const std::string& path, IVFS* vfs,
                                     IRHICommandList* cmdList) {
    // Check cache
    auto it = m_textureMap.find(path);
    if (it != m_textureMap.end()) {
        return it->second;
    }

    // Resolve via VFS for relative paths
    const bool isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                             (path.size() > 1 && path[1] == ':'));
    const std::string resolvedPath = (vfs && !isAbsolute)
        ? vfs->ResolvePath(path)
        : path;

    TextureLoadResult result = TextureLoader::Load(m_device, cmdList, resolvedPath);
    if (!result.handle.IsValid()) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to load texture: '{}'", path);
        return UINT32_MAX;
    }

    const auto index = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(result);
    m_textureMap[path] = index;

    UZ_LOG(Renderer, Info, "SceneRenderer: loaded texture '{}' (id={}, {}x{}, srv={})",
           path, index, result.width, result.height, result.srvIndex);
    return index;
}

// ─────────────────────────────────────────────────────────────────────────────
// Deferred Load Queue — Lua API
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SceneRenderer::QueueMeshLoad(const std::string& path) {
    if (path.empty()) return UINT32_MAX;

    // Return cached ID immediately — no need to queue.
    auto it = m_meshMap.find(path);
    if (it != m_meshMap.end()) {
        return it->second;
    }

    // Reserve a stable slot before the GPU upload occurs.
    // The slot will be filled by FlushPendingLoads() on the next RenderFrame().
    // Insert a placeholder GPUMesh so the vector slot exists.
    const uint32_t slotIndex = static_cast<uint32_t>(m_meshes.size());
    m_meshes.emplace_back();          // placeholder (IsValid() == false)
    m_meshMap[path] = slotIndex;

    m_pendingMeshPaths.push_back(path);
    UZ_LOG(Renderer, Debug, "SceneRenderer: queued mesh load '{}' (slot={})", path, slotIndex);
    return slotIndex;
}

uint32_t SceneRenderer::QueueTextureLoad(const std::string& path) {
    if (path.empty()) return UINT32_MAX;

    // Return cached ID immediately — no need to queue.
    auto it = m_textureMap.find(path);
    if (it != m_textureMap.end()) {
        return it->second;
    }

    // Reserve a stable slot before the GPU upload occurs.
    const uint32_t slotIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.emplace_back();        // placeholder (handle.IsValid() == false)
    m_textureMap[path] = slotIndex;

    m_pendingTexturePaths.push_back(path);
    UZ_LOG(Renderer, Debug, "SceneRenderer: queued texture load '{}' (slot={})", path, slotIndex);
    return slotIndex;
}

void SceneRenderer::FlushPendingLoads(IRHICommandList* cmdList, IVFS* vfs) {
    // Flush pending mesh uploads.
    for (const std::string& path : m_pendingMeshPaths) {
        const uint32_t slotIndex = m_meshMap.at(path);
        Mesh cpuMesh = uz::LoadMesh(path);
        if (!cpuMesh.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to load queued mesh '{}'", path);
            continue;
        }
        GPUMesh gpuMesh = UploadMesh(m_device, cmdList, cpuMesh);
        if (!gpuMesh.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to upload queued mesh '{}'", path);
            continue;
        }
        m_meshes[slotIndex] = gpuMesh;
        UZ_LOG(Renderer, Info,
            "SceneRenderer: flushed mesh '{}' (id={}, {} verts, {} indices)",
            path, slotIndex, gpuMesh.vertexCount, gpuMesh.indexCount);
    }
    m_pendingMeshPaths.clear();

    // Flush pending texture uploads.
    for (const std::string& path : m_pendingTexturePaths) {
        const uint32_t slotIndex = m_textureMap.at(path);
        TextureLoadResult result = TextureLoader::Load(m_device, cmdList, path);
        if (!result.handle.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to load queued texture '{}'", path);
            continue;
        }
        m_textures[slotIndex] = result;
        UZ_LOG(Renderer, Info,
            "SceneRenderer: flushed texture '{}' (id={}, {}x{}, srv={})",
            path, slotIndex, result.width, result.height, result.srvIndex);
    }
    m_pendingTexturePaths.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

// CreateShadowAtlas() moved to ShadowAtlas::Init() in shadows/shadow_atlas.cpp

void SceneRenderer::DestroyAssets() {
    for (auto& mesh : m_meshes) {
        DestroyGPUMesh(m_device, mesh);
    }
    m_meshes.clear();
    m_meshMap.clear();

    for (auto& tex : m_textures) {
        if (tex.handle.IsValid()) {
            m_device->DestroyTexture(tex.handle);
        }
    }
    m_textures.clear();
    m_textureMap.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

Camera& SceneRenderer::GetCamera() { return *m_camera; }
const Camera& SceneRenderer::GetCamera() const { return *m_camera; }
FPSCameraController& SceneRenderer::GetCameraController() { return *m_cameraCtrl; }
InputSystem* SceneRenderer::GetInput() const { return m_input.get(); }
StatsOverlay* SceneRenderer::GetStatsOverlay() const { return m_statsOverlay.get(); }

} // namespace uz
