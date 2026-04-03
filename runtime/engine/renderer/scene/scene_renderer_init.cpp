/// UzEngine Scene Renderer — Lifecycle (Init, Resize, Shutdown, CVar registration).

#include "scene_renderer.h"
#include "quality_presets.h"

#include "renderer_core.h"
#include "subsystems.h"

#include "atmosphere/cloud_pass.h"
#include "async_texture_decoder.h"
#include "camera.h"
#include "units.h"
#include "fly_camera_controller.h"
#include "render_graph_resources.h"
#include "shadow_atlas.h"
#include "shadow_pass.h"
#include "rhi.h"
#include "engine_assert.h"
#include "input.h"
#include "platform_window.h"
#include "log.h"
#include "sh_projection.h"
#include "editor_grid.h"
#include "gizmo_renderer.h"
#include "outline_pass.h"
#include "stats_overlay.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "dynamic_resolution.h"
#include "cvar.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/asset/vfx_hot_reload.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/scripting/lua_vm.h"
#include "engine/scripting/scene/vfx_lua_bindings.h"
#include "engine/scripting/scene/trail_lua_bindings.h"
#include "engine/scripting/scene/decal_lua_bindings.h"

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

    // ── Quality Presets (default Ultra, overridable via CVar quality=N) ──
    QualityPresetManager::Get().Init();

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
    // SHADER DEBUG: disabled — some new shaders have root sig mismatch.
    // Pipelines will be created lazily on first use instead.
    // const uint32_t warmedPipelines = m_pipelines->WarmupAll();
    // UZ_LOG(Renderer, Info, "SceneRenderer: pre-warmed {} pipelines at startup", warmedPipelines);
    UZ_LOG(Renderer, Warning, "SceneRenderer: WarmupAll DISABLED for shader debugging");

    // ── CB Ring Allocator ───────────────────────────────────────────────
    m_cbRing = std::make_unique<CBRingAllocator>();
    m_cbRing->Init(device, kCBRingFrames, kCBRingBytesPerFrame);

    // ── Editor Grid ─────────────────────────────────────────────────────
    m_editorGrid = std::make_unique<EditorGrid>();
    if (!m_editorGrid->Init(device, m_shaderMgr->GetPipeline(),
                            RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to init editor grid");
    }

    // ── Outline Pass (editor selection outline) ────────────────────────
    m_outlinePass = std::make_unique<OutlinePass>();
    if (!m_outlinePass->Init(device, m_shaderMgr->GetPipeline(),
                             RHIFormat::RGBA8Unorm, RHIFormat::D32Float,
                             width, height)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to init outline pass — disabling");
        m_outlinePass.reset();
    }

    // ── Gizmo Renderer (transform gizmo handles) ────────────────────
    m_gizmoRenderer = std::make_unique<GizmoRenderer>();
    if (!m_gizmoRenderer->Init(device, m_shaderMgr->GetPipeline(),
                                RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to init gizmo renderer");
    }

    // ── Stats Overlay (deferred init — needs cmdList in recording state) ─
    m_statsOverlay = std::make_unique<StatsOverlay>();
    m_statsOverlayInitialized = false;

    // ── Render Graph Resource Pool ──────────────────────────────────────
    m_rgPool = std::make_unique<rg::RgResourcePool>(device);

    // ── Camera ──────────────────────────────────────────────────────────
    m_camera = std::make_unique<Camera>();
    m_cameraCtrl = std::make_unique<FlyCameraController>();

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    m_camera->SetHorizontalFOV(90.f, aspect, uz::Units::kDefaultNearPlaneCm, uz::Units::kDefaultFarPlaneCm);
    m_camera->SetPosition({0.f, 200.f, -500.f});
    m_camera->LookAt({0.f, 0.f, 0.f}, Vec3::Up());
    m_cameraCtrl->InitFromCamera(*m_camera);
    m_cameraCtrl->SetMoveSpeed(5.f);  // 5 cm/s — matches Lua pbr_showcase_controller default

    // ── GBuffer (at render resolution — upscaled to display res by TAA) ──
    if (!m_gbuffer.Create(device, m_renderWidth, m_renderHeight)) {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create GBuffer");
        return false;
    }

    // ── DBuffer (Deferred Decals — 3 RGBA8 targets at render resolution) ──
    // @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
    {
        RHITextureDesc dbufDesc{};
        dbufDesc.width  = m_renderWidth;
        dbufDesc.height = m_renderHeight;
        dbufDesc.format = RHIFormat::RGBA8Unorm;
        dbufDesc.usage  = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;

        dbufDesc.debugName = "DBuffer_A_AlbedoOpacity";
        m_dbufferA = device->CreateTexture(dbufDesc);
        if (m_dbufferA.IsValid()) {
            m_dbufferASrv = device->GetTextureSRVIndex(m_dbufferA);
        }

        dbufDesc.debugName = "DBuffer_B_NormalBlend";
        m_dbufferB = device->CreateTexture(dbufDesc);
        if (m_dbufferB.IsValid()) {
            m_dbufferBSrv = device->GetTextureSRVIndex(m_dbufferB);
        }

        dbufDesc.debugName = "DBuffer_C_MaterialBlend";
        m_dbufferC = device->CreateTexture(dbufDesc);
        if (m_dbufferC.IsValid()) {
            m_dbufferCSrv = device->GetTextureSRVIndex(m_dbufferC);
        }

        if (m_dbufferA.IsValid() && m_dbufferB.IsValid() && m_dbufferC.IsValid()) {
            UZ_LOG(Renderer, Info,
                   "SceneRenderer: DBuffer created ({}x{}, 3x RGBA8Unorm, SRV={}/{}/{})",
                   m_renderWidth, m_renderHeight,
                   m_dbufferASrv, m_dbufferBSrv, m_dbufferCSrv);
        } else {
            UZ_LOG(Renderer, Warning,
                   "SceneRenderer: DBuffer creation partially failed — decals disabled");
        }
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

    // ── Bruneton 3D Scattering LUT (256x128x32 RGBA16F) ──────────────
    // Combined single+multi-scatter 3D texture for sky rendering.
    {
        RHITextureDesc desc{};
        desc.width            = 256;
        desc.height           = 128;
        desc.depthOrArraySize = 32;
        desc.is3D             = true;
        desc.format           = RHIFormat::RGBA16Float;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::UnorderedAccess;
        desc.debugName        = "ScatteringLUT3D";

        m_scatteringLut3D = m_device->CreateTexture(desc);
        if (!m_scatteringLut3D.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create ScatteringLUT3D texture");
        } else {
            m_scatteringLut3DSrv = m_device->GetTextureSRVIndex(m_scatteringLut3D);
            m_scatteringLut3DUav = m_device->GetTextureUAVIndex(m_scatteringLut3D);
            UZ_LOG(Renderer, Info, "SceneRenderer: ScatteringLUT3D created (256x128x32 RGBA16F, SRV={}, UAV={})",
                   m_scatteringLut3DSrv, m_scatteringLut3DUav);
        }
    }

    // ── Delta Rayleigh 3D (256x128x32 RGBA16F, temp for precompute) ──
    {
        RHITextureDesc desc{};
        desc.width            = 256;
        desc.height           = 128;
        desc.depthOrArraySize = 32;
        desc.is3D             = true;
        desc.format           = RHIFormat::RGBA16Float;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::UnorderedAccess;
        desc.debugName        = "DeltaRayleigh3D";

        m_deltaRayleigh3D = m_device->CreateTexture(desc);
        if (!m_deltaRayleigh3D.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create DeltaRayleigh3D texture");
        } else {
            m_deltaRayleigh3DSrv = m_device->GetTextureSRVIndex(m_deltaRayleigh3D);
            m_deltaRayleigh3DUav = m_device->GetTextureUAVIndex(m_deltaRayleigh3D);
            UZ_LOG(Renderer, Info, "SceneRenderer: DeltaRayleigh3D created (256x128x32 RGBA16F, SRV={}, UAV={})",
                   m_deltaRayleigh3DSrv, m_deltaRayleigh3DUav);
        }
    }

    // ── Delta Mie 3D (256x128x32 RGBA16F, temp for precompute) ───────
    {
        RHITextureDesc desc{};
        desc.width            = 256;
        desc.height           = 128;
        desc.depthOrArraySize = 32;
        desc.is3D             = true;
        desc.format           = RHIFormat::RGBA16Float;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::UnorderedAccess;
        desc.debugName        = "DeltaMie3D";

        m_deltaMie3D = m_device->CreateTexture(desc);
        if (!m_deltaMie3D.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create DeltaMie3D texture");
        } else {
            m_deltaMie3DSrv = m_device->GetTextureSRVIndex(m_deltaMie3D);
            m_deltaMie3DUav = m_device->GetTextureUAVIndex(m_deltaMie3D);
            UZ_LOG(Renderer, Info, "SceneRenderer: DeltaMie3D created (256x128x32 RGBA16F, SRV={}, UAV={})",
                   m_deltaMie3DSrv, m_deltaMie3DUav);
        }
    }

    // ── Scattering Density 3D (256x128x32 RGBA16F, temp for precompute)
    {
        RHITextureDesc desc{};
        desc.width            = 256;
        desc.height           = 128;
        desc.depthOrArraySize = 32;
        desc.is3D             = true;
        desc.format           = RHIFormat::RGBA16Float;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::UnorderedAccess;
        desc.debugName        = "ScatteringDensity3D";

        m_scatteringDensity3D = m_device->CreateTexture(desc);
        if (!m_scatteringDensity3D.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create ScatteringDensity3D texture");
        } else {
            m_scatteringDensity3DSrv = m_device->GetTextureSRVIndex(m_scatteringDensity3D);
            m_scatteringDensity3DUav = m_device->GetTextureUAVIndex(m_scatteringDensity3D);
            UZ_LOG(Renderer, Info, "SceneRenderer: ScatteringDensity3D created (256x128x32 RGBA16F, SRV={}, UAV={})",
                   m_scatteringDensity3DSrv, m_scatteringDensity3DUav);
        }
    }

    // ── Irradiance LUT (64x16 RGBA16F) ──────────────────────────────────
    {
        RHITextureDesc desc{};
        desc.width     = 64;
        desc.height    = 16;
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = "IrradianceLUT";

        m_irradianceLut = m_device->CreateTexture(desc);
        if (!m_irradianceLut.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create IrradianceLUT texture");
        } else {
            m_irradianceLutSrv = m_device->GetTextureSRVIndex(m_irradianceLut);
            m_irradianceLutUav = m_device->GetTextureUAVIndex(m_irradianceLut);
            UZ_LOG(Renderer, Info, "SceneRenderer: IrradianceLUT created (64x16 RGBA16F, SRV={}, UAV={})",
                   m_irradianceLutSrv, m_irradianceLutUav);
        }
    }

    // ── Delta Irradiance (64x16 RGBA16F, temp for precompute) ──────────
    {
        RHITextureDesc desc{};
        desc.width     = 64;
        desc.height    = 16;
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = "DeltaIrradiance";

        m_deltaIrradiance = m_device->CreateTexture(desc);
        if (!m_deltaIrradiance.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create DeltaIrradiance texture");
        } else {
            m_deltaIrradianceSrv = m_device->GetTextureSRVIndex(m_deltaIrradiance);
            m_deltaIrradianceUav = m_device->GetTextureUAVIndex(m_deltaIrradiance);
            UZ_LOG(Renderer, Info, "SceneRenderer: DeltaIrradiance created (64x16 RGBA16F, SRV={}, UAV={})",
                   m_deltaIrradianceSrv, m_deltaIrradianceUav);
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

    // ── Environment Map Importance Sampler ────────────────────────────
    m_envMapSampler = std::make_unique<EnvMapSampler>();
    if (m_envMapSampler->Init(device, m_shaderMgr.get())) {
        UZ_LOG(Renderer, Info, "SceneRenderer: EnvMapSampler initialized");
    } else {
        UZ_LOG(Renderer, Warning, "SceneRenderer: EnvMapSampler init failed (MIS disabled)");
    }

    // ── Environment MIS Temporal Accumulation ────────────────────────
    m_envMISPass = std::make_unique<EnvMISPass>();
    if (m_envMISPass->Init(device, m_shaderMgr.get(), width, height)) {
        UZ_LOG(Renderer, Info, "SceneRenderer: EnvMISPass initialized ({}x{})", width, height);
    } else {
        UZ_LOG(Renderer, Warning, "SceneRenderer: EnvMISPass init failed (env MIS disabled)");
    }

    // ── Reflection Probes ─────────────────────────────────────────────
    if (m_iblBaker) {
        if (m_reflectionProbes.Init(device, m_iblBaker.get())) {
            UZ_LOG(Renderer, Info, "SceneRenderer: ReflectionProbeRenderer initialized");
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: ReflectionProbeRenderer init failed (probes disabled)");
        }
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
    (void)m_lensFlarePass->Init(device, m_shaderMgr.get(), width, height);

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

    // Cloud noise loading disabled — cloud pass disabled for manual testing.
    // TODO(clouds): re-enable when cloud pipeline is debugged

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

    // ── GPU Occlusion Culling Buffers (initial allocation) ──────────────
    // Start with 4096 capacity — frame_setup.cpp will grow if needed.
    CreateGpuCullBuffers(4096);

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

    // ── Async Texture Decoder (scales with CPU cores for parallel VFS read + decode) ──
    const int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
    const int decoderThreads = std::max(2, hwThreads / 2); // clamped to [2,8] by decoder
    m_asyncDecoder = std::make_unique<AsyncTextureDecoder>(decoderThreads);

    // ── Copy command list for async texture uploads (zero-stall DMA queue) ──
    // Texture staging copies run on a dedicated copy queue so the graphics
    // pipeline never blocks on texture uploads. Falls back to graphics queue
    // WaitIdle if the backend does not support a copy queue.
    m_copyCmdList = m_device->CreateCommandList(RHIQueueType::Copy);
    if (m_copyCmdList) {
        m_copyCmdList->Begin();
        UZ_LOG(Renderer, Info, "SceneRenderer: copy command list created for async texture uploads");
    } else {
        UZ_LOG(Renderer, Warning, "SceneRenderer: copy command list unavailable — falling back to graphics queue uploads");
    }

    // ── Upload ring for zero-alloc texture staging ───────────────────────
    // 16MB per frame is enough for ~4 BC7 2K textures with full mip chains.
    // Triple-buffered (3 frames in flight) = 48MB total. Falls back to
    // UploadContext if Init fails or the ring is exhausted mid-frame.
    constexpr uint32_t kUploadRingBytesPerFrame = 64u * 1024u * 1024u;
    constexpr uint32_t kUploadRingFrameCount = 3;
    m_uploadRing = std::make_unique<UploadRing>();
    if (m_uploadRing->Init(m_device, kUploadRingFrameCount, kUploadRingBytesPerFrame)) {
        UZ_LOG(Renderer, Info,
               "SceneRenderer: upload ring initialized ({}MB/frame, {} frames, {}MB total)",
               kUploadRingBytesPerFrame / (1024 * 1024),
               kUploadRingFrameCount,
               (kUploadRingBytesPerFrame * kUploadRingFrameCount) / (1024 * 1024));
    } else {
        UZ_LOG(Renderer, Warning,
               "SceneRenderer: upload ring init failed — falling back to per-mip staging buffers");
        m_uploadRing.reset();
    }

    // ── VFX System ───────────────────────────────────────────────────────
    // Owned by the renderer; ticked every frame in RenderFrame() before
    // render-graph pass registration. Budget matches the default arg in
    // VFXSystem::Initialize() so emitter code can rely on the constant.
    constexpr uint32_t kVFXParticleBudget = 500000;
    m_vfxSystem = std::make_unique<VFXSystem>();
    m_vfxSystem->Initialize(m_device, kVFXParticleBudget);
    UZ_LOG(Renderer, Info,
           "SceneRenderer: VFX system initialized (budget={})", kVFXParticleBudget);

    // Create a 4-byte upload buffer containing uint32 zero.
    // Used by the sim pass to clear aliveCount each frame via CopyBuffer.
    {
        RHIBufferDesc zeroDesc{};
        zeroDesc.sizeBytes = sizeof(uint32_t);
        zeroDesc.usage     = RHIBufferUsage::CopySrc;
        zeroDesc.memory    = RHIMemoryType::Upload;
        zeroDesc.debugName = "VFX_ZeroBuffer";
        m_vfxZeroBuffer = m_device->CreateBuffer(zeroDesc);
        auto* mapped = static_cast<uint32_t*>(m_device->MapBuffer(m_vfxZeroBuffer));
        if (mapped) { *mapped = 0; m_device->UnmapBuffer(m_vfxZeroBuffer); }
    }

    // Register uz.vfx Lua API now that VFXSystem is available.
    // Must be called after LuaVM::Init() (which registers uz.scene etc.)
    // and after VFXSystem::Initialize().
    {
        auto* L = uz::LuaVM::GetState();
        if (L && m_vfxSystem) {
            RegisterVFXLuaBindings(L, m_vfxSystem.get());
            if (m_trailSystem) {
                RegisterTrailLuaBindings(L, m_trailSystem);
            }
            if (m_decalManager) {
                RegisterDecalLuaBindings(L, m_decalManager);
            }
            UZ_LOG(Renderer, Info, "SceneRenderer: uz.vfx Lua API registered");
        }
    }

    // ── VFX Hot-Reload (.uzfx + .uzvfx files) ───────────────────────────
    // Watches content directories for changes and triggers emitter
    // re-instantiation when .uzfx files are modified on disk.
#if !defined(NDEBUG)
    {
        m_vfxHotReload = std::make_unique<VFXHotReload>();
        m_vfxHotReload->Initialize("content/", m_vfxSystem.get(), nullptr);

        // When a .uzfx file changes, reset all emitters that reference it
        // so SyncVfxEmitters re-creates them next frame.
        m_vfxHotReload->OnReload([this](const std::string& path) {
            for (auto& emitter : m_runtimeEmitters) {
                if (emitter) {
                    m_vfxSystem->UnregisterEmitter(emitter.get());
                    emitter->Shutdown(m_device, m_vfxSystem->GetBudget());
                }
            }
            m_runtimeEmitters.clear();
            UZ_LOG(Renderer, Info, "VFX hot-reload: cleared runtime emitters for '{}'", path);
        });
    }
#endif

    // Cache fallback texture SRV indices for use as placeholders in loading
    // texture slots. Avoids per-frame device queries.
    if (m_fallbackWhite.IsValid()) {
        m_fallbackWhiteSrvIndex = m_device->GetTextureSRVIndex(m_fallbackWhite);
    }
    if (m_fallbackNormal.IsValid()) {
        m_fallbackNormalSrvIndex = m_device->GetTextureSRVIndex(m_fallbackNormal);
    }

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

    // All other CVars (pass toggles, parameters) are registered decentrally
    // via UZ_CVAR_* macros in their respective subsystem .cpp files.
    // ApplyCVars() reads them by name — no local registration needed.
}

void SceneRenderer::ApplyCVars() {
    auto& cvars = CVarRegistry::Get();

    // ── Quality Presets (check if --cvar quality=N was set) ──────────
    {
        const int qualityLevel = cvars.GetInt("quality", -1);
        static int lastAppliedQuality = -1;
        if (qualityLevel >= 0 && qualityLevel != lastAppliedQuality) {
            auto level = static_cast<QualityLevel>(std::clamp(qualityLevel, 0, 4));
            QualityPresetManager::Get().ApplyPreset(level);
            lastAppliedQuality = qualityLevel;
        }
    }

    // Screen percentage
    const float pct = cvars.GetFloat("r.ScreenPercentage", 100.f);
    if (std::abs(pct - m_screenPercentage) > 0.5f) {
        SetScreenPercentage(pct);
    }

    // Anti-aliasing method → upscaler type
    const auto aaMethod = static_cast<UpscalerType>(cvars.GetInt("r.AntiAliasingMethod", 1));
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

    // ── Render Pass Toggles ─────────────────────────────────────────────
    m_taaEnabled        = cvars.GetBool("r.TAA", true);
    m_ssrEnabled        = cvars.GetBool("r.SSR", true);
    m_motionBlurEnabled = cvars.GetBool("r.MotionBlur", true);
    m_casEnabled        = cvars.GetBool("r.CAS", true);
    m_lensFlareEnabled  = cvars.GetBool("r.LensFlare", false);
    m_dofEnabled        = cvars.GetBool("r.DOF", false);
    // Only allow SDF GI when both subsystems initialized successfully.
    m_sdfGiEnabled      = cvars.GetBool("r.RadianceCache", false)
                        && m_globalDistanceField.IsInitialized()
                        && m_radianceCache.IsInitialized();
    m_depthPrepassEnabled = cvars.GetBool("r.DepthPrepass", true);
    m_gpuOcclusionCullingEnabled = cvars.GetBool("r.GpuCull", false);
    m_shadowDebugMode   = static_cast<uint32_t>(cvars.GetInt("r.ShadowDebug", 0));

    // Pass inspector pass enables/disables — these control per-pass checks in
    // frame_lighting.cpp and frame_postprocess.cpp via IsPassEnabled().
    m_passInspector.SetPassEnabled("gtao", cvars.GetBool("r.GTAO", true));
    m_passInspector.SetPassEnabled("bloom", cvars.GetBool("r.Bloom", true));
    m_passInspector.SetPassEnabled("fog", cvars.GetBool("r.Fog", true));
    m_passInspector.SetPassEnabled("clouds", cvars.GetBool("r.Clouds", true));
    m_passInspector.SetPassEnabled("contact_shadow", cvars.GetBool("r.ContactShadows", true));
    m_passInspector.SetPassEnabled("aerial_persp", cvars.GetBool("r.AerialPerspective", true));
    m_passInspector.SetPassEnabled("ssgi", cvars.GetBool("r.SSGI", true));
    m_passInspector.SetPassEnabled("sdf_gi", cvars.GetBool("r.RadianceCache", false));
    m_passInspector.SetPassEnabled("ssr", cvars.GetBool("r.SSR", true));
    m_passInspector.SetPassEnabled("motionblur", cvars.GetBool("r.MotionBlur", true));
    m_passInspector.SetPassEnabled("dof", cvars.GetBool("r.DOF", false));
    m_passInspector.SetPassEnabled("cas", cvars.GetBool("r.CAS", true));
    m_passInspector.SetPassEnabled("taa", cvars.GetBool("r.TAA", true));
    m_passInspector.SetPassEnabled("lensflare", cvars.GetBool("r.LensFlare", false));

    // NOTE: Bloom, GTAO, AutoExposure, and SSGI parameters are now read
    // directly from decentralized CVar pointers in each subsystem's
    // Execute()/Render() function. No need to relay through member variables.

    // ── Motion Blur ──────────────────────────────────────────────────────
    m_motionBlurAmount = cvars.GetFloat("r.MotionBlur.Amount", 0.5f);
    m_motionBlurMax    = cvars.GetFloat("r.MotionBlur.Max", 5.f);

    // ── CAS ──────────────────────────────────────────────────────────────
    m_casSharpenStrength = cvars.GetFloat("r.CAS.Sharpness", 0.15f);

    // ── Depth of Field ───────────────────────────────────────────────────
    m_dofFocusDistance  = cvars.GetFloat("r.DOF.FocusDistance", 0.f);
    m_dofAperture       = cvars.GetFloat("r.DOF.Aperture", 4.f);
    m_dofFocalRegion    = cvars.GetFloat("r.DOF.FocalRegion", 0.f);
    m_dofNearTransition = cvars.GetFloat("r.DOF.NearTransition", 300.f);
    m_dofFarTransition  = cvars.GetFloat("r.DOF.FarTransition", 500.f);

    // NOTE: Tonemapping, film grain, vignette, chromatic aberration,
    // contact shadow, skylight, mip bias, and GI intensity are now read
    // directly from decentralized CVar pointers in frame_setup.cpp.

    // ── GI General ───────────────────────────────────────────────────────
    m_rcGridExtentCVar = cvars.GetFloat("r.GI.RadianceCache.GridExtent", 5000.f);

    // NOTE: Lens flare parameters are now read directly from decentralized
    // CVar pointers in lens_flare_pass.cpp Execute().

    // ── LOD & Resolution ─────────────────────────────────────────────────
    m_lodBias = cvars.GetFloat("r.LOD.Bias", 1.f);
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

    // Recreate DBuffer textures at render resolution
    {
        auto recreateDbufTex = [this](RHITextureHandle& tex, uint32_t& srv, const char* name) {
            if (tex.IsValid()) {
                m_device->DestroyTexture(tex);
            }
            RHITextureDesc dbufDesc{};
            dbufDesc.width     = m_renderWidth;
            dbufDesc.height    = m_renderHeight;
            dbufDesc.format    = RHIFormat::RGBA8Unorm;
            dbufDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
            dbufDesc.debugName = name;
            tex = m_device->CreateTexture(dbufDesc);
            srv = tex.IsValid() ? m_device->GetTextureSRVIndex(tex) : UINT32_MAX;
        };
        recreateDbufTex(m_dbufferA, m_dbufferASrv, "DBuffer_A_AlbedoOpacity");
        recreateDbufTex(m_dbufferB, m_dbufferBSrv, "DBuffer_B_NormalBlend");
        recreateDbufTex(m_dbufferC, m_dbufferCSrv, "DBuffer_C_MaterialBlend");
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

    if (m_outlinePass) {
        m_outlinePass->Resize(width, height);
    }

    if (m_statsOverlay) {
        m_statsOverlay->SetScreenSize(width, height);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor Mode
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::SetEditorMode(bool enabled) {
    m_editorMode = enabled;
    UZ_LOG(Renderer, Info, "Editor mode: {}", enabled ? "ON" : "OFF");
}

bool SceneRenderer::IsEditorMode() const {
    return m_editorMode;
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

    // ── DBuffer (Deferred Decals) ────────────────────────────────────
    if (m_dbufferA.IsValid()) { m_device->DestroyTexture(m_dbufferA); m_dbufferA = RHITextureHandle{}; }
    if (m_dbufferB.IsValid()) { m_device->DestroyTexture(m_dbufferB); m_dbufferB = RHITextureHandle{}; }
    if (m_dbufferC.IsValid()) { m_device->DestroyTexture(m_dbufferC); m_dbufferC = RHITextureHandle{}; }
    m_dbufferASrv = m_dbufferBSrv = m_dbufferCSrv = UINT32_MAX;

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

    // ── Bruneton 3D Scattering LUT textures ─────────────────────────
    if (m_scatteringLut3D.IsValid()) {
        m_device->DestroyTexture(m_scatteringLut3D);
        m_scatteringLut3D = RHITextureHandle{};
    }
    if (m_deltaRayleigh3D.IsValid()) {
        m_device->DestroyTexture(m_deltaRayleigh3D);
        m_deltaRayleigh3D = RHITextureHandle{};
    }
    if (m_deltaMie3D.IsValid()) {
        m_device->DestroyTexture(m_deltaMie3D);
        m_deltaMie3D = RHITextureHandle{};
    }
    if (m_scatteringDensity3D.IsValid()) {
        m_device->DestroyTexture(m_scatteringDensity3D);
        m_scatteringDensity3D = RHITextureHandle{};
    }
    if (m_irradianceLut.IsValid()) {
        m_device->DestroyTexture(m_irradianceLut);
        m_irradianceLut = RHITextureHandle{};
    }
    if (m_deltaIrradiance.IsValid()) {
        m_device->DestroyTexture(m_deltaIrradiance);
        m_deltaIrradiance = RHITextureHandle{};
    }

    // ── Reflection Probes (must shut down before IBLBaker it references) ──
    m_reflectionProbes.Shutdown();

    // ── Env MIS Pass + Sampler ──────────────────────────────────────────
    if (m_envMISPass) m_envMISPass->Shutdown();
    if (m_envMapSampler) m_envMapSampler->Shutdown();

    // ── IBL resources ──────────────────────────────────────────────────
    if (m_iblBaker) m_iblBaker->Shutdown();
    if (m_hdriCubemap.IsValid()) {
        m_device->DestroyTexture(m_hdriCubemap);
        m_hdriCubemap = RHITextureHandle{};
        m_hdriCubemapSrv = UINT32_MAX;
        m_hdriActive = false;
    }
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

    // ── GPU Occlusion Culling cleanup ────────────────────────────────
    DestroyGpuCullBuffers();

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

    // ── Runtime VFX Emitter cleanup ─────────────────────────────────
    // Unregister and destroy all emitters created by SyncVfxEmitters()
    // before shutting down the VFXSystem that tracks them.
    {
        VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
        if (activeVFX) {
            for (auto& emitter : m_runtimeEmitters) {
                if (emitter) {
                    activeVFX->UnregisterEmitter(emitter.get());
                    emitter->Shutdown(m_device, activeVFX->GetBudget());
                }
            }
        }
        m_runtimeEmitters.clear();
    }

    // ── VFX System cleanup ───────────────────────────────────────────
    // Shut down before the upload ring and copy queue so any pending emitter
    // GPU work is properly fenced out. The override pointer is non-owning
    // and must be cleared to prevent a dangling access in RenderFrame().
    m_vfxSystemOverride = nullptr;
    if (m_vfxSystem) {
        m_vfxSystem->Shutdown(m_device);
        m_vfxSystem.reset();
    }
    if (m_vfxZeroBuffer.IsValid()) {
        m_device->DestroyBuffer(m_vfxZeroBuffer);
        m_vfxZeroBuffer = RHIBufferHandle{};
    }

    // ── Upload ring cleanup ─────────────────────────────────────────
    if (m_uploadRing) {
        m_uploadRing->Shutdown();
        m_uploadRing.reset();
    }

    // ── Copy queue command list cleanup ──────────────────────────────
    if (m_copyCmdList) {
        m_device->DestroyCommandList(m_copyCmdList);
        m_copyCmdList = nullptr;
    }
    m_copyFenceValue = 0;
    m_pendingCopyUploads.clear();

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

    if (m_statsOverlay)    m_statsOverlay->Shutdown();
    if (m_gizmoRenderer)   m_gizmoRenderer->Shutdown();
    if (m_outlinePass)     m_outlinePass->Shutdown();
    if (m_editorGrid)      m_editorGrid->Shutdown();
    if (m_cbRing)          m_cbRing->Shutdown();
    if (m_pipelines)       m_pipelines->Shutdown();
    if (m_shaderMgr)       m_shaderMgr->Shutdown();

    m_rgPool.reset();
    m_cameraCtrl.reset();
    m_camera.reset();
    m_statsOverlay.reset();
    m_gizmoRenderer.reset();
    m_outlinePass.reset();
    m_editorGrid.reset();
    m_cbRing.reset();
    m_pipelines.reset();
    m_shaderMgr.reset();
    m_input.reset();

    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor Selection
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::SetSelectedEntity(Entity entity) {
    m_selectedEntity = entity;
}

Entity SceneRenderer::GetSelectedEntity() const {
    return m_selectedEntity;
}

bool SceneRenderer::GetMeshBounds(uint32_t meshId, Vec3& outMin, Vec3& outMax) const {
    if (meshId >= static_cast<uint32_t>(m_meshBoundsMin.size())) return false;
    if (meshId >= static_cast<uint32_t>(m_meshBoundsMax.size())) return false;
    outMin = m_meshBoundsMin[meshId];
    outMax = m_meshBoundsMax[meshId];
    return true;
}

} // namespace uz
