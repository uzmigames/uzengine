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
#include "per_frame_params.h"
#include "shadow_data_params.h"
#include "pipeline_library.h"
#include "shader_manager.h"
#include "mesh.h"
#include "gpu_mesh.h"
#include "depth_prepass.h"
#include "gbuffer_pass.h"
#include "gbuffer_debug_pass.h"
#include "hiz_pyramid_pass.h"
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
#include "shadows/sdf_atlas_manager.h"
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
#include <cstdio>
#include <cstring>

namespace uz {

/// Per-cascade shadow constant buffer — mirrors the one in shadow_pass.cpp.
/// Contains only the light-space view-projection matrix, bound at root slot 3.
struct ShadowCB {
    Mat4 viewProj;
};

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
    // @source UE5 SkyAtmosphereRendering.cpp — MultiScatteredLuminanceLutTexture (32x32)
    // Precomputed infinite-bounce multi-scattering transfer function.
    // UE5 uses R11G11B10F but our RHI doesn't support that format;
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

    // ── GTAO ─────────────────────────────────────────────────────────
    m_gtaoPass = std::make_unique<GTAOPass>();
    if (!m_gtaoPass->Init(device, m_shaderMgr.get(), width, height)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: GTAO init failed (SSAO disabled)");
    }

    // ── Bloom ────────────────────────────────────────────────────────
    UZ_LOG(Renderer, Info, "SceneRenderer: initializing BloomPass...");
    m_bloomPass = std::make_unique<BloomPass>();
    if (!m_bloomPass->Init(device, m_shaderMgr.get(), width, height)) {
        UZ_LOG(Renderer, Warning, "SceneRenderer: bloom init failed (bloom disabled)");
    }
    UZ_LOG(Renderer, Info, "SceneRenderer: BloomPass initialized");

    // ── DFS Output Texture + Culling Buffers ──────────────────────────────
    // Created here but only populated when DFS is enabled at runtime.
    // The output texture starts at half-resolution by default; fullResolution
    // mode can be toggled via DistanceFieldShadowConfig.
    CreateDfsResources(width, height);

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

    // ── Hi-Z Pyramid (per-mip R32F textures) ────────────────────────────
    CreateHiZPyramid(width, height);

    // ── TAA History Textures (ping-pong RGBA16F) ────────────────────────
    CreateTaaResources(width, height);

    UZ_LOG(Renderer, Info, "SceneRenderer initialized ({}x{}, {})",
           width, height, RenderPathToString(m_renderPath));
    return true;
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
    // @source UE5 DistanceFieldShadowing.cpp:180 — CulledObjectBuffers allocation
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

    // Recreate Hi-Z pyramid at new resolution
    CreateHiZPyramid(width, height);

    // Recreate GTAO textures at new resolution
    if (m_gtaoPass && m_gtaoPass->IsInitialized()) {
        [[maybe_unused]] bool gtaoOk = m_gtaoPass->Resize(width, height);
    }

    // Recreate DFS output texture at new resolution
    DestroyDfsResources();
    CreateDfsResources(width, height);

    // Recreate TAA history textures at new resolution
    CreateTaaResources(width, height);

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

    // ── Shadow Cache + Static Shadow Atlas ───────────────────────────
    m_shadowCacheManager.Shutdown();
    if (m_staticShadowAtlas.IsValid()) {
        m_device->DestroyTexture(m_staticShadowAtlas);
        m_staticShadowAtlas = RHITextureHandle{};
        m_staticShadowAtlasSrv = UINT32_MAX;
    }

    // ── Transmittance LUT ────────────────────────────────────────────
    if (m_transmittanceLut.IsValid()) {
        m_device->DestroyTexture(m_transmittanceLut);
        m_transmittanceLut = RHITextureHandle{};
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

    // ── TAA cleanup ──────────────────────────────────────────────────
    DestroyTaaResources();

    // ── Auto-Exposure cleanup ─────────────────────────────────────
    if (m_autoExposure) m_autoExposure->Shutdown();
    if (m_gtaoPass) m_gtaoPass->Shutdown();
    if (m_bloomPass) m_bloomPass->Shutdown();

    // ── Instance buffer cleanup ─────────────────────────────────────
    for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
        if (m_instanceBuffers[i].IsValid()) {
            m_device->DestroyBuffer(m_instanceBuffers[i]);
            m_instanceBuffers[i] = RHIBufferHandle{};
        }
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

    // ── TAA Jitter — Halton(2,3) sub-pixel offset on the projection matrix ──
    // @source UE5 SceneVisibility.cpp:5240-5243 — TemporalJitterPixels
    // @source UE5 SceneVisibility.cpp:5243 — HackAddTemporalAAProjectionJitter(SampleX * 2/W, SampleY * -2/H)
    //
    // Halton(2,3) 8-sample sequence pre-computed, centered around 0.
    // UE5 uses a full Halton sequence with Faure permutations (HaltonUtilities.cpp),
    // but the standard Halton(2,3) 8-sample cycle produces equivalent low-discrepancy
    // jitter for TAA convergence.
    //
    // Values: Halton base-2 = {1/2, 1/4, 3/4, 1/8, 5/8, 3/8, 7/8, ...}
    //         Halton base-3 = {1/3, 2/3, 1/9, 4/9, 7/9, 2/9, 5/9, ...}
    // Centered by subtracting 0.5 to get [-0.5, 0.5] range.
    static const float kHalton23[8][2] = {
        { 0.5f - 0.5f,   1.0f/3.0f - 0.5f },  // {0.0,  -0.167}
        { 0.25f - 0.5f,  2.0f/3.0f - 0.5f },  // {-0.25, 0.167}
        { 0.75f - 0.5f,  1.0f/9.0f - 0.5f },  // {0.25, -0.389}
        { 0.125f - 0.5f, 4.0f/9.0f - 0.5f },  // {-0.375, -0.056}
        { 0.625f - 0.5f, 7.0f/9.0f - 0.5f },  // {0.125,  0.278}
        { 0.375f - 0.5f, 2.0f/9.0f - 0.5f },  // {-0.125, -0.278}
        { 0.875f - 0.5f, 5.0f/9.0f - 0.5f },  // {0.375,  0.056}
        { 0.0625f - 0.5f, 8.0f/9.0f - 0.5f }, // {-0.4375, 0.389}
    };

    float jitterX = 0.f;
    float jitterY = 0.f;

    const bool taaActive = m_taaEnabled
                        && m_taaHistory[0].IsValid()
                        && m_taaHistory[1].IsValid()
                        && m_device->IsBindless();  // TAA compute requires bindless

    if (taaActive) {
        const uint32_t jitterIdx = frameIndex % 8;
        jitterX = kHalton23[jitterIdx][0];
        jitterY = kHalton23[jitterIdx][1];
    }

    // Store unjittered viewProj for reprojection and velocity computation.
    Mat4 unjitteredViewProj = camera.GetViewProjection();

    // Apply jitter to the projection matrix as a sub-pixel translation in clip space.
    // @source UE5 SceneVisibility.cpp:5243 — HackAddTemporalAAProjectionJitter
    // The jitter is applied as an offset to the projection matrix's [2][0] and [2][1]
    // elements, which translates the clip-space output by a sub-pixel amount.
    Mat4 jitteredViewProj = unjitteredViewProj;
    if (taaActive) {
        const float vpW = static_cast<float>(m_width);
        const float vpH = static_cast<float>(m_height);
        // UE5: SampleX * 2.0 / ViewRect.Width(), SampleY * -2.0 / ViewRect.Height()
        // The *2 converts from pixel units to NDC (-1 to 1 range = 2 units per viewport dim).
        // Y is negated because clip-space Y increases upward but pixel Y increases downward.
        jitteredViewProj.m.m[2][0] += jitterX * 2.0f / vpW;
        jitteredViewProj.m.m[2][1] += jitterY * -2.0f / vpH;
    }

    // ── Build PerFrameParams (scalar section byte-identical to PerFrameCB) ──
    PerFrameParams perFrame{};
    // Use the jittered viewProj for rendering (geometry pass, lighting).
    // The unjittered version is stored in m_prevViewProj for next frame's reprojection.
    perFrame.viewProj    = jitteredViewProj;
    perFrame.invViewProj = jitteredViewProj.Inverse();
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
    // @source UE5 SkyLightComponent.cpp:308 — MinOcclusion default = 0.0
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

    // Ambient color from light extraction (directional light ambient term).
    // When IBL is available, the shader uses SH irradiance instead of this.
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
        // Without auto-exposure, use neutral exposure.
        // @source UE5 — default exposure = 1.0 (no bias applied)
        perFrame.exposure = 1.0f;
    }

