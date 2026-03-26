/// VFX Viewport Demo — Main Implementation
///
/// Implements OnInit, OnFrame, OnResize, and OnShutdown for the VFX viewport
/// demo. Uses DemoApplication infrastructure for window, device, input, camera,
/// shaders, pipelines, CB ring, editor grid, and stats overlay.

#include "vfx_viewport_demo.h"
#include "uzengine.h"
#include "constant_buffers.h"
#include "engine_shaders.h"
#include "upload_context.h"
#include "pipeline_library.h"
#include "cb_ring_allocator.h"
#include "editor_grid.h"
#include "stats_overlay.h"
#include "screenshot_capture.h"
#include "camera.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "rhi_swapchain.h"
#include "texture_loader.h"
#include "root_slots.h"
#include "platform_window.h"
#include "renderdoc_capture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Material Presets
// ─────────────────────────────────────────────────────────────────────────────

static const MaterialPreset kMaterials[] = {
    {"Red Plastic",     {0.8f, 0.1f, 0.1f},  0.0f, 0.4f,  1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Gold Metal",      {1.0f, 0.76f, 0.33f}, 1.0f, 0.3f,  1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Rough Stone",     {0.5f, 0.45f, 0.4f},  0.0f, 0.85f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Polished Chrome", {0.9f, 0.9f, 0.9f},   1.0f, 0.05f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Rubber",          {0.15f, 0.15f, 0.15f}, 0.0f, 0.9f,  1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Copper",          {0.97f, 0.74f, 0.62f}, 1.0f, 0.45f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Ceramic White",   {0.95f, 0.93f, 0.88f}, 0.0f, 0.15f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Emissive Blue",   {0.1f, 0.1f, 0.3f},   0.0f, 0.5f,  1.0f, {0.2f, 0.4f, 1.0f}, 5.0f},
};
static constexpr int kMaterialCount = static_cast<int>(sizeof(kMaterials) / sizeof(kMaterials[0]));

static const char* kMeshNames[] = {"Sphere", "Cube", "Plane", "Cylinder", "Torus"};

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

VFXViewportDemo::VFXViewportDemo(const DemoConfig& config)
    : DemoApplication(config) {
    // Initialize the VFX emitter from the first preset
    int presetCount = 0;
    const VFXPreset* presets = GetVFXPresets(presetCount);
    m_vfxEmitter = presets[0].config;
    m_vfxSpawnRate = presets[0].spawnRate;
    m_prevViewProj = Mat4::Identity();
}

void VFXViewportDemo::SetTexturePaths(const std::string& albedo,
                                       const std::string& normal,
                                       const std::string& roughness) {
    m_albedoPath = albedo;
    m_normalPath = normal;
    m_roughnessPath = roughness;
}

// ─────────────────────────────────────────────────────────────────────────────
// Size-Dependent Resource Management
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::CreateSizeDependent(uint32_t width, uint32_t height) {
    auto* device = GetDevice();

    // HDR render target (RGBA16F)
    {
        RHITextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = RHIFormat::RGBA16Float;
        desc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource | RHITextureUsage::CopySrc;
        desc.debugName = "VFXViewport_HDR";
        m_hdrTarget = device->CreateTexture(desc);
    }

    // Depth buffer (D32_FLOAT)
    {
        RHITextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = RHIFormat::D32Float;
        desc.usage = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
        desc.debugName = "VFXViewport_Depth";
        m_depthBuffer = device->CreateTexture(desc);
    }

    // Distortion accumulation buffer (RGBA16F)
    {
        RHITextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = RHIFormat::RGBA16Float;
        desc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource;
        desc.debugName = "VFXViewport_DistortionRT";
        m_distortionRT = device->CreateTexture(desc);
    }

    // Pre-distortion HDR copy (same format as HDR target, used by resolve pass)
    {
        RHITextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = RHIFormat::RGBA16Float;
        desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        desc.debugName = "VFXViewport_PreDistortionCopy";
        m_preDistortionCopy = device->CreateTexture(desc);
    }
}

void VFXViewportDemo::DestroySizeDependent() {
    auto* device = GetDevice();
    if (m_hdrTarget.IsValid()) { device->DestroyTexture(m_hdrTarget); m_hdrTarget = {}; }
    if (m_depthBuffer.IsValid()) { device->DestroyTexture(m_depthBuffer); m_depthBuffer = {}; }
    if (m_distortionRT.IsValid()) { device->DestroyTexture(m_distortionRT); m_distortionRT = {}; }
    if (m_preDistortionCopy.IsValid()) { device->DestroyTexture(m_preDistortionCopy); m_preDistortionCopy = {}; }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnInit
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::OnInit() {
    auto* device = GetDevice();
    auto* cmd = GetCommandList();
    auto* shaders = GetShaderManager();

    UploadContext upload(device);

    // ── Generate and upload procedural meshes ────────────────────────────
    {
        auto sphereData = GenerateSphere(32, 16);
        m_meshes[0] = UploadProceduralMesh(device, cmd, upload, sphereData, "Sphere");

        auto cubeData = GenerateCube();
        m_meshes[1] = UploadProceduralMesh(device, cmd, upload, cubeData, "Cube");

        auto planeData = GeneratePlane(5.0f, 10);
        m_meshes[2] = UploadProceduralMesh(device, cmd, upload, planeData, "Plane");

        auto cylinderData = GenerateCylinder(0.5f, 2.0f, 32);
        m_meshes[3] = UploadProceduralMesh(device, cmd, upload, cylinderData, "Cylinder");

        auto torusData = GenerateTorus(1.0f, 0.3f, 32, 16);
        m_meshes[4] = UploadProceduralMesh(device, cmd, upload, torusData, "Torus");
    }

    // ── Shadow map (2048x2048 depth-only) ────────────────────────────────
    {
        RHITextureDesc desc{};
        desc.width = kShadowMapSize;
        desc.height = kShadowMapSize;
        desc.format = RHIFormat::D32Float;
        desc.usage = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
        desc.debugName = "VFXViewport_ShadowMap";
        m_shadowMap = device->CreateTexture(desc);
    }

    // ── Size-dependent resources (HDR target, depth buffer) ──────────────
    CreateSizeDependent(GetWidth(), GetHeight());

    // ── Pipeline creation ────────────────────────────────────────────────
    // Sky atmosphere (fullscreen triangle)
    m_skyPSO = GetPipelines()->GetSkyAtmosphere();
    if (!m_skyPSO.IsValid()) {
        UZ_LOG(App, Error, "VFXViewport: Failed to get sky atmosphere PSO");
    }

    // Tonemap (fullscreen triangle)
    m_tonemapPSO = GetPipelines()->GetTonemap();
    if (!m_tonemapPSO.IsValid()) {
        UZ_LOG(App, Error, "VFXViewport: Failed to get tonemap PSO");
    }

    // ── VFX particle system ──────────────────────────────────────────────
    if (!m_vfxSystem.Init(device, shaders, 50000)) {
        UZ_LOG(App, Error, "VFXViewport: Failed to initialize VFX particle system");
        m_vfxEnabled = false;
    }

    // ── Bloom pass ──────────────────────────────────────────────────────
    if (!m_bloomPass.Init(device, shaders, GetWidth(), GetHeight())) {
        UZ_LOG(App, Error, "VFXViewport: Failed to initialize bloom pass");
        m_bloomEnabled = false;
    }

    // ── Load PBR textures from CLI paths ─────────────────────────────────
    // TextureLoader::Load is synchronous — it flushes the command list internally
    // and leaves it in recording state for subsequent commands.
    if (!m_albedoPath.empty()) {
        auto result = TextureLoader::Load(device, cmd, m_albedoPath, true);
        if (result.handle.IsValid()) {
            m_albedoTex = result.handle;
            m_albedoSrvIndex = result.srvIndex;
            UZ_LOG(App, Info, "VFXViewport: Loaded albedo texture '{}' (SRV={})",
                   m_albedoPath, m_albedoSrvIndex);
        } else {
            UZ_LOG(App, Error, "VFXViewport: Failed to load albedo texture '{}'",
                   m_albedoPath);
        }
    }
    if (!m_normalPath.empty()) {
        auto result = TextureLoader::Load(device, cmd, m_normalPath, false);
        if (result.handle.IsValid()) {
            m_normalTex = result.handle;
            m_normalSrvIndex = result.srvIndex;
            UZ_LOG(App, Info, "VFXViewport: Loaded normal texture '{}' (SRV={})",
                   m_normalPath, m_normalSrvIndex);
        } else {
            UZ_LOG(App, Error, "VFXViewport: Failed to load normal texture '{}'",
                   m_normalPath);
        }
    }
    if (!m_roughnessPath.empty()) {
        auto result = TextureLoader::Load(device, cmd, m_roughnessPath, false);
        if (result.handle.IsValid()) {
            m_roughnessTex = result.handle;
            m_roughnessSrvIndex = result.srvIndex;
            UZ_LOG(App, Info, "VFXViewport: Loaded roughness texture '{}' (SRV={})",
                   m_roughnessPath, m_roughnessSrvIndex);
        } else {
            UZ_LOG(App, Error, "VFXViewport: Failed to load roughness texture '{}'",
                   m_roughnessPath);
        }
    }

    // ── Trail ribbon PSO ───────────────────────────────────────────────
    // Premultiplied alpha blend, depth test (no write), double-sided.
    // Uses Input Assembler vertex buffer (TrailVertex, 36 bytes stride).
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = shaders->CreateShader(EngineShader::TrailVS, RHIShaderStage::Vertex);
        desc.pixelShader  = shaders->CreateShader(EngineShader::TrailPS, RHIShaderStage::Pixel);
        desc.renderTargetCount = 1;
        desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR target
        desc.depthStencilFormat = RHIFormat::D32Float;
        desc.depthStencil.depthEnable = true;
        desc.depthStencil.depthWrite = false;   // Trails don't write depth
        desc.depthStencil.depthFunc = RHICompareFunc::Greater; // Reverse-Z: near=1, far=0
        desc.rasterizer.cullMode = RHICullMode::None; // Ribbon viewable from both sides
        // Premultiplied alpha: SrcOne / InvSrcAlpha
        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp  = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].alphaOp  = RHIBlendOp::Add;
        // Input layout: TrailVertex (36 bytes)
        // POSITION float3 offset 0, TEXCOORD0 float2 offset 12, COLOR0 float4 offset 20
        static const RHIInputElementDesc kTrailInputLayout[] = {
            { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float, 0, 0 },
            { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,  0, 12 },
            { RHIInputSemantic::Color,    0, RHIFormat::RGBA32Float, 0, 20 },
        };
        desc.inputElements = kTrailInputLayout;
        desc.inputElementCount = 3;
        desc.vertexStride = 36; // sizeof(TrailVertex) = 36
        desc.debugName = "VFX_TrailPSO";
        m_trailPSO = device->CreateGraphicsPipeline(desc);
        if (!m_trailPSO.IsValid()) {
            UZ_LOG(App, Error, "VFXViewport: Failed to create trail ribbon PSO");
            m_trailEnabled = false;
        }
    }

    // ── Trail dynamic vertex / index upload buffers ─────────────────────
    if (m_trailEnabled) {
        {
            RHIBufferDesc vbDesc{};
            vbDesc.sizeBytes = kMaxTrailVertices * 36; // sizeof(TrailVertex) = 36 bytes
            vbDesc.stride = 36;
            vbDesc.usage = RHIBufferUsage::VertexBuffer;
            vbDesc.memory = RHIMemoryType::Upload;
            vbDesc.debugName = "Trail_DynamicVB";
            m_trailVB = device->CreateBuffer(vbDesc);
        }
        {
            RHIBufferDesc ibDesc{};
            ibDesc.sizeBytes = kMaxTrailIndices * sizeof(uint32_t);
            ibDesc.stride = sizeof(uint32_t);
            ibDesc.usage = RHIBufferUsage::IndexBuffer;
            ibDesc.memory = RHIMemoryType::Upload;
            ibDesc.debugName = "Trail_DynamicIB";
            m_trailIB = device->CreateBuffer(ibDesc);
        }
    }

    // ── Register a demo trail (orbiting fire trail matching Fire Fountain) ──
    if (m_trailEnabled) {
        TrailComponent trail;
        trail.maxTrailPoints  = 128;
        trail.widthStart      = 0.3f;
        trail.widthEnd        = 0.02f;
        trail.pointLifetime   = 1.5f;
        trail.minPointDistance = 0.05f;
        trail.colorStart      = Vec4(1.0f, 0.6f, 0.2f, 0.8f); // Warm orange, semi-transparent
        trail.colorEnd        = Vec4(1.0f, 0.2f, 0.05f, 0.0f); // Red-orange, fully faded
        trail.uvScrollRate    = 1.0f;
        m_trailHandle = m_trailSystem.RegisterTrail(std::move(trail));
    }

    // ── Decal forward PSO ───────────────────────────────────────────────
    // Front-face culling (render back faces), no depth write, alpha blend.
    // Uses DecalVS (SV_VertexID unit cube) + DecalForwardPS (single RT).
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = shaders->CreateShader(EngineShader::DecalVS, RHIShaderStage::Vertex);
        desc.pixelShader  = shaders->CreateShader(EngineShader::DecalForwardPS, RHIShaderStage::Pixel);
        desc.renderTargetCount = 1;
        desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR target
        desc.depthStencilFormat = RHIFormat::Unknown;         // No depth attachment (depth read as SRV)
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        // Front-face cull: render back faces of decal OBB so fragments are
        // generated even when the camera is inside the decal volume.
        desc.rasterizer.cullMode = RHICullMode::Front;
        // Alpha blend: SrcAlpha / InvSrcAlpha
        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp  = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].alphaOp  = RHIBlendOp::Add;
        // No input layout — VS uses SV_VertexID
        desc.debugName = "VFX_DecalForwardPSO";
        m_decalPSO = device->CreateGraphicsPipeline(desc);
        if (!m_decalPSO.IsValid()) {
            UZ_LOG(App, Error, "VFXViewport: Failed to create decal forward PSO");
            m_decalEnabled = false;
        }
    }

    // ── Spawn demo decals on the ground plane ───────────────────────────
    // Place decals at interesting positions around the scene.
    // Y=-0.49 sits just above the ground plane (grid at Y=-0.5).
    if (m_decalEnabled) {
        m_decals[0] = { Vec3(0.0f, -0.49f, 0.0f), Vec3(2.0f, 0.5f, 2.0f), 0.9f };   // Center ground
        m_decals[1] = { Vec3(-3.0f, -0.49f, -3.0f), Vec3(1.5f, 0.5f, 1.5f), 0.7f };  // Under ref sphere
        m_decals[2] = { Vec3(3.0f, -0.49f, -3.0f), Vec3(1.5f, 0.5f, 1.5f), 0.7f };   // Under ref sphere
        m_decals[3] = { Vec3(0.0f, -0.49f, -3.0f), Vec3(1.0f, 0.5f, 1.0f), 0.8f };   // Under ref sphere
        m_activeDecalCount = 4;
    }

    // ── Distortion accumulation PSO (fullscreen, additive blend into RGBA16F) ──
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = shaders->CreateShader(EngineShader::FullscreenVS, RHIShaderStage::Vertex);
        desc.pixelShader  = shaders->CreateShader(EngineShader::DistortionHazePS, RHIShaderStage::Pixel);
        desc.renderTargetCount = 1;
        desc.renderTargetFormats[0] = RHIFormat::RGBA16Float;
        desc.depthStencilFormat = RHIFormat::Unknown;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        // Additive blending for UV offset accumulation
        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].colorOp  = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].alphaOp  = RHIBlendOp::Add;
        desc.rasterizer.cullMode = RHICullMode::None;
        desc.debugName = "VFX_DistortionHazePSO";
        m_distortionHazePSO = device->CreateGraphicsPipeline(desc);
        if (!m_distortionHazePSO.IsValid()) {
            UZ_LOG(App, Error, "VFXViewport: Failed to create distortion haze PSO");
            m_distortionEnabled = false;
        }
    }

    // ── Distortion resolve PSO (fullscreen, replace blend) ──
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = shaders->CreateShader(EngineShader::FullscreenVS, RHIShaderStage::Vertex);
        desc.pixelShader  = shaders->CreateShader(EngineShader::DistortionResolvePS, RHIShaderStage::Pixel);
        desc.renderTargetCount = 1;
        desc.renderTargetFormats[0] = RHIFormat::RGBA16Float;
        desc.depthStencilFormat = RHIFormat::Unknown;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        desc.rasterizer.cullMode = RHICullMode::None;
        desc.debugName = "VFX_DistortionResolvePSO";
        m_distortionResolvePSO = device->CreateGraphicsPipeline(desc);
        if (!m_distortionResolvePSO.IsValid()) {
            UZ_LOG(App, Error, "VFXViewport: Failed to create distortion resolve PSO");
            m_distortionEnabled = false;
        }
    }

    // Flush upload staging buffers
    upload.ReleaseStaging();

    UZ_LOG(App, Info, "VFXViewport: Init complete — {} meshes, {} materials, VFX={}, Trails={}, Decals={}, Distortion={}",
           kMeshNames[0], kMaterialCount, m_vfxEnabled ? "on" : "off",
           m_trailEnabled ? "on" : "off", m_decalEnabled ? "on" : "off",
           m_distortionEnabled ? "on" : "off");

    SetReady();
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Handling
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::HandleInput(float dt) {
    const auto* input = GetInput();

    // Debug view mode (F1-F11)
    ApplyDebugViewKeyboard(input);

    // Material cycling (Left/Right arrows)
    if (input->IsKeyJustPressed(Key::Left)) {
        m_currentMaterial = (m_currentMaterial + kMaterialCount - 1) % kMaterialCount;
        UZ_LOG(App, Info, "Material: {}", kMaterials[m_currentMaterial].name);
    }
    if (input->IsKeyJustPressed(Key::Right)) {
        m_currentMaterial = (m_currentMaterial + 1) % kMaterialCount;
        UZ_LOG(App, Info, "Material: {}", kMaterials[m_currentMaterial].name);
    }

    // Mesh cycling (Up/Down arrows)
    if (input->IsKeyJustPressed(Key::Up)) {
        m_currentMesh = (m_currentMesh + kMeshCount - 1) % kMeshCount;
        UZ_LOG(App, Info, "Mesh: {}", kMeshNames[m_currentMesh]);
    }
    if (input->IsKeyJustPressed(Key::Down)) {
        m_currentMesh = (m_currentMesh + 1) % kMeshCount;
        UZ_LOG(App, Info, "Mesh: {}", kMeshNames[m_currentMesh]);
    }

    // Time-of-day (T / Shift+T)
    if (input->IsKeyJustPressed(Key::T)) {
        if (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift)) {
            m_sunElevation -= 5.0f;
        } else {
            m_sunElevation += 5.0f;
        }
        m_sunElevation = std::clamp(m_sunElevation, -10.0f, 90.0f);
        UZ_LOG(App, Info, "Sun elevation: {:.1f} deg", m_sunElevation);
    }

    // Shadow bias (O = decrease, P-adjacent = use Num7/Num8)
    if (input->IsKeyJustPressed(Key::Num7)) {
        m_shadowBias = std::max(0.0001f, m_shadowBias * 0.5f);
        UZ_LOG(App, Info, "Shadow bias: {:.4f}", m_shadowBias);
    }
    if (input->IsKeyJustPressed(Key::Num8)) {
        m_shadowBias = std::min(0.05f, m_shadowBias * 2.0f);
        UZ_LOG(App, Info, "Shadow bias: {:.4f}", m_shadowBias);
    }

    // Exposure (Num9 = increase, Num0 = decrease)
    if (input->IsKeyJustPressed(Key::Num9)) {
        m_exposure = std::min(10.0f, m_exposure * 1.2f);
        UZ_LOG(App, Info, "Exposure: {:.2f}", m_exposure);
    }
    if (input->IsKeyJustPressed(Key::Num0)) {
        m_exposure = std::max(0.1f, m_exposure / 1.2f);
        UZ_LOG(App, Info, "Exposure: {:.2f}", m_exposure);
    }

    // Vignette (V key)
    if (input->IsKeyJustPressed(Key::V)) {
        m_vignetteIntensity = (m_vignetteIntensity > 0.01f) ? 0.0f : 0.4f;
        UZ_LOG(App, Info, "Vignette: {}", m_vignetteIntensity > 0.01f ? "on" : "off");
    }

    // Post-processing toggle (P key)
    if (input->IsKeyJustPressed(Key::P)) {
        m_postProcessEnabled = !m_postProcessEnabled;
        UZ_LOG(App, Info, "Post-processing: {}", m_postProcessEnabled ? "ACES" : "linear");
    }

    // Shadow map debug overlay (G key)
    if (input->IsKeyJustPressed(Key::G)) {
        m_showShadowDebug = !m_showShadowDebug;
        UZ_LOG(App, Info, "Shadow debug: {}", m_showShadowDebug ? "on" : "off");
    }

    // Bloom toggle (B key)
    if (input->IsKeyJustPressed(Key::B)) {
        m_bloomEnabled = !m_bloomEnabled;
        UZ_LOG(App, Info, "Bloom: {}", m_bloomEnabled ? "on" : "off");
    }

    // VFX preset cycling (Tab / Shift+Tab)
    if (input->IsKeyJustPressed(Key::Tab)) {
        if (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift)) {
            // Toggle VFX on/off
            m_vfxEnabled = !m_vfxEnabled;
            UZ_LOG(App, Info, "VFX: {}", m_vfxEnabled ? "enabled" : "disabled");
        } else {
            // Cycle preset
            int presetCount = 0;
            const VFXPreset* presets = GetVFXPresets(presetCount);
            m_vfxPresetIndex = (m_vfxPresetIndex + 1) % presetCount;
            m_vfxEmitter = presets[m_vfxPresetIndex].config;
            m_vfxSpawnRate = presets[m_vfxPresetIndex].spawnRate;
            m_vfxSpawnAccum = 0.0f;
            UZ_LOG(App, Info, "VFX preset: {}", presets[m_vfxPresetIndex].name);
        }
    }

    // VFX parameter tweaking (number keys 1-9)
    if (input->IsKeyJustPressed(Key::Num1)) {
        m_vfxSpawnRate = std::min(5000.0f, m_vfxSpawnRate * 1.5f);
        UZ_LOG(App, Info, "Spawn rate: {:.0f}/s", m_vfxSpawnRate);
    }
    if (input->IsKeyJustPressed(Key::Num2)) {
        m_vfxSpawnRate = std::max(10.0f, m_vfxSpawnRate / 1.5f);
        UZ_LOG(App, Info, "Spawn rate: {:.0f}/s", m_vfxSpawnRate);
    }
    if (input->IsKeyJustPressed(Key::Num3)) {
        m_vfxEmitter.initialSize *= 1.5f;
        UZ_LOG(App, Info, "Particle size: {:.3f}", m_vfxEmitter.initialSize);
    }
    if (input->IsKeyJustPressed(Key::Num4)) {
        m_vfxEmitter.initialSize /= 1.5f;
        UZ_LOG(App, Info, "Particle size: {:.3f}", m_vfxEmitter.initialSize);
    }
    if (input->IsKeyJustPressed(Key::Num5)) {
        m_vfxEmitter.gravity[1] -= 2.0f;
        UZ_LOG(App, Info, "Gravity Y: {:.1f}", m_vfxEmitter.gravity[1]);
    }
    if (input->IsKeyJustPressed(Key::Num6)) {
        m_vfxEmitter.gravity[1] += 2.0f;
        UZ_LOG(App, Info, "Gravity Y: {:.1f}", m_vfxEmitter.gravity[1]);
    }

    // Trail toggle (Y key)
    if (input->IsKeyJustPressed(Key::Y)) {
        m_trailEnabled = !m_trailEnabled;
        UZ_LOG(App, Info, "Trails: {}", m_trailEnabled ? "enabled" : "disabled");
    }

    // Decal toggle (D key)
    if (input->IsKeyJustPressed(Key::D)) {
        m_decalEnabled = !m_decalEnabled;
        UZ_LOG(App, Info, "Decals: {}", m_decalEnabled ? "enabled" : "disabled");
    }

    // Distortion toggle (X key)
    if (input->IsKeyJustPressed(Key::X)) {
        m_distortionEnabled = !m_distortionEnabled;
        UZ_LOG(App, Info, "Distortion: {}", m_distortionEnabled ? "enabled" : "disabled");
    }

    // Point lights toggle (L key)
    if (input->IsKeyJustPressed(Key::L)) {
        m_pointLightsEnabled = !m_pointLightsEnabled;
        UZ_LOG(App, Info, "Point lights: {}", m_pointLightsEnabled ? "enabled" : "disabled");
    }

    // Environment rotation (R key — 15 deg step, Shift+R = reverse)
    if (input->IsKeyJustPressed(Key::R)) {
        if (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift)) {
            m_envRotation -= 15.0f;
        } else {
            m_envRotation += 15.0f;
        }
        // Wrap to [0, 360)
        m_envRotation = std::fmod(m_envRotation + 360.0f, 360.0f);
        UZ_LOG(App, Info, "Environment rotation: {:.0f} deg", m_envRotation);
    }

    // Object rotation (Alt + LMB drag)
    if (input->IsKeyDown(Key::LeftAlt) && input->IsMouseDown(MouseButton::Left)) {
        constexpr float kRotSensitivity = 0.3f;
        m_objectYaw   += input->GetMouseDeltaX() * kRotSensitivity;
        m_objectPitch += input->GetMouseDeltaY() * kRotSensitivity;
        m_objectPitch = std::clamp(m_objectPitch, -89.0f, 89.0f);
    }

    // Background preset cycling (C key)
    if (input->IsKeyJustPressed(Key::C)) {
        m_bgPreset = (m_bgPreset + 1) % kBgPresetCount;
        static const char* kBgNames[] = {"Sky", "Dark Gray", "Black", "White", "Neutral Gray"};
        UZ_LOG(App, Info, "Background: {}", kBgNames[m_bgPreset]);
    }

    // Help overlay (H key) — prints keyboard shortcut reference to console
    if (input->IsKeyJustPressed(Key::H)) {
        UZ_LOG(App, Info, "=== VFX Viewport Controls ===");
        UZ_LOG(App, Info, "  WASD / Space / Ctrl  — Camera movement (Shift = sprint)");
        UZ_LOG(App, Info, "  RMB + Mouse          — Mouse look (yaw/pitch)");
        UZ_LOG(App, Info, "  MMB + Mouse          — Orbit camera");
        UZ_LOG(App, Info, "  Scroll               — Dolly zoom");
        UZ_LOG(App, Info, "  Alt + LMB + Mouse    — Rotate object");
        UZ_LOG(App, Info, "  Left/Right Arrows    — Cycle material presets");
        UZ_LOG(App, Info, "  Up/Down Arrows       — Cycle mesh types");
        UZ_LOG(App, Info, "  T / Shift+T          — Sun elevation +/- 5 deg");
        UZ_LOG(App, Info, "  R / Shift+R          — Environment rotation +/- 15 deg");
        UZ_LOG(App, Info, "  L                    — Toggle point lights");
        UZ_LOG(App, Info, "  B                    — Toggle bloom");
        UZ_LOG(App, Info, "  C                    — Cycle background (Sky/Gray/Black/White)");
        UZ_LOG(App, Info, "  V                    — Toggle vignette");
        UZ_LOG(App, Info, "  P                    — Toggle post-processing (ACES/linear)");
        UZ_LOG(App, Info, "  G                    — Toggle shadow debug overlay");
        UZ_LOG(App, Info, "  Tab                  — Cycle VFX presets (Shift+Tab = toggle)");
        UZ_LOG(App, Info, "  Y                    — Toggle trails");
        UZ_LOG(App, Info, "  D                    — Toggle decals");
        UZ_LOG(App, Info, "  X                    — Toggle distortion");
        UZ_LOG(App, Info, "  1-6                  — VFX param tweaks (spawn/size/gravity)");
        UZ_LOG(App, Info, "  7/8                  — Shadow bias decrease/increase");
        UZ_LOG(App, Info, "  9/0                  — Exposure increase/decrease");
        UZ_LOG(App, Info, "  F1-F8                — Debug view modes");
        UZ_LOG(App, Info, "  F5                   — Screenshot");
        UZ_LOG(App, Info, "  Alt+Enter            — Toggle fullscreen");
        UZ_LOG(App, Info, "  H                    — Show this help");
    }

    // Fullscreen toggle (Alt+Enter)
    if (input->IsKeyJustPressed(Key::Enter) &&
        (input->IsKeyDown(Key::LeftAlt) || input->IsKeyDown(Key::RightAlt))) {
        auto* window = GetWindow();
        if (window) {
            if (window->IsFullscreen()) {
                window->SetWindowed();
                UZ_LOG(App, Info, "Windowed mode");
            } else {
                window->SetFullscreen();
                UZ_LOG(App, Info, "Borderless fullscreen");
            }
        }
    }

    // RenderDoc frame capture (F11)
    if (input->IsKeyJustPressed(Key::F11)) {
        if (RenderDocIsLoaded()) {
            RenderDocTriggerCapture();
            UZ_LOG(App, Info, "RenderDoc: capture triggered");
        } else {
            UZ_LOG(App, Warn, "RenderDoc: not loaded (install RenderDoc and relaunch)");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::OnFrame(float dt) {
    auto* cmd = GetCommandList();
    auto* cbRing = GetCBRing();

    m_totalTime += dt;
    HandleInput(dt);

    // Begin CB ring frame
    cbRing->BeginFrame(GetFrameIndex());

    // ── Compute sun direction from elevation + environment rotation ──────
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    const float elevRad = m_sunElevation * kDegToRad;
    const float azimRad = m_envRotation * kDegToRad;
    // Base sun direction from elevation, then rotate around Y by envRotation
    const float cosElev = std::cos(elevRad);
    const Vec3 sunDir = Vec3(
        cosElev * std::sin(azimRad),
        std::sin(elevRad),
        cosElev * std::cos(azimRad)
    ).Normalized();

    // ── Fill PerFrameCB ──────────────────────────────────────────────────
    const Camera& cam = GetCamera();
    const Mat4 viewProj = cam.GetViewProjection();
    const Mat4 invViewProj = viewProj.Inverse();

    PerFrameCB perFrame{};
    perFrame.viewProj = viewProj;
    perFrame.cameraPos = Vec4(cam.GetPosition(), 0.0f);
    perFrame.timeParams = Vec4(m_totalTime, dt, static_cast<float>(GetFrameIndex()), 0.0f);
    perFrame.lightDir = Vec4(sunDir, 0.0f);
    perFrame.lightColor = Vec4(1.0f, 0.95f, 0.85f, 0.0f);
    perFrame.ambientColor = Vec4(0.1f, 0.12f, 0.15f, 0.0f);
    perFrame.invViewProj = invViewProj;
    perFrame.nearPlane = 10.0f;
    perFrame.farPlane = 100000.0f;
    perFrame.exposure = m_exposure;

    // ── Point lights (3 colored lights around the scene) ──────────────
    if (m_pointLightsEnabled) {
        perFrame.numPointLights = 3;
        // Red light — right side
        perFrame.pointLight0PosX      = 4.0f;
        perFrame.pointLight0PosY      = 2.0f;
        perFrame.pointLight0PosZ      = 0.0f;
        perFrame.pointLight0Radius    = 800.0f;
        perFrame.pointLight0ColorR    = 1.0f;
        perFrame.pointLight0ColorG    = 0.2f;
        perFrame.pointLight0ColorB    = 0.1f;
        perFrame.pointLight0Intensity = 3.0f;
        // Blue light — left-back
        perFrame.pointLight1PosX      = -3.0f;
        perFrame.pointLight1PosY      = 3.0f;
        perFrame.pointLight1PosZ      = -3.0f;
        perFrame.pointLight1Radius    = 800.0f;
        perFrame.pointLight1ColorR    = 0.1f;
        perFrame.pointLight1ColorG    = 0.3f;
        perFrame.pointLight1ColorB    = 1.0f;
        perFrame.pointLight1Intensity = 3.0f;
        // Green light — front
        perFrame.pointLight2PosX      = 0.0f;
        perFrame.pointLight2PosY      = 1.5f;
        perFrame.pointLight2PosZ      = 4.0f;
        perFrame.pointLight2Radius    = 800.0f;
        perFrame.pointLight2ColorR    = 0.2f;
        perFrame.pointLight2ColorG    = 1.0f;
        perFrame.pointLight2ColorB    = 0.2f;
        perFrame.pointLight2Intensity = 2.0f;
    }

    auto perFrameAlloc = cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));
    const uint64_t perFrameGpu = perFrameAlloc.gpuAddr;

    // ── VFX compute dispatch ─────────────────────────────────────────────
    if (m_vfxEnabled) {
        // Calculate spawn count from rate and accumulator
        m_vfxSpawnAccum += m_vfxSpawnRate * dt;
        const auto spawnCount = static_cast<uint32_t>(m_vfxSpawnAccum);
        m_vfxSpawnAccum -= static_cast<float>(spawnCount);

        m_vfxSystem.DispatchCompute(cmd, cbRing, m_vfxEmitter, spawnCount,
                                    dt, GetFrameIndex());
    }

    // ── Shadow pass ──────────────────────────────────────────────────────
    RenderShadowPass(cmd);

    // ── HDR scene rendering ──────────────────────────────────────────────
    {
        // Transition HDR target to render target state
        RHIBarrierDesc barrier{};
        barrier.texture = m_hdrTarget;
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Sky pass (fullscreen into HDR target) ────────────────────────────
    RenderSkyPass(cmd, perFrameGpu);

    // ── Scene pass (PBR mesh into HDR target with depth) ─────────────────
    RenderScenePass(cmd, perFrameGpu);

    // ── Copy HDR for grid ground reflection ────────────────────────────
    // The grid shader uses the pre-grid HDR content for screen-space
    // ground reflection. Copy before the grid render pass begins.
    uint32_t reflectionSrv = UINT32_MAX;
    {
        RHIBarrierDesc barriers[2]{};
        barriers[0].texture = m_hdrTarget;
        barriers[0].stateBefore = RHIResourceState::RenderTarget;
        barriers[0].stateAfter = RHIResourceState::CopySrc;
        barriers[1].texture = m_preDistortionCopy;
        barriers[1].stateBefore = RHIResourceState::Common;
        barriers[1].stateAfter = RHIResourceState::CopyDst;
        cmd->ResourceBarrier(barriers, 2);

        cmd->CopyTexture(m_hdrTarget, m_preDistortionCopy);

        RHIBarrierDesc barriers2[2]{};
        barriers2[0].texture = m_preDistortionCopy;
        barriers2[0].stateBefore = RHIResourceState::CopyDst;
        barriers2[0].stateAfter = RHIResourceState::Common;
        barriers2[1].texture = m_hdrTarget;
        barriers2[1].stateBefore = RHIResourceState::CopySrc;
        barriers2[1].stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(barriers2, 2);

        reflectionSrv = GetDevice()->GetTextureSRVIndex(m_preDistortionCopy);
    }

    // ── Editor grid ──────────────────────────────────────────────────────
    {
        auto* grid = GetEditorGrid();
        if (grid) {
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture = m_hdrTarget;
            rpDesc.hasDepth = true;
            rpDesc.depthAttachment.texture = m_depthBuffer;
            cmd->BeginRenderPass(rpDesc);

            RHIViewport vp{};
            vp.width = static_cast<float>(GetWidth());
            vp.height = static_cast<float>(GetHeight());
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right = static_cast<int32_t>(GetWidth());
            sc.bottom = static_cast<int32_t>(GetHeight());
            cmd->SetScissor(sc);

            // EditorGrid::Draw(cmdList, cbRing, camera, depthSrv, width, height, reflectionHdrSrv)
            uint32_t depthSrv = GetDevice()->GetTextureSRVIndex(m_depthBuffer);
            grid->Draw(cmd, *cbRing, cam, depthSrv,
                       static_cast<float>(GetWidth()),
                       static_cast<float>(GetHeight()),
                       reflectionSrv);

            cmd->EndRenderPass();
        }
    }

    // ── Decal projection pass ───────────────────────────────────────────
    // Decals read depth as SRV (bindless) and write to HDR target.
    // Depth must be transitioned to Common (SRV-readable) before decals,
    // then back to DepthStencilWrite for subsequent VFX/trail passes.
    if (m_decalEnabled && m_activeDecalCount > 0 && m_decalPSO.IsValid()) {
        // Transition depth to SRV for decal projection (depth read in PS)
        {
            RHIBarrierDesc barrier{};
            barrier.texture = m_depthBuffer;
            barrier.stateBefore = RHIResourceState::DepthStencilWrite;
            barrier.stateAfter = RHIResourceState::Common;
            cmd->ResourceBarrier(&barrier, 1);
        }

        RenderDecalPass(cmd, perFrameGpu);

        // Transition depth back to DepthStencilWrite for VFX/trails
        {
            RHIBarrierDesc barrier{};
            barrier.texture = m_depthBuffer;
            barrier.stateBefore = RHIResourceState::Common;
            barrier.stateAfter = RHIResourceState::DepthStencilWrite;
            cmd->ResourceBarrier(&barrier, 1);
        }
    }

    // ── VFX billboard rendering ──────────────────────────────────────────
    if (m_vfxEnabled) {
        RenderVFXPass(cmd, perFrameGpu, dt);
        m_vfxSystem.FlipPingPong();
    }

    // ── Trail ribbon update + rendering ──────────────────────────────────
    if (m_trailEnabled && m_trailHandle != TrailSystem::kInvalidHandle) {
        // Compute an orbiting trail source position around the particle emitter
        constexpr float kOrbitRadius = 1.5f;
        constexpr float kOrbitSpeed  = 2.0f;
        const Vec3 trailPos(
            m_vfxEmitter.position[0] + kOrbitRadius * std::cos(m_totalTime * kOrbitSpeed),
            m_vfxEmitter.position[1] + 0.5f,
            m_vfxEmitter.position[2] + kOrbitRadius * std::sin(m_totalTime * kOrbitSpeed)
        );

        // Update trail system (aging, point emission, mesh generation)
        std::unordered_map<TrailSystem::TrailHandle, Vec3> trailPositions;
        trailPositions[m_trailHandle] = trailPos;
        m_trailSystem.Update(dt, trailPositions, cam.GetPosition());

        // Render trail ribbons into HDR target
        RenderTrailPass(cmd, perFrameGpu);
    }

    // ── Distortion pass (if enabled) ─────────────────────────────────────
    // Two-pass approach:
    //   1. Accumulate procedural heat-haze UV offsets into m_distortionRT
    //   2. Copy HDR → m_preDistortionCopy
    //   3. Resolve: read preDistortionCopy + distortionRT → write warped color to HDR
    if (m_distortionEnabled && m_distortionHazePSO.IsValid() && m_distortionResolvePSO.IsValid()) {
        RenderDistortionPass(cmd, perFrameGpu);
    }

    // ── Transition HDR target to SRV for tonemap ─────────────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_hdrTarget;
        barrier.stateBefore = RHIResourceState::RenderTarget;
        barrier.stateAfter = RHIResourceState::Common;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Bloom compute pass (HDR must be in SRV state) ────────────────────
    if (m_bloomEnabled && m_bloomPass.IsInitialized()) {
        uint32_t hdrSrvIdx = GetDevice()->GetTextureSRVIndex(m_hdrTarget);
        m_bloomPass.Execute(cmd, hdrSrvIdx, GetWidth(), GetHeight(), m_exposure);
    }

    // ── Tonemap pass (HDR → backbuffer) ──────────────────────────────────
    RenderTonemapPass(cmd, perFrameGpu);

    // ── Stats overlay ────────────────────────────────────────────────────
    {
        auto* stats = GetStatsOverlay();
        auto* swapChain = GetSwapChain();
        if (stats && swapChain) {
            stats->BeginFrame(dt);

            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture = swapChain->GetCurrentBackBuffer();
            cmd->BeginRenderPass(rpDesc);

            RHIViewport vp{};
            vp.width = static_cast<float>(GetWidth());
            vp.height = static_cast<float>(GetHeight());
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right = static_cast<int32_t>(GetWidth());
            sc.bottom = static_cast<int32_t>(GetHeight());
            cmd->SetScissor(sc);

            stats->Render(cmd, *cbRing, GetFrameIndex());

            cmd->EndRenderPass();
        }
    }

    // ── Shadow map debug overlay ─────────────────────────────────────────
    // Renders the shadow depth map as a small grayscale quad in the
    // bottom-left corner, reusing the tonemap PSO (which samples a
    // texture via bindless SRV index and draws a fullscreen triangle).
    // The viewport/scissor restrict drawing to a 256x256 region.
    if (m_showShadowDebug && m_tonemapPSO.IsValid() && m_shadowMap.IsValid()) {
        auto* swapChain = GetSwapChain();
        auto backBuffer = swapChain->GetCurrentBackBuffer();

        RHIRenderPassDesc rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments[0].texture = backBuffer;
        cmd->BeginRenderPass(rpDesc);

        constexpr uint32_t kDebugOverlaySize = 256;
        RHIViewport debugVp{};
        debugVp.x = 0.0f;
        debugVp.y = static_cast<float>(GetHeight() - kDebugOverlaySize);
        debugVp.width = static_cast<float>(kDebugOverlaySize);
        debugVp.height = static_cast<float>(kDebugOverlaySize);
        debugVp.minDepth = 0.0f;
        debugVp.maxDepth = 1.0f;
        cmd->SetViewport(debugVp);

        RHIScissorRect debugSc{};
        debugSc.left = 0;
        debugSc.top = static_cast<int32_t>(GetHeight() - kDebugOverlaySize);
        debugSc.right = static_cast<int32_t>(kDebugOverlaySize);
        debugSc.bottom = static_cast<int32_t>(GetHeight());
        cmd->SetScissor(debugSc);

        cmd->SetPipeline(m_tonemapPSO);
        cmd->SetRootCBV(0, perFrameGpu);

        if (IsBindless()) {
            auto* device = GetDevice();
            uint32_t shadowSrvIdx = device->GetTextureSRVIndex(m_shadowMap);
            cmd->SetRootConstants(1, &shadowSrvIdx, 1);
        }

        cmd->Draw(3, 1, 0, 0);
        cmd->EndRenderPass();
    }

    // ── Screenshot capture ───────────────────────────────────────────────
    if (ShouldCaptureScreenshot()) {
        auto* capture = GetScreenshotCapture();
        auto* swapChain = GetSwapChain();
        if (capture && swapChain) {
            capture->CaptureBackBuffer(cmd, swapChain);
            SetScreenshotSaveDeferred();
        }
    }

    // Store view-projection for next frame's motion vectors
    m_prevViewProj = viewProj;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderShadowPass(IRHICommandList* cmd) {
    auto* pipelines = GetPipelines();
    auto* cbRing = GetCBRing();

    // Compute light view-projection (orthographic from sun direction)
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    const float elevRad = m_sunElevation * kDegToRad;
    const Vec3 sunDir = Vec3(
        std::cos(elevRad) * 0.5f,
        std::sin(elevRad),
        std::cos(elevRad) * 0.866f
    ).Normalized();

    const Vec3 lightPos = sunDir * 20.0f;
    const Vec3 lightTarget = Vec3(0.0f, 0.0f, 0.0f);

    // Orthographic shadow projection covering the scene
    const Mat4 lightView = Mat4::LookAt(lightPos, lightTarget, Vec3::Up());
    // Shadow ortho projection: 30x30 world units centered on the scene
    const Mat4 lightProj = Mat4::Orthographic(30.0f, 30.0f, 0.1f, 50.0f);
    const Mat4 shadowViewProj = lightProj * lightView;

    // Transition shadow map to depth-write
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_shadowMap;
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter = RHIResourceState::DepthStencilWrite;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // Begin shadow render pass
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 0;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_shadowMap;
    rpDesc.depthAttachment.clear = true;
    rpDesc.depthAttachment.clearDepth = 1.0f; // Shadow uses standard Z (not reverse-Z)
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(kShadowMapSize);
    vp.height = static_cast<float>(kShadowMapSize);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(kShadowMapSize);
    sc.bottom = static_cast<int32_t>(kShadowMapSize);
    cmd->SetScissor(sc);

    // Bind shadow depth PSO
    auto shadowPSO = pipelines->GetShadowDepth();
    if (shadowPSO.IsValid()) {
        cmd->SetPipeline(shadowPSO);

        // Upload shadow view-projection as a root CBV
        struct ShadowCB { Mat4 viewProj; };
        ShadowCB shadowCB{};
        shadowCB.viewProj = shadowViewProj;
        auto shadowAlloc = cbRing->Allocate(sizeof(ShadowCB));
        std::memcpy(shadowAlloc.cpuPtr, &shadowCB, sizeof(ShadowCB));
        cmd->SetRootCBV(0, shadowAlloc.gpuAddr);

        // ── Helper: upload PerObjectCB with a given world matrix for the shadow pass ──
        // The shadow depth VS reads PerObjectCB.world (register b2) to transform
        // vertices into object space before multiplying by shadowViewProj.
        auto uploadShadowPerObject = [&](const Mat4& worldMatrix) {
            PerObjectCB shadowObj{};
            shadowObj.model = worldMatrix;
            shadowObj.normalMatrix = Mat4::Identity(); // Unused in shadow pass
            auto alloc = cbRing->Allocate(sizeof(PerObjectCB));
            std::memcpy(alloc.cpuPtr, &shadowObj, sizeof(PerObjectCB));
            cmd->SetRootCBV(kSlotPerObject, alloc.gpuAddr);
        };

        // Draw current mesh (at world origin)
        uploadShadowPerObject(Mat4::Identity());
        const auto& mesh = m_meshes[m_currentMesh];
        if (mesh.vertexBuffer.IsValid() && mesh.indexBuffer.IsValid()) {
            cmd->SetVertexBuffer(0, mesh.vertexBuffer);
            cmd->SetIndexBuffer(mesh.indexBuffer, RHIIndexFormat::Uint32);
            cmd->DrawIndexed(mesh.indexCount);
        }

        // ── Draw 3 reference spheres into shadow map ──
        // Positions must match RenderScenePass so shadows align with geometry.
        constexpr float kRefPositionX[] = { -3.0f, 0.0f, 3.0f };
        constexpr float kRefPositionZ   = -3.0f;

        const auto& sphereMesh = m_meshes[0];
        if (sphereMesh.vertexBuffer.IsValid() && sphereMesh.indexBuffer.IsValid()) {
            cmd->SetVertexBuffer(0, sphereMesh.vertexBuffer);
            cmd->SetIndexBuffer(sphereMesh.indexBuffer, RHIIndexFormat::Uint32);

            for (int i = 0; i < 3; ++i) {
                uploadShadowPerObject(
                    Mat4::Translation(Vec3(kRefPositionX[i], 0.0f, kRefPositionZ)));
                cmd->DrawIndexed(sphereMesh.indexCount);
            }
        }
    }

    cmd->EndRenderPass();

    // Transition shadow map to SRV
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_shadowMap;
        barrier.stateBefore = RHIResourceState::DepthStencilWrite;
        barrier.stateAfter = RHIResourceState::Common;
        cmd->ResourceBarrier(&barrier, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderSkyPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    // Background preset clear colors (linear RGB)
    static constexpr float kBgColors[][3] = {
        {0.0f,  0.0f,  0.0f },   // 0: Sky (clear black, sky shader overwrites)
        {0.05f, 0.05f, 0.05f},   // 1: Dark Gray
        {0.0f,  0.0f,  0.0f },   // 2: Black
        {1.0f,  1.0f,  1.0f },   // 3: White
        {0.18f, 0.18f, 0.18f},   // 4: Neutral Gray (18% reflectance)
    };

    const int bg = std::clamp(m_bgPreset, 0, kBgPresetCount - 1);

    // The sky writes to HDR target with depth output (reverse-Z far plane = 0.0)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.colorAttachments[0].clear = true;
    rpDesc.colorAttachments[0].clearColor[0] = kBgColors[bg][0];
    rpDesc.colorAttachments[0].clearColor[1] = kBgColors[bg][1];
    rpDesc.colorAttachments[0].clearColor[2] = kBgColors[bg][2];
    rpDesc.colorAttachments[0].clearColor[3] = 1.0f;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_depthBuffer;
    rpDesc.depthAttachment.clear = true;
    // Reverse-Z: clear to 0.0 (far plane)
    rpDesc.depthAttachment.clearDepth = 0.0f;
    cmd->BeginRenderPass(rpDesc);

    // Skip sky rendering for solid-color backgrounds
    if (bg != 0 || !m_skyPSO.IsValid()) {
        cmd->EndRenderPass();
        return;
    }

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    cmd->SetPipeline(m_skyPSO);
    cmd->SetRootCBV(0, perFrameGpu);

    // Fullscreen triangle (3 vertices, no vertex buffer)
    cmd->Draw(3, 1, 0, 0);

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene Pass (Forward PBR mesh)
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderScenePass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    auto forwardPBR = GetPipelines()->GetForwardPBR();
    if (!forwardPBR.IsValid()) return;

    auto* cbRing = GetCBRing();

    // Render into HDR target with depth (don't clear — sky already wrote)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_depthBuffer;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    cmd->SetPipeline(forwardPBR);

    // Slot 1 = PerFrameCB (b1) per the engine's standard root signature layout
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

    // ── Fill PerObjectCB with material preset and loaded texture SRV indices ──
    const auto& mat = kMaterials[m_currentMaterial];

    PerObjectCB perObj{};
    // Texture SRV indices — 0 maps to the engine's default white/flat-normal/0.5 textures
    perObj.albedoIndex    = (m_albedoSrvIndex    != UINT32_MAX) ? m_albedoSrvIndex    : 0;
    perObj.normalIndex    = (m_normalSrvIndex    != UINT32_MAX) ? m_normalSrvIndex    : 0;
    perObj.roughnessIndex = (m_roughnessSrvIndex != UINT32_MAX) ? m_roughnessSrvIndex : 0;
    perObj.metallicIndex  = 0;
    perObj.aoIndex         = 0;
    perObj.emissiveIndex   = 0;

    // Object transform — mesh at world origin with optional manual rotation (Alt+LMB)
    constexpr float kDeg2Rad = 3.14159265f / 180.0f;
    const Mat4 rotY = Mat4::RotationY(m_objectYaw * kDeg2Rad);
    const Mat4 rotX = Mat4::RotationX(m_objectPitch * kDeg2Rad);
    const Mat4 objRotation = rotX * rotY;
    perObj.model        = objRotation;
    perObj.normalMatrix = objRotation; // Rotation-only — no non-uniform scale, so normal matrix = model

    // Material color tints from the active preset
    perObj.baseColorTintR      = mat.baseColor[0];
    perObj.baseColorTintG      = mat.baseColor[1];
    perObj.baseColorTintB      = mat.baseColor[2];
    perObj.roughnessMultiplier = mat.roughness;
    perObj.metallicMultiplier  = mat.metallic;
    perObj.emissiveColorR      = mat.emissiveColor[0];
    perObj.emissiveColorG      = mat.emissiveColor[1];
    perObj.emissiveColorB      = mat.emissiveColor[2];
    perObj.emissiveIntensity   = mat.emissiveIntensity;

    auto objAlloc = cbRing->Allocate(sizeof(PerObjectCB));
    std::memcpy(objAlloc.cpuPtr, &perObj, sizeof(PerObjectCB));

    // Slot 2 = PerObjectCB (b2)
    cmd->SetRootCBV(kSlotPerObject, objAlloc.gpuAddr);

    // Draw the current mesh
    const auto& mesh = m_meshes[m_currentMesh];
    if (mesh.vertexBuffer.IsValid() && mesh.indexBuffer.IsValid()) {
        cmd->SetVertexBuffer(0, mesh.vertexBuffer);
        cmd->SetIndexBuffer(mesh.indexBuffer, RHIIndexFormat::Uint32);
        cmd->DrawIndexed(mesh.indexCount);
    }

    // ── Draw 3 reference spheres with metallic sweep ──
    // Dielectric (0.0) → half-metal (0.5) → full metal (1.0), all at roughness 0.3
    // and white base color so the metallic effect is clearly visible.
    constexpr float kRefMetallic[]  = { 0.0f, 0.5f, 1.0f };
    constexpr float kRefPositionX[] = { -3.0f, 0.0f, 3.0f };
    constexpr float kRefPositionZ   = -3.0f;

    const auto& sphereMesh = m_meshes[0]; // index 0 = sphere (first generated in OnInit)
    if (sphereMesh.vertexBuffer.IsValid() && sphereMesh.indexBuffer.IsValid()) {
        cmd->SetVertexBuffer(0, sphereMesh.vertexBuffer);
        cmd->SetIndexBuffer(sphereMesh.indexBuffer, RHIIndexFormat::Uint32);

        for (int i = 0; i < 3; ++i) {
            PerObjectCB refObj{};
            refObj.albedoIndex    = 0;
            refObj.normalIndex    = 0;
            refObj.roughnessIndex = 0;
            refObj.metallicIndex  = 0;
            refObj.aoIndex        = 0;
            refObj.emissiveIndex  = 0;

            refObj.model        = Mat4::Translation(Vec3(kRefPositionX[i], 0.0f, kRefPositionZ));
            refObj.normalMatrix = Mat4::Identity(); // Translation-only — normals unaffected

            refObj.baseColorTintR      = 1.0f;
            refObj.baseColorTintG      = 1.0f;
            refObj.baseColorTintB      = 1.0f;
            refObj.roughnessMultiplier = 0.3f;
            refObj.metallicMultiplier  = kRefMetallic[i];

            auto refAlloc = cbRing->Allocate(sizeof(PerObjectCB));
            std::memcpy(refAlloc.cpuPtr, &refObj, sizeof(PerObjectCB));
            cmd->SetRootCBV(kSlotPerObject, refAlloc.gpuAddr);

            cmd->DrawIndexed(sphereMesh.indexCount);
        }
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// VFX Billboard Rendering
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderVFXPass(IRHICommandList* cmd, uint64_t perFrameGpu,
                                      float /*dt*/) {
    auto billboardPSO = m_vfxSystem.GetBillboardPSO();
    if (!billboardPSO.IsValid()) return;

    // Render into HDR target with depth read (no write — particles are translucent)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_depthBuffer;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    const Vec3 emitterPos(m_vfxEmitter.position[0],
                           m_vfxEmitter.position[1],
                           m_vfxEmitter.position[2]);

    const uint32_t depthSrvIdx = GetDevice()->GetTextureSRVIndex(m_depthBuffer);

    m_vfxSystem.DrawBillboards(cmd, GetCBRing(), perFrameGpu,
                                GetCamera().GetView(), emitterPos,
                                m_prevViewProj,
                                m_vfxEmitter,
                                depthSrvIdx);

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Trail Ribbon Rendering
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderTrailPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    if (!m_trailPSO.IsValid()) return;
    if (!m_trailVB.IsValid() || !m_trailIB.IsValid()) return;

    auto renderData = m_trailSystem.GetRenderData();
    if (renderData.empty()) return;

    // Begin render pass: HDR target + depth read (no write)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_depthBuffer;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    cmd->SetPipeline(m_trailPSO);

    // PerFrameCB at b1 (root CBV slot 1)
    cmd->SetRootCBV(1, perFrameGpu);

    auto* device = GetDevice();
    const uint32_t depthSrvIdx = device->GetTextureSRVIndex(m_depthBuffer);

    for (const auto& trail : renderData) {
        if (trail.vertexCount == 0 || trail.vertexCount > kMaxTrailVertices) continue;
        if (trail.indexCount == 0 || trail.indexCount > kMaxTrailIndices) continue;

        // Upload vertices to the dynamic upload VB
        void* vbPtr = device->MapBuffer(m_trailVB);
        std::memcpy(vbPtr, trail.vertices, trail.vertexCount * sizeof(TrailVertex));
        device->UnmapBuffer(m_trailVB);

        // Upload indices to the dynamic upload IB
        void* ibPtr = device->MapBuffer(m_trailIB);
        std::memcpy(ibPtr, trail.indices, trail.indexCount * sizeof(uint32_t));
        device->UnmapBuffer(m_trailIB);

        // Set trail root constants at b0 (root param 0)
        TrailRootConstants rootConsts{};
        rootConsts.uvScrollRate       = 1.0f;
        rootConsts.softDepthRange     = 0.3f;
        rootConsts.alphaClipThreshold = 1.0f / 255.0f; // kTrailAlphaClipMin
        rootConsts.trailTexSrvIndex   = UINT32_MAX;    // No trail texture — vertex color only
        rootConsts.depthBufferSrvIndex = depthSrvIdx;
        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rootConsts),
                              sizeof(rootConsts) / sizeof(uint32_t));

        cmd->SetVertexBuffer(0, m_trailVB);
        cmd->SetIndexBuffer(m_trailIB, RHIIndexFormat::Uint32);
        cmd->DrawIndexed(trail.indexCount);
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Decal Projection Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderDecalPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    if (!m_decalPSO.IsValid()) return;

    auto* cbRing = GetCBRing();
    auto* device = GetDevice();

    // Render into HDR target with NO depth attachment (depth read as bindless SRV in PS)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.hasDepth = false;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    cmd->SetPipeline(m_decalPSO);

    // PerFrameCB at b1 (root CBV slot 1)
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

    const uint32_t depthSrvIdx = device->GetTextureSRVIndex(m_depthBuffer);

    for (uint32_t i = 0; i < m_activeDecalCount; ++i) {
        const auto& decal = m_decals[i];

        // Build decal world matrix: translation + scale by half-extents.
        // The VS cube is [-0.5, 0.5], so scale by 2*halfExtents for the full OBB.
        const Mat4 decalScale = Mat4::Scaling(decal.halfExtents * 2.0f);
        const Mat4 decalTranslation = Mat4::Translation(decal.position);
        const Mat4 decalWorld = decalScale * decalTranslation;
        const Mat4 decalInvWorld = decalWorld.Inverse();

        // Fill DecalObjectCB
        DecalObjectCB decalCB{};
        decalCB.decalWorld = decalWorld;
        decalCB.decalInvWorld = decalInvWorld;
        decalCB.decalOpacity = decal.opacity;
        decalCB.decalFadeDistance = 10.0f;   // 10 cm edge fade
        decalCB.decalDepthSrv = depthSrvIdx;
        // No textures — PS will use constant white (decalAlbedoSrv = UINT32_MAX)

        auto decalAlloc = cbRing->Allocate(sizeof(DecalObjectCB));
        std::memcpy(decalAlloc.cpuPtr, &decalCB, sizeof(DecalObjectCB));

        // DecalObjectCB at b2 (root CBV slot 2 = kSlotPerObject)
        cmd->SetRootCBV(kSlotPerObject, decalAlloc.gpuAddr);

        // Draw unit cube (36 vertices via SV_VertexID, no vertex buffer)
        cmd->Draw(36, 1, 0, 0);
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Distortion Pass (Heat-Haze Accumulation + HDR Warp Resolve)
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderDistortionPass(IRHICommandList* cmd, uint64_t /*perFrameGpu*/) {
    auto* device = GetDevice();

    // ── Step 1: Compute emitter screen UV ─────────────────────────────────
    // Project emitter world position through viewProj to get screen UV.
    const Camera& cam = GetCamera();
    const Mat4 viewProj = cam.GetViewProjection();
    const Vec3 emitterWorld(m_vfxEmitter.position[0],
                            m_vfxEmitter.position[1],
                            m_vfxEmitter.position[2]);
    const Vec4 emitterClip = viewProj * Vec4(emitterWorld, 1.0f);

    // Perspective divide + NDC to UV
    float emitterScreenU = 0.5f;
    float emitterScreenV = 0.5f;
    if (emitterClip.w > 0.001f) {
        const float ndcX = emitterClip.x / emitterClip.w;
        const float ndcY = emitterClip.y / emitterClip.w;
        emitterScreenU = ndcX * 0.5f + 0.5f;
        emitterScreenV = 1.0f - (ndcY * 0.5f + 0.5f); // Flip Y for D3D UV convention
    }

    // ── Step 2: Accumulate distortion into m_distortionRT ──────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_distortionRT;
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(&barrier, 1);
    }

    {
        RHIRenderPassDesc rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments[0].texture = m_distortionRT;
        rpDesc.colorAttachments[0].clear = true;
        rpDesc.colorAttachments[0].clearColor[0] = 0.0f;
        rpDesc.colorAttachments[0].clearColor[1] = 0.0f;
        rpDesc.colorAttachments[0].clearColor[2] = 0.0f;
        rpDesc.colorAttachments[0].clearColor[3] = 0.0f;
        rpDesc.hasDepth = false;
        cmd->BeginRenderPass(rpDesc);

        RHIViewport vp{};
        vp.width = static_cast<float>(GetWidth());
        vp.height = static_cast<float>(GetHeight());
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        cmd->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = static_cast<int32_t>(GetWidth());
        sc.bottom = static_cast<int32_t>(GetHeight());
        cmd->SetScissor(sc);

        cmd->SetPipeline(m_distortionHazePSO);

        // Root constants at b0: emitter screen UV, strength, time, radius, frequency
        DistortionHazeConstants hazeConsts{};
        hazeConsts.emitterScreenUV[0] = emitterScreenU;
        hazeConsts.emitterScreenUV[1] = emitterScreenV;
        hazeConsts.strength = m_distortionStrength;
        hazeConsts.time = m_totalTime;
        hazeConsts.radius = 0.25f;     // 25% of screen
        hazeConsts.frequency = 30.0f;  // Wave frequency
        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&hazeConsts),
                              sizeof(hazeConsts) / sizeof(uint32_t));

        // Fullscreen triangle
        cmd->Draw(3, 1, 0, 0);

        cmd->EndRenderPass();
    }

    // Transition distortion RT to SRV
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_distortionRT;
        barrier.stateBefore = RHIResourceState::RenderTarget;
        barrier.stateAfter = RHIResourceState::Common;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Step 3: Copy HDR → preDistortionCopy ───────────────────────────────
    {
        // Transition HDR to CopySrc, preDistortionCopy to CopyDst
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture = m_hdrTarget;
        barriers[0].stateBefore = RHIResourceState::RenderTarget;
        barriers[0].stateAfter = RHIResourceState::CopySrc;
        barriers[1].texture = m_preDistortionCopy;
        barriers[1].stateBefore = RHIResourceState::Common;
        barriers[1].stateAfter = RHIResourceState::CopyDst;
        cmd->ResourceBarrier(barriers, 2);
    }

    cmd->CopyTexture(m_hdrTarget, m_preDistortionCopy);

    {
        // Transition preDistortionCopy to SRV, HDR back to RenderTarget
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture = m_preDistortionCopy;
        barriers[0].stateBefore = RHIResourceState::CopyDst;
        barriers[0].stateAfter = RHIResourceState::Common;
        barriers[1].texture = m_hdrTarget;
        barriers[1].stateBefore = RHIResourceState::CopySrc;
        barriers[1].stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(barriers, 2);
    }

    // ── Step 4: Resolve — warp HDR color by distortion offsets ─────────────
    {
        RHIRenderPassDesc rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments[0].texture = m_hdrTarget;
        rpDesc.hasDepth = false;
        cmd->BeginRenderPass(rpDesc);

        RHIViewport vp{};
        vp.width = static_cast<float>(GetWidth());
        vp.height = static_cast<float>(GetHeight());
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        cmd->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = static_cast<int32_t>(GetWidth());
        sc.bottom = static_cast<int32_t>(GetHeight());
        cmd->SetScissor(sc);

        cmd->SetPipeline(m_distortionResolvePSO);

        // Root constants at b0: SRV indices for pre-distortion color and distortion RT
        DistortionResolveConstants resolveConsts{};
        resolveConsts.colorTextureSrvIndex = device->GetTextureSRVIndex(m_preDistortionCopy);
        resolveConsts.distortionTextureSrvIndex = device->GetTextureSRVIndex(m_distortionRT);
        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&resolveConsts),
                              sizeof(resolveConsts) / sizeof(uint32_t));

        // Fullscreen triangle
        cmd->Draw(3, 1, 0, 0);

        cmd->EndRenderPass();
    }

    // HDR is now in RenderTarget state with warped color — the caller will
    // transition it to Common/SRV for bloom and tonemap.
}

