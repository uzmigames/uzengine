/// UzEngine Scene Renderer — Lifecycle (Init, Resize, Shutdown, resource helpers).

#include "scene_renderer.h"

#include "renderer_core.h"
#include "subsystems.h"

#include "atmosphere/cloud_pass.h"
#include "async_texture_decoder.h"
#include "camera.h"
#include "units.h"
#include "fps_camera_controller.h"
#include "render_graph_resources.h"
#include "shadow_atlas.h"
#include "shadow_pass.h"
#include "rhi.h"
#include "engine_assert.h"
#include "input.h"
#include "platform_window.h"
#include "log.h"
#include "editor_grid.h"
#include "stats_overlay.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "dynamic_resolution.h"
#include "cvar.h"

#include <cmath>

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

    // Compute internal render resolution from screen percentage.
    // At 100% (default), render resolution equals display resolution.
    m_renderWidth  = std::max(1u, static_cast<uint32_t>(width  * m_screenPercentage / 100.f));
    m_renderHeight = std::max(1u, static_cast<uint32_t>(height * m_screenPercentage / 100.f));

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

    // ── Pre-warm all pipelines eagerly ──────────────────────────────────
    // Compile all standard shaders now to avoid lazy DXC invocations during
    // the first rendered frame. This eliminates 20-35 cold shader compiles
    // that would otherwise stall the render thread on first use.
    const uint32_t warmedPipelines = m_pipelines->WarmupAll();
    UZ_LOG(Renderer, Info, "SceneRenderer: pre-warmed {} pipelines at startup", warmedPipelines);

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
    m_camera->SetHorizontalFOV(90.f, aspect, uz::Units::kDefaultNearPlaneCm, uz::Units::kDefaultFarPlaneCm);
    m_camera->SetPosition({0.f, 200.f, -500.f});
    m_camera->LookAt({0.f, 0.f, 0.f}, Vec3::Up());
    m_cameraCtrl->InitFromCamera(*m_camera);
    m_cameraCtrl->SetMoveSpeed(500.f);  // 500 cm/s = 5 m/s 

    // ── GBuffer (at render resolution — upscaled to display res by TAA) ──
    if (!m_gbuffer.Create(device, m_renderWidth, m_renderHeight)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create GBuffer");
        return false;
    }

    // ── HDR Scene Color (RGBA16F — at render resolution, upscaled by TAA) ──
    {
        RHITextureDesc hdrDesc{};
        hdrDesc.width     = m_renderWidth;
        hdrDesc.height    = m_renderHeight;
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
               m_renderWidth, m_renderHeight, m_hdrSceneColorSrv);
    }

    // ── Shadow Atlas ────────────────────────────────────────────────────
    m_shadowAtlas.Init(m_device);

    // ── Local Light Shadow Atlas (spot/point light shadows) ──────────
    [[maybe_unused]] bool localOk = m_localShadowAtlas.Init(m_device);

    // ── Shadow Cache Manager + Static Shadow Atlas ──────────────────
    // The static shadow atlas is a separate depth texture with the same
    // dimensions as the dynamic atlas. Static geometry is rendered once
    // (per invalidation) and reused across frames. The pixel shader
    // composites both atlases: finalShadow = min(static, dynamic).
    {
        ShadowCacheConfig cacheConfig{};
        cacheConfig.enabled = true;
        cacheConfig.cacheBudgetMB = 150;
        cacheConfig.scrollingOverlapThreshold = 0.75f;
        m_shadowCacheManager.Init(m_device, cacheConfig);

        // Create static shadow atlas with same dimensions as dynamic atlas
        const uint32_t cols = GetAtlasColumns(4); // default 4 cascades
        const uint32_t rows = GetAtlasRows(4);
        const uint32_t atlasW = cols * kShadowResolution;
        const uint32_t atlasH = rows * kShadowResolution;

        RHITextureDesc staticAtlasDesc{};
        staticAtlasDesc.width     = atlasW;
        staticAtlasDesc.height    = atlasH;
        staticAtlasDesc.format    = RHIFormat::D32Float;
        staticAtlasDesc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
        staticAtlasDesc.debugName = "StaticShadowAtlas";

        m_staticShadowAtlas = m_device->CreateTexture(staticAtlasDesc);
        if (m_staticShadowAtlas.IsValid()) {
            m_staticShadowAtlasSrv = m_device->GetTextureSRVIndex(m_staticShadowAtlas);
            UZ_LOG(Renderer, Info, "SceneRenderer: Static shadow atlas created ({}x{}, D32F, SRV={})",
                   atlasW, atlasH, m_staticShadowAtlasSrv);
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: Failed to create static shadow atlas — shadow caching disabled");
            m_staticShadowAtlasSrv = UINT32_MAX;
        }
    }

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

    // ── Multi-Scattering LUT texture (32x32 RGBA16F) ──────────────────────
    // Precomputed infinite-bounce multi-scattering transfer function.
    // R11G11B10F is ideal but our RHI doesn't support that format;
    // RGBA16F is the closest available (8 KB at 32x32, negligible overhead).
    {
        RHITextureDesc msDesc{};
        msDesc.width     = 32;
        msDesc.height    = 32;
        msDesc.format    = RHIFormat::RGBA16Float;
        msDesc.usage     = RHITextureUsage::ShaderResource
                         | RHITextureUsage::UnorderedAccess;
        msDesc.debugName = "MultiScatterLUT";

        m_multiScatterLut = m_device->CreateTexture(msDesc);
        if (!m_multiScatterLut.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create MultiScatterLUT texture");
        } else {
            m_multiScatterLutSrv = m_device->GetTextureSRVIndex(m_multiScatterLut);
            m_multiScatterLutUav = m_device->GetTextureUAVIndex(m_multiScatterLut);
            UZ_LOG(Renderer, Info, "SceneRenderer: MultiScatterLUT created (32x32 RGBA16F)");
        }
    }

    // ── Aerial Perspective Volume (32x32x16 RGBA16F 3D) ────────────────
    // Precomputed per-frame inscattering + transmittance 3D texture.
    {
        RHITextureDesc apDesc{};
        apDesc.width            = 32;
        apDesc.height           = 32;
        apDesc.depthOrArraySize = 16;
        apDesc.is3D             = true;
        apDesc.format           = RHIFormat::RGBA16Float;
        apDesc.usage            = RHITextureUsage::ShaderResource
                                | RHITextureUsage::UnorderedAccess;
        apDesc.debugName        = "APVolume_3D";

        m_apVolume = m_device->CreateTexture(apDesc);
        if (!m_apVolume.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create APVolume 3D texture");
        } else {
            m_apVolumeSrv = m_device->GetTextureSRVIndex(m_apVolume);
            m_apVolumeUav = m_device->GetTextureUAVIndex(m_apVolume);
            UZ_LOG(Renderer, Info, "SceneRenderer: APVolume created (32x32x16 RGBA16F 3D, SRV={}, UAV={})",
                   m_apVolumeSrv, m_apVolumeUav);
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

    // ── GTAO (at render resolution — operates on GBuffer depth/normals) ──
    m_gtaoPass = std::make_unique<GTAOPass>();
    if (!m_gtaoPass->Init(device, m_shaderMgr.get(), m_renderWidth, m_renderHeight)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: GTAO init failed (SSAO disabled)");
    }

    // ── Bloom (at render resolution — operates on HDR scene color) ───
    UZ_LOG(Renderer, Info, "SceneRenderer: initializing BloomPass...");
    m_bloomPass = std::make_unique<BloomPass>();
    if (!m_bloomPass->Init(device, m_shaderMgr.get(), m_renderWidth, m_renderHeight)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: bloom init failed (bloom disabled)");
    }
    UZ_LOG(Renderer, Info, "SceneRenderer: BloomPass initialized");

    // ── Lens Flare (at display resolution — post-bloom effect) ──────
    m_lensFlarePass = std::make_unique<LensFlarePass>();
    m_lensFlarePass->Init(device, m_shaderMgr.get(), width, height);

    // ── Volumetric Clouds (compute pipeline, half-res output) ────────
    m_cloudPass = std::make_unique<CloudPass>();
    if (!m_cloudPass->Init(m_device, m_shaderMgr.get(), m_renderWidth, m_renderHeight)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: CloudPass init failed (clouds disabled)");
        m_cloudPass.reset();
    }
    // Dedicated command list for cloud compute — isolates cloud barrier
    // management from the main render frame command list to avoid DX12
    // debug layer state tracking conflicts.
    m_cloudCmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
    if (!m_cloudCmdList) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: failed to create cloud command list");
    }

    // ── DFS Output Texture + Culling Buffers (at render resolution) ──
    // Created here but only populated when DFS is enabled at runtime.
    // The output texture starts at half-resolution by default; fullResolution
    // mode can be toggled via DistanceFieldShadowConfig.
    CreateDfsResources(m_renderWidth, m_renderHeight);

    // ── Sky cubemap (64x64x6 RGBA16F) for IBL capture ──────────────────
    {
        RHITextureDesc cubeDesc{};
        cubeDesc.width     = 64;
        cubeDesc.height    = 64;
        cubeDesc.depthOrArraySize = 6;
        cubeDesc.format    = RHIFormat::RGBA16Float;
        cubeDesc.usage     = RHITextureUsage::ShaderResource
                           | RHITextureUsage::UnorderedAccess
                           | RHITextureUsage::RenderTarget
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

    // ── Hi-Z Pyramid (at render resolution — used by depth prepass/occlusion) ──
    CreateHiZPyramid(m_renderWidth, m_renderHeight);

    // ── TAA History Textures (at DISPLAY resolution — TAA is the upscale point) ──
    CreateTaaResources(width, height);

    // ── SSGI (at render resolution — half-res internal, full-res output) ──
    if (!m_ssgiRenderer.CreateResources(device, m_renderWidth, m_renderHeight)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: SSGI init failed (SSGI disabled)");
    }

    // ── SDF-based GI (Global Distance Field + Radiance Cache) ──────────
    if (!m_globalDistanceField.CreateResources(device)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: GDF init failed (SDF GI disabled)");
    }
    if (!m_radianceCache.CreateResources(device, 5000.0f)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: RadianceCache init failed (SDF GI disabled)");
    }
    // Enable SDF GI when both subsystems initialized successfully.
    m_sdfGiEnabled = m_globalDistanceField.IsInitialized() && m_radianceCache.IsInitialized();
    if (m_sdfGiEnabled) {
        UZ_LOG(Renderer, Info, "SceneRenderer: SDF GI enabled (GDF + RadianceCache ready)");
    }

    // ── SSR Output Textures (at render resolution — operates on GBuffer) ──
    CreateSsrResources(m_renderWidth, m_renderHeight);

    // ── Color Grading LUT (32x32x32 3D texture) ────────────────────────
    CreateColorGradingLutResources();

    // ── Motion Blur Output (at render resolution — pre-upscale effect) ──
    CreateMotionBlurResources(m_renderWidth, m_renderHeight);

    // ── DOF Textures (at render resolution — pre-upscale effect) ─────
    CreateDofResources(m_renderWidth, m_renderHeight);

    // ── FXAA LDR Intermediate (at DISPLAY resolution — post-tonemap AA) ──
    CreateFxaaResources(width, height);

    // ── Async Texture Decoder (4 worker threads for parallel stbi_load) ──
    m_asyncDecoder = std::make_unique<AsyncTextureDecoder>(4);

    // ── Temporal Upscaler (default: TAA — matches existing behavior) ────
    m_upscaler = CreateTemporalUpscaler(m_upscalerType);
    if (m_upscaler) {
        m_upscaler->Init(m_device, m_pipelines.get(), m_cbRing.get());
    }

    // ── Dynamic Resolution Controller ────────────────────────────────────
    m_dynRes = std::make_unique<DynamicResolutionController>();

    // ── Register CVars ───────────────────────────────────────────────────
    RegisterCVars();

    UZ_LOG(Renderer, Info, "SceneRenderer initialized ({}x{} display, {}x{} render, {}% scale, {})",
           width, height, m_renderWidth, m_renderHeight,
           m_screenPercentage, RenderPathToString(m_renderPath));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CVar Registration
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::RegisterCVars() {
    auto& cvars = CVarRegistry::Get();

    cvars.Register("r.ScreenPercentage", m_screenPercentage,
                   "Render scale percentage (10-200)", 10.f, 200.f);

    cvars.Register("r.AntiAliasingMethod", static_cast<float>(m_upscalerType),
                   "Anti-aliasing method: 0=None, 1=TAA, 2=TSR", 0.f, 5.f);

    cvars.Register("r.TSR.History.ScreenPercentage", 100.f,
                   "TSR history buffer resolution multiplier (100-200)", 100.f, 200.f);

    cvars.Register("r.DynamicResolution.Enabled", 0.f,
                   "Enable GPU-bound adaptive resolution", 0.f, 1.f);

    cvars.Register("r.DynamicResolution.TargetFPS", 60.f,
                   "Target FPS for dynamic resolution", 15.f, 240.f);

    cvars.Register("r.TSR.History.UpdateQuality", 2.f,
                   "TSR quality level: 0=Low, 1=Medium, 2=High, 3=Epic", 0.f, 3.f);

    // ── Pass Inspector CVars ─────────────────────────────────────────────
    // These CVars allow runtime control of the pass inspector via the CVar
    // system (console, Lua, IPC). They act as the "console command" mechanism
    // since UzEngine does not have a dedicated console command registry.

    /// pass.stats: 0=off, 1=show pass timing overlay
    cvars.Register("r.PassInspector.Stats", 0.f,
                   "Show per-pass GPU timing overlay (0=off, 1=on)", 0.f, 1.f);

    /// pass.validate: 0=off, 1=run validation this frame
    cvars.Register("r.PassInspector.Validate", 0.f,
                   "Run pass validation (0=off, 1=validate all)", 0.f, 1.f);

    /// pass.dump: frame number to dump at (0=off/disabled)
    cvars.Register("r.PassInspector.DumpFrame", 0.f,
                   "Dump all targets at frame N (0=off)", 0.f, 1e6f);

    /// pass.grid: 0=off, 1=4x4 grid, 2=6x4 extended grid
    cvars.Register("r.PassInspector.Grid", 0.f,
                   "Show pass grid overlay (0=off, 1=4x4, 2=6x4 extended)", 0.f, 2.f);

    /// pass.grid columns
    cvars.Register("r.PassInspector.GridColumns", 4.f,
                   "Pass grid column count", 1.f, 8.f);

    /// pass.grid rows
    cvars.Register("r.PassInspector.GridRows", 4.f,
                   "Pass grid row count", 1.f, 8.f);
}

void SceneRenderer::ApplyCVars() {
    auto& cvars = CVarRegistry::Get();

    // Screen percentage
    const float pct = cvars.GetFloat("r.ScreenPercentage", 100.f);
    if (std::abs(pct - m_screenPercentage) > 0.5f) {
        SetScreenPercentage(pct);
    }

    // Anti-aliasing method → upscaler type
    const auto aaMethod = static_cast<UpscalerType>(cvars.GetInt("r.AntiAliasingMethod", 2));
    if (aaMethod != m_upscalerType) {
        SetUpscalerType(aaMethod);
    }

    // Dynamic resolution
    const bool dynResEnabled = cvars.GetBool("r.DynamicResolution.Enabled");
    m_dynRes->SetEnabled(dynResEnabled);
    m_dynRes->SetTargetFPS(cvars.GetFloat("r.DynamicResolution.TargetFPS", 60.f));

    // ── Pass Inspector CVars ─────────────────────────────────────────────

    // pass.stats overlay
    m_passInspector.SetStatsOverlayEnabled(cvars.GetBool("r.PassInspector.Stats"));

    // pass.grid
    const int gridMode = cvars.GetInt("r.PassInspector.Grid");
    if (gridMode == 0) {
        m_passInspector.SetGridEnabled(false);
        m_passInspector.SetGridExtended(false);
    } else if (gridMode == 1) {
        m_passInspector.SetGridEnabled(true);
        m_passInspector.SetGridExtended(false);
    } else {
        m_passInspector.SetGridEnabled(true);
        m_passInspector.SetGridExtended(true);
    }

    PassGridConfig gridCfg;
    gridCfg.columns = static_cast<uint32_t>(cvars.GetInt("r.PassInspector.GridColumns", 4));
    gridCfg.rows    = static_cast<uint32_t>(cvars.GetInt("r.PassInspector.GridRows", 4));
    if (m_passInspector.IsGridExtended()) {
        gridCfg.columns = 6;
        gridCfg.rows    = 4;
    }
    m_passInspector.SetGridConfig(gridCfg);
}

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
// Hi-Z Pyramid Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

/// Helper that computes the number of mip levels for a given resolution.
static uint32_t ComputeMipCount(uint32_t width, uint32_t height) {
    uint32_t mips = 0;
    uint32_t w = width;
    uint32_t h = height;
    while (w > 1 || h > 1) {
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        ++mips;
        if (mips >= kHiZMaxMipLevels) break;
    }
    return mips;
}

void SceneRenderer::CreateHiZPyramid(uint32_t width, uint32_t height) {
    // Destroy any existing Hi-Z mips.
    DestroyHiZPyramid();

    m_hizMipCount = ComputeMipCount(width, height);
    if (m_hizMipCount == 0) return;

    uint32_t mipW = width;
    uint32_t mipH = height;

    for (uint32_t i = 0; i < m_hizMipCount; ++i) {
        mipW = (mipW > 1) ? (mipW >> 1) : 1;
        mipH = (mipH > 1) ? (mipH >> 1) : 1;

        RHITextureDesc texDesc{};
        texDesc.width     = mipW;
        texDesc.height    = mipH;
        texDesc.format    = RHIFormat::R32Float;
        texDesc.usage     = RHITextureUsage::ShaderResource
                          | RHITextureUsage::UnorderedAccess;
        texDesc.debugName = "HiZ_Mip";  // Device may append index for debug.

        RHITextureHandle tex = m_device->CreateTexture(texDesc);
        if (!tex.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create Hi-Z mip {} ({}x{})", i, mipW, mipH);
            // Partial creation — clean up and disable.
            DestroyHiZPyramid();
            return;
        }

        m_hizMipLevels[i].texture  = tex;
        m_hizMipLevels[i].srvIndex = m_device->GetTextureSRVIndex(tex);
        m_hizMipLevels[i].uavIndex = m_device->GetTextureUAVIndex(tex);
        m_hizMipLevels[i].width    = mipW;
        m_hizMipLevels[i].height   = mipH;
    }

    UZ_LOG(Renderer, Info, "SceneRenderer: Hi-Z pyramid created ({} mips, {}x{} -> {}x{})",
           m_hizMipCount, width, height,
           m_hizMipLevels[m_hizMipCount - 1].width,
           m_hizMipLevels[m_hizMipCount - 1].height);
}

void SceneRenderer::DestroyHiZPyramid() {
    for (uint32_t i = 0; i < m_hizMipCount; ++i) {
        if (m_hizMipLevels[i].texture.IsValid()) {
            m_device->DestroyTexture(m_hizMipLevels[i].texture);
            m_hizMipLevels[i] = {};
        }
    }
    m_hizMipCount = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// DFS Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateDfsResources(uint32_t width, uint32_t height) {
    // Only create DFS resources when DFS is enabled
    if (!m_dfsConfig.enabled) return;

    // Output texture: RG16Float (shadow_factor in R, scene_depth in G).
    // Half-res by default unless fullResolution is set.
    const uint32_t dfsWidth  = m_dfsConfig.fullResolution ? width  : width  / 2;
    const uint32_t dfsHeight = m_dfsConfig.fullResolution ? height : height / 2;

    if (dfsWidth == 0 || dfsHeight == 0) return;

    // ── DFS Output Texture ──────────────────────────────────────────────
    {
        RHITextureDesc dfsDesc{};
        dfsDesc.width     = dfsWidth;
        dfsDesc.height    = dfsHeight;
        dfsDesc.format    = RHIFormat::RG16Float;
        dfsDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        dfsDesc.debugName = "DFS_Output";

        m_dfsOutputTexture = m_device->CreateTexture(dfsDesc);
        if (m_dfsOutputTexture.IsValid()) {
            m_dfsOutputSrvIndex = m_device->GetTextureSRVIndex(m_dfsOutputTexture);
            m_dfsOutputUavIndex = m_device->GetTextureUAVIndex(m_dfsOutputTexture);
            UZ_LOG(Renderer, Info, "SceneRenderer: DFS output texture created ({}x{}, RG16F, SRV={}, UAV={})",
                   dfsWidth, dfsHeight, m_dfsOutputSrvIndex, m_dfsOutputUavIndex);
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: failed to create DFS output texture — DFS disabled");
        }
    }

    // ── DFS Culled Indices Buffer ────────────────────────────────────────
    // AppendStructuredBuffer<uint> with capacity for all SDF objects.
    {
        RHIBufferDesc bufDesc{};
        bufDesc.sizeBytes = kMaxSdfObjects * sizeof(uint32_t);
        bufDesc.stride    = sizeof(uint32_t);
        bufDesc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        bufDesc.debugName = "DFS_CulledIndices";

        m_dfsCulledIndicesBuffer = m_device->CreateBuffer(bufDesc);
        if (m_dfsCulledIndicesBuffer.IsValid()) {
            m_dfsCulledIndicesSrvIndex = m_device->GetBufferSRVIndex(m_dfsCulledIndicesBuffer);
            m_dfsCulledIndicesUavIndex = m_device->GetBufferUAVIndex(m_dfsCulledIndicesBuffer);
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: failed to create DFS culled indices buffer");
        }
    }

    // ── DFS Indirect Args Buffer ─────────────────────────────────────────
    // RWByteAddressBuffer for DispatchIndirect args (3 x uint32_t = 12 bytes).
    // The cull pass atomically increments the X thread group count.
    {
        RHIBufferDesc argsDesc{};
        argsDesc.sizeBytes = 3 * sizeof(uint32_t);
        argsDesc.stride    = 0; // Raw byte address buffer
        argsDesc.usage     = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::CopyDst;
        argsDesc.debugName = "DFS_IndirectArgs";

        m_dfsIndirectArgsBuffer = m_device->CreateBuffer(argsDesc);
        if (m_dfsIndirectArgsBuffer.IsValid()) {
            m_dfsIndirectArgsUavIndex = m_device->GetBufferUAVIndex(m_dfsIndirectArgsBuffer);
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: failed to create DFS indirect args buffer");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TAA Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateTaaResources(uint32_t width, uint32_t height) {
    DestroyTaaResources();

    for (int i = 0; i < 2; ++i) {
        RHITextureDesc taaDesc{};
        taaDesc.width     = width;
        taaDesc.height    = height;
        taaDesc.format    = RHIFormat::RGBA16Float;
        taaDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        taaDesc.debugName = (i == 0) ? "TAA_History_0" : "TAA_History_1";

        m_taaHistory[i] = m_device->CreateTexture(taaDesc);
        if (m_taaHistory[i].IsValid()) {
            m_taaHistorySrv[i] = m_device->GetTextureSRVIndex(m_taaHistory[i]);
            m_taaHistoryUav[i] = m_device->GetTextureUAVIndex(m_taaHistory[i]);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create TAA history texture {}", i);
        }
    }

    m_taaHistoryIndex = 0;
    m_prevViewProjValid = false;

    UZ_LOG(Renderer, Info, "SceneRenderer: TAA history textures created ({}x{}, RGBA16F)", width, height);
}

void SceneRenderer::DestroyTaaResources() {
    for (int i = 0; i < 2; ++i) {
        if (m_taaHistory[i].IsValid()) {
            m_device->DestroyTexture(m_taaHistory[i]);
            m_taaHistory[i] = RHITextureHandle{};
            m_taaHistorySrv[i] = UINT32_MAX;
            m_taaHistoryUav[i] = UINT32_MAX;
        }
    }
    m_taaHistoryIndex = 0;
    m_prevViewProjValid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSR Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateSsrResources(uint32_t width, uint32_t height) {
    DestroySsrResources();

    // SSR trace output: RGBA16F full-res, rgb=hit color, a=confidence
    {
        RHITextureDesc traceDesc{};
        traceDesc.width     = width;
        traceDesc.height    = height;
        traceDesc.format    = RHIFormat::RGBA16Float;
        traceDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        traceDesc.debugName = "SSR_TraceOutput";

        m_ssrTraceOutput = m_device->CreateTexture(traceDesc);
        if (m_ssrTraceOutput.IsValid()) {
            m_ssrTraceOutputSrv = m_device->GetTextureSRVIndex(m_ssrTraceOutput);
            m_ssrTraceOutputUav = m_device->GetTextureUAVIndex(m_ssrTraceOutput);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR trace output texture");
        }
    }

    // SSR resolved output: RGBA16F full-res, rgb=Fresnel-weighted reflection, a=blend weight
    {
        RHITextureDesc resolveDesc{};
        resolveDesc.width     = width;
        resolveDesc.height    = height;
        resolveDesc.format    = RHIFormat::RGBA16Float;
        resolveDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        resolveDesc.debugName = "SSR_ResolvedOutput";

        m_ssrResolvedOutput = m_device->CreateTexture(resolveDesc);
        if (m_ssrResolvedOutput.IsValid()) {
            m_ssrResolvedOutputSrv = m_device->GetTextureSRVIndex(m_ssrResolvedOutput);
            m_ssrResolvedOutputUav = m_device->GetTextureUAVIndex(m_ssrResolvedOutput);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR resolved output texture");
        }
    }

    UZ_LOG(Renderer, Info, "SceneRenderer: SSR resources created ({}x{}, RGBA16F, trace SRV={}, resolve SRV={})",
           width, height, m_ssrTraceOutputSrv, m_ssrResolvedOutputSrv);
}

void SceneRenderer::DestroySsrResources() {
    if (m_ssrTraceOutput.IsValid()) {
        m_device->DestroyTexture(m_ssrTraceOutput);
        m_ssrTraceOutput = RHITextureHandle{};
        m_ssrTraceOutputSrv = UINT32_MAX;
        m_ssrTraceOutputUav = UINT32_MAX;
    }
    if (m_ssrResolvedOutput.IsValid()) {
        m_device->DestroyTexture(m_ssrResolvedOutput);
        m_ssrResolvedOutput = RHITextureHandle{};
        m_ssrResolvedOutputSrv = UINT32_MAX;
        m_ssrResolvedOutputUav = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Color Grading LUT Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateColorGradingLutResources() {
    DestroyColorGradingLutResources();

    RHITextureDesc lutDesc{};
    lutDesc.width            = kColorGradingLutSize;
    lutDesc.height           = kColorGradingLutSize;
    lutDesc.depthOrArraySize = static_cast<uint16_t>(kColorGradingLutSize);
    lutDesc.is3D             = true;
    lutDesc.format           = RHIFormat::RGBA8Unorm;
    lutDesc.usage            = RHITextureUsage::ShaderResource
                             | RHITextureUsage::UnorderedAccess;
    lutDesc.debugName        = "ColorGradingLUT_3D";

    m_colorGradingLut = m_device->CreateTexture(lutDesc);
    if (m_colorGradingLut.IsValid()) {
        m_colorGradingLutSrv = m_device->GetTextureSRVIndex(m_colorGradingLut);
        m_colorGradingLutUav = m_device->GetTextureUAVIndex(m_colorGradingLut);
        m_colorGradingLutDirty = true;
        UZ_LOG(Renderer, Info, "SceneRenderer: Color grading LUT created ({}x{}x{}, RGBA8, 3D, SRV={}, UAV={})",
               kColorGradingLutSize, kColorGradingLutSize, kColorGradingLutSize,
               m_colorGradingLutSrv, m_colorGradingLutUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create color grading LUT texture");
    }
}

void SceneRenderer::DestroyColorGradingLutResources() {
    if (m_colorGradingLut.IsValid()) {
        m_device->DestroyTexture(m_colorGradingLut);
        m_colorGradingLut = RHITextureHandle{};
        m_colorGradingLutSrv = UINT32_MAX;
        m_colorGradingLutUav = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion Blur Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateMotionBlurResources(uint32_t width, uint32_t height) {
    DestroyMotionBlurResources();

    // ── Main blur output (full resolution) ───────────────────────────────
    RHITextureDesc desc{};
    desc.width     = width;
    desc.height    = height;
    desc.format    = RHIFormat::RGBA16Float;
    desc.usage     = RHITextureUsage::ShaderResource
                   | RHITextureUsage::UnorderedAccess;
    desc.debugName = "MotionBlur_Output";

    m_motionBlurOutput = m_device->CreateTexture(desc);
    if (m_motionBlurOutput.IsValid()) {
        m_motionBlurOutputSrv = m_device->GetTextureSRVIndex(m_motionBlurOutput);
        m_motionBlurOutputUav = m_device->GetTextureUAVIndex(m_motionBlurOutput);
        UZ_LOG(Renderer, Info, "SceneRenderer: Motion blur output created ({}x{}, RGBA16F, SRV={}, UAV={})",
               width, height, m_motionBlurOutputSrv, m_motionBlurOutputUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create motion blur output texture");
    }

    // ── Tile max texture (1/16th resolution) ─────────────────────────────
    const uint32_t tileW = (width  + 15) / 16;
    const uint32_t tileH = (height + 15) / 16;

    RHITextureDesc tileDesc{};
    tileDesc.width     = tileW;
    tileDesc.height    = tileH;
    tileDesc.format    = RHIFormat::RG16Float;
    tileDesc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
    tileDesc.debugName = "MotionBlur_TileMax";

    m_motionBlurTileMax = m_device->CreateTexture(tileDesc);
    if (m_motionBlurTileMax.IsValid()) {
        m_motionBlurTileMaxSrv = m_device->GetTextureSRVIndex(m_motionBlurTileMax);
        m_motionBlurTileMaxUav = m_device->GetTextureUAVIndex(m_motionBlurTileMax);
        UZ_LOG(Renderer, Info, "SceneRenderer: Motion blur tile max created ({}x{}, RG16F, SRV={}, UAV={})",
               tileW, tileH, m_motionBlurTileMaxSrv, m_motionBlurTileMaxUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create motion blur tile max texture");
    }

    // ── Neighbor max texture (same tile resolution) ──────────────────────
    RHITextureDesc neighborDesc{};
    neighborDesc.width     = tileW;
    neighborDesc.height    = tileH;
    neighborDesc.format    = RHIFormat::RG16Float;
    neighborDesc.usage     = RHITextureUsage::ShaderResource
                           | RHITextureUsage::UnorderedAccess;
    neighborDesc.debugName = "MotionBlur_NeighborMax";

    m_motionBlurNeighborMax = m_device->CreateTexture(neighborDesc);
    if (m_motionBlurNeighborMax.IsValid()) {
        m_motionBlurNeighborMaxSrv = m_device->GetTextureSRVIndex(m_motionBlurNeighborMax);
        m_motionBlurNeighborMaxUav = m_device->GetTextureUAVIndex(m_motionBlurNeighborMax);
        UZ_LOG(Renderer, Info, "SceneRenderer: Motion blur neighbor max created ({}x{}, RG16F, SRV={}, UAV={})",
               tileW, tileH, m_motionBlurNeighborMaxSrv, m_motionBlurNeighborMaxUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create motion blur neighbor max texture");
    }
}

void SceneRenderer::DestroyMotionBlurResources() {
    if (m_motionBlurOutput.IsValid()) {
        m_device->DestroyTexture(m_motionBlurOutput);
        m_motionBlurOutput = RHITextureHandle{};
        m_motionBlurOutputSrv = UINT32_MAX;
        m_motionBlurOutputUav = UINT32_MAX;
    }
    if (m_motionBlurTileMax.IsValid()) {
        m_device->DestroyTexture(m_motionBlurTileMax);
        m_motionBlurTileMax = RHITextureHandle{};
        m_motionBlurTileMaxSrv = UINT32_MAX;
        m_motionBlurTileMaxUav = UINT32_MAX;
    }
    if (m_motionBlurNeighborMax.IsValid()) {
        m_device->DestroyTexture(m_motionBlurNeighborMax);
        m_motionBlurNeighborMax = RHITextureHandle{};
        m_motionBlurNeighborMaxSrv = UINT32_MAX;
        m_motionBlurNeighborMaxUav = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FXAA Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateFxaaResources(uint32_t width, uint32_t height) {
    DestroyFxaaResources();

    RHITextureDesc desc{};
    desc.width     = width;
    desc.height    = height;
    desc.format    = RHIFormat::RGBA8Unorm;
    desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
    desc.debugName = "FXAA_LdrIntermediate";

    m_fxaaLdrIntermediate = m_device->CreateTexture(desc);
    if (m_fxaaLdrIntermediate.IsValid()) {
        m_fxaaLdrIntermediateSrv = m_device->GetTextureSRVIndex(m_fxaaLdrIntermediate);
        UZ_LOG(Renderer, Info, "SceneRenderer: FXAA LDR intermediate created ({}x{}, RGBA8, SRV={})",
               width, height, m_fxaaLdrIntermediateSrv);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create FXAA LDR intermediate texture");
    }
}

void SceneRenderer::DestroyFxaaResources() {
    if (m_fxaaLdrIntermediate.IsValid()) {
        m_device->DestroyTexture(m_fxaaLdrIntermediate);
        m_fxaaLdrIntermediate = RHITextureHandle{};
        m_fxaaLdrIntermediateSrv = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DOF Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateDofResources(uint32_t width, uint32_t height) {
    DestroyDofResources();

    // CoC texture — R16F signed circle of confusion per pixel
    {
        RHITextureDesc cocDesc{};
        cocDesc.width     = width;
        cocDesc.height    = height;
        cocDesc.format    = RHIFormat::R16Float;
        cocDesc.usage     = RHITextureUsage::ShaderResource
                          | RHITextureUsage::UnorderedAccess;
        cocDesc.debugName = "DOF_CoC";

        m_dofCocTexture = m_device->CreateTexture(cocDesc);
        if (m_dofCocTexture.IsValid()) {
            m_dofCocSrv = m_device->GetTextureSRVIndex(m_dofCocTexture);
            m_dofCocUav = m_device->GetTextureUAVIndex(m_dofCocTexture);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create DOF CoC texture");
        }
    }

    // DOF output — RGBA16F blurred result
    {
        RHITextureDesc outDesc{};
        outDesc.width     = width;
        outDesc.height    = height;
        outDesc.format    = RHIFormat::RGBA16Float;
        outDesc.usage     = RHITextureUsage::ShaderResource
                          | RHITextureUsage::UnorderedAccess;
        outDesc.debugName = "DOF_Output";

        m_dofOutputTexture = m_device->CreateTexture(outDesc);
        if (m_dofOutputTexture.IsValid()) {
            m_dofOutputSrv = m_device->GetTextureSRVIndex(m_dofOutputTexture);
            m_dofOutputUav = m_device->GetTextureUAVIndex(m_dofOutputTexture);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create DOF output texture");
        }
    }

    UZ_LOG(Renderer, Info, "SceneRenderer: DOF textures created ({}x{}, CoC SRV={}, Output SRV={})",
           width, height, m_dofCocSrv, m_dofOutputSrv);
}

void SceneRenderer::DestroyDofResources() {
    if (m_dofCocTexture.IsValid()) {
        m_device->DestroyTexture(m_dofCocTexture);
        m_dofCocTexture = RHITextureHandle{};
        m_dofCocSrv = UINT32_MAX;
        m_dofCocUav = UINT32_MAX;
    }
    if (m_dofOutputTexture.IsValid()) {
        m_device->DestroyTexture(m_dofOutputTexture);
        m_dofOutputTexture = RHITextureHandle{};
        m_dofOutputSrv = UINT32_MAX;
        m_dofOutputUav = UINT32_MAX;
    }
}

void SceneRenderer::DestroyDfsResources() {
    if (m_dfsOutputTexture.IsValid()) {
        m_device->DestroyTexture(m_dfsOutputTexture);
        m_dfsOutputTexture = RHITextureHandle{};
        m_dfsOutputSrvIndex = UINT32_MAX;
        m_dfsOutputUavIndex = UINT32_MAX;
    }
    if (m_dfsCulledIndicesBuffer.IsValid()) {
        m_device->DestroyBuffer(m_dfsCulledIndicesBuffer);
        m_dfsCulledIndicesBuffer = RHIBufferHandle{};
        m_dfsCulledIndicesSrvIndex = UINT32_MAX;
        m_dfsCulledIndicesUavIndex = UINT32_MAX;
    }
    if (m_dfsIndirectArgsBuffer.IsValid()) {
        m_device->DestroyBuffer(m_dfsIndirectArgsBuffer);
        m_dfsIndirectArgsBuffer = RHIBufferHandle{};
        m_dfsIndirectArgsUavIndex = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;

    m_width = width;
    m_height = height;

    // Recompute internal render resolution from screen percentage.
    m_renderWidth  = std::max(1u, static_cast<uint32_t>(width  * m_screenPercentage / 100.f));
    m_renderHeight = std::max(1u, static_cast<uint32_t>(height * m_screenPercentage / 100.f));

    // ── Render-resolution targets (GBuffer, HDR, effects before upscale) ──

    if (m_gbuffer.IsValid()) {
        [[maybe_unused]] bool ok = m_gbuffer.Resize(m_device, m_renderWidth, m_renderHeight);
        UZ_CHECK(ok);
    }

    // Recreate HDR scene color at render resolution
    if (m_hdrSceneColor.IsValid()) {
        m_device->DestroyTexture(m_hdrSceneColor);
        RHITextureDesc hdrDesc{};
        hdrDesc.width     = m_renderWidth;
        hdrDesc.height    = m_renderHeight;
        hdrDesc.format    = RHIFormat::RGBA16Float;
        hdrDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
        hdrDesc.debugName = "HDR_SceneColor";
        m_hdrSceneColor = m_device->CreateTexture(hdrDesc);
        m_hdrSceneColorSrv = m_device->GetTextureSRVIndex(m_hdrSceneColor);
    }

    // Recreate Hi-Z pyramid at render resolution
    CreateHiZPyramid(m_renderWidth, m_renderHeight);

    // Recreate GTAO textures at render resolution
    if (m_gtaoPass && m_gtaoPass->IsInitialized()) {
        [[maybe_unused]] bool gtaoOk = m_gtaoPass->Resize(m_renderWidth, m_renderHeight);
    }

    // Recreate DFS output texture at render resolution
    DestroyDfsResources();
    CreateDfsResources(m_renderWidth, m_renderHeight);

    // ── Display-resolution targets (TAA output is the upscale point) ──

    // Recreate TAA history textures at DISPLAY resolution (upscale target)
    CreateTaaResources(width, height);

    // ── Render-resolution targets (effects before upscale) ──

    // Recreate SSGI textures at render resolution
    m_ssgiRenderer.OnResize(m_renderWidth, m_renderHeight);

    // Recreate cloud output textures at render resolution
    if (m_cloudPass && m_cloudPass->IsInitialized()) {
        m_cloudPass->OnResize(m_renderWidth, m_renderHeight);
    }

    // Recreate SSR output textures at render resolution
    DestroySsrResources();
    CreateSsrResources(m_renderWidth, m_renderHeight);

    // Recreate motion blur output at render resolution
    DestroyMotionBlurResources();
    CreateMotionBlurResources(m_renderWidth, m_renderHeight);

    // Recreate DOF textures at render resolution
    DestroyDofResources();
    CreateDofResources(m_renderWidth, m_renderHeight);

    // ── Display-resolution targets (post-tonemap AA) ──

    // Recreate FXAA LDR intermediate at display resolution
    DestroyFxaaResources();
    CreateFxaaResources(width, height);

    // Color grading LUT is resolution-independent (32x32x32 volume), no resize needed.
    // Just mark dirty so it regenerates with any changed parameters.
    m_colorGradingLutDirty = true;

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

    // Wait for GPU to finish all in-flight work before destroying any resources.
    // Without this, D3D12MA asserts "Some allocations were not freed" because
    // resources are destroyed while the GPU still references them.
    m_device->WaitIdle();

    // Destroy screenshot readback buffer
    m_screenshotCapture.Shutdown();

    // Destroy shadow readback buffer if still alive
    if (m_shadowReadbackBuffer.IsValid()) {
        m_device->DestroyBuffer(m_shadowReadbackBuffer);
        m_shadowReadbackBuffer = RHIBufferHandle{};
    }

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
    m_localShadowAtlas.Shutdown(m_device);

    // ── Shadow Cache + Static Shadow Atlas ───────────────────────────
    m_shadowCacheManager.Shutdown();
    if (m_staticShadowAtlas.IsValid()) {
        m_device->DestroyTexture(m_staticShadowAtlas);
        m_staticShadowAtlas = RHITextureHandle{};
        m_staticShadowAtlasSrv = UINT32_MAX;
    }

    // ── Aerial Perspective Volume ────────────────────────────────────
    if (m_apVolume.IsValid()) {
        m_device->DestroyTexture(m_apVolume);
        m_apVolume = RHITextureHandle{};
    }

    // ── Transmittance LUT ────────────────────────────────────────────
    if (m_transmittanceLut.IsValid()) {
        m_device->DestroyTexture(m_transmittanceLut);
        m_transmittanceLut = RHITextureHandle{};
    }

    // ── Multi-Scatter LUT ────────────────────────────────────────────
    if (m_multiScatterLut.IsValid()) {
        m_device->DestroyTexture(m_multiScatterLut);
        m_multiScatterLut = RHITextureHandle{};
    }

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

    // ── Hi-Z Pyramid cleanup ─────────────────────────────────────
    DestroyHiZPyramid();

    // ── DFS cleanup ─────────────────────────────────────────────────
    DestroyDfsResources();

    // ── SSGI cleanup ──────────────────────────────────────────────────
    m_ssgiRenderer.DestroyResources();

    // ── SDF GI cleanup ───────────────────────────────────────────────
    m_radianceCache.DestroyResources();
    m_globalDistanceField.DestroyResources();

    // ── SSR cleanup ──────────────────────────────────────────────────
    DestroySsrResources();

    // ── TAA cleanup ──────────────────────────────────────────────────
    DestroyTaaResources();

    // ── Temporal Upscaler cleanup ─────────────────────────────────────
    if (m_upscaler) {
        m_upscaler->Shutdown();
        m_upscaler.reset();
    }

    // ── Dynamic Resolution cleanup ────────────────────────────────────
    m_dynRes.reset();

    // ── Color Grading LUT cleanup ───────────────────────────────────
    DestroyColorGradingLutResources();

    // ── Motion Blur cleanup ─────────────────────────────────────────
    DestroyMotionBlurResources();

    // ── DOF cleanup ─────────────────────────────────────────────────
    DestroyDofResources();

    // ── FXAA cleanup ──────────────────────────────────────────────────
    DestroyFxaaResources();

    // ── Auto-Exposure cleanup ─────────────────────────────────────
    if (m_autoExposure) m_autoExposure->Shutdown();
    if (m_gtaoPass) m_gtaoPass->Shutdown();
    if (m_cloudPass) { m_cloudPass->Shutdown(); m_cloudPass.reset(); }
    if (m_cloudCmdList) { m_device->DestroyCommandList(m_cloudCmdList); m_cloudCmdList = nullptr; }
    if (m_lensFlarePass) m_lensFlarePass->Shutdown();
    if (m_bloomPass) m_bloomPass->Shutdown();

    // ── Instance buffer cleanup ─────────────────────────────────────
    for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
        if (m_instanceBuffers[i].IsValid()) {
            m_device->DestroyBuffer(m_instanceBuffers[i]);
            m_instanceBuffers[i] = RHIBufferHandle{};
        }
    }

    // ── Async Texture Decoder cleanup ─────────────────────────────
    if (m_asyncDecoder) m_asyncDecoder->Shutdown();
    m_asyncDecoder.reset();
    m_asyncPendingSlots.clear();

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

} // namespace uz