    // ── GTAO SRV index (set early — actual compute runs in render graph) ──
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled) {
        perFrame.gtaoSrvIndex = m_gtaoPass->GetFilteredAOSrvIndex();
    } else {
        perFrame.gtaoSrvIndex = UINT32_MAX;
    }

    perFrame.shadowDebugMode = m_shadowDebugMode;
    perFrame.brdfDebugMode   = m_brdfDebugMode;

    // ── Aerial Perspective parameters (from SkyAtmosphereComponent) ───
    // Populate AP atmosphere constants so deferred_lighting_ps can ray-march
    // inscattering per pixel. Only enabled when a SkyAtmosphereComponent exists
    // and the transmittance LUT has been baked.
    if (world) {
        auto skyView = world->GetRegistry().view<SkyAtmosphereComponent>();
        for (auto enttEntity : skyView) {
            auto& skyComp = skyView.get<SkyAtmosphereComponent>(enttEntity);
            if (!skyComp.enabled) continue;
            if (m_transmittanceLutSrv != UINT32_MAX && m_transmittanceLutFilled) {
                perFrame.aerialPerspectiveEnabled = 1;
                perFrame.transmittanceLutSrvIndex = m_transmittanceLutSrv;
                perFrame.apBottomRadiusKm = skyComp.bottomRadiusKm;
                perFrame.apTopRadiusKm    = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;
                perFrame.apRayleighBetaX  = skyComp.rayleighScattering.x;
                perFrame.apRayleighBetaY  = skyComp.rayleighScattering.y;
                perFrame.apRayleighBetaZ  = skyComp.rayleighScattering.z;
                perFrame.apRayleighScaleH = skyComp.rayleighScaleHeightKm;
                perFrame.apMieBeta        = skyComp.mieScattering;
                perFrame.apMieScaleH      = skyComp.mieScaleHeightKm;
                perFrame.apMieAnisotropy  = skyComp.mieAnisotropy;
                perFrame.apSunIntensity   = sunColor.Length();
                perFrame.apRayleighScale  = skyComp.rayleighScatteringScale;
                perFrame.apMieScale       = skyComp.mieScatteringScale;
            }
            // @source UE5 SceneRendering.cpp:1440 — SkyAtmosphereHeightFogContribution
            // Propagate heightFogContribution to reduce AP inscattering when height fog
            // is active, preventing double-fogging.
            perFrame.heightFogContribution = skyComp.heightFogContribution;
            break; // First SkyAtmosphereComponent only
        }
    }

    // ── IBL texture handles (for SM5 binder path) ─────────────────────
    // The binder's BindResources() reads these handles and calls
    // SetFragmentTexture at the metadata-defined register slots (t5-t7).
    if (m_iblBaker) {
        perFrame.irradianceCubemap   = m_iblBaker->GetIrradianceTexture();
        perFrame.prefilteredSpecular = m_iblBaker->GetPrefilteredTexture();
        perFrame.brdfLut             = m_iblBaker->GetBRDFLutTexture();
    }

    // Allocate CB for per-frame data — only the scalar section (512 bytes)
    // is copied to the GPU. Resource handles live after the CB boundary
    // and are bound separately by BindResources().
    CBAllocation perFrameAlloc = m_cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── Shadow cascades — delegated to shadow_renderer ─────────────────
    // Determine shadow cache status early so ShadowDataCB gets the right fields.
    const bool shadowCacheEnabled = m_shadowCacheManager.GetConfig().enabled
                                  && m_staticShadowAtlas.IsValid()
                                  && m_staticShadowAtlasSrv != UINT32_MAX;
    ShadowFrameData shadowFrame = SetupShadowFrame(
        camera, sunDir,
        lightData.perLightShadowBias, lightData.perLightShadowSlopeBias,
        lightData.lightSourceAngleDeg,
        m_shadowAtlas.GetSrvIndex(), kShadowResolution, *m_cbRing,
        lightData.farShadowCascadeCount,
        lightData.farShadowDistance,
        lightData.farShadowLodBias,
        shadowCacheEnabled,
        m_staticShadowAtlasSrv);
    const auto& shadowCfg  = shadowFrame.config;
    const auto* cascades    = shadowFrame.cascades;
    const uint32_t totalCascadeCount = shadowFrame.totalCascadeCount;
    CBAllocation shadowAlloc = shadowFrame.cbAllocation;

    // ── Build ShadowDataParams for binder path ──────────────────────────
    // Copy the 768-byte scalar section from the CB allocation, then attach
    // the RHI texture handles so ShaderParameterBinder can bind them at
    // their SM5 register slots (t3, t8, t9).
    ShadowDataParams shadowDataParams{};
    static_assert(offsetof(ShadowDataParams, shadowAtlas) == sizeof(ShadowDataCB),
                  "ShadowDataParams scalar section must match ShadowDataCB size");
    std::memcpy(&shadowDataParams, shadowAlloc.cpuPtr, sizeof(ShadowDataCB));
    shadowDataParams.shadowAtlas       = m_shadowAtlas.GetTexture();
    shadowDataParams.staticShadowAtlas = m_staticShadowAtlas;
    // Wire DFS output into shadow params when DFS is active.
    // The cone trace compute pass writes to m_dfsOutputTexture each frame.
    // The deferred lighting shader reads it via dfsOutputSrvIndex and composites
    // DFS shadows multiplicatively with CSM shadows.
    if (m_dfsOutputTexture.IsValid() && m_dfsConfig.enabled
        && m_sdfAtlas && m_sdfAtlas->IsInitialized()
        && m_sdfAtlas->GetRegisteredObjectCount() > 0) {
        shadowDataParams.dfsEnabled        = 1;
        shadowDataParams.dfsOutputSrvIndex = m_dfsOutputSrvIndex;
        shadowDataParams.dfsOutputTexture  = m_dfsOutputTexture;
    }

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
    // Disable instancing when multiple mesh types are loaded — the instanced path
    // currently only supports homogeneous batches (all mesh[0]). Per-mesh batching
    // is future work. Fall back to per-draw-command path for multi-mesh scenes.
    const bool useInstancing = bindless && instanceCount > 0 && m_meshes.size() <= 1;

    // Triple-buffered instance data: CPU writes to frame N's buffer while GPU
    // reads from frame N-2. No WaitIdle needed — fence sync in the app layer
    // guarantees frame N-2 is complete before we reach this point in frame N.
    const uint32_t ibSlot = frameIndex % kInstanceBufferCount;

    if (useInstancing && instanceCount > m_instanceBufferCapacity) {
        // Need bigger buffers — wait for GPU idle before destroying all slots
        m_device->WaitIdle();
        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            if (m_instanceBuffers[i].IsValid()) {
                m_device->DestroyBuffer(m_instanceBuffers[i]);
                m_instanceBuffers[i] = RHIBufferHandle{};
                m_instanceBufferSrvIndices[i] = UINT32_MAX;
            }
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

        bool allCreated = true;
        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            ibDesc.debugName = (i == 0) ? "InstanceBuffer[0]"
                             : (i == 1) ? "InstanceBuffer[1]"
                                        : "InstanceBuffer[2]";
            m_instanceBuffers[i] = m_device->CreateBuffer(ibDesc);
            if (m_instanceBuffers[i].IsValid()) {
                m_instanceBufferSrvIndices[i] = m_device->GetBufferSRVIndex(m_instanceBuffers[i]);
            } else {
                allCreated = false;
            }
        }
        m_instanceBufferCapacity = allCreated ? newCapacity : 0;
    }

    // Upload instance data to the current frame's buffer (no GPU sync needed)
    uint32_t instanceBufferSrvIndex = UINT32_MAX;
    if (useInstancing && m_instanceBuffers[ibSlot].IsValid()) {
        void* mapped = m_device->MapBuffer(m_instanceBuffers[ibSlot]);
        if (mapped) {
            std::memcpy(mapped, instanceDataVec.data(),
                        instanceCount * sizeof(InstanceData));
            m_device->UnmapBuffer(m_instanceBuffers[ibSlot]);
        }
        instanceBufferSrvIndex = m_instanceBufferSrvIndices[ibSlot];
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

    // ── Shadow pass(es) — two-pass cache-aware rendering ───────────────
    // When shadow caching is active and the static shadow atlas is available:
    //   Pass 1 (conditional): Render static/stationary geometry into the
    //     static shadow atlas. Only executes when the cache is invalidated
    //     (light direction change, cascade parameter change, etc.).
    //   Pass 2 (every frame): Render only movable geometry into the dynamic
    //     shadow atlas. The pixel shader composites both atlases.
    //
    // When shadow caching is disabled or the static atlas is unavailable,
    // a single uncached pass renders all geometry into the dynamic atlas
    // (backward compatible behavior).
    //
    // @source UE5 ShadowSetup.cpp:3846-3970 — EShadowDepthCacheMode
    auto shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", m_shadowAtlas.GetTexture(), RHIResourceState::ShaderResource);
    if (!m_meshes.empty()) {
        // Use the shadow cache status computed earlier (before SetupShadowFrame).
        const bool shadowCacheActive = shadowCacheEnabled;

        // Stable up vector for cache key construction (Z-up world up).
        const Vec3 lightUp = Vec3{0.f, 0.f, 1.f};

        // Query the cache manager for each cascade.
        // Track per-cascade modes for CSM scrolling support.
        // @source UE5 ShadowSetup.cpp:3846-3970 — EShadowDepthCacheMode
        bool needStaticRerender = false;
        bool anyScrolling = false;
        ShadowCacheMode cascadeModes[kMaxShadowCascades]{};
        if (shadowCacheActive) {
            const uint32_t lightId = 0; // Primary directional light
            for (uint32_t i = 0; i < totalCascadeCount; ++i) {
                cascadeModes[i] = m_shadowCacheManager.UpdateCache(
                    lightId, i, cascades[i], sunDir, lightUp,
                    kShadowResolution, frameIndex);
                if (cascadeModes[i] == ShadowCacheMode::StaticPrimitivesOnly
                    || cascadeModes[i] == ShadowCacheMode::Uncached) {
                    needStaticRerender = true;
                }
                if (cascadeModes[i] == ShadowCacheMode::CSMScrolling) {
                    anyScrolling = true;
                }
            }
        }

        // Common fields shared between shadow pass descriptors.
        auto fillCommonShadowDesc = [&](ShadowPassDesc& desc) {
            desc.drawList             = &drawList;
            desc.cbAllocator          = m_cbRing.get();
            desc.pipeline             = pipelines->GetShadowDepth(bindless);
            desc.meshes               = m_meshes.data();
            desc.meshCount            = static_cast<uint32_t>(m_meshes.size());
            desc.bindless             = bindless;
            for (uint32_t ci = 0; ci < totalCascadeCount; ++ci) {
                desc.cascades[ci] = cascades[ci];
            }
            desc.cascadeCount     = totalCascadeCount;
            desc.atlasResolution  = kShadowResolution;
            desc.farShadowLodBias = shadowFrame.config.farShadowLodBias;
        };

        if (shadowCacheActive) {
            // ── CSM Scrolling: GPU copy overlap + render edge strips ──
            // When UpdateCache returns CSMScrolling for any cascade, we need to
            // copy the overlapping region of the cached atlas to the correct
            // position (accounting for camera movement), then render only the
            // newly exposed edge strips.
            //
            // @source UE5 ShadowDepthRendering.cpp:1121-1126 — CopyCachedShadowMap
            // @source UE5 ShadowSetup.cpp:5600-5668 — scrolling data
            if (anyScrolling) {
                const uint32_t lightId = 0;

                // Import the static shadow atlas for GPU copy operations
                auto staticAtlasRes = rgBuilder.ImportTexture(
                    "StaticShadowAtlas", m_staticShadowAtlas, RHIResourceState::ShaderResource);

                // Register a render graph pass that copies the overlapping region
                // and renders edge-strip static primitives.
                // Capture by value since the lambda outlives this scope.
                ShadowPassDesc scrollingDesc{};
                scrollingDesc.shadowAtlas        = staticAtlasRes;
                scrollingDesc.shadowAtlasTexture = m_staticShadowAtlas;
                scrollingDesc.cacheMode          = ShadowCacheMode::CSMScrolling;
                fillCommonShadowDesc(scrollingDesc);

                // Store per-cascade scrolling data and modes in the desc for the pass.
                // The CSMScrolling pass uses CopyTexture for the overlap region and
                // renders only edge-strip primitives based on culling planes.
                auto* shadowCacheMgr = &m_shadowCacheManager;
                const auto staticAtlasTex = m_staticShadowAtlas;

                rgBuilder.AddPass("ShadowCSMScrolling", [scrollingDesc, shadowCacheMgr, lightId,
                                                          cascadeModes, staticAtlasTex](rg::RgPassBuilder& pb) {
                    pb.Write(scrollingDesc.shadowAtlas, RHIResourceState::DepthStencilWrite);

                    pb.SetExecutionCallback([scrollingDesc, shadowCacheMgr, lightId,
                                             cascadeModes, staticAtlasTex](
                                                IRHICommandList* cmd, const rg::RgPassResources& res) {
                        UZ_PROFILE_SCOPE("ShadowCSMScrolling::Execute");

                        const uint32_t resolution = scrollingDesc.atlasResolution;
                        const auto& commands = scrollingDesc.drawList->GetCommands();

                        for (uint32_t cascade = 0; cascade < scrollingDesc.cascadeCount; ++cascade) {
                            if (cascadeModes[cascade] != ShadowCacheMode::CSMScrolling) {
                                continue;
                            }

                            const auto& scrollData = shadowCacheMgr->GetScrollingData(lightId, cascade);
                            if (!scrollData.active) continue;

                            // GPU copy: the overlapping region from the cached atlas
                            // is already in the same texture (we reuse the allocation).
                            // UE5 draws a fullscreen quad with a copy shader; we use
                            // CopyTexture since our cached atlas is a separate texture
                            // from the per-frame atlas, and the overlap is the entire texture.
                            //
                            // For our architecture, the static atlas IS the cached atlas — so
                            // the overlap data is already in place. We just need to render
                            // the edge strips to fill in the non-overlapped regions.
                            //
                            // @source UE5 ShadowDepthRendering.cpp:1000-1035

                            // Set viewport/scissor to the cascade tile
                            uint32_t tileX = 0, tileY = 0;
                            const uint32_t cols = GetAtlasColumns(scrollingDesc.cascadeCount);
                            tileX = (cascade % cols) * resolution;
                            tileY = (cascade / cols) * resolution;

                            RHIViewport vp{};
                            vp.x = static_cast<float>(tileX);
                            vp.y = static_cast<float>(tileY);
                            vp.width    = static_cast<float>(resolution);
                            vp.height   = static_cast<float>(resolution);
                            vp.maxDepth = 1.0f;
                            cmd->SetViewport(vp);

                            RHIScissorRect sc{};
                            sc.left   = static_cast<int32_t>(tileX);
                            sc.top    = static_cast<int32_t>(tileY);
                            sc.right  = static_cast<int32_t>(tileX + resolution);
                            sc.bottom = static_cast<int32_t>(tileY + resolution);
                            cmd->SetScissor(sc);

                            // Begin depth render pass (no clear — we want to keep overlap data)
                            RHIRenderPassDesc passDesc{};
                            passDesc.colorAttachmentCount = 0;
                            passDesc.hasDepth = true;
                            passDesc.depthAttachment.texture = staticAtlasTex;
                            passDesc.depthAttachment.clear   = false;
                            cmd->BeginRenderPass(passDesc);

                            cmd->SetPipeline(scrollingDesc.pipeline);

                            // Bind per-cascade shadow CB
                            ShadowCB shadowCb{};
                            shadowCb.viewProj = scrollingDesc.cascades[cascade].viewProj;
                            CBAllocation shadowAlloc = scrollingDesc.cbAllocator->Allocate(sizeof(ShadowCB));
                            if (shadowAlloc.cpuPtr) {
                                std::memcpy(shadowAlloc.cpuPtr, &shadowCb, sizeof(ShadowCB));
                                cmd->SetRootCBV(kSlotShadow, shadowAlloc.bufferHandle,
                                                shadowAlloc.offsetBytes, shadowAlloc.cpuPtr, sizeof(ShadowCB));
                            }

                            // Render only static primitives that:
                            // 1. Are NOT already in the cached map (new primitives), OR
                            // 2. Intersect the edge-strip culling planes (partially in the new region)
                            //
                            // @source UE5 ShadowSetup.cpp:4997-5013
                            uint32_t extraSubjects = 0;
                            uint32_t lastBoundMeshId = UINT32_MAX;
                            for (uint32_t di = 0; di < commands.size(); ++di) {
                                const auto& drawCmd = commands[di];

                                // Only static/stationary primitives
                                if (drawCmd.mobility == EMobility::Movable) continue;

                                // Check if already rendered into the cached shadow map
                                bool isNewPrimitive = !shadowCacheMgr->IsStaticPrimitiveRendered(
                                    lightId, cascade, di);

                                if (!isNewPrimitive) {
                                    // Check if it intersects any edge-strip culling plane.
                                    // For simplicity, we use the draw command's position
                                    // (worldMatrix translation) as a point test against the planes.
                                    // A more precise test would use the mesh's AABB, but this
                                    // is sufficient for most cases.
                                    //
                                    // @source UE5 ShadowSetup.cpp:5007-5011
                                    const Vec3 objPos{
                                        drawCmd.worldMatrix.m._41,
                                        drawCmd.worldMatrix.m._42,
                                        drawCmd.worldMatrix.m._43
                                    };

                                    bool intersectsStrip = false;
                                    for (uint32_t pi = 0; pi < scrollData.cullingPlaneCount; ++pi) {
                                        const auto& plane = scrollData.cullingPlanes[pi];
                                        const Vec3 toObj = objPos - plane.point;
                                        // If the primitive is on the negative side of the plane,
                                        // it intersects the edge strip.
                                        if (toObj.Dot(plane.normal) <= 0.f) {
                                            intersectsStrip = true;
                                            break;
                                        }
                                    }

                                    if (!intersectsStrip) continue;
                                }

                                // Render this primitive
                                if (drawCmd.meshId >= scrollingDesc.meshCount) continue;
                                const GPUMesh& mesh = scrollingDesc.meshes[drawCmd.meshId];
                                if (!mesh.IsValid()) continue;

                                if (drawCmd.meshId != lastBoundMeshId) {
                                    if (scrollingDesc.bindless) {
                                        cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
                                    } else {
                                        cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                                    }
                                    cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
                                    lastBoundMeshId = drawCmd.meshId;
                                }

                                PerObjectCB perObject{};
                                perObject.model = drawCmd.worldMatrix;
                                CBAllocation objAlloc = scrollingDesc.cbAllocator->Allocate(sizeof(PerObjectCB));
                                if (objAlloc.cpuPtr) {
                                    std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));
                                    cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle,
                                                    objAlloc.offsetBytes, objAlloc.cpuPtr, sizeof(PerObjectCB));
                                }

                                cmd->DrawIndexed(mesh.indexCount);
                                ++extraSubjects;

                                // Mark this primitive as rendered for future frames
                                shadowCacheMgr->MarkStaticPrimitiveRendered(
                                    lightId, cascade, di, static_cast<uint32_t>(commands.size()));
                            }

                            cmd->EndRenderPass();

                            UZ_LOG(Renderer, Trace,
                                   "CSMScrolling: cascade {} — {} extra static subjects rendered",
                                   cascade, extraSubjects);
                        }
                    });
                });
            }

            // ── Pass 1: Static geometry into cached atlas (conditional) ──
            // Only re-render when cache is fully invalidated (not scrolling)
            if (needStaticRerender) {
                auto staticAtlasRes = rgBuilder.ImportTexture(
                    "StaticShadowAtlas", m_staticShadowAtlas, RHIResourceState::ShaderResource);

                ShadowPassDesc staticDesc{};
                staticDesc.shadowAtlas        = staticAtlasRes;
                staticDesc.shadowAtlasTexture = m_staticShadowAtlas;
                staticDesc.cacheMode          = ShadowCacheMode::StaticPrimitivesOnly;
                fillCommonShadowDesc(staticDesc);
                AddShadowPass(rgBuilder, staticDesc);
            }

            // ── Pass 2: Dynamic (movable) geometry into per-frame atlas ──
            ShadowPassDesc dynamicDesc{};
            dynamicDesc.shadowAtlas        = shadowAtlasRes;
            dynamicDesc.shadowAtlasTexture = m_shadowAtlas.GetTexture();
            dynamicDesc.cacheMode          = ShadowCacheMode::MovablePrimitivesOnly;
            fillCommonShadowDesc(dynamicDesc);
            AddShadowPass(rgBuilder, dynamicDesc);
        } else {
            // ── Single uncached pass — backward compatible ──────────────
            ShadowPassDesc shadowDesc{};
            shadowDesc.shadowAtlas        = shadowAtlasRes;
            shadowDesc.shadowAtlasTexture = m_shadowAtlas.GetTexture();
            shadowDesc.cacheMode          = ShadowCacheMode::Uncached;
            fillCommonShadowDesc(shadowDesc);
            AddShadowPass(rgBuilder, shadowDesc);
        }
    }

    if (m_renderPath == RenderPath::Deferred) {
        // ── Deferred: optional DepthPrepass + Hi-Z + GBuffer + lighting/debug ──
        if (!m_meshes.empty()) {
            // ── Depth Prepass (optional, enabled by default) ─────────────
            if (m_depthPrepassEnabled) {
                DepthPrepassDesc zpDesc{};
                zpDesc.depthBuffer      = depthRes;
                zpDesc.depthTexture     = m_gbuffer.depth;
                zpDesc.drawList         = &drawList;
                zpDesc.cbAllocator      = m_cbRing.get();
                zpDesc.perFrameAlloc    = perFrameAlloc;
                zpDesc.pipeline         = pipelines->GetDepthPrepass(bindless);
                zpDesc.meshes           = m_meshes.data();
                zpDesc.meshCount        = static_cast<uint32_t>(m_meshes.size());
                zpDesc.bindless         = bindless;
                zpDesc.viewportWidth    = vpWidth;
                zpDesc.viewportHeight   = vpHeight;
                zpDesc.cbRingAllocator  = m_cbRing.get();
                if (useInstancing) {
                    zpDesc.instanced              = true;
                    zpDesc.instancedPipeline      = pipelines->GetDepthPrepassInstanced();
                    zpDesc.instanceBufferSrvIndex = instanceBufferSrvIndex;
                    zpDesc.batchData              = instanceBatches.data();
                    zpDesc.batchCount             = static_cast<uint32_t>(instanceBatches.size());
                }
                AddDepthPrepass(rgBuilder, zpDesc);

                // ── Hi-Z Pyramid (runs after depth prepass) ─────────────
                if (m_hizMipCount > 0) {
                    HiZPyramidPassDesc hizDesc{};
                    hizDesc.depthSource          = depthRes;
                    hizDesc.depthSourceTexture   = m_gbuffer.depth;
                    hizDesc.depthSourceSrvIndex  = m_gbuffer.depthSRV;
                    hizDesc.mipCount             = m_hizMipCount;
                    hizDesc.pipeline             = pipelines->GetHiZPyramid();
                    hizDesc.sourceWidth          = m_width;
                    hizDesc.sourceHeight         = m_height;
                    hizDesc.bindless             = bindless;
                    for (uint32_t i = 0; i < m_hizMipCount; ++i) {
                        hizDesc.mipLevels[i] = m_hizMipLevels[i];
                    }
                    AddHiZPyramidPass(rgBuilder, hizDesc);
                }
            }

            // ── GBuffer Pass ─────────────────────────────────────────────
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
            gbDesc.meshes               = m_meshes.data();
            gbDesc.meshCount            = static_cast<uint32_t>(m_meshes.size());
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

        // ── GTAO Pass (after GBuffer, before lighting) ─────────────────
        if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled) {
            rgBuilder.AddPass("GTAO", [&](rg::RgPassBuilder& pb) {
                pb.Read(depthRes, RHIResourceState::ShaderResource);
                pb.Read(normalRes, RHIResourceState::ShaderResource);

                pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("GTAO::Execute");

                    // Get view matrix from perFrame (viewProj was set earlier)
                    // We need the VIEW matrix (not view-projection).
                    // The camera's view matrix is available via m_camera.
                    const auto& cam = *m_camera;
                    float viewMatrixData[16];
                    const auto& viewMat = cam.GetView();
                    std::memcpy(viewMatrixData, &viewMat, 16 * sizeof(float));

                    float invTanHalfFov = 1.0f / std::tan(cam.GetFovY() * 0.5f);
                    float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);

                    // Log input SRV indices being passed to GTAO on first frame
                    static bool loggedGtaoInputs = false;
                    if (!loggedGtaoInputs) {
                        UZ_LOG(Renderer, Info,
                               "SceneRenderer: GTAO inputs — depthSRV={}, normalSRV={}, "
                               "depthHandle={}, normalHandle={}, size={}x{}, frame={}",
                               m_gbuffer.depthSRV, m_gbuffer.normalSRV,
                               m_gbuffer.depth.IsValid(), m_gbuffer.normal.IsValid(),
                               m_width, m_height, frameIndex);
                        loggedGtaoInputs = true;
                    }

                    const uint32_t gtaoResult = m_gtaoPass->Execute(
                        cmd, m_cbRing.get(),
                        m_gbuffer.depthSRV, m_gbuffer.normalSRV,
                        m_gbuffer.depth, m_gbuffer.normal,
                        viewMatrixData,
                        invTanHalfFov, aspectRatio,
                        perFrame.nearPlane, perFrame.farPlane,
                        m_width, m_height,
                        frameIndex);

                    if (gtaoResult == UINT32_MAX) {
                        UZ_LOG(Renderer, Warning,
                               "SceneRenderer: GTAO Execute() returned UINT32_MAX — "
                               "AO will not be applied this frame (depthSRV={}, normalSRV={})",
                               m_gbuffer.depthSRV, m_gbuffer.normalSRV);
                    }
                });
            });
        }

        // ── DFS Compute Passes (after GBuffer + GTAO, before lighting) ──────
        // Runs only when DFS is enabled, an SDF atlas is bound and populated,
        // and the DFS output texture exists. Two passes:
        //   1. CullObjects: frustum-cull SDF objects into a compacted index buffer
        //   2. ConeTrace: per-pixel ray march through culled SDFs → shadow texture
        //
        // @source UE5 DistanceFieldShadowing.cpp — dispatch order
        const bool dfsEnabled = m_dfsConfig.enabled
                              && m_sdfAtlas && m_sdfAtlas->IsInitialized()
                              && m_sdfAtlas->GetRegisteredObjectCount() > 0
                              && m_dfsOutputTexture.IsValid()
                              && m_dfsCulledIndicesBuffer.IsValid()
                              && m_dfsIndirectArgsBuffer.IsValid();
        if (dfsEnabled) {
            // Upload dirty SDF object transforms before dispatching cull/trace.
            m_sdfAtlas->UploadObjectBuffer();

            const uint32_t objectCount = m_sdfAtlas->GetRegisteredObjectCount();
            const uint32_t dfsWidth  = m_dfsConfig.fullResolution ? m_width  : m_width  / 2;
            const uint32_t dfsHeight = m_dfsConfig.fullResolution ? m_height : m_height / 2;
            const uint32_t downsampleFactor = m_dfsConfig.fullResolution ? 1 : 2;

            // ── Async compute note (§8.3) ─────────────────────────────────────
            // UE5 dispatches DFS on an async compute queue overlapping GBuffer
            // rendering (DistanceFieldShadowing.cpp:148 — FComputeFenceRHIRef).
            // Our render graph (RgBuilder) operates on a single IRHICommandList*
            // passed to Execute(), with no per-pass queue assignment. The RHI layer
            // supports RHIQueueType::Compute (CreateCommandList), but the render
            // graph would need:
            //   1. RgPassBuilder::SetQueue(RHIQueueType) to tag compute passes.
            //   2. RgBuilder::Execute() to manage multiple command lists + fences
            //      for cross-queue synchronization (UAV barriers across queues).
            //   3. Resource state tracking that understands concurrent queue access
            //      (D3D12 simultaneous-access / split barriers).
            // Until the render graph supports multi-queue execution, DFS dispatches
            // run on the direct (graphics) queue sequentially.

            // ── Pass 1: DFS Cull Objects (compute) ──────────────────────────
            /// @source UE5 DistanceFieldShadowing.cpp:196-210 — CullObjectsForShadow dispatch
            rgBuilder.AddPass("DFS_CullObjects", [&](rg::RgPassBuilder& pb) {
                pb.Read(depthRes, RHIResourceState::ShaderResource);

                pb.SetExecutionCallback([&, objectCount](IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("DFS::CullObjects");

                    auto cullPipeline = pipelines->GetDfsCullObjects();
                    if (!cullPipeline.IsValid()) return;

                    cmd->SetPipeline(cullPipeline);

                    // Build CullConstants cbuffer (56 DWORDs = 224 bytes).
                    // Shadow convex hull planes from the primary cascade's view-projection.
                    // For now, use the first cascade's viewProj to extract 6 frustum planes
                    // expanded by the DFS max trace distance.
                    struct CullConstantsCPU {
                        float shadowConvexHull[12 * 4]; // float4 x 12 = 48 DW
                        float shadowBoundingSphere[4];  // float4 = 4 DW
                        uint32_t numShadowHullPlanes;   // 1 DW
                        uint32_t totalObjectCount;      // 1 DW
                        uint32_t pad[2];                // 2 DW
                    };
                    static_assert(sizeof(CullConstantsCPU) == 224, "CullConstants must be 224 bytes");

                    CullConstantsCPU cullCB{};
                    std::memset(&cullCB, 0, sizeof(cullCB));

                    // Extract 6 frustum planes from the first cascade's VP matrix.
                    // Each plane: float4(nx, ny, nz, d) where dot(n, p) - d < 0 = inside.
                    // @source UE5 DistanceFieldShadowing.cpp:230-260 — FrustumPlaneExtraction
                    if (totalCascadeCount > 0) {
                        const Mat4& vp = cascades[0].viewProj;
                        // Left plane
                        cullCB.shadowConvexHull[0]  = vp.m._14 + vp.m._11;
                        cullCB.shadowConvexHull[1]  = vp.m._24 + vp.m._21;
                        cullCB.shadowConvexHull[2]  = vp.m._34 + vp.m._31;
                        cullCB.shadowConvexHull[3]  = vp.m._44 + vp.m._41;
                        // Right plane
                        cullCB.shadowConvexHull[4]  = vp.m._14 - vp.m._11;
                        cullCB.shadowConvexHull[5]  = vp.m._24 - vp.m._21;
                        cullCB.shadowConvexHull[6]  = vp.m._34 - vp.m._31;
                        cullCB.shadowConvexHull[7]  = vp.m._44 - vp.m._41;
                        // Bottom plane
                        cullCB.shadowConvexHull[8]  = vp.m._14 + vp.m._12;
                        cullCB.shadowConvexHull[9]  = vp.m._24 + vp.m._22;
                        cullCB.shadowConvexHull[10] = vp.m._34 + vp.m._32;
                        cullCB.shadowConvexHull[11] = vp.m._44 + vp.m._42;
                        // Top plane
                        cullCB.shadowConvexHull[12] = vp.m._14 - vp.m._12;
                        cullCB.shadowConvexHull[13] = vp.m._24 - vp.m._22;
                        cullCB.shadowConvexHull[14] = vp.m._34 - vp.m._32;
                        cullCB.shadowConvexHull[15] = vp.m._44 - vp.m._42;
                        // Near plane
                        cullCB.shadowConvexHull[16] = vp.m._13;
                        cullCB.shadowConvexHull[17] = vp.m._23;
                        cullCB.shadowConvexHull[18] = vp.m._33;
                        cullCB.shadowConvexHull[19] = vp.m._43;
                        // Far plane
                        cullCB.shadowConvexHull[20] = vp.m._14 - vp.m._13;
                        cullCB.shadowConvexHull[21] = vp.m._24 - vp.m._23;
                        cullCB.shadowConvexHull[22] = vp.m._34 - vp.m._33;
                        cullCB.shadowConvexHull[23] = vp.m._44 - vp.m._43;

                        // Normalize each plane
                        for (uint32_t pi = 0; pi < 6; ++pi) {
                            float* p = &cullCB.shadowConvexHull[pi * 4];
                            float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                            if (len > 0.0001f) {
                                float invLen = 1.0f / len;
                                p[0] *= invLen;
                                p[1] *= invLen;
                                p[2] *= invLen;
                                p[3] *= invLen;
                            }
                        }

                        cullCB.numShadowHullPlanes = 6;
                    }

                    // Directional light: no bounding sphere (w=0 disables sphere test)
                    cullCB.shadowBoundingSphere[3] = 0.0f;
                    cullCB.totalObjectCount = objectCount;

                    // Upload cull constants via CB ring at slot 1 (b1 = per-frame CBV)
                    CBAllocation cullAlloc = m_cbRing->Allocate(sizeof(CullConstantsCPU));
                    if (cullAlloc.cpuPtr) {
                        std::memcpy(cullAlloc.cpuPtr, &cullCB, sizeof(CullConstantsCPU));
                        cmd->SetRootCBV(kSlotPerFrame, cullAlloc.bufferHandle,
                                        cullAlloc.offsetBytes, cullAlloc.cpuPtr, sizeof(CullConstantsCPU));
                    }

                    // Clear indirect args buffer: reset culled object count to 0.
                    // Allocate a tiny staging buffer from the CB ring (upload heap)
                    // and CopyBuffer to the GPU-only indirect args buffer.
                    {
                        uint32_t clearZero[3] = {0, 1, 1}; // dispatch(0, 1, 1) — X filled by atomics
                        CBAllocation clearAlloc = m_cbRing->Allocate(sizeof(clearZero));
                        if (clearAlloc.cpuPtr) {
                            std::memcpy(clearAlloc.cpuPtr, clearZero, sizeof(clearZero));

                            // Transition indirect args to CopyDst for the clear
                            RHIBarrierDesc toCopy{};
                            toCopy.buffer      = m_dfsIndirectArgsBuffer;
                            toCopy.stateBefore = RHIResourceState::UnorderedAccess;
                            toCopy.stateAfter  = RHIResourceState::CopyDst;
                            cmd->ResourceBarrier(&toCopy, 1);

                            cmd->CopyBuffer(clearAlloc.bufferHandle, clearAlloc.offsetBytes,
                                            m_dfsIndirectArgsBuffer, 0, sizeof(clearZero));

                            // Transition back to UAV for atomic writes
                            RHIBarrierDesc toUav{};
                            toUav.buffer      = m_dfsIndirectArgsBuffer;
                            toUav.stateBefore = RHIResourceState::CopyDst;
                            toUav.stateAfter  = RHIResourceState::UnorderedAccess;
                            cmd->ResourceBarrier(&toUav, 1);
                        }
                    }

                    // Bind object data SRV at t0 (via descriptor table slot 5 for SM5,
                    // or accessed via ResourceDescriptorHeap[] for bindless)
                    if (!m_device->IsBindless()) {
                        cmd->SetDescriptorTable(5, m_sdfAtlas->GetObjectBufferSrvIndex());
                    }

                    // Bind culled indices UAV (u0) and indirect args UAV (u1)
                    cmd->SetDescriptorTable(4, m_dfsCulledIndicesUavIndex);

                    // Dispatch: one thread per SDF object, 64 threads per group
                    const uint32_t cullGroupsX = (objectCount + 63) / 64;
                    cmd->Dispatch(cullGroupsX, 1, 1);

                    // UAV barriers: synchronize cull writes before cone trace reads.
                    // Both buffers stay in UnorderedAccess state — the barrier is a
                    // UAV synchronization (same state before and after).
                    RHIBarrierDesc postCullBarriers[2] = {};
                    postCullBarriers[0].buffer      = m_dfsCulledIndicesBuffer;
                    postCullBarriers[0].stateBefore = RHIResourceState::UnorderedAccess;
                    postCullBarriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
                    postCullBarriers[1].buffer      = m_dfsIndirectArgsBuffer;
                    postCullBarriers[1].stateBefore = RHIResourceState::UnorderedAccess;
                    postCullBarriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(postCullBarriers, 2);
                });
            });

            // ── Pass 2: DFS Cone Trace (compute) ────────────────────────────
            /// @source UE5 DistanceFieldShadowing.cpp:380-420 — DistanceFieldShadowing dispatch
            rgBuilder.AddPass("DFS_ConeTrace", [&, objectCount, dfsWidth, dfsHeight, downsampleFactor](rg::RgPassBuilder& pb) {
                pb.Read(depthRes, RHIResourceState::ShaderResource);

                pb.SetExecutionCallback([&, objectCount, dfsWidth, dfsHeight, downsampleFactor](
                        IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("DFS::ConeTrace");

                    auto coneTracePipeline = pipelines->GetDfsConeTrace();
                    if (!coneTracePipeline.IsValid()) return;

                    cmd->SetPipeline(coneTracePipeline);

                    // Build DfsShadowParams cbuffer.
                    // The cone trace shader reads this at b0 as a cbuffer.
                    // Since it's larger than 8 DWORDs, we bind it as a CBV via the ring allocator.
                    // CPU-side mirror of cbuffer DfsShadowParams in dfs_cone_trace_cs.hlsl.
                    // Must match HLSL 16-byte row packing exactly.
                    struct DfsConeTraceCB {
                        // Row 0-3: float4x4 (64 bytes)
                        float invViewProj[16];
                        // Row 4: float3 + float (16 bytes)
                        float lightDirX;
                        float lightDirY;
                        float lightDirZ;
                        float tanLightAngle;
                        // Row 5: 4 floats (16 bytes)
                        float maxTraceDistance;
                        float selfShadowBias;
                        float nearPlane;
                        float farPlane;
                        // Row 6: 4 uints (16 bytes)
                        uint32_t objectCount;
                        uint32_t outputWidth;
                        uint32_t outputHeight;
                        uint32_t downsampleFactor;
                        // Row 7: uint3 + float (16 bytes)
                        uint32_t atlasSizeInBricksX;
                        uint32_t atlasSizeInBricksY;
                        uint32_t atlasSizeInBricksZ;
                        float atlasTexelSizeX;
                        // Row 8: 4 floats (16 bytes)
                        float atlasTexelSizeY;
                        float atlasTexelSizeZ;
                        float rayStartOffsetDepthScale;
                        float minDepth;
                        // Row 9: float + uint + 8 bytes pad (16 bytes)
                        float maxDepth;
                        uint32_t pad0;
                        uint32_t pad1;
                        uint32_t pad2;
                    };
                    static_assert(sizeof(DfsConeTraceCB) == 160,
                                  "DfsConeTraceCB must be 160 bytes (10 x 16-byte rows)");

                    DfsConeTraceCB traceCB{};

                    // Compute inverse view-projection matrix
                    const Mat4 viewProj = camera.GetView() * camera.GetProjection();
                    Mat4 invViewProj = viewProj.Inverse();
                    std::memcpy(traceCB.invViewProj, &invViewProj, 64);

                    traceCB.lightDirX = sunDir.x;
                    traceCB.lightDirY = sunDir.y;
                    traceCB.lightDirZ = sunDir.z;

                    // @source UE5 DirectionalLightComponent.h — default LightSourceAngle = 0.5357 degrees
                    const float lightSourceAngleDeg = lightData.lightSourceAngleDeg > 0.001f
                                                    ? lightData.lightSourceAngleDeg : 0.5357f;
                    traceCB.tanLightAngle = std::tan(lightSourceAngleDeg * 3.14159265f / 180.0f);
                    traceCB.maxTraceDistance = m_dfsConfig.distance;
                    traceCB.selfShadowBias  = m_dfsConfig.selfShadowBias;
                    traceCB.nearPlane  = perFrame.nearPlane;
                    traceCB.farPlane   = perFrame.farPlane;
                    traceCB.objectCount = objectCount;
                    traceCB.outputWidth  = dfsWidth;
                    traceCB.outputHeight = dfsHeight;
                    traceCB.downsampleFactor = downsampleFactor;

                    // Atlas dimensions: the atlas is a cube of atlasSize^3 texels,
                    // divided into 8^3 bricks. BricksPerAxis = atlasSize / 8.
                    // We compute from the atlas total capacity and brick size.
                    const uint32_t totalBricks = m_sdfAtlas->GetTotalBrickCapacity();
                    // Cube root approximation: atlas is cube, so bricksPerAxis = cbrt(totalBricks)
                    // For 256^3 atlas with 8^3 bricks: 32 bricks per axis, totalBricks = 32768
                    uint32_t bricksPerAxis = 1;
                    while (bricksPerAxis * bricksPerAxis * bricksPerAxis < totalBricks) {
                        ++bricksPerAxis;
                    }
                    traceCB.atlasSizeInBricksX = bricksPerAxis;
                    traceCB.atlasSizeInBricksY = bricksPerAxis;
                    traceCB.atlasSizeInBricksZ = bricksPerAxis;

                    const float atlasTexelDim = static_cast<float>(bricksPerAxis * 8);
                    traceCB.atlasTexelSizeX = 1.0f / atlasTexelDim;
                    traceCB.atlasTexelSizeY = 1.0f / atlasTexelDim;
                    traceCB.atlasTexelSizeZ = 1.0f / atlasTexelDim;

                    // @source UE5 DistanceFieldShadowing.usf:696 — RayStartOffsetDepthScale
                    traceCB.rayStartOffsetDepthScale = 0.0005f;
                    traceCB.minDepth = 0.0f;
                    traceCB.maxDepth = m_dfsConfig.distance;
                    traceCB.pad0 = 0;
                    traceCB.pad1 = 0;
                    traceCB.pad2 = 0;

                    // Upload via CB ring at slot 1 (b1 in the shader's cbuffer register).
                    // The shader declares the cbuffer at register(b1) to match the
                    // compute root signature's kSlotPerFrame = slot 1 = b1.
                    CBAllocation traceAlloc = m_cbRing->Allocate(sizeof(DfsConeTraceCB));
                    if (traceAlloc.cpuPtr) {
                        std::memcpy(traceAlloc.cpuPtr, &traceCB, sizeof(DfsConeTraceCB));
                        cmd->SetRootCBV(kSlotPerFrame, traceAlloc.bufferHandle,
                                        traceAlloc.offsetBytes, traceAlloc.cpuPtr, sizeof(DfsConeTraceCB));
                    }

                    // Bind SRVs:
                    // t0 = depth buffer, t1 = object data, t2 = SDF atlas, t3 = indirection, t4 = culled indices
                    if (!m_device->IsBindless()) {
                        cmd->SetDescriptorTable(5, m_gbuffer.depthSRV);
                    }

                    // Bind output UAV (u0) — DFS result texture
                    cmd->SetDescriptorTable(4, m_dfsOutputUavIndex);

                    // Dispatch: 8x8 thread groups covering the output resolution
                    const uint32_t groupsX = (dfsWidth  + 7) / 8;
                    const uint32_t groupsY = (dfsHeight + 7) / 8;
                    cmd->Dispatch(groupsX, groupsY, 1);

                    // Transition DFS output: UAV write -> SRV read for lighting pass
                    RHIBarrierDesc outputBarrier{};
                    outputBarrier.texture     = m_dfsOutputTexture;
                    outputBarrier.stateBefore = RHIResourceState::UnorderedAccess;
                    outputBarrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&outputBarrier, 1);
                });
            });
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
            // IBL texture handles for SM5 fallback path (t5-t7) — kept for
            // backward compat; the binder path reads from perFrameParams instead.
            if (m_iblBaker) {
                lightDesc.iblIrradiance   = m_iblBaker->GetIrradianceTexture();
                lightDesc.iblPrefiltered  = m_iblBaker->GetPrefilteredTexture();
                lightDesc.iblBrdfLut      = m_iblBaker->GetBRDFLutTexture();
            }
            lightDesc.bindless            = bindless;
            lightDesc.viewportWidth       = vpWidth;
            lightDesc.viewportHeight      = vpHeight;
            // Binder path — ShaderParameterBinder uploads per-frame CB and
            // binds IBL textures via metadata. On SM5, BindResources calls
            // SetFragmentTexture for each texture member automatically.
            lightDesc.perFrameParams    = &perFrame;
            lightDesc.shadowDataParams  = &shadowDataParams;
            lightDesc.cbRingAllocator   = m_cbRing.get();
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
        if (!m_meshes.empty()) {
            PBRPassDesc fwdDesc{};
            fwdDesc.backbuffer           = hdrRes;
            fwdDesc.depthBuffer          = depthRes;
            fwdDesc.depthTexture         = m_gbuffer.depth;
            fwdDesc.drawList             = &drawList;
            fwdDesc.cbAllocator          = m_cbRing.get();
            fwdDesc.perFrameAlloc        = perFrameAlloc;
            fwdDesc.pipeline             = pipelines->GetForwardPBR(bindless);
            fwdDesc.meshes               = m_meshes.data();
            fwdDesc.meshCount            = static_cast<uint32_t>(m_meshes.size());
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

    // Skip sky + fog when buffer visualization grid is active — the grid
    // takes over the entire HDR target and these passes would overwrite it.
    if (m_shadowDebugMode == 0) {
        AddAtmospherePasses(rgBuilder, hdrRes, depthRes, perFrameAlloc, atmoCtx);

        // Sky light color from SH L0 (DC term) — for fog sky atmosphere injection
        /// @source UE5 HeightFogCommon.ush:175 — GetViewDistanceSkyLightColor()
        float skyFogR = m_skyIrradianceSH.coeffs[0].w;
        float skyFogG = m_skyIrradianceSH.coeffs[1].w;
        float skyFogB = m_skyIrradianceSH.coeffs[2].w;

        AddHeightFogFromECS(rgBuilder, hdrRes, depthRes, perFrameAlloc,
                            world, camera, pipelines, *m_cbRing,
                            m_gbuffer.depthSRV, m_gbuffer.depth,
                            bindless, vpWidth, vpHeight,
                            skyFogR, skyFogG, skyFogB);
    }

    // (atmosphere and fog passes registered above via AddAtmospherePasses + AddHeightFogFromECS)

    // ── Bloom pass (compute: threshold + downsample + upsample) ─────────
    // Runs after all HDR rendering is complete. Reads HDR scene color,
    // extracts bright pixels, and builds a multi-scale bloom mip chain.
    // The bloom result (mip[0]) is composited by the tonemap shader.
    //
    // Only enabled when:
    // 1. BloomPass is initialized (DX12 compute available)
    // 2. HDR scene color has content from a previous frame (first frame skipped)
    // 3. Bloom is enabled in settings
    // 4. Buffer debug grid is not active
    const bool bloomEnabled = m_bloomPass && m_bloomPass->IsInitialized()
                            && m_bloomPass->settings.enabled
                            && m_shadowDebugMode == 0
                            && bindless;  // Bloom compute requires SM6.6 bindless

    if (bloomEnabled) {
        rgBuilder.AddPass("Bloom", [&](rg::RgPassBuilder& pb) {
            pb.Read(hdrRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("Bloom::Execute");

                // HDR is in ShaderResource state (render graph transition).
                // Dispatch the full bloom compute chain.
                m_bloomPass->Execute(cmd, m_hdrSceneColorSrv,
                                     vpWidth, vpHeight,
                                     perFrame.exposure);
            });
        });
    }

    // ── TAA Resolve pass (compute: temporal accumulation) ────────────────
    // @source UE5 TemporalAA.cpp:395-396 — FTemporalAACS compute shader
    // @source UE5 TemporalAA.cpp:41 — r.TemporalAACurrentFrameWeight default 0.04
    //
    // Reads the current HDR scene color and the previous frame's TAA history,
    // performs YCoCg neighborhood clamping, HDR-weighted blend, and writes
    // the new history. The tonemap pass then reads from the TAA output.
    //
    // The current write target is m_taaHistory[m_taaHistoryIndex].
    // The previous read target is m_taaHistory[1 - m_taaHistoryIndex].
    uint32_t taaSrvForTonemap = m_hdrSceneColorSrv;  // fallback if TAA off

    if (taaActive && m_shadowDebugMode == 0) {
        const uint32_t writeIdx = m_taaHistoryIndex;
        const uint32_t readIdx  = 1 - m_taaHistoryIndex;

        rgBuilder.AddPass("TAA_Resolve", [&, writeIdx, readIdx, jitterX, jitterY](rg::RgPassBuilder& pb) {
            pb.Read(hdrRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([&, writeIdx, readIdx, jitterX, jitterY](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("TAA::Resolve");

                auto taaPSO = pipelines->GetTaaResolve();
                if (!taaPSO.IsValid()) return;

                // Transition TAA history textures:
                // - Write target: SRV → UAV (will be written by compute)
                // - Read target stays in SRV (sampled by compute)
                {
                    RHIBarrierDesc barriers[2]{};

                    // Write target → UAV
                    barriers[0].texture     = m_taaHistory[writeIdx];
                    barriers[0].buffer      = RHIBufferHandle::Invalid();
                    barriers[0].stateBefore = RHIResourceState::ShaderResource;
                    barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;

                    // Read target → SRV (may already be SRV, but be explicit)
                    barriers[1].texture     = m_taaHistory[readIdx];
                    barriers[1].buffer      = RHIBufferHandle::Invalid();
                    barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
                    barriers[1].stateAfter  = RHIResourceState::ShaderResource;

                    cmd->ResourceBarrier(barriers, 2);
                }

                cmd->SetPipeline(taaPSO);

                // Root constants: 10 DWORDs (matches TAARootConstants cbuffer)
                // [0-1] = jitterOffset (float2)
                // [2-3] = texelSize (float2)
                // [4]   = currentFrameWeight (float)
                // [5]   = sceneSrvIndex (uint)
                // [6]   = depthSrvIndex (uint)
                // [7]   = historySrvIndex (uint)
                // [8]   = outputUavIndex (uint)
                // [9]   = padding
                struct TaaConstants {
                    float jitterX, jitterY;
                    float texelSizeX, texelSizeY;
                    float blendWeight;
                    uint32_t sceneSrv;
                    uint32_t depthSrv;
                    uint32_t historySrv;
                    uint32_t outputUav;
                    uint32_t pad0;
                } constants;
                constants.jitterX    = jitterX;
                constants.jitterY    = jitterY;
                constants.texelSizeX = 1.0f / static_cast<float>(vpWidth);
                constants.texelSizeY = 1.0f / static_cast<float>(vpHeight);
                /// @source UE5 TemporalAA.cpp:41 — CurrentFrameWeight = 0.04
                constants.blendWeight = m_prevViewProjValid ? 0.04f : 1.0f;  // First frame: use current only
                constants.sceneSrv   = m_hdrSceneColorSrv;
                constants.depthSrv   = m_gbuffer.depthSRV;
                constants.historySrv = m_taaHistorySrv[readIdx];
                constants.outputUav  = m_taaHistoryUav[writeIdx];
                constants.pad0       = 0;

                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&constants), 10);

                // Dispatch thread groups (8x8 per group)
                const uint32_t groupsX = (vpWidth  + 7) / 8;
                const uint32_t groupsY = (vpHeight + 7) / 8;
                cmd->Dispatch(groupsX, groupsY, 1);

                // Transition write target back to SRV for tonemap to read.
                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_taaHistory[writeIdx];
                    barrier.buffer      = RHIBufferHandle::Invalid();
                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                    barrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&barrier, 1);
                }
            });
        });

        // Tonemap reads from the TAA output instead of raw HDR scene color.
        taaSrvForTonemap = m_taaHistorySrv[writeIdx];

        // Flip ping-pong index for next frame.
        m_taaHistoryIndex = 1 - m_taaHistoryIndex;

        // Store unjittered viewProj for next frame's reprojection.
        m_prevViewProj = unjitteredViewProj;
        m_prevViewProjValid = true;
    }

    // ── Tonemap pass (HDR → LDR backbuffer) ────────────────────────────
    // Reads HDR scene color (RGBA16F) + optional bloom result, applies
    // bloom composite + auto-exposure + ACES filmic tonemap + sRGB gamma,
    // writes LDR to backbuffer (RGBA8Unorm).
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

            // Root constants: HDR scene color SRV + bloom SRV + bloom intensity
            // Bloom SRV index = 0 means no bloom (shader checks for this)
            uint32_t bloomSrvIdx = 0;
            float bloomIntensity = 0.0f;
            if (bloomEnabled && m_bloomPass->GetBloomResultSrvIndex() != UINT32_MAX) {
                bloomSrvIdx = m_bloomPass->GetBloomResultSrvIndex();
                bloomIntensity = m_bloomPass->settings.intensity;
            }
            uint32_t bloomIntensityBits = 0;
            std::memcpy(&bloomIntensityBits, &bloomIntensity, sizeof(float));

            // When TAA is active, read from TAA resolve output; otherwise raw HDR.
            uint32_t indices[4] = { taaSrvForTonemap, bloomSrvIdx, bloomIntensityBits, 0 };
            cmd->SetRootConstants(kSlotRootConstants, indices, 4);

            // SM5: bind HDR scene color texture at t0 directly
            // (TAA not available on SM5 path since it requires bindless)
            if (!bindless) {
                cmd->SetFragmentTexture(0, m_hdrSceneColor);
            }

            // PerFrameCB at b1 — contains exposure from auto-exposure
            cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.bufferHandle,
                            perFrameAlloc.offsetBytes, perFrameAlloc.cpuPtr,
                            perFrameAlloc.sizeBytes);

            cmd->Draw(3); // fullscreen triangle

            cmd->EndRenderPass();
        });
    });

    // ── Overlay pass (editor grid + stats overlay) ──────────────────────
    // Skip overlays when buffer visualization grid is active — grid owns the full screen.
    if (m_shadowDebugMode == 0)
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
                const uint32_t drawCount = useInstancing
                    ? static_cast<uint32_t>(instanceBatches.size())
                    : static_cast<uint32_t>(drawList.Count());
                // Accumulate exact vertex/triangle counts across all draw commands.
                // This correctly handles scenes with multiple distinct meshes.
                uint32_t totalVertices  = 0;
                uint32_t totalTriangles = 0;
                for (const auto& dc : drawList.GetCommands()) {
                    if (dc.meshId < m_meshes.size() && m_meshes[dc.meshId].IsValid()) {
                        const GPUMesh& m = m_meshes[dc.meshId];
                        totalVertices  += m.vertexCount;
                        totalTriangles += m.indexCount / 3;
                    }
                }
                m_statsOverlay->SetDrawCalls(drawCount);
                m_statsOverlay->SetVertexCount(totalVertices);
                m_statsOverlay->SetTriangleCount(totalTriangles);
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

    // Shadow Atlas Diagnostic Readback (shadowDebugMode >= 2).
    // Currently disabled — requires render graph pass integration for safe
    // barrier management. Use RenderDoc/PIX to inspect the shadow atlas.
    // The readback infrastructure is in PerformShadowAtlasReadback().

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