// ─────────────────────────────────────────────────────────────────────────────
// Tonemap Pass (HDR → LDR backbuffer)
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderTonemapPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    if (!m_tonemapPSO.IsValid()) return;

    auto* swapChain = GetSwapChain();
    auto backBuffer = swapChain->GetCurrentBackBuffer();

    // Begin render pass targeting the backbuffer
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = backBuffer;
    rpDesc.colorAttachments[0].clear = true;
    rpDesc.colorAttachments[0].clearColor[0] = 0.0f;
    rpDesc.colorAttachments[0].clearColor[1] = 0.0f;
    rpDesc.colorAttachments[0].clearColor[2] = 0.0f;
    rpDesc.colorAttachments[0].clearColor[3] = 1.0f;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    cmd->SetPipeline(m_tonemapPSO);
    cmd->SetRootCBV(0, perFrameGpu);

    // Bind HDR + bloom SRV indices — the tonemap shader reads them via
    // bindless descriptor indices packed into root constants slot 1.
    if (IsBindless()) {
        auto* device = GetDevice();

        struct TonemapIndices {
            uint32_t hdrColorIdx;          ///< SRV index for HDR scene color.
            uint32_t bloomColorIdx;        ///< SRV index for bloom result (0 = no bloom).
            uint32_t bloomIntensityBits;   ///< Bloom intensity as float bits (0 = disabled).
            uint32_t lensFlareSrvIdx;      ///< SRV index for lens flare (0 = disabled).
        };

        TonemapIndices indices{};
        indices.hdrColorIdx = device->GetTextureSRVIndex(m_hdrTarget);

        if (m_bloomEnabled && m_bloomPass.IsInitialized()) {
            indices.bloomColorIdx = m_bloomPass.GetBloomResultSrvIndex();
            const float bloomIntensity = m_bloomPass.settings.intensity;
            std::memcpy(&indices.bloomIntensityBits, &bloomIntensity, sizeof(float));
        }
        // lensFlareSrvIdx stays 0 (no lens flare in this demo)

        cmd->SetRootConstants(1, &indices, 4);
    }

    // Fullscreen triangle
    cmd->Draw(3, 1, 0, 0);

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::OnResize(uint32_t width, uint32_t height) {
    DestroySizeDependent();
    CreateSizeDependent(width, height);

    if (m_bloomPass.IsInitialized()) {
        m_bloomPass.Resize(width, height);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::OnShutdown() {
    auto* device = GetDevice();

    m_bloomPass.Shutdown();
    m_vfxSystem.Shutdown(device);

    // ── Trail system cleanup ─────────────────────────────────────────────
    m_trailSystem.Clear();
    if (m_trailVB.IsValid())  { device->DestroyBuffer(m_trailVB);  m_trailVB = {}; }
    if (m_trailIB.IsValid())  { device->DestroyBuffer(m_trailIB);  m_trailIB = {}; }
    if (m_trailPSO.IsValid()) { device->DestroyPipeline(m_trailPSO); m_trailPSO = {}; }

    // ── Decal cleanup ───────────────────────────────────────────────────
    if (m_decalPSO.IsValid()) { device->DestroyPipeline(m_decalPSO); m_decalPSO = {}; }

    // ── Distortion cleanup ──────────────────────────────────────────────
    if (m_distortionHazePSO.IsValid()) { device->DestroyPipeline(m_distortionHazePSO); m_distortionHazePSO = {}; }
    if (m_distortionResolvePSO.IsValid()) { device->DestroyPipeline(m_distortionResolvePSO); m_distortionResolvePSO = {}; }

    DestroySizeDependent();

    // Destroy loaded PBR textures
    if (m_albedoTex.IsValid()) { device->DestroyTexture(m_albedoTex); m_albedoTex = {}; }
    if (m_normalTex.IsValid()) { device->DestroyTexture(m_normalTex); m_normalTex = {}; }
    if (m_roughnessTex.IsValid()) { device->DestroyTexture(m_roughnessTex); m_roughnessTex = {}; }

    if (m_shadowMap.IsValid()) {
        device->DestroyTexture(m_shadowMap);
        m_shadowMap = {};
    }

    for (auto& mesh : m_meshes) {
        if (mesh.vertexBuffer.IsValid()) device->DestroyBuffer(mesh.vertexBuffer);
        if (mesh.indexBuffer.IsValid()) device->DestroyBuffer(mesh.indexBuffer);
        mesh = {};
    }
}

} // namespace uz