// ─────────────────────────────────────────────────────────────────────────────
// RequestScreenshot
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::RequestScreenshot(const std::string& path) {
    m_screenshotPath    = path;
    m_screenshotPending = true;
}

void SceneRenderer::RequestScreenshotBase64() {
    m_screenshotBase64  = true;
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

        // Map ECS mobility to renderer-side EMobility for shadow cache filtering.
        // Stationary is treated as Static for shadow caching (UE5 parity).
        switch (sceneComp.mobility) {
            case SceneComponent::Mobility::Static:
                cmd.mobility = EMobility::Static;
                break;
            case SceneComponent::Mobility::Stationary:
                cmd.mobility = EMobility::Stationary;
                break;
            case SceneComponent::Mobility::Movable:
            default:
                cmd.mobility = EMobility::Movable;
                break;
        }

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

std::string SceneRenderer::GetTexturePath(uint32_t textureId) const {
    for (const auto& [path, id] : m_textureMap) {
        if (id == textureId) return path;
    }
    return {};
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

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Atlas Diagnostic Readback
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::PerformShadowAtlasReadback(IRHICommandList* cmdList) {
    if (!m_shadowAtlas.GetTexture().IsValid()) {
        m_shadowReadbackPhase = 2;
        return;
    }

    if (m_shadowReadbackPhase == 0) {
        // Phase 0: Create readback buffer, issue GPU copy.
        // Buffer stays alive — destroyed only in phase 1 after GPU is done.
        const uint32_t atlasWidth = kShadowResolution * GetAtlasColumns(4);
        const uint32_t rowBytes   = atlasWidth * 4;
        const uint32_t rowPitch   = (rowBytes + 255) & ~255u;
        m_shadowReadbackRowPitch  = rowPitch;
        m_shadowReadbackWidth     = atlasWidth;

        RHIBufferDesc rbDesc{};
        rbDesc.sizeBytes = rowPitch;
        rbDesc.memory    = RHIMemoryType::Readback;
        rbDesc.debugName = "ShadowAtlasReadback";
        m_shadowReadbackBuffer = m_device->CreateBuffer(rbDesc);
        if (!m_shadowReadbackBuffer.IsValid()) {
            UZ_LOG(Shadow, Error, "ShadowAtlasReadback: failed to create buffer");
            m_shadowReadbackPhase = 2;
            return;
        }

        RHIBarrierDesc toCopy{};
        toCopy.texture     = m_shadowAtlas.GetTexture();
        toCopy.stateBefore = RHIResourceState::ShaderResource;
        toCopy.stateAfter  = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&toCopy, 1);

        cmdList->CopyTextureToBuffer(m_shadowAtlas.GetTexture(), 0,
                                      m_shadowReadbackBuffer, 0, rowPitch);

        RHIBarrierDesc toSrv{};
        toSrv.texture     = m_shadowAtlas.GetTexture();
        toSrv.stateBefore = RHIResourceState::CopySrc;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&toSrv, 1);

        UZ_LOG(Shadow, Info, "ShadowAtlasReadback: GPU copy issued (phase 0)");
        m_shadowReadbackPhase = 1;

    } else if (m_shadowReadbackPhase >= 1 && m_shadowReadbackPhase <= 3) {
        // Phases 1-3: Wait for GPU to finish the copy command.
        // Triple-buffered = need 3 frames before the copy is guaranteed complete.
        UZ_LOG(Shadow, Info, "ShadowAtlasReadback: waiting (phase {})", m_shadowReadbackPhase);
        m_shadowReadbackPhase++;

    } else if (m_shadowReadbackPhase == 4) {
        // Phase 4: At least 3 full frames passed since the copy — safe to read.
        // Buffer is NOT destroyed here — kept alive until Shutdown to avoid
        // any possibility of destroying while GPU references it.
        void* mapped = m_device->MapBuffer(m_shadowReadbackBuffer);
        if (mapped) {
            const float* row0 = reinterpret_cast<const float*>(mapped);
            const uint32_t rowPixels = m_shadowReadbackWidth;
            const uint32_t halfRes   = kShadowResolution / 2;

            UZ_LOG(Shadow, Info, "=== Shadow Atlas Diagnostic Readback ===");
            UZ_LOG(Shadow, Info, "Atlas: {}x{} D32Float", rowPixels, rowPixels);
            UZ_LOG(Shadow, Info, "Row 0: px[0]={:.6f} px[{}]={:.6f} px[{}]={:.6f} px[{}]={:.6f}",
                   row0[0],
                   halfRes/2, row0[std::min(halfRes/2, rowPixels-1)],
                   halfRes,   row0[std::min(halfRes, rowPixels-1)],
                   kShadowResolution-1, row0[std::min(kShadowResolution-1, rowPixels-1)]);

            uint32_t nCleared = 0, nGeom = 0, nZero = 0;
            float minV = 1.0f, maxV = 0.0f;
            for (uint32_t x = 0; x < std::min(rowPixels, kShadowResolution); ++x) {
                float v = row0[x];
                if (v >= 0.9999f) ++nCleared;
                else if (v <= 0.0001f) ++nZero;
                else ++nGeom;
                minV = std::min(minV, v);
                maxV = std::max(maxV, v);
            }
            UZ_LOG(Shadow, Info, "Stats: cleared(1.0)={} geometry={} zero(0.0)={} min={:.8f} max={:.8f}",
                   nCleared, nGeom, nZero, minV, maxV);

            if (nZero > 0 && nGeom == 0 && nCleared == 0)
                UZ_LOG(Shadow, Error, "ALL ZERO — shadow atlas not written!");
            else if (nGeom > 0)
                UZ_LOG(Shadow, Info, "Shadow atlas contains valid depth data");
            else
                UZ_LOG(Shadow, Warning, "No geometry in row 0 — check cascade coverage");

            m_device->UnmapBuffer(m_shadowReadbackBuffer);
        } else {
            UZ_LOG(Shadow, Error, "ShadowAtlasReadback: failed to map buffer");
        }

        // Keep buffer alive — destroyed in Shutdown() to avoid GPU reference issues.
        m_shadowReadbackPhase = 5; // done
    }
}

} // namespace uz
