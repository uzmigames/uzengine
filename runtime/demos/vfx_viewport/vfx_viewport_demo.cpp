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
#include "fly_camera_controller.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "rhi_swapchain.h"
#include "texture_loader.h"
#include "root_slots.h"
#include "platform_window.h"
#include "renderdoc_capture.h"
#include "draw_list.h"
#include "gpu_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <unordered_map>
#include <vector>

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
    // Initialize with a single emitter preset (backward-compatible default).
    // BuildCompoundPreset populates m_emitters but does NOT init GPU resources.
    BuildCompoundPreset(VFXCompoundPreset::Single);
    m_prevViewProj = Mat4::Identity();

    // ── Sub-emitter secondary "sparks" config (Task 3.87.0) ──────────────
    // Orange-yellow sparks that fire after kSubEmitterDelay seconds following
    // the primary burst. High initial velocity, wide cone spread, short
    // lifetime, additive blending, velocity-aligned stretch — reads as hot
    // sparks flying outward from an explosion origin.
    // Position is set at dispatch time from the primary emitter's location.
    m_subEmitterConfig = {};
    // Small, fast particles
    m_subEmitterConfig.initialSize        = 0.02f;    // Much smaller than primary
    m_subEmitterConfig.spawnRadius        = 0.05f;    // Tight origin cluster
    // High velocity — sparks fly outward fast in all directions
    m_subEmitterConfig.spawnVelocity[0]   = 0.0f;
    m_subEmitterConfig.spawnVelocity[1]   = 0.0f;
    m_subEmitterConfig.spawnVelocity[2]   = 12.0f;    // Z-up mean velocity
    m_subEmitterConfig.velocityVariation  = 8.0f;     // Large spread
    m_subEmitterConfig.spreadAngle        = 3.14159f; // Full sphere spread
    // Short lifetime — sparks burn out quickly
    m_subEmitterConfig.minLifetime        = 0.3f;
    m_subEmitterConfig.maxLifetime        = 0.8f;
    // Orange-yellow base color
    m_subEmitterConfig.initialColor[0]    = 1.0f;     // R
    m_subEmitterConfig.initialColor[1]    = 0.55f;    // G (orange-yellow)
    m_subEmitterConfig.initialColor[2]    = 0.05f;    // B
    m_subEmitterConfig.initialColor[3]    = 1.0f;     // A
    // Color fades from bright orange to dark red then transparent over lifetime
    m_subEmitterConfig.colorLifeStartR    = 1.0f;
    m_subEmitterConfig.colorLifeStartG    = 0.6f;
    m_subEmitterConfig.colorLifeStartB    = 0.1f;
    m_subEmitterConfig.colorLifeStartA    = 1.0f;
    m_subEmitterConfig.colorLifeEndR      = 0.6f;
    m_subEmitterConfig.colorLifeEndG      = 0.1f;
    m_subEmitterConfig.colorLifeEndB      = 0.0f;
    m_subEmitterConfig.colorLifeEndA      = 0.0f;    // Fade to transparent
    // Size over lifetime: born full size, shrink to nothing
    m_subEmitterConfig.sizeOverLifeStart    = 1.0f;
    m_subEmitterConfig.sizeOverLifePeak     = 0.8f;
    m_subEmitterConfig.sizeOverLifeEnd      = 0.0f;
    m_subEmitterConfig.sizeOverLifePeakTime = 0.2f;  // Peak early, then shrink fast
    // High emissive — sparks are self-illuminating hot particles
    m_subEmitterConfig.emissiveIntensity  = 4.0f;
    // Velocity-aligned stretch so sparks elongate along their trajectory
    m_subEmitterConfig.facingMode      = 1;        // VelocityAligned
    m_subEmitterConfig.stretchFactor      = 0.8f;
    // Light gravity — sparks arc downward but slowly
    m_subEmitterConfig.gravity[0]         = 0.0f;
    m_subEmitterConfig.gravity[1]         = 0.0f;
    m_subEmitterConfig.gravity[2]         = -4.0f;   // Gentle Z-down pull
    m_subEmitterConfig.damping            = 0.05f;    // Low air resistance
    // Additive blending so sparks glow over the background
    m_subEmitterConfig.blendMode          = 1;        // Additive (One/One)
    // Soft particle fade for smooth edge at depth intersections
    m_subEmitterConfig.softFadeDistance   = 0.1f;
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

    // Re-initialize editor grid for HDR render target (RGBA16Float, not RGBA8Unorm)
    auto* grid = GetEditorGrid();
    if (grid) {
        grid->Shutdown();
        grid->Init(device, shaders->GetPipeline(),
                   RHIFormat::RGBA16Float, RHIFormat::D32Float);
    }

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

        for (int i = 0; i < kMeshCount; ++i) {
            UZ_LOG(App, Info, "Mesh[{}] '{}': vbSrvIndex={}, indexCount={}, bindless={}",
                   i, m_meshes[i].name, m_meshes[i].vbSrvIndex, m_meshes[i].indexCount,
                   IsBindless());
        }
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

    // ── Dummy LUT textures for sky atmosphere ray march fallback ────────
    // The sky shader samples transmittance and multi-scattering LUTs even in
    // the ray march fallback path (bindless mode). Provide 1x1 textures with
    // neutral values to avoid invalid descriptor heap reads.
    {
        RHITextureDesc lutDesc{};
        lutDesc.width = 1;
        lutDesc.height = 1;
        lutDesc.format = RHIFormat::RGBA16Float;
        lutDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        lutDesc.debugName = "DummyTransmittanceLUT";
        m_dummyTransmittanceLut = device->CreateTexture(lutDesc);

        lutDesc.debugName = "DummyMultiScatterLUT";
        m_dummyMultiScatterLut = device->CreateTexture(lutDesc);

        // Upload white (1,1,1,1) for transmittance (no extinction) and
        // black (0,0,0,0) for multi-scattering (no contribution).
        const uint16_t kHalfOne  = 0x3C00; // IEEE 754 half-float 1.0
        const uint16_t kHalfZero = 0x0000; // IEEE 754 half-float 0.0
        const uint16_t whiteData[4] = { kHalfOne, kHalfOne, kHalfOne, kHalfOne };
        const uint16_t blackData[4] = { kHalfZero, kHalfZero, kHalfZero, kHalfZero };
        upload.UploadTexture(cmd, m_dummyTransmittanceLut, whiteData, sizeof(whiteData));
        upload.UploadTexture(cmd, m_dummyMultiScatterLut, blackData, sizeof(blackData));
    }

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

    // ── Multi-emitter VFX system initialization ─────────────────────────
    // BuildCompoundPreset was already called in the constructor. Now init
    // GPU resources for each emitter (buffers, compute/graphics pipelines).
    InitAllEmitters(device, shaders, cmd);

    // ── VFX material presets (ready-to-use material configurations) ──────
    // M_VFX_Fire_Additive: bright additive fire (procedural circles)
    m_vfxMaterials[0].blendMode            = 1; // Additive
    m_vfxMaterials[0].emissiveIntensity    = 2.0f;
    m_vfxMaterials[0].softFadeDistance     = 0.5f;
    m_vfxMaterials[0].baseColorTintR       = 1.0f;
    m_vfxMaterials[0].baseColorTintG       = 0.7f;
    m_vfxMaterials[0].baseColorTintB       = 0.3f;

    // M_VFX_Smoke_Translucent: soft alpha-blended smoke puffs
    m_vfxMaterials[1].blendMode            = 0; // AlphaBlend
    m_vfxMaterials[1].emissiveIntensity    = 0.0f;
    m_vfxMaterials[1].softFadeDistance     = 1.0f;
    m_vfxMaterials[1].baseColorTintR       = 0.6f;
    m_vfxMaterials[1].baseColorTintG       = 0.6f;
    m_vfxMaterials[1].baseColorTintB       = 0.6f;

    // M_VFX_Sparks_Additive: bright hot sparks, no sprite (procedural circle)
    m_vfxMaterials[2].blendMode            = 1; // Additive
    m_vfxMaterials[2].emissiveIntensity    = 3.0f;
    m_vfxMaterials[2].softFadeDistance     = 0.3f;
    m_vfxMaterials[2].baseColorTintR       = 1.0f;
    m_vfxMaterials[2].baseColorTintG       = 0.9f;
    m_vfxMaterials[2].baseColorTintB       = 0.5f;

    // M_VFX_Magic_Additive: cool blue-cyan magic glow
    m_vfxMaterials[3].blendMode            = 1; // Additive
    m_vfxMaterials[3].emissiveIntensity    = 1.0f;
    m_vfxMaterials[3].softFadeDistance     = 0.3f;
    m_vfxMaterials[3].baseColorTintR       = 0.3f;
    m_vfxMaterials[3].baseColorTintG       = 0.6f;
    m_vfxMaterials[3].baseColorTintB       = 1.0f;

    // M_VFX_Debris_AlphaBlend: opaque-like debris chunks
    m_vfxMaterials[4].blendMode            = 0; // AlphaBlend
    m_vfxMaterials[4].emissiveIntensity    = 0.0f;
    m_vfxMaterials[4].softFadeDistance     = 0.5f;
    m_vfxMaterials[4].baseColorTintR       = 0.8f;
    m_vfxMaterials[4].baseColorTintG       = 0.4f;
    m_vfxMaterials[4].baseColorTintB       = 0.2f;

    // M_VFX_Distortion_PremulAlpha: premultiplied alpha for distortion effects
    m_vfxMaterials[5].blendMode            = 2; // PremulAlpha
    m_vfxMaterials[5].emissiveIntensity    = 0.5f;
    m_vfxMaterials[5].softFadeDistance     = 0.5f;
    m_vfxMaterials[5].baseColorTintR       = 1.0f;
    m_vfxMaterials[5].baseColorTintG       = 1.0f;
    m_vfxMaterials[5].baseColorTintB       = 1.0f;

    UZ_LOG(App, Info, "VFXViewport: {} VFX material presets initialized (M key to cycle)",
           kVFXMaterialCount);

    // ── Color ramp LUT presets ──────────────────────────────────────────
    // Build predefined color ramp gradients for color-over-lifetime LUT sampling.
    // These are baked to RGBA16F textures on demand when the user activates them (N key).
    m_colorRamps[0] = BuildFireRamp();
    m_colorRamps[1] = BuildIceRamp();
    m_colorRamps[2] = BuildRainbowRamp();
    m_currentRampIndex = -1; // Start in legacy lerp mode
    UZ_LOG(App, Info, "VFXViewport: {} color ramp presets ready (N key to cycle)",
           kColorRampCount);

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
    // No Input Assembler — bindless vertex fetch via SV_VertexID + StructuredBuffer.
    // This matches the engine's SM6.6 bindless root signature which does not
    // declare IA support (E_INVALIDARG if input layout is specified).
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
        // No input layout — bindless vertex fetch via StructuredBuffer + SV_VertexID
        desc.inputElements = nullptr;
        desc.inputElementCount = 0;
        desc.vertexStride = 0;
        desc.debugName = "VFX_TrailPSO";
        m_trailPSO = device->CreateGraphicsPipeline(desc);
        if (!m_trailPSO.IsValid()) {
            UZ_LOG(App, Error, "VFXViewport: Failed to create trail ribbon PSO");
            m_trailEnabled = false;
        }
    }

    // ── Trail dynamic vertex / index upload buffers (StructuredBuffer for bindless) ──
    // Created as StructuredBuffers so the VS can read via bindless SRV (SV_VertexID).
    // No Input Assembler is used — the engine's SM6.6 root signature does not support IA.
    if (m_trailEnabled) {
        {
            RHIBufferDesc vbDesc{};
            vbDesc.sizeBytes = kMaxTrailVertices * 36; // sizeof(TrailVertex) = 36 bytes
            vbDesc.stride = 36;                        // StructuredBuffer element stride
            vbDesc.usage = RHIBufferUsage::StructuredBuffer;
            vbDesc.memory = RHIMemoryType::Upload;
            vbDesc.debugName = "Trail_DynamicVB";
            m_trailVB = device->CreateBuffer(vbDesc);
            m_trailVBSrvIndex = device->GetBufferSRVIndex(m_trailVB);
        }
        {
            RHIBufferDesc ibDesc{};
            ibDesc.sizeBytes = kMaxTrailIndices * sizeof(uint32_t);
            ibDesc.stride = sizeof(uint32_t);          // StructuredBuffer<uint> element stride
            ibDesc.usage = RHIBufferUsage::StructuredBuffer;
            ibDesc.memory = RHIMemoryType::Upload;
            ibDesc.debugName = "Trail_DynamicIB";
            m_trailIB = device->CreateBuffer(ibDesc);
            m_trailIBSrvIndex = device->GetBufferSRVIndex(m_trailIB);
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

    // ── Initialize per-mesh transforms ─────────────────────────────────
    // Meshes 0,1,3,4 (sphere, cube, cylinder, torus) start at the origin
    // with identity rotation and unit scale. Mesh 2 (ground plane) has
    // its existing hardcoded transform: scale(20,1,20) * rotX(pi/2) * translate(0,0,-0.1).
    for (int i = 0; i < kMeshCount; ++i) {
        m_meshTransforms[i] = MeshInstance{};
    }
    // Ground plane: special transform preserved from the original hardcoded values.
    // The ground plane mesh is generated as an XZ plane, rotated 90 deg around X
    // to become an XY ground plane, then translated down to Z=-0.1 and scaled 20x.
    {
        constexpr float kHalfPi = 3.14159265f * 0.5f;
        m_meshTransforms[2].position = Vec3(0.0f, 0.0f, -0.1f);
        m_meshTransforms[2].rotation = Quat::FromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), kHalfPi);
        m_meshTransforms[2].scale = Vec3(20.0f, 1.0f, 20.0f);
    }

    // ── Outline pass initialization ─────────────────────────────────────
    if (!m_outlinePass.Init(device, shaders->GetPipeline(),
                            RHIFormat::RGBA16Float, RHIFormat::D32Float,
                            GetWidth(), GetHeight())) {
        UZ_LOG(App, Error, "VFXViewport: Failed to initialize outline pass");
        m_outlineEnabled = false;
    }

    // ── Gizmo renderer initialization ───────────────────────────────────
    if (!m_gizmoRenderer.Init(device, shaders->GetPipeline(),
                              RHIFormat::RGBA16Float, RHIFormat::D32Float)) {
        UZ_LOG(App, Error, "VFXViewport: Failed to initialize gizmo renderer");
    }

    // Submit and flush all upload copy commands before releasing staging buffers.
    // ReleaseStaging() destroys the underlying ID3D12Resource objects. If those
    // resources are still referenced by an open command list the D3D12 debug layer
    // reports "resource deleted prior to Close()" and the device hangs (0x887A0006).
    // demo_application.cpp submits this command list only AFTER OnInit() returns,
    // so we must do our own flush here and then re-open the list for the caller.
    cmd->End();
    cmd->Execute();
    device->WaitIdle();
    upload.ReleaseStaging();
    cmd->Begin();

    // ── VFX editor UI (mouse-driven visual overlay) ────────────────────────
    // Needs UploadContext for BitmapFont atlas upload. Create a second upload
    // context, init the UI, flush, and re-open the command list for the caller.
    {
        UploadContext uiUpload(device);
        if (!m_editorUI.Init(device, cmd, uiUpload, shaders->GetPipeline(),
                             RHIFormat::RGBA8Unorm, GetWidth(), GetHeight(),
                             IsBindless())) {
            UZ_LOG(App, Error, "VFXViewport: Failed to init VFXEditorUI");
        }
        cmd->End();
        cmd->Execute();
        device->WaitIdle();
        uiUpload.ReleaseStaging();
        cmd->Begin();
    }

    // Wire up editor UI callbacks
    m_editorUI.onRestart = [this]() {
        auto* c = GetCommandList();
        ResetAllEmitters(c);
    };
    m_editorUI.onBurst = [this]() {
        for (auto& e : m_emitters) e.burstPending = true;
    };
    m_editorUI.onFireball = [this]() {
        const auto& cam = GetCamera();
        m_fireballPos = cam.GetPosition();
        m_fireballVel = cam.GetForward() * kFireballSpeed;
        m_fireballLife = 0.0f;
        m_fireballActive = true;
    };
    m_editorUI.onLoadPreset = [this](int idx) { LoadPreset(idx); };
    m_editorUI.onSelectEmitter = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(m_emitters.size())) {
            m_activeEmitterIndex = idx;
            auto& ae = ActiveEmitter();
            m_paramPanel.Init(&ae.config, &ae.spawnRate, &ae.burstCount,
                              &ae.burstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
            m_moduleStack.Init(&ae.config, &m_subEmitterEnabled, &m_frustumCullingEnabled);
        }
    };

    // ── VFX parameter panel (console-based, no GPU rendering) ────────────
    // Points to the active emitter's config. The panel reads/writes these
    // pointers directly, so we refresh them when the active emitter changes.
    {
        auto& ae = ActiveEmitter();
        m_paramPanel.Init(&ae.config, &ae.spawnRate, &ae.burstCount,
                          &ae.burstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
    }

    // ── VFX module stack panel (console-based module visibility + toggle) ──
    m_moduleStack.Init(&ActiveEmitter().config, &m_subEmitterEnabled, &m_frustumCullingEnabled);

    // ── Preset management — export built-in presets and scan directory ───────
    // Export each built-in preset to content/vfx/presets/ on first run (skips
    // existing files), then scan the directory to build the file-based preset
    // list. Tab key cycling and LoadPreset() both use m_presetFiles.
    RefreshPresetList("content/vfx/presets/");

    // Wire up the panel's Presets browser.
    m_paramPanel.SetPresetList(m_presetNames);
    m_paramPanel.SetPresetLoadCallback([this](int idx) { LoadPreset(idx); });
    m_editorUI.SetPresetNames(m_presetNames);

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

    // ── VFX editor UI (mouse-driven overlay — consumes mouse when hovered) ──
    m_editorUI.HandleMouse(input, dt);
    if (m_editorUI.WantsMouseInput()) {
        // UI consumed the mouse — still allow keyboard input to pass through
        // but skip camera/gizmo mouse handling below.
    }

    // ── VFX parameter panel (F10 toggles, consumes input when visible) ──
    if (input->IsKeyJustPressed(Key::F10)) {
        m_paramPanel.Toggle();
    }
    if (m_paramPanel.IsVisible()) {
        bool needsRestart = false;
        if (m_paramPanel.HandleInput(input, needsRestart)) {
            if (needsRestart) {
                auto* cmd = GetCommandList();
                ResetAllEmitters(cmd);
            }
            return; // Panel consumed the input — skip all other handlers.
        }
    }

    // ── VFX module stack panel (Q toggles, consumes input when visible) ──
    if (input->IsKeyJustPressed(Key::Q) && !m_paramPanel.IsVisible()) {
        m_moduleStack.Toggle();
    }
    if (m_moduleStack.HandleInput(input)) {
        return; // Module stack consumed the input — skip all other handlers.
    }

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

    // Vortex force mode cycle (F key)
    // @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
    if (input->IsKeyJustPressed(Key::F)) {
        m_vortexMode = (m_vortexMode + 1) % 3; // 0=Off, 1=Tornado, 2=Whirlpool
        static const char* kVortexModeNames[] = { "OFF", "TORNADO", "WHIRLPOOL" };
        UZ_LOG(App, Info, "[F] Vortex mode: {}", kVortexModeNames[m_vortexMode]);
    }

    // Wind zone mode cycle (N key)
    // @ref Stam 1999 "Stable Fluids" -- wind zone accumulation
    if (input->IsKeyJustPressed(Key::N)) {
        m_windMode = (m_windMode + 1) % 3; // 0=Off, 1=Ambient, 2=Gust
        static const char* kWindModeNames[] = { "OFF", "AMBIENT", "GUST" };
        UZ_LOG(App, Info, "[N] Wind mode: {}", kWindModeNames[m_windMode]);
    }

    // Kill volume mode cycle (K key)
    // @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    if (input->IsKeyJustPressed(Key::K)) {
        m_killVolumeMode = (m_killVolumeMode + 1) % 4; // 0=Off, 1=InvWall, 2=Ground, 3=Sphere
        static const char* kKillModeNames[] = { "OFF", "INVISIBLE_WALL", "GROUND_BOUNDARY", "SPHERE_ZONE" };
        UZ_LOG(App, Info, "[K] Kill volume mode: {}", kKillModeNames[m_killVolumeMode]);
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

    // Lit particles toggle (J key) — applies to all emitters
    // @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL
    if (input->IsKeyJustPressed(Key::J)) {
        const uint32_t newMode = (ActiveEmitter().config.litMode == 0) ? 1u : 0u;
        for (auto& e : m_emitters) e.config.litMode = newMode;
        UZ_LOG(App, Info, "[J] Lit particles: {}", newMode ? "ON" : "OFF");
    }

    // VFX restart (Delete key) — clears all particles and fires new burst/restarts
    if (input->IsKeyJustPressed(Key::Delete)) {
        auto* cmd = GetCommandList();
        ResetAllEmitters(cmd);
        // Start sub-emitter countdown if enabled — fires secondary sparks burst
        // after kSubEmitterDelay seconds at the same world position.
        if (m_subEmitterEnabled) {
            m_subEmitterTimer = kSubEmitterDelay;
        }
        UZ_LOG(App, Info, "[VFX] Particles reset — emission restarted{}",
               m_subEmitterEnabled ? " (sub-emitter armed)" : "");
    }

    // Toggle burst/continuous mode (Insert key) — applies to all emitters
    if (input->IsKeyJustPressed(Key::Insert)) {
        const bool newMode = !ActiveEmitter().burstMode;
        for (auto& e : m_emitters) {
            e.burstMode = newMode;
            if (newMode) e.burstPending = true;
        }
        UZ_LOG(App, Info, "[VFX] Mode: {} (active emitter burst count={})",
               newMode ? "Burst" : "Continuous", ActiveEmitter().burstCount);
    }

    // VFX material preset cycling (M key)
    if (input->IsKeyJustPressed(Key::M)) {
        m_currentMaterialIndex = (m_currentMaterialIndex + 1) % kVFXMaterialCount;
        static const char* kVFXMaterialNames[] = {
            "Fire_Additive", "Smoke_Translucent", "Sparks_Additive",
            "Magic_Additive", "Debris_AlphaBlend", "Distortion_PremulAlpha"
        };
        UZ_LOG(App, Info, "[VFX] Material: {} (blend={}, emissive={:.1f}, tint=[{:.1f},{:.1f},{:.1f}])",
               kVFXMaterialNames[m_currentMaterialIndex],
               m_vfxMaterials[m_currentMaterialIndex].blendMode,
               m_vfxMaterials[m_currentMaterialIndex].emissiveIntensity,
               m_vfxMaterials[m_currentMaterialIndex].baseColorTintR,
               m_vfxMaterials[m_currentMaterialIndex].baseColorTintG,
               m_vfxMaterials[m_currentMaterialIndex].baseColorTintB);
    }

    // Color ramp LUT cycling (N key) — Off -> Fire -> Ice -> Rainbow -> Off
    // Applies the same ramp to ALL emitters so the compound effect is consistent.
    if (input->IsKeyJustPressed(Key::N)) {
        m_currentRampIndex = (m_currentRampIndex + 1);
        if (m_currentRampIndex >= kColorRampCount) {
            m_currentRampIndex = -1; // wrap back to legacy lerp
        }

        if (m_currentRampIndex < 0) {
            for (auto& e : m_emitters) e.system->ClearColorRampLUT();
            UZ_LOG(App, Info, "[VFX] Color ramp: Off (legacy lerp)");
        } else {
            auto* device = GetDevice();
            auto* cmd = GetCommandList();
            UploadContext upload(device);
            bool ok = true;
            for (auto& e : m_emitters) {
                if (!e.system->UploadColorRampLUT(upload, cmd, m_colorRamps[m_currentRampIndex])) {
                    ok = false;
                }
            }
            if (ok) {
                static const char* kRampNames[] = {"Fire", "Ice", "Rainbow"};
                UZ_LOG(App, Info, "[VFX] Color ramp: {}", kRampNames[m_currentRampIndex]);
            } else {
                UZ_LOG(App, Error, "[VFX] Failed to upload color ramp LUT");
                m_currentRampIndex = -1;
            }
        }
    }

    // Space key fireball removed — conflicts with camera jump/movement.
    // Use the [Fireball] button in the bottom toolbar or File menu instead.

    // VFX preset cycling (Tab / Shift+Tab) — skip if Ctrl is held (Ctrl+Tab = compound preset)
    const bool ctrlDown = input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl);
    if (input->IsKeyJustPressed(Key::Tab) && !ctrlDown) {
        if (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift)) {
            // Toggle VFX on/off
            m_vfxEnabled = !m_vfxEnabled;
            UZ_LOG(App, Info, "VFX: {}", m_vfxEnabled ? "enabled" : "disabled");
        } else {
            // Cycle through file-based presets (applied to the active emitter).
            // Fall back to built-in presets if the list is empty.
            if (!m_presetFiles.empty()) {
                const int nextIdx = (m_vfxPresetIndex + 1) % static_cast<int>(m_presetFiles.size());
                ActiveEmitter().spawnAccum = 0.0f;
                LoadPreset(nextIdx);
            } else {
                int presetCount = 0;
                const VFXPreset* presets = GetVFXPresets(presetCount);
                m_vfxPresetIndex = (m_vfxPresetIndex + 1) % presetCount;
                auto& ae = ActiveEmitter();
                ae.config = presets[m_vfxPresetIndex].config;
                ae.spawnRate = presets[m_vfxPresetIndex].spawnRate;
                ae.spawnAccum = 0.0f;
                UZ_LOG(App, Info, "VFX preset: {} (built-in fallback)", presets[m_vfxPresetIndex].name);
            }
        }
    }

    // Compound preset cycling (Ctrl+Tab) — Single -> Fire -> Explosion -> Magic
    if (input->IsKeyJustPressed(Key::Tab) &&
        (input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl))) {
        const int next = (static_cast<int>(m_compoundPreset) + 1)
                         % static_cast<int>(VFXCompoundPreset::Count);
        auto* device = GetDevice();
        auto* shaders = GetShaderManager();
        auto* cmd = GetCommandList();
        ShutdownAllEmitters(device);
        BuildCompoundPreset(static_cast<VFXCompoundPreset>(next));
        InitAllEmitters(device, shaders, cmd);
        // Re-point the parameter panel to the new active emitter
        {
            auto& ae = ActiveEmitter();
            m_paramPanel.Init(&ae.config, &ae.spawnRate, &ae.burstCount,
                              &ae.burstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
            m_moduleStack.Init(&ae.config, &m_subEmitterEnabled, &m_frustumCullingEnabled);
        }
        static const char* kCompoundNames[] = {"Single", "Fire", "Explosion", "Magic"};
        UZ_LOG(App, Info, "[VFX] Compound preset: {} ({} emitters)",
               kCompoundNames[next], static_cast<int>(m_emitters.size()));
    }

    // Emitter selection cycling (Ctrl+1..8 selects emitter index)
    for (int i = 0; i < static_cast<int>(m_emitters.size()) && i < kMaxEmitters; ++i) {
        const Key numKey = static_cast<Key>(static_cast<int>(Key::Num1) + i);
        if (input->IsKeyJustPressed(numKey) &&
            (input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl))) {
            m_activeEmitterIndex = i;
            auto& ae = ActiveEmitter();
            // Re-point the parameter panel to the new active emitter
            m_paramPanel.Init(&ae.config, &ae.spawnRate, &ae.burstCount,
                              &ae.burstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
            m_moduleStack.Init(&ae.config, &m_subEmitterEnabled, &m_frustumCullingEnabled);
            UZ_LOG(App, Info, "[VFX] Active emitter: {} ({})",
                   i, ae.name.c_str());
        }
    }

    // VFX parameter tweaking (number keys — plain = basic, Shift = advanced)
    // Ctrl+number is reserved for emitter selection, so skip tweaking when Ctrl is held.
    const bool shift = input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift);
    if (!shift && !ctrlDown) {
        // 1/2: Spawn rate ↑/↓
        if (input->IsKeyJustPressed(Key::Num1)) {
            ActiveEmitter().spawnRate = std::min(5000.0f, ActiveEmitter().spawnRate * 1.5f);
            UZ_LOG(App, Info, "[VFX] Spawn rate: {:.0f}/s", ActiveEmitter().spawnRate);
        }
        if (input->IsKeyJustPressed(Key::Num2)) {
            ActiveEmitter().spawnRate = std::max(10.0f, ActiveEmitter().spawnRate / 1.5f);
            UZ_LOG(App, Info, "[VFX] Spawn rate: {:.0f}/s", ActiveEmitter().spawnRate);
        }
        // 3/4: Particle size ↑/↓
        if (input->IsKeyJustPressed(Key::Num3)) {
            ActiveEmitter().config.initialSize = std::min(5.0f, ActiveEmitter().config.initialSize * 1.3f);
            UZ_LOG(App, Info, "[VFX] Size: {:.3f}", ActiveEmitter().config.initialSize);
        }
        if (input->IsKeyJustPressed(Key::Num4)) {
            ActiveEmitter().config.initialSize = std::max(0.02f, ActiveEmitter().config.initialSize / 1.3f);
            UZ_LOG(App, Info, "[VFX] Size: {:.3f}", ActiveEmitter().config.initialSize);
        }
        // 5/6: Gravity Z ↑/↓ (Z-up)
        if (input->IsKeyJustPressed(Key::Num5)) {
            ActiveEmitter().config.gravity[2] -= 2.0f;
            UZ_LOG(App, Info, "[VFX] Gravity Z: {:.1f}", ActiveEmitter().config.gravity[2]);
        }
        if (input->IsKeyJustPressed(Key::Num6)) {
            ActiveEmitter().config.gravity[2] += 2.0f;
            UZ_LOG(App, Info, "[VFX] Gravity Z: {:.1f}", ActiveEmitter().config.gravity[2]);
        }
        // 7/8: Velocity speed ↑/↓
        if (input->IsKeyJustPressed(Key::Num7)) {
            ActiveEmitter().config.spawnVelocity[2] = std::min(30.0f, ActiveEmitter().config.spawnVelocity[2] * 1.3f);
            UZ_LOG(App, Info, "[VFX] Velocity Z: {:.1f}", ActiveEmitter().config.spawnVelocity[2]);
        }
        if (input->IsKeyJustPressed(Key::Num8)) {
            ActiveEmitter().config.spawnVelocity[2] = std::max(0.5f, ActiveEmitter().config.spawnVelocity[2] / 1.3f);
            UZ_LOG(App, Info, "[VFX] Velocity Z: {:.1f}", ActiveEmitter().config.spawnVelocity[2]);
        }
        // 9/0: Spread angle ↑/↓
        if (input->IsKeyJustPressed(Key::Num9)) {
            ActiveEmitter().config.spreadAngle = std::min(3.14159f, ActiveEmitter().config.spreadAngle + 0.1f);
            UZ_LOG(App, Info, "[VFX] Spread: {:.2f} rad", ActiveEmitter().config.spreadAngle);
        }
        if (input->IsKeyJustPressed(Key::Num0)) {
            ActiveEmitter().config.spreadAngle = std::max(0.0f, ActiveEmitter().config.spreadAngle - 0.1f);
            UZ_LOG(App, Info, "[VFX] Spread: {:.2f} rad", ActiveEmitter().config.spreadAngle);
        }
    }
    // Shift+1/Shift+2: Lifetime ↑/↓
    if (input->IsKeyJustPressed(Key::Num1) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.minLifetime = std::min(10.0f, ActiveEmitter().config.minLifetime * 1.3f);
        ActiveEmitter().config.maxLifetime = std::min(15.0f, ActiveEmitter().config.maxLifetime * 1.3f);
        UZ_LOG(App, Info, "[VFX] Lifetime: {:.1f}-{:.1f}s", ActiveEmitter().config.minLifetime, ActiveEmitter().config.maxLifetime);
    }
    if (input->IsKeyJustPressed(Key::Num2) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.minLifetime = std::max(0.1f, ActiveEmitter().config.minLifetime / 1.3f);
        ActiveEmitter().config.maxLifetime = std::max(0.2f, ActiveEmitter().config.maxLifetime / 1.3f);
        UZ_LOG(App, Info, "[VFX] Lifetime: {:.1f}-{:.1f}s", ActiveEmitter().config.minLifetime, ActiveEmitter().config.maxLifetime);
    }
    // Shift+3/Shift+4: Emissive intensity ↑/↓
    if (input->IsKeyJustPressed(Key::Num3) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.emissiveIntensity = std::min(10.0f, ActiveEmitter().config.emissiveIntensity + 0.5f);
        UZ_LOG(App, Info, "[VFX] Emissive: {:.1f}", ActiveEmitter().config.emissiveIntensity);
    }
    if (input->IsKeyJustPressed(Key::Num4) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.emissiveIntensity = std::max(0.0f, ActiveEmitter().config.emissiveIntensity - 0.5f);
        UZ_LOG(App, Info, "[VFX] Emissive: {:.1f}", ActiveEmitter().config.emissiveIntensity);
    }
    // Shift+5/Shift+6: Damping ↑/↓
    if (input->IsKeyJustPressed(Key::Num5) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.damping = std::min(2.0f, ActiveEmitter().config.damping + 0.1f);
        UZ_LOG(App, Info, "[VFX] Damping: {:.2f}", ActiveEmitter().config.damping);
    }
    if (input->IsKeyJustPressed(Key::Num6) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.damping = std::max(0.0f, ActiveEmitter().config.damping - 0.1f);
        UZ_LOG(App, Info, "[VFX] Damping: {:.2f}", ActiveEmitter().config.damping);
    }
    // Shift+7/Shift+8: Spawn radius ↑/↓
    if (input->IsKeyJustPressed(Key::Num7) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.spawnRadius = std::min(5.0f, ActiveEmitter().config.spawnRadius + 0.2f);
        UZ_LOG(App, Info, "[VFX] Spawn radius: {:.1f}", ActiveEmitter().config.spawnRadius);
    }
    if (input->IsKeyJustPressed(Key::Num8) && (input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift))) {
        ActiveEmitter().config.spawnRadius = std::max(0.0f, ActiveEmitter().config.spawnRadius - 0.2f);
        UZ_LOG(App, Info, "[VFX] Spawn radius: {:.1f}", ActiveEmitter().config.spawnRadius);
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

    // ── Gizmo mode keys (W/E/R) — only when an entity is selected ──────
    // When nothing is selected, R still controls environment rotation.
    if (m_selectedMeshIndex != UINT32_MAX) {
        if (input->IsKeyJustPressed(Key::W)) {
            m_gizmoMode = GizmoMode::Translate;
            UZ_LOG(App, Info, "Gizmo mode: Translate");
        }
        if (input->IsKeyJustPressed(Key::E)) {
            m_gizmoMode = GizmoMode::Rotate;
            UZ_LOG(App, Info, "Gizmo mode: Rotate");
        }
        if (input->IsKeyJustPressed(Key::R)) {
            m_gizmoMode = GizmoMode::Scale;
            UZ_LOG(App, Info, "Gizmo mode: Scale");
        }
    } else {
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
        // Sub-emitter toggle (E key — no selection) — Task 3.87.0
        // When enabled, pressing Delete fires the primary burst AND starts a
        // kSubEmitterDelay-second countdown that dispatches a secondary sparks
        // burst at the same world position. This is a CPU-side mechanism: no
        // GPU readback, no per-particle death flags.
        if (input->IsKeyJustPressed(Key::E)) {
            m_subEmitterEnabled = !m_subEmitterEnabled;
            // Cancel any in-flight countdown when disabling, so a stale timer
            // from a previous burst does not fire after the mode is turned off.
            if (!m_subEmitterEnabled) {
                m_subEmitterTimer = -1.0f;
            }
            UZ_LOG(App, Info, "[VFX] Sub-emitter: {} ({} sparks after {:.2f}s)",
                   m_subEmitterEnabled ? "ON" : "OFF",
                   m_subEmitterCount, kSubEmitterDelay);
        }
    }

    // Object rotation (Alt + LMB drag) — only when NOT dragging gizmo
    if (input->IsKeyDown(Key::LeftAlt) && input->IsMouseDown(MouseButton::Left)
        && !m_gizmoInteraction.IsDragging()) {
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

    // Outline toggle (O key)
    if (input->IsKeyJustPressed(Key::O)) {
        m_outlineEnabled = !m_outlineEnabled;
        UZ_LOG(App, Info, "Outline: {}", m_outlineEnabled ? "enabled" : "disabled");
    }

    // Curl noise toggle (N key) — toggles curl noise force on/off for active emitter
    if (input->IsKeyJustPressed(Key::N)) {
        if (ActiveEmitter().config.curlStrength > 0.0f) {
            ActiveEmitter().config.curlStrength = 0.0f;
            UZ_LOG(App, Info, "[N] Curl noise: OFF");
        } else {
            ActiveEmitter().config.curlStrength = 2.0f;
            UZ_LOG(App, Info, "[N] Curl noise: ON  strength=2.0");
        }
    }

    // Escape — deselect
    if (input->IsKeyJustPressed(Key::Escape)) {
        if (m_selectedMeshIndex != UINT32_MAX) {
            m_selectedMeshIndex = UINT32_MAX;
            m_gizmoInteraction.EndDrag();
            UZ_LOG(App, Info, "Deselected");
        }
    }

    // ── Gizmo interaction (hover + drag) ────────────────────────────────
    // Every frame when a mesh is selected: hit-test the gizmo, handle drag.
    {
        const float mouseX = input->GetMouseX();
        const float mouseY = input->GetMouseY();

        const float vpW = static_cast<float>(GetWidth());
        const float vpH = static_cast<float>(GetHeight());
        const Camera& cam = GetCamera();

        if (m_selectedMeshIndex != UINT32_MAX && m_gizmoRenderer.IsInitialized()) {
            const auto& xform = m_meshTransforms[m_selectedMeshIndex];

            // Compute constant screen-space scale factor for gizmo hit testing.
            // Same formula as GizmoRenderer: 120px / (dist * tan(fov/2) * 2 / vpH).
            const float dist = (cam.GetPosition() - xform.position).Length();
            constexpr float kTargetPx = 120.0f;
            const float fovRad = cam.GetFovY() * (3.14159265f / 180.0f);
            const float screenScale = (dist > 0.001f)
                ? kTargetPx / (dist * std::tan(fovRad * 0.5f) * 2.0f / vpH)
                : 1.0f;

            if (m_gizmoInteraction.IsDragging()) {
                // Update drag — compute transform delta
                Vec3 deltaPos{};
                Quat deltaRot = Quat::Identity();
                Vec3 deltaScale{};
                const bool snap = input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl);
                float snapGrid = kDefaultTranslateSnap;
                if (m_gizmoMode == GizmoMode::Rotate) snapGrid = kDefaultRotateSnap;
                else if (m_gizmoMode == GizmoMode::Scale) snapGrid = kDefaultScaleSnap;

                if (m_gizmoInteraction.UpdateDrag(mouseX, mouseY, vpW, vpH, cam,
                                                   snap, snapGrid,
                                                   deltaPos, deltaRot, deltaScale)) {
                    auto& xf = m_meshTransforms[m_selectedMeshIndex];
                    if (m_gizmoMode == GizmoMode::Translate) {
                        xf.position = m_gizmoInteraction.GetDragStartPos() + deltaPos;
                    } else if (m_gizmoMode == GizmoMode::Rotate) {
                        xf.rotation = deltaRot * m_gizmoInteraction.GetDragStartRot();
                    } else if (m_gizmoMode == GizmoMode::Scale) {
                        xf.scale = m_gizmoInteraction.GetDragStartScale() + deltaScale;
                    }
                }

                // End drag on LMB release
                if (input->IsMouseJustReleased(MouseButton::Left)) {
                    m_gizmoInteraction.EndDrag();
                }
            } else {
                // Hit-test gizmo for hover highlight
                auto hitResult = m_gizmoInteraction.HitTest(
                    mouseX, mouseY, vpW, vpH, cam,
                    xform.position, screenScale,
                    static_cast<uint8_t>(m_gizmoMode),
                    static_cast<uint8_t>(m_gizmoSpace),
                    xform.rotation);
                m_gizmoInteraction.SetHoveredAxis(hitResult);

                // Begin drag on LMB press when hovering a gizmo axis
                if (input->IsMouseJustPressed(MouseButton::Left)
                    && hitResult != GizmoHitResult::None
                    && !input->IsKeyDown(Key::LeftAlt)) {
                    m_gizmoInteraction.BeginDrag(
                        hitResult, mouseX, mouseY, vpW, vpH, cam,
                        xform.position, xform.rotation, xform.scale,
                        screenScale,
                        static_cast<uint8_t>(m_gizmoMode),
                        static_cast<uint8_t>(m_gizmoSpace));
                }
                // Click-to-select (LMB click, not on gizmo, not Alt-held)
                else if (input->IsMouseJustPressed(MouseButton::Left)
                         && hitResult == GizmoHitResult::None
                         && !input->IsKeyDown(Key::LeftAlt)
                         && !input->IsKeyDown(Key::RightAlt)) {
                    // Ray-sphere intersection: test against the current mesh's bounding sphere.
                    // The main mesh sits at m_meshTransforms[m_currentMesh].position.
                    // Use a conservative bounding sphere radius of 1.5 (most procedural meshes
                    // fit within a radius of ~1.0-1.5).
                    const auto& meshXform = m_meshTransforms[m_currentMesh];
                    const Mat4 invVP = cam.GetViewProjection().Inverse();

                    // Unproject mouse position to a world-space ray
                    const float ndcX = (mouseX / vpW) * 2.0f - 1.0f;
                    const float ndcY = 1.0f - (mouseY / vpH) * 2.0f; // Flip Y: screen top=+1
                    const Vec4 nearClip = invVP * Vec4(ndcX, ndcY, 1.0f, 1.0f); // Reverse-Z: near=1
                    const Vec4 farClip  = invVP * Vec4(ndcX, ndcY, 0.0f, 1.0f); // Reverse-Z: far=0
                    const Vec3 nearW = Vec3(nearClip.x, nearClip.y, nearClip.z) / nearClip.w;
                    const Vec3 farW  = Vec3(farClip.x, farClip.y, farClip.z) / farClip.w;
                    const Vec3 rayDir = (farW - nearW).Normalized();

                    // Ray-sphere test: |P + t*D - C|^2 = r^2
                    const Vec3 oc = nearW - meshXform.position;
                    constexpr float kBoundingRadius = 1.5f;
                    const float a = rayDir.Dot(rayDir);
                    const float b = 2.0f * oc.Dot(rayDir);
                    const float c = oc.Dot(oc) - kBoundingRadius * kBoundingRadius;
                    const float discriminant = b * b - 4.0f * a * c;

                    if (discriminant >= 0.0f) {
                        m_selectedMeshIndex = static_cast<uint32_t>(m_currentMesh);
                        UZ_LOG(App, Info, "Selected mesh: {}", kMeshNames[m_currentMesh]);
                    } else {
                        m_selectedMeshIndex = UINT32_MAX;
                    }
                }
            }
        } else if (m_selectedMeshIndex == UINT32_MAX) {
            // No selection — still support click-to-select
            if (input->IsMouseJustPressed(MouseButton::Left)
                && !input->IsKeyDown(Key::LeftAlt)
                && !input->IsKeyDown(Key::RightAlt)
                && !input->IsMouseDown(MouseButton::Right)) {
                const auto& meshXform = m_meshTransforms[m_currentMesh];
                const Mat4 invVP = cam.GetViewProjection().Inverse();
                const float ndcX = (mouseX / vpW) * 2.0f - 1.0f;
                const float ndcY = 1.0f - (mouseY / vpH) * 2.0f;
                const Vec4 nearClip = invVP * Vec4(ndcX, ndcY, 1.0f, 1.0f);
                const Vec4 farClip  = invVP * Vec4(ndcX, ndcY, 0.0f, 1.0f);
                const Vec3 nearW = Vec3(nearClip.x, nearClip.y, nearClip.z) / nearClip.w;
                const Vec3 farW  = Vec3(farClip.x, farClip.y, farClip.z) / farClip.w;
                const Vec3 rayDir = (farW - nearW).Normalized();
                const Vec3 oc = nearW - meshXform.position;
                constexpr float kBoundingRadius = 1.5f;
                const float a = rayDir.Dot(rayDir);
                const float b = 2.0f * oc.Dot(rayDir);
                const float c = oc.Dot(oc) - kBoundingRadius * kBoundingRadius;
                const float discriminant = b * b - 4.0f * a * c;
                if (discriminant >= 0.0f) {
                    m_selectedMeshIndex = static_cast<uint32_t>(m_currentMesh);
                    UZ_LOG(App, Info, "Selected mesh: {}", kMeshNames[m_currentMesh]);
                }
            }
        }
    }

    // Frustum culling toggle (Ctrl+F) — skips VFX dispatch/render when emitter is off-screen
    if (input->IsKeyJustPressed(Key::F) &&
        (input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl))) {
        m_frustumCullingEnabled = !m_frustumCullingEnabled;
        UZ_LOG(App, Info, "[Ctrl+F] VFX frustum culling: {} (culled frames so far: {})",
               m_frustumCullingEnabled ? "ON" : "OFF", m_culledFrameCount);
    }

    // Help overlay (H key) — prints keyboard shortcut reference to console
    if (input->IsKeyJustPressed(Key::H)) {
        UZ_LOG(App, Info, "=== VFX Viewport Controls ===");
        UZ_LOG(App, Info, "  WASD / Space / Ctrl  — Camera movement (Shift = sprint)");
        UZ_LOG(App, Info, "  RMB + Mouse          — Mouse look (yaw/pitch)");
        UZ_LOG(App, Info, "  MMB + Mouse          — Orbit camera");
        UZ_LOG(App, Info, "  Scroll               — Dolly zoom");
        UZ_LOG(App, Info, "  Alt + LMB + Mouse    — Rotate object");
        UZ_LOG(App, Info, "  LMB (click mesh)     — Select mesh");
        UZ_LOG(App, Info, "  Escape               — Deselect");
        UZ_LOG(App, Info, "  W (when selected)    — Gizmo: Translate mode");
        UZ_LOG(App, Info, "  E (when selected)    — Gizmo: Rotate mode");
        UZ_LOG(App, Info, "  R (when selected)    — Gizmo: Scale mode");
        UZ_LOG(App, Info, "  O                    — Toggle outline");
        UZ_LOG(App, Info, "  Ctrl + drag          — Snap gizmo");
        UZ_LOG(App, Info, "  Left/Right Arrows    — Cycle material presets");
        UZ_LOG(App, Info, "  Up/Down Arrows       — Cycle mesh types");
        UZ_LOG(App, Info, "  T / Shift+T          — Sun elevation +/- 5 deg");
        UZ_LOG(App, Info, "  R (no selection)     — Environment rotation +/- 15 deg");
        UZ_LOG(App, Info, "  L                    — Toggle point lights");
        UZ_LOG(App, Info, "  B                    — Toggle bloom");
        UZ_LOG(App, Info, "  C                    — Cycle background (Sky/Gray/Black/White)");
        UZ_LOG(App, Info, "  V                    — Toggle vignette");
        UZ_LOG(App, Info, "  P                    — Toggle post-processing (ACES/linear)");
        UZ_LOG(App, Info, "  G                    — Toggle shadow debug overlay");
        UZ_LOG(App, Info, "  Tab                  — Cycle VFX presets (Shift+Tab = toggle)");
        UZ_LOG(App, Info, "  Ctrl+Tab             — Cycle compound presets (Single/Fire/Explosion/Magic)");
        UZ_LOG(App, Info, "  Ctrl+1..8            — Select emitter (multi-emitter mode)");
        UZ_LOG(App, Info, "  Delete               — Reset particles + fire burst");
        UZ_LOG(App, Info, "  Insert               — Toggle Burst / Continuous mode");
        UZ_LOG(App, Info, "  E (no selection)     — Toggle sub-emitter (secondary sparks burst after {:.2f}s)",
               kSubEmitterDelay);
        UZ_LOG(App, Info, "  M                    — Cycle VFX material presets");
        UZ_LOG(App, Info, "  Fireball             — Use toolbar [Fireball] button or File menu");
        UZ_LOG(App, Info, "  Y                    — Toggle trails");
        UZ_LOG(App, Info, "  D                    — Toggle decals");
        UZ_LOG(App, Info, "  X                    — Toggle distortion");
        UZ_LOG(App, Info, "  J                    — Toggle lit particles (NdotL + ambient)");
        UZ_LOG(App, Info, "  Ctrl+F               — Toggle VFX frustum culling (sphere-frustum test)");
        UZ_LOG(App, Info, "  --- VFX Controls (number keys) ---");
        UZ_LOG(App, Info, "  1/2                  — Spawn rate ↑/↓");
        UZ_LOG(App, Info, "  3/4                  — Particle size ↑/↓");
        UZ_LOG(App, Info, "  5/6                  — Gravity Z ↑/↓");
        UZ_LOG(App, Info, "  7/8                  — Velocity ↑/↓");
        UZ_LOG(App, Info, "  9/0                  — Spread angle ↑/↓");
        UZ_LOG(App, Info, "  Shift+1/2            — Lifetime ↑/↓");
        UZ_LOG(App, Info, "  Shift+3/4            — Emissive ↑/↓");
        UZ_LOG(App, Info, "  Shift+5/6            — Damping ↑/↓");
        UZ_LOG(App, Info, "  Shift+7/8            — Spawn radius ↑/↓");
        UZ_LOG(App, Info, "  F1-F8                — Debug view modes");
        UZ_LOG(App, Info, "  F5                   — Screenshot");
        UZ_LOG(App, Info, "  F10                  — VFX parameter panel (emitter config editor)");
        UZ_LOG(App, Info, "  Q                    — VFX module stack panel (module visibility + toggle)");
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
            UZ_LOG(App, Warning, "RenderDoc: not loaded (install RenderDoc and relaunch)");
        }
    }

    // Save current preset to JSON (Shift+S)
    // Generates a timestamped filename and writes to content/vfx/presets/.
    // After saving, rescans the directory so the new file appears in Tab cycling.
    const bool shiftHeld = input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift);
    if (shiftHeld && input->IsKeyJustPressed(Key::S)) {
        std::time_t now = std::time(nullptr);
        struct tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &now);
#else
        localtime_r(&now, &tm_buf);
#endif
        char nameBuf[64];
        std::strftime(nameBuf, sizeof(nameBuf), "preset_%Y%m%d_%H%M%S", &tm_buf);

        const std::string filePath = std::string("content/vfx/presets/") + nameBuf + ".json";
        if (VFXPresetSerializer::Save(ActiveEmitter().config, ActiveEmitter().spawnRate, nameBuf, filePath)) {
            UZ_LOG(App, Info, "VFX: Saved preset '{}'", filePath.c_str());
            // Rescan so the new file is immediately available in Tab cycling.
            RefreshPresetList("content/vfx/presets/");
            m_saveHintTimer = 2.0f;
        } else {
            UZ_LOG(App, Warning, "VFX: Failed to save preset to '{}'", filePath.c_str());
        }
    }

    // Preset directory refresh (F6)
    // Rescans content/vfx/presets/ and updates m_presetFiles/m_presetNames.
    if (input->IsKeyJustPressed(Key::F6)) {
        RefreshPresetList("content/vfx/presets/");
        UZ_LOG(App, Info, "VFX: Presets refreshed — {} found",
               static_cast<int>(m_presetFiles.size()));
    }

    // Delete confirmation for the currently-selected file preset (Delete key).
    // First press shows a log prompt; second press within 2 seconds deletes it.
    if (input->IsKeyJustPressed(Key::Delete) && !m_presetFiles.empty() &&
        m_vfxPresetIndex >= 0 && m_vfxPresetIndex < static_cast<int>(m_presetFiles.size())) {
        if (m_deleteConfirmPending && m_deleteConfirmIdx == m_vfxPresetIndex &&
            m_deleteConfirmTimer > 0.0f) {
            // Second press — perform deletion.
            const std::string path = m_presetFiles[m_vfxPresetIndex];
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (!ec) {
                UZ_LOG(App, Info, "VFX: Deleted preset '{}'", path.c_str());
            } else {
                UZ_LOG(App, Warning, "VFX: Failed to delete '{}': {}",
                       path.c_str(), ec.message().c_str());
            }
            RefreshPresetList("content/vfx/presets/");
            // Clamp index after deletion.
            if (!m_presetFiles.empty()) {
                m_vfxPresetIndex = std::min(m_vfxPresetIndex,
                                            static_cast<int>(m_presetFiles.size()) - 1);
            } else {
                m_vfxPresetIndex = 0;
            }
            m_deleteConfirmPending = false;
            m_deleteConfirmIdx     = -1;
        } else {
            // First press — start confirmation window.
            m_deleteConfirmPending = true;
            m_deleteConfirmIdx     = m_vfxPresetIndex;
            m_deleteConfirmTimer   = 2.0f;
            UZ_LOG(App, Info, "VFX: Delete '{}'? Press Del again within 2 seconds to confirm",
                   m_presetNames[m_vfxPresetIndex].c_str());
        }
    }

    // Tick save hint and delete confirm countdowns (using dt from HandleInput's caller).
    // dt is available because HandleInput receives it as a parameter.
    if (m_saveHintTimer > 0.0f) {
        m_saveHintTimer = std::max(0.0f, m_saveHintTimer - dt);
    }
    if (m_deleteConfirmTimer > 0.0f) {
        m_deleteConfirmTimer = std::max(0.0f, m_deleteConfirmTimer - dt);
        if (m_deleteConfirmTimer <= 0.0f) {
            m_deleteConfirmPending = false;
            m_deleteConfirmIdx     = -1;
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
    GetCameraController().Update(GetCamera(), *GetInput(), dt);

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

    // Point lights: inline CB fields removed.


    auto perFrameAlloc = cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));
    const uint64_t perFrameGpu = perFrameAlloc.gpuAddr;

    // ── Fireball projectile update ─────────────────────────────────────
    if (m_fireballActive) {
        m_fireballPos = m_fireballPos + m_fireballVel * dt;
        m_fireballLife += dt;
        if (m_fireballLife >= kFireballMaxLife) {
            m_fireballActive = false;
            UZ_LOG(App, Info, "[VFX] Fireball expired");
        }
    }

    // ── VFX frustum culling (sphere-frustum test in clip space) ──────────
    // Conservative bounding sphere centred on the emitter origin with a radius
    // large enough to contain the maximum particle travel distance. We project
    // the sphere centre into clip space and check whether it overlaps NDC.
    bool vfxCulled = false;
    if (m_frustumCullingEnabled && m_vfxEnabled) {
        const Vec3 emitterPos = m_fireballActive
            ? m_fireballPos
            : Vec3(m_meshTransforms[m_currentMesh].position.x,
                   m_meshTransforms[m_currentMesh].position.y,
                   m_meshTransforms[m_currentMesh].position.z + 3.0f);

        constexpr float kMaxParticleRadius = 30.0f;
        const Vec4 clipPos = viewProj * Vec4(emitterPos.x, emitterPos.y, emitterPos.z, 1.0f);

        if (clipPos.w < -kMaxParticleRadius) {
            vfxCulled = true;
        } else if (clipPos.w > 0.0f) {
            const float rcpW      = 1.0f / clipPos.w;
            const float ndcX      = clipPos.x * rcpW;
            const float ndcY      = clipPos.y * rcpW;
            const float ndcRadius = kMaxParticleRadius * rcpW;
            if (ndcX < -1.0f - ndcRadius || ndcX > 1.0f + ndcRadius ||
                ndcY < -1.0f - ndcRadius || ndcY > 1.0f + ndcRadius) {
                vfxCulled = true;
            }
        }

        if (vfxCulled) {
            ++m_culledFrameCount;
            UZ_LOG(App, Trace, "[VFX] Frustum-culled (frame {})", m_culledFrameCount);
        }
    }

    // ── VFX compute dispatch (per-emitter loop) ─────────────────────────
    if (m_vfxEnabled && !vfxCulled) {
        // Compute the shared emitter origin — fireball or mesh position
        Vec3 baseOrigin;
        if (m_fireballActive) {
            baseOrigin = m_fireballPos;
        } else {
            const auto& meshPos = m_meshTransforms[m_currentMesh].position;
            baseOrigin = Vec3(meshPos.x, meshPos.y, meshPos.z + 3.0f);
        }

        // Build wind zone configuration from current mode (shared across all emitters).
        // @ref Stam 1999 "Stable Fluids" -- wind zone accumulation
        m_windZoneCount = 0u;
        if (m_windMode == 1) {
            m_windZones[0].position[0]   = 0.0f; m_windZones[0].position[1] = 0.0f; m_windZones[0].position[2] = 0.0f;
            m_windZones[0].radius        = 0.0f;
            m_windZones[0].direction[0]  = 1.0f; m_windZones[0].direction[1] = 0.0f; m_windZones[0].direction[2] = 0.0f;
            m_windZones[0].strength      = 2.0f;
            m_windZones[0].gustFrequency = 0.3f;
            m_windZones[0].gustAmplitude = 3.0f;
            m_windZoneCount = 1u;
        } else if (m_windMode == 2) {
            m_windZones[0].position[0]   = 0.0f; m_windZones[0].position[1] = 0.0f; m_windZones[0].position[2] = 0.0f;
            m_windZones[0].radius        = 0.0f;
            m_windZones[0].direction[0]  = 0.707f; m_windZones[0].direction[1] = 0.0f; m_windZones[0].direction[2] = 0.707f;
            m_windZones[0].strength      = 1.0f;
            m_windZones[0].gustFrequency = 0.1f;
            m_windZones[0].gustAmplitude = 0.5f;
            m_windZones[1].position[0]   = baseOrigin.x;
            m_windZones[1].position[1]   = baseOrigin.y;
            m_windZones[1].position[2]   = baseOrigin.z;
            m_windZones[1].radius        = 5.0f;
            m_windZones[1].direction[0]  = 1.0f; m_windZones[1].direction[1] = 0.2f; m_windZones[1].direction[2] = 0.0f;
            m_windZones[1].strength      = 8.0f;
            m_windZones[1].gustFrequency = 1.0f;
            m_windZones[1].gustAmplitude = 4.0f;
            m_windZoneCount = 2u;
        }

        // Build kill volume configuration from current mode (shared across all emitters).
        // @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
        m_killVolumeCount = 0u;
        if (m_killVolumeMode == 1) {
            m_killVolumes[0].position[0]   = baseOrigin.x + 3.0f;
            m_killVolumes[0].position[1]   = baseOrigin.y;
            m_killVolumes[0].position[2]   = baseOrigin.z;
            m_killVolumes[0].shape         = static_cast<uint32_t>(KillVolumeShape::AABB);
            m_killVolumes[0].halfExtents[0] = 0.3f;
            m_killVolumes[0].halfExtents[1] = 5.0f;
            m_killVolumes[0].halfExtents[2] = 5.0f;
            m_killVolumeCount = 1u;
        } else if (m_killVolumeMode == 2) {
            m_killVolumes[0].position[0]   = baseOrigin.x;
            m_killVolumes[0].position[1]   = baseOrigin.y - 2.0f;
            m_killVolumes[0].position[2]   = baseOrigin.z;
            m_killVolumes[0].shape         = static_cast<uint32_t>(KillVolumeShape::AABB);
            m_killVolumes[0].halfExtents[0] = 10.0f;
            m_killVolumes[0].halfExtents[1] = 0.5f;
            m_killVolumes[0].halfExtents[2] = 10.0f;
            m_killVolumeCount = 1u;
        } else if (m_killVolumeMode == 3) {
            m_killVolumes[0].position[0]   = baseOrigin.x;
            m_killVolumes[0].position[1]   = baseOrigin.y;
            m_killVolumes[0].position[2]   = baseOrigin.z + 2.0f;
            m_killVolumes[0].shape         = static_cast<uint32_t>(KillVolumeShape::Sphere);
            m_killVolumes[0].halfExtents[0] = 1.5f;
            m_killVolumes[0].halfExtents[1] = 1.5f;
            m_killVolumes[0].halfExtents[2] = 1.5f;
            m_killVolumeCount = 1u;
        }

        // Dispatch each emitter independently
        for (auto& emitter : m_emitters) {
            if (!emitter.enabled) continue;

            // ── Emitter lifecycle: duration and loop count ─────────────────────
            // When emitterDuration > 0 the emitter accumulates age each frame and
            // either restarts (loopCount==0 or currentLoop < loopCount) or disables
            // itself permanently when the loop budget is exhausted.
            //
            // loopCount == 0  → infinite restarts (run until manually disabled).
            // loopCount >  0  → restart at most loopCount times then stop.
            //
            // @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine" — emitter lifetime
            if (emitter.config.emitterDuration > 0.0f) {
                emitter.emitterAge += dt;
                if (emitter.emitterAge >= emitter.config.emitterDuration) {
                    // Age exceeded duration — decide whether to loop or stop.
                    bool canLoop = (emitter.config.loopCount == 0)
                                || (emitter.currentLoop < emitter.config.loopCount);
                    if (canLoop) {
                        // Restart: reset age, increment loop counter, arm burst.
                        emitter.emitterAge   = 0.0f;
                        emitter.currentLoop += 1;
                        emitter.burstPending = true;
                        emitter.spawnAccum   = 0.0f;
                        // Re-upload free list so the pool is clean for the new loop.
                        emitter.system->UploadInitialFreeList(cmd);
                        UZ_LOG(App, Info,
                            "[VFX] Emitter '{}' restarted (loop {}/{})",
                            emitter.name, emitter.currentLoop,
                            emitter.config.loopCount);
                    } else {
                        // Budget exhausted — disable permanently until manual reset.
                        emitter.enabled = false;
                        UZ_LOG(App, Info,
                            "[VFX] Emitter '{}' expired after {} loop(s), disabling.",
                            emitter.name, emitter.currentLoop);
                        continue;
                    }
                }
            }

            // Update emitter position: base origin + per-emitter offset
            emitter.config.position[0] = baseOrigin.x + emitter.positionOffset.x;
            emitter.config.position[1] = baseOrigin.y + emitter.positionOffset.y;
            emitter.config.position[2] = baseOrigin.z + emitter.positionOffset.z;

            // Compute emitter velocity for velocity inheritance (position delta / dt).
            // On the first frame (prevPositionValid == false), velocity is zero to
            // avoid a huge spike from the initial position jump.
            if (emitter.prevPositionValid && dt > 0.0f) {
                emitter.config.emitterVelocity[0] = (emitter.config.position[0] - emitter.prevPositionX) / dt;
                emitter.config.emitterVelocity[1] = (emitter.config.position[1] - emitter.prevPositionY) / dt;
                emitter.config.emitterVelocity[2] = (emitter.config.position[2] - emitter.prevPositionZ) / dt;
            } else {
                emitter.config.emitterVelocity[0] = 0.0f;
                emitter.config.emitterVelocity[1] = 0.0f;
                emitter.config.emitterVelocity[2] = 0.0f;
            }
            emitter.prevPositionX     = emitter.config.position[0];
            emitter.prevPositionY     = emitter.config.position[1];
            emitter.prevPositionZ     = emitter.config.position[2];
            emitter.prevPositionValid = true;

            // Apply vortex force configuration from current mode
            // @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
            if (m_vortexMode == 0) {
                emitter.config.vortexTangentialStrength = 0.0f;
                emitter.config.vortexRadialStrength     = 0.0f;
            } else if (m_vortexMode == 1) {
                emitter.config.vortexAxisOrigin[0] = emitter.config.position[0];
                emitter.config.vortexAxisOrigin[1] = emitter.config.position[1];
                emitter.config.vortexAxisOrigin[2] = emitter.config.position[2];
                emitter.config.vortexAxisDir[0] = 0.0f;
                emitter.config.vortexAxisDir[1] = 0.0f;
                emitter.config.vortexAxisDir[2] = 1.0f;
                emitter.config.vortexTangentialStrength = 8.0f;
                emitter.config.vortexRadialStrength     = 2.0f;
                emitter.config.vortexMaxRadius          = 3.0f;
            } else {
                emitter.config.vortexAxisOrigin[0] = emitter.config.position[0];
                emitter.config.vortexAxisOrigin[1] = emitter.config.position[1];
                emitter.config.vortexAxisOrigin[2] = emitter.config.position[2];
                emitter.config.vortexAxisDir[0] = 0.0f;
                emitter.config.vortexAxisDir[1] = 0.0f;
                emitter.config.vortexAxisDir[2] = 1.0f;
                emitter.config.vortexTangentialStrength = 5.0f;
                emitter.config.vortexRadialStrength     = -1.0f;
                emitter.config.vortexMaxRadius          = 4.0f;
            }

            // Calculate spawn count — burst or continuous
            uint32_t spawnCount = 0;
            if (emitter.burstMode) {
                if (emitter.burstPending) {
                    spawnCount = emitter.burstCount;
                    emitter.burstPending = false;
                }
            } else {
                emitter.spawnAccum += emitter.spawnRate * dt;
                spawnCount = static_cast<uint32_t>(emitter.spawnAccum);
                emitter.spawnAccum -= static_cast<float>(spawnCount);
            }

            // Upload wind zones and kill volumes to GPU for this emitter
            emitter.system->UploadWindZones(cmd, cbRing, m_windZones, m_windZoneCount);
            emitter.system->UploadKillVolumes(cmd, cbRing, m_killVolumes, m_killVolumeCount);

            emitter.system->DispatchCompute(cmd, cbRing, emitter.config, spawnCount,
                                           dt, GetFrameIndex(), m_totalTime);
        }

        // ── Sub-emitter secondary burst (Task 3.87.0) ───────────────────
        // Count down the sub-emitter timer. When it fires, dispatch a second
        // DispatchCompute on the first emitter's system using the sparks config.
        if (m_subEmitterTimer > 0.0f) {
            m_subEmitterTimer -= dt;
            if (m_subEmitterTimer <= 0.0f) {
                m_subEmitterTimer = -1.0f;

                VFXEmitterConfig subCfg = m_subEmitterConfig;
                subCfg.position[0] = baseOrigin.x;
                subCfg.position[1] = baseOrigin.y;
                subCfg.position[2] = baseOrigin.z;

                if (!m_emitters.empty()) {
                    m_emitters[0].system->DispatchCompute(cmd, cbRing, subCfg,
                                                         m_subEmitterCount,
                                                         dt, GetFrameIndex(), m_totalTime);
                }
                UZ_LOG(App, Info, "[VFX] Sub-emitter fired {} sparks at ({:.1f},{:.1f},{:.1f})",
                       m_subEmitterCount,
                       subCfg.position[0], subCfg.position[1], subCfg.position[2]);
            }
        }
    }

    // ── Shadow pass ──────────────────────────────────────────────────────
    if (m_meshVisible) {
        RenderShadowPass(cmd);
    }

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
    if (m_meshVisible) {
        RenderScenePass(cmd, perFrameGpu);
    }

    // ── Copy HDR for grid ground reflection ────────────────────────────
    // The grid shader uses the pre-grid HDR content for screen-space
    // ground reflection. Copy before the grid render pass begins.
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

            uint32_t depthSrv = GetDevice()->GetTextureSRVIndex(m_depthBuffer);
            grid->Draw(cmd, *cbRing, cam, depthSrv,
                       static_cast<float>(GetWidth()),
                       static_cast<float>(GetHeight()));

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
    // Skip render (and ping-pong flip) when the emitter was frustum-culled.
    // FlipPingPong is intentionally omitted for culled frames: the GPU
    // simulation was also skipped, so the live particle buffer is unchanged
    // and the previous frame's read/write assignment remains correct.
    // ── Update module stack kill-volume count (needed for isActive logic) ───
    m_moduleStack.SetKillVolumeCount(m_killVolumeCount);

    if (m_vfxEnabled && !vfxCulled) {
        RenderVFXPass(cmd, perFrameGpu, dt);
        for (auto& e : m_emitters) {
            if (e.enabled) e.system->FlipPingPong();
        }
    }

    // ── Trail ribbon update + rendering ──────────────────────────────────
    if (m_trailEnabled && m_trailHandle != TrailSystem::kInvalidHandle) {
        // Trail follows fireball when active, otherwise orbits the emitter
        Vec3 trailPos;
        if (m_fireballActive) {
            trailPos = m_fireballPos;
        } else {
            constexpr float kOrbitRadius = 1.5f;
            constexpr float kOrbitSpeed  = 2.0f;
            const auto& aePos = ActiveEmitter().config.position;
            trailPos = Vec3(
                aePos[0] + kOrbitRadius * std::cos(m_totalTime * kOrbitSpeed),
                aePos[1] + 0.5f,
                aePos[2] + kOrbitRadius * std::sin(m_totalTime * kOrbitSpeed)
            );
        }

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

    // ── Outline pass (selection highlight) ──────────────────────────────
    // The outline pass needs a CBAllocation (not just a GPU address) because
    // it uses the 4-arg SetRootCBV overload for DX11 compatibility.
    {
        auto outlinePerFrameAlloc = cbRing->Allocate(sizeof(PerFrameCB));
        std::memcpy(outlinePerFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));
        RenderOutlinePass(cmd, outlinePerFrameAlloc);
    }

    // ── Gizmo pass (transform gizmo overlay) ────────────────────────────
    RenderGizmoPass(cmd);

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

    // ── VFX editor UI overlay ───────────────────────────────────────────
    {
        auto* swapChain = GetSwapChain();
        if (swapChain) {
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

            m_editorUI.BeginFrame(GetFrameIndex());
            {
                // Update emitter names for the selector bar
                std::vector<std::string> emitterNames;
                emitterNames.reserve(m_emitters.size());
                for (const auto& e : m_emitters) emitterNames.push_back(e.name);
                m_editorUI.SetEmitterNames(emitterNames, m_activeEmitterIndex);

                auto& ae = ActiveEmitter();
                m_editorUI.BuildUI(&ae.config, &ae.spawnRate, &ae.burstMode,
                                   &ae.burstCount, &m_vfxEnabled);

                // Handle emitter selection from UI tab bar
                const int selIdx = m_editorUI.GetSelectedEmitterIndex();
                if (selIdx >= 0 && selIdx < static_cast<int>(m_emitters.size())) {
                    m_activeEmitterIndex = selIdx;
                    auto& newAe = ActiveEmitter();
                    m_paramPanel.Init(&newAe.config, &newAe.spawnRate, &newAe.burstCount,
                                      &newAe.burstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
                    m_moduleStack.Init(&newAe.config, &m_subEmitterEnabled, &m_frustumCullingEnabled);
                }
            }
            m_editorUI.Render(cmd, *cbRing);

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
        cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

        if (IsBindless()) {
            auto* device = GetDevice();
            uint32_t shadowSrvIdx = device->GetTextureSRVIndex(m_shadowMap);
            cmd->SetRootConstants(0, &shadowSrvIdx, 1);
        }

        cmd->Draw(3, 1, 0, 0);
        cmd->EndRenderPass();
    }

    // ── Screenshot capture ───────────────────────────────────────────────
    if (ShouldCaptureScreenshot()) {
        UZ_LOG(App, Info, "Screenshot: capturing frame {}", GetFrameIndex());
        auto* capture = GetScreenshotCapture();
        auto* swapChain = GetSwapChain();
        if (capture && swapChain) {
            capture->CaptureFromPresent(cmd, swapChain);
            SetScreenshotSaveDeferred();
            UZ_LOG(App, Info, "Screenshot: deferred save set");
        } else {
            UZ_LOG(App, Warning, "Screenshot: capture={} swapChain={} — skipping",
                   capture != nullptr, swapChain != nullptr);
        }
    }

    // Transition backbuffer from RenderTarget → Present for DXGI Present()
    {
        auto* swapChain = GetSwapChain();
        auto backBuffer = swapChain->GetCurrentBackBuffer();
        RHIBarrierDesc barrier{};
        barrier.texture = backBuffer;
        barrier.stateBefore = RHIResourceState::RenderTarget;
        barrier.stateAfter = RHIResourceState::Present;
        cmd->ResourceBarrier(&barrier, 1);
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

        // Upload shadow view-projection as a root CBV at slot 3 (b4)
        struct ShadowCB { Mat4 viewProj; };
        ShadowCB shadowCB{};
        shadowCB.viewProj = shadowViewProj;
        auto shadowAlloc = cbRing->Allocate(sizeof(ShadowCB));
        std::memcpy(shadowAlloc.cpuPtr, &shadowCB, sizeof(ShadowCB));
        static constexpr uint32_t kSlotShadow = 3; // root param 3 = CBV at b4
        cmd->SetRootCBV(kSlotShadow, shadowAlloc.gpuAddr);

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

        // Draw current mesh (using per-mesh transform)
        uploadShadowPerObject(BuildMeshWorldMatrix(static_cast<uint32_t>(m_currentMesh)));
        const auto& mesh = m_meshes[m_currentMesh];
        if (mesh.vertexBuffer.IsValid() && mesh.indexBuffer.IsValid()) {
            if (IsBindless()) {
                cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
            }
            cmd->SetVertexBuffer(0, mesh.vertexBuffer);
            cmd->SetIndexBuffer(mesh.indexBuffer, RHIIndexFormat::Uint32);
            cmd->DrawIndexed(mesh.indexCount);
        }

        // ── Draw ground plane into shadow map ──
        const auto& planeMesh = m_meshes[2]; // index 2 = plane
        if (planeMesh.vertexBuffer.IsValid() && planeMesh.indexBuffer.IsValid()) {
            if (IsBindless()) {
                cmd->SetDescriptorIndex(kSlotRootConstants, planeMesh.vbSrvIndex);
            }
            cmd->SetVertexBuffer(0, planeMesh.vertexBuffer);
            cmd->SetIndexBuffer(planeMesh.indexBuffer, RHIIndexFormat::Uint32);
            uploadShadowPerObject(BuildMeshWorldMatrix(2));
            cmd->DrawIndexed(planeMesh.indexCount);
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
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

    // ── Set SkyIndices root constants (b0) — depth + LUT bindless indices ──
    // The sky shader reads depth from ResourceDescriptorHeap[depthIdx] to skip
    // geometry pixels. Dummy 1x1 LUTs provide neutral values for the ray march
    // fallback (transmittance=1, multi-scatter=0). scatteringLut3DIdx=0xFFFFFFFF
    // triggers the ray march path instead of the precomputed 3D LUT path.
    {
        const uint32_t depthSrv = GetDevice()->GetTextureSRVIndex(m_depthBuffer);
        const uint32_t transmittanceSrv = GetDevice()->GetTextureSRVIndex(m_dummyTransmittanceLut);
        const uint32_t multiScatterSrv  = GetDevice()->GetTextureSRVIndex(m_dummyMultiScatterLut);
        const uint32_t skyIndices[6] = {
            depthSrv,           // depthIdx
            transmittanceSrv,   // transmittanceLutIdx (1x1 white = no extinction)
            multiScatterSrv,    // multiScatteringLutIdx (1x1 black = no multi-scatter)
            UINT32_MAX,         // apVolumeSrvIdx (none)
            UINT32_MAX,         // scatteringLut3DIdx (none → triggers ray march fallback)
            UINT32_MAX          // irradianceLutIdx (none)
        };
        cmd->SetRootConstants(kSlotRootConstants, skyIndices, 6);
    }

    // ── Fill SkyAtmosphereCB with Earth-default atmosphere parameters (b2) ──
    // Values from SkyAtmosphereComponent defaults and atmosphere_renderer.cpp
    // ozone tent calculation.
    {
        SkyAtmosphereCB skyCB{};

        // Rayleigh: xyz = scattering coefficients (km^-1), w = scale height (km)
        skyCB.rayleighScattering = {5.802e-3f, 13.558e-3f, 33.1e-3f, 8.0f};

        // Mie: x = coefficient (km^-1), y = anisotropy g, z = scale height (km)
        skyCB.mieScattering = {3.996e-3f, 0.8f, 1.2f, 0.0f};

        // Ground reflectance
        skyCB.groundAlbedo = {0.4f, 0.4f, 0.4f, 0.0f};

        // Sun: xyz = illuminance color, w = angular radius (rad)
        skyCB.sunIlluminance = {20.0f, 20.0f, 20.0f, 0.00872665f};

        skyCB.bottomRadiusKm       = 6360.0f;
        skyCB.atmosphereHeightKm   = 60.0f;
        skyCB.sunDiskIntensity     = 1.0f;
        skyCB.multiScatteringFactor = 1.0f;
        skyCB.rayleighScatteringScale = 1.0f;
        skyCB.mieScatteringScale   = 1.0f;
        skyCB.mieAbsorptionScale   = 1.0f;
        skyCB.mieAbsorption        = 4.44e-4f;
        skyCB.skyLuminanceFactor   = {1.0f, 1.0f, 1.0f, 1.0f};

        // Ozone tent distribution — piecewise-linear coefficients
        // from atmosphere_renderer.cpp: tipAltitude=25km, tipValue=1.0, width=15km
        constexpr float kOzoneTipAlt = 25.0f;
        constexpr float kOzoneTipVal = 1.0f;
        constexpr float kOzoneWidth  = 15.0f;
        constexpr float kOzoneSlope  = kOzoneTipVal / kOzoneWidth; // 1/15 = 0.06667

        skyCB.ozoneAbsorption = {0.000650f, 0.001881f, 0.000085f, kOzoneTipAlt};
        skyCB.ozoneLinTerm0   =  kOzoneSlope;                              //  1/15
        skyCB.ozoneConstTerm0 = kOzoneTipVal - kOzoneTipAlt * kOzoneSlope; // 1 - 25/15 = -0.6667
        skyCB.ozoneLinTerm1   = -kOzoneSlope;                              // -1/15
        skyCB.ozoneConstTerm1 = kOzoneTipVal - kOzoneTipAlt * (-kOzoneSlope); // 1 + 25/15 = 2.6667

        auto* cbRing = GetCBRing();
        auto skyAlloc = cbRing->Allocate(sizeof(SkyAtmosphereCB));
        std::memcpy(skyAlloc.cpuPtr, &skyCB, sizeof(SkyAtmosphereCB));
        cmd->SetRootCBV(kSlotPerObject, skyAlloc.gpuAddr);
    }

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

    // Object transform — from per-mesh transform + optional manual rotation (Alt+LMB)
    constexpr float kDeg2Rad = 3.14159265f / 180.0f;
    const Mat4 manualRotY = Mat4::RotationY(m_objectYaw * kDeg2Rad);
    const Mat4 manualRotX = Mat4::RotationX(m_objectPitch * kDeg2Rad);
    const Mat4 manualRotation = manualRotX * manualRotY;
    const Mat4 meshWorld = BuildMeshWorldMatrix(static_cast<uint32_t>(m_currentMesh));
    perObj.model        = manualRotation * meshWorld;
    const Mat4 meshRot = m_meshTransforms[m_currentMesh].rotation.ToMatrix();
    perObj.normalMatrix = manualRotation * meshRot; // Rotation-only for normals

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
        if (IsBindless()) {
            cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
        }
        cmd->SetVertexBuffer(0, mesh.vertexBuffer);
        cmd->SetIndexBuffer(mesh.indexBuffer, RHIIndexFormat::Uint32);
        cmd->DrawIndexed(mesh.indexCount);
    }

    // ── Draw ground plane ──
    {
        const auto& planeMesh = m_meshes[2]; // index 2 = plane
        if (planeMesh.vertexBuffer.IsValid() && planeMesh.indexBuffer.IsValid()) {
            if (IsBindless()) {
                cmd->SetDescriptorIndex(kSlotRootConstants, planeMesh.vbSrvIndex);
            }
            cmd->SetVertexBuffer(0, planeMesh.vertexBuffer);
            cmd->SetIndexBuffer(planeMesh.indexBuffer, RHIIndexFormat::Uint32);

            PerObjectCB groundObj{};
            groundObj.albedoIndex    = 0;
            groundObj.normalIndex    = 0;
            groundObj.roughnessIndex = 0;
            groundObj.metallicIndex  = 0;
            groundObj.aoIndex        = 0;
            groundObj.emissiveIndex  = 0;

            groundObj.model        = BuildMeshWorldMatrix(2);
            groundObj.normalMatrix = m_meshTransforms[2].rotation.ToMatrix(); // Rotation only for normals

            groundObj.baseColorTintR      = 0.15f;
            groundObj.baseColorTintG      = 0.15f;
            groundObj.baseColorTintB      = 0.15f;
            groundObj.roughnessMultiplier = 0.95f;
            groundObj.metallicMultiplier  = 0.0f;
            groundObj.emissiveColorR      = 0.0f;
            groundObj.emissiveColorG      = 0.0f;
            groundObj.emissiveColorB      = 0.0f;
            groundObj.emissiveIntensity   = 0.0f;

            auto groundAlloc = cbRing->Allocate(sizeof(PerObjectCB));
            std::memcpy(groundAlloc.cpuPtr, &groundObj, sizeof(PerObjectCB));
            cmd->SetRootCBV(kSlotPerObject, groundAlloc.gpuAddr);

            cmd->DrawIndexed(planeMesh.indexCount);
        }
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// VFX Billboard Rendering
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderVFXPass(IRHICommandList* cmd, uint64_t perFrameGpu,
                                      float /*dt*/) {
    // Early-out if no emitters have a valid billboard PSO
    bool anyValid = false;
    for (const auto& e : m_emitters) {
        if (e.enabled && e.system->GetBillboardPSO().IsValid()) {
            anyValid = true;
            break;
        }
    }
    if (!anyValid) return;

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

    const uint32_t depthSrvIdx = GetDevice()->GetTextureSRVIndex(m_depthBuffer);

    // Use the active preset material's blend mode
    const auto& mat = m_vfxMaterials[m_currentMaterialIndex];
    const uint32_t blendMode = mat.blendMode;

    // Draw each emitter's particles
    for (auto& emitter : m_emitters) {
        if (!emitter.enabled) continue;
        if (!emitter.system->GetBillboardPSO().IsValid()) continue;

        const Vec3 emitterPos(emitter.config.position[0],
                               emitter.config.position[1],
                               emitter.config.position[2]);

        emitter.system->DrawBillboards(cmd, GetCBRing(), perFrameGpu,
                                       GetCamera().GetView(), emitterPos,
                                       m_prevViewProj,
                                       emitter.config,
                                       blendMode,
                                       depthSrvIdx);
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Trail Ribbon Rendering
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderTrailPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    if (!m_trailPSO.IsValid()) return;
    if (!m_trailVB.IsValid() || !m_trailIB.IsValid()) return;
    if (m_trailVBSrvIndex == UINT32_MAX || m_trailIBSrvIndex == UINT32_MAX) return;

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

        // Upload vertices to the StructuredBuffer (bindless SRV)
        void* vbPtr = device->MapBuffer(m_trailVB);
        std::memcpy(vbPtr, trail.vertices, trail.vertexCount * sizeof(TrailVertex));
        device->UnmapBuffer(m_trailVB);

        // Upload indices to the StructuredBuffer<uint> (bindless SRV)
        void* ibPtr = device->MapBuffer(m_trailIB);
        std::memcpy(ibPtr, trail.indices, trail.indexCount * sizeof(uint32_t));
        device->UnmapBuffer(m_trailIB);

        // Set trail root constants at b0 (root param 0)
        // Includes bindless SRV indices for vertex/index buffers so the VS
        // can fetch vertex data via ResourceDescriptorHeap[srvIndex].
        TrailRootConstants rootConsts{};
        rootConsts.uvScrollRate         = 1.0f;
        rootConsts.softDepthRange       = 0.3f;
        rootConsts.alphaClipThreshold   = 1.0f / 255.0f; // kTrailAlphaClipMin
        rootConsts.trailTexSrvIndex     = UINT32_MAX;     // No trail texture — vertex color only
        rootConsts.depthBufferSrvIndex  = depthSrvIdx;
        rootConsts.vertexBufferSrvIndex = m_trailVBSrvIndex;
        rootConsts.indexBufferSrvIndex  = m_trailIBSrvIndex;
        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rootConsts),
                              sizeof(rootConsts) / sizeof(uint32_t));

        // Draw with bindless vertex fetch: SV_VertexID [0, indexCount) maps into
        // the index buffer SRV, which then indexes into the vertex buffer SRV.
        // No SetVertexBuffer or SetIndexBuffer needed — no IA is used.
        cmd->Draw(trail.indexCount);
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
    const Vec3 emitterWorld(ActiveEmitter().config.position[0],
                            ActiveEmitter().config.position[1],
                            ActiveEmitter().config.position[2]);
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

    // Transition backbuffer from Present → RenderTarget
    {
        RHIBarrierDesc barrier{};
        barrier.texture = backBuffer;
        barrier.stateBefore = RHIResourceState::Present;
        barrier.stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(&barrier, 1);
    }

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
    // Slot 1 = PerFrameCB (b1), Slot 0 = TonemapIndices root constants (b0)
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

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

        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&indices), 4);
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

    if (m_outlineEnabled) {
        m_outlinePass.Resize(width, height);
    }

    m_editorUI.SetScreenSize(width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::OnShutdown() {
    auto* device = GetDevice();

    m_editorUI.Shutdown();
    m_outlinePass.Shutdown();
    m_gizmoRenderer.Shutdown();
    m_bloomPass.Shutdown();
    ShutdownAllEmitters(device);

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

    // Destroy dummy sky LUT textures
    if (m_dummyTransmittanceLut.IsValid()) { device->DestroyTexture(m_dummyTransmittanceLut); m_dummyTransmittanceLut = {}; }
    if (m_dummyMultiScatterLut.IsValid()) { device->DestroyTexture(m_dummyMultiScatterLut); m_dummyMultiScatterLut = {}; }

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

// ─────────────────────────────────────────────────────────────────────────────
// Preset Management
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RefreshPresetList(const std::string& dir) {
    // Export built-in presets to JSON files (skip any that already exist so
    // user-edited presets are not overwritten on every launch).
    int builtInCount = 0;
    const VFXPreset* builtIns = GetVFXPresets(builtInCount);
    int written = 0;
    for (int i = 0; i < builtInCount; ++i) {
        // Derive a safe filename: lowercase name, spaces → underscores.
        std::string stem(builtIns[i].name);
        for (char& ch : stem) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (ch == ' ') ch = '_';
        }
        const std::string path = dir + stem + ".json";

        // Only write if the file does not already exist.
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            if (VFXPresetSerializer::Save(builtIns[i].config,
                                          builtIns[i].spawnRate,
                                          builtIns[i].name,
                                          path)) {
                ++written;
            }
        }
    }
    if (written > 0) {
        UZ_LOG(App, Info, "VFX: Wrote {} built-in preset(s) to '{}'", written, dir.c_str());
    }

    // Rescan the directory to populate m_presetFiles and m_presetNames.
    m_presetFiles = VFXPresetSerializer::ScanDirectory(dir);
    m_presetNames.clear();
    m_presetNames.reserve(m_presetFiles.size());
    for (const auto& p : m_presetFiles) {
        m_presetNames.push_back(std::filesystem::path(p).stem().string());
    }

    UZ_LOG(App, Info, "VFX: {} preset(s) available in '{}'",
           static_cast<int>(m_presetFiles.size()), dir.c_str());

    // Keep the panel's Presets browser in sync. This is a no-op before Init
    // (panel has no categories yet) because SetPresetList is also called
    // explicitly after the first RefreshPresetList() call in OnInit.
    m_paramPanel.SetPresetList(m_presetNames);
    m_editorUI.SetPresetNames(m_presetNames);
}

void VFXViewportDemo::LoadPreset(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_presetFiles.size())) {
        UZ_LOG(App, Warning, "VFX: LoadPreset({}) — index out of range ({} presets)",
               idx, static_cast<int>(m_presetFiles.size()));
        return;
    }

    auto& ae = ActiveEmitter();
    std::string name;
    float spawnRate = ae.spawnRate;
    if (!VFXPresetSerializer::Load(m_presetFiles[idx], ae.config, spawnRate, name)) {
        UZ_LOG(App, Warning, "VFX: Failed to load preset '{}'", m_presetFiles[idx].c_str());
        return;
    }
    ae.spawnRate     = spawnRate;
    m_vfxPresetIndex = idx;
    ae.burstPending  = true;
    UZ_LOG(App, Info, "VFX: Loaded preset '{}'", name.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Emitter Helpers
// ─────────────────────────────────────────────────────────────────────────────

VFXEmitterInstance& VFXViewportDemo::ActiveEmitter() {
    if (m_activeEmitterIndex < 0 || m_activeEmitterIndex >= static_cast<int>(m_emitters.size())) {
        m_activeEmitterIndex = 0;
    }
    return m_emitters[m_activeEmitterIndex];
}

const VFXEmitterInstance& VFXViewportDemo::ActiveEmitter() const {
    const int idx = (m_activeEmitterIndex >= 0 &&
                     m_activeEmitterIndex < static_cast<int>(m_emitters.size()))
                        ? m_activeEmitterIndex : 0;
    return m_emitters[idx];
}

void VFXViewportDemo::InitAllEmitters(IRHIDevice* device, ShaderManager* shaders,
                                       IRHICommandList* cmd) {
    for (auto& e : m_emitters) {
        if (!e.system->Init(device, shaders, e.poolSize)) {
            UZ_LOG(App, Error, "VFX: Failed to init emitter '{}'", e.name.c_str());
            e.enabled = false;
        } else {
            e.system->UploadInitialFreeList(cmd);
        }
    }
}

void VFXViewportDemo::ShutdownAllEmitters(IRHIDevice* device) {
    for (auto& e : m_emitters) {
        e.system->Shutdown(device);
    }
    m_emitters.clear();
}

void VFXViewportDemo::ResetAllEmitters(IRHICommandList* cmd) {
    for (auto& e : m_emitters) {
        e.system->UploadInitialFreeList(cmd);
        e.burstPending = true;
        e.spawnAccum = 0.0f;
        // Reset lifecycle counters so the emitter starts fresh.
        // @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine" — emitter lifetime
        e.emitterAge  = 0.0f;
        e.currentLoop = 0;
        e.enabled     = true;  // re-enable in case it had auto-disabled.
    }
}

void VFXViewportDemo::BuildCompoundPreset(VFXCompoundPreset preset) {
    m_emitters.clear();
    m_activeEmitterIndex = 0;

    switch (preset) {
    case VFXCompoundPreset::Single: {
        // Single emitter — backward-compatible with legacy behavior.
        // Uses the first built-in preset config and full 50K pool.
        auto& e = m_emitters.emplace_back();
        e.name = "Default";
        int presetCount = 0;
        const VFXPreset* presets = GetVFXPresets(presetCount);
        e.config = presets[0].config;
        e.spawnRate = presets[0].spawnRate;
        e.burstCount = 500;
        e.poolSize = 50000;
        break;
    }

    case VFXCompoundPreset::Fire: {
        // Flame core: warm orange upward, medium size, additive glow
        {
            auto& flame = m_emitters.emplace_back();
            flame.name = "Flame";
            flame.config = {};
            flame.config.spawnVelocity[0] = 0.0f;
            flame.config.spawnVelocity[1] = 0.0f;
            flame.config.spawnVelocity[2] = 4.0f;  // Z-up
            flame.config.velocityVariation = 1.0f;
            flame.config.spreadAngle = 0.4f;
            flame.config.minLifetime = 0.5f;
            flame.config.maxLifetime = 1.5f;
            flame.config.initialSize = 0.08f;
            flame.config.initialColor[0] = 1.0f;
            flame.config.initialColor[1] = 0.5f;
            flame.config.initialColor[2] = 0.1f;
            flame.config.initialColor[3] = 0.9f;
            flame.config.gravity[0] = 0.0f;
            flame.config.gravity[1] = 0.0f;
            flame.config.gravity[2] = 2.0f; // buoyancy
            flame.config.damping = 0.3f;
            flame.config.emissiveIntensity = 3.0f;
            flame.config.blendMode = 1; // Additive
            flame.config.sizeOverLifeStart = 0.5f;
            flame.config.sizeOverLifePeak = 1.0f;
            flame.config.sizeOverLifeEnd = 0.0f;
            flame.config.sizeOverLifePeakTime = 0.3f;
            flame.config.colorLifeStartR = 1.0f;
            flame.config.colorLifeStartG = 0.7f;
            flame.config.colorLifeStartB = 0.2f;
            flame.config.colorLifeStartA = 1.0f;
            flame.config.colorLifeEndR = 1.0f;
            flame.config.colorLifeEndG = 0.2f;
            flame.config.colorLifeEndB = 0.0f;
            flame.config.colorLifeEndA = 0.0f;
            flame.config.softFadeDistance = 0.3f;
            flame.config.particleLightEnabled = 1;
            flame.config.particleLightRadius = 800.0f;
            flame.config.particleLightIntensity = 3.0f;
            flame.burstCount = 200;
            flame.spawnRate = 400.0f;
            flame.burstMode = false; // continuous
            flame.burstPending = false;
            flame.poolSize = 10000;
        }

        // Smoke: gray, slower, bigger, alpha blend, curl noise
        {
            auto& smoke = m_emitters.emplace_back();
            smoke.name = "Smoke";
            smoke.config = {};
            smoke.config.spawnVelocity[0] = 0.0f;
            smoke.config.spawnVelocity[1] = 0.0f;
            smoke.config.spawnVelocity[2] = 1.5f;
            smoke.config.velocityVariation = 0.5f;
            smoke.config.spreadAngle = 0.6f;
            smoke.config.minLifetime = 1.5f;
            smoke.config.maxLifetime = 3.5f;
            smoke.config.initialSize = 0.15f;
            smoke.config.initialColor[0] = 0.4f;
            smoke.config.initialColor[1] = 0.4f;
            smoke.config.initialColor[2] = 0.4f;
            smoke.config.initialColor[3] = 0.5f;
            smoke.config.gravity[0] = 0.0f;
            smoke.config.gravity[1] = 0.0f;
            smoke.config.gravity[2] = 0.5f; // slight buoyancy
            smoke.config.damping = 0.5f;
            smoke.config.emissiveIntensity = 0.0f;
            smoke.config.blendMode = 0; // AlphaBlend
            smoke.config.curlStrength = 2.0f;
            smoke.config.curlFrequency = 0.3f;
            smoke.config.curlSpeed = 0.15f;
            smoke.config.sizeOverLifeStart = 0.3f;
            smoke.config.sizeOverLifePeak = 1.0f;
            smoke.config.sizeOverLifeEnd = 1.2f;
            smoke.config.sizeOverLifePeakTime = 0.5f;
            smoke.config.colorLifeStartR = 0.5f;
            smoke.config.colorLifeStartG = 0.5f;
            smoke.config.colorLifeStartB = 0.5f;
            smoke.config.colorLifeStartA = 0.6f;
            smoke.config.colorLifeEndR = 0.3f;
            smoke.config.colorLifeEndG = 0.3f;
            smoke.config.colorLifeEndB = 0.3f;
            smoke.config.colorLifeEndA = 0.0f;
            smoke.config.softFadeDistance = 0.8f;
            smoke.positionOffset = {0.0f, 0.0f, 1.0f}; // above flame
            smoke.burstCount = 100;
            smoke.spawnRate = 150.0f;
            smoke.burstMode = false;
            smoke.burstPending = false;
            smoke.poolSize = 10000;
        }

        // Sparks: bright, fast, velocity-aligned stretch
        {
            auto& sparks = m_emitters.emplace_back();
            sparks.name = "Sparks";
            sparks.config = {};
            sparks.config.spawnVelocity[0] = 0.0f;
            sparks.config.spawnVelocity[1] = 0.0f;
            sparks.config.spawnVelocity[2] = 8.0f;
            sparks.config.velocityVariation = 5.0f;
            sparks.config.spreadAngle = 1.5f;
            sparks.config.minLifetime = 0.3f;
            sparks.config.maxLifetime = 0.8f;
            sparks.config.initialSize = 0.02f;
            sparks.config.spawnRadius = 0.05f;
            sparks.config.initialColor[0] = 1.0f;
            sparks.config.initialColor[1] = 0.8f;
            sparks.config.initialColor[2] = 0.3f;
            sparks.config.initialColor[3] = 1.0f;
            sparks.config.gravity[0] = 0.0f;
            sparks.config.gravity[1] = 0.0f;
            sparks.config.gravity[2] = -5.0f; // gravity pulls sparks down
            sparks.config.damping = 0.05f;
            sparks.config.emissiveIntensity = 4.0f;
            sparks.config.blendMode = 1; // Additive
            sparks.config.facingMode = 1; // VelocityAligned
            sparks.config.stretchFactor = 0.6f;
            sparks.config.sizeOverLifeStart = 1.0f;
            sparks.config.sizeOverLifePeak = 0.8f;
            sparks.config.sizeOverLifeEnd = 0.0f;
            sparks.config.sizeOverLifePeakTime = 0.2f;
            sparks.config.colorLifeStartR = 1.0f;
            sparks.config.colorLifeStartG = 0.9f;
            sparks.config.colorLifeStartB = 0.5f;
            sparks.config.colorLifeStartA = 1.0f;
            sparks.config.colorLifeEndR = 1.0f;
            sparks.config.colorLifeEndG = 0.3f;
            sparks.config.colorLifeEndB = 0.0f;
            sparks.config.colorLifeEndA = 0.0f;
            sparks.config.softFadeDistance = 0.1f;
            sparks.burstCount = 50;
            sparks.spawnRate = 80.0f;
            sparks.burstMode = false;
            sparks.burstPending = false;
            sparks.poolSize = 5000;
        }
        break;
    }

    case VFXCompoundPreset::Explosion: {
        // Flash: brief bright burst at center
        {
            auto& flash = m_emitters.emplace_back();
            flash.name = "Flash";
            flash.config = {};
            flash.config.spawnVelocity[0] = 0.0f;
            flash.config.spawnVelocity[1] = 0.0f;
            flash.config.spawnVelocity[2] = 0.5f;
            flash.config.velocityVariation = 0.3f;
            flash.config.spreadAngle = 3.14159f; // full sphere
            flash.config.minLifetime = 0.1f;
            flash.config.maxLifetime = 0.3f;
            flash.config.initialSize = 0.3f;
            flash.config.initialColor[0] = 1.0f;
            flash.config.initialColor[1] = 0.9f;
            flash.config.initialColor[2] = 0.7f;
            flash.config.initialColor[3] = 1.0f;
            flash.config.gravity[0] = 0.0f;
            flash.config.gravity[1] = 0.0f;
            flash.config.gravity[2] = 0.0f;
            flash.config.damping = 0.0f;
            flash.config.emissiveIntensity = 8.0f;
            flash.config.blendMode = 1; // Additive
            flash.config.sizeOverLifeStart = 0.5f;
            flash.config.sizeOverLifePeak = 1.0f;
            flash.config.sizeOverLifeEnd = 2.0f;
            flash.config.sizeOverLifePeakTime = 0.3f;
            flash.config.colorLifeStartR = 1.0f;
            flash.config.colorLifeStartG = 1.0f;
            flash.config.colorLifeStartB = 0.9f;
            flash.config.colorLifeStartA = 1.0f;
            flash.config.colorLifeEndR = 1.0f;
            flash.config.colorLifeEndG = 0.5f;
            flash.config.colorLifeEndB = 0.1f;
            flash.config.colorLifeEndA = 0.0f;
            flash.config.particleLightEnabled = 1;
            flash.config.particleLightRadius = 1200.0f;
            flash.config.particleLightIntensity = 5.0f;
            flash.burstCount = 80;
            flash.burstMode = true;
            flash.burstPending = true;
            flash.poolSize = 5000;
        }

        // Debris: heavy chunks flying outward
        {
            auto& debris = m_emitters.emplace_back();
            debris.name = "Debris";
            debris.config = {};
            debris.config.spawnVelocity[0] = 0.0f;
            debris.config.spawnVelocity[1] = 0.0f;
            debris.config.spawnVelocity[2] = 10.0f;
            debris.config.velocityVariation = 6.0f;
            debris.config.spreadAngle = 2.0f;
            debris.config.minLifetime = 0.5f;
            debris.config.maxLifetime = 2.0f;
            debris.config.initialSize = 0.04f;
            debris.config.initialColor[0] = 0.8f;
            debris.config.initialColor[1] = 0.4f;
            debris.config.initialColor[2] = 0.2f;
            debris.config.initialColor[3] = 1.0f;
            debris.config.gravity[0] = 0.0f;
            debris.config.gravity[1] = 0.0f;
            debris.config.gravity[2] = -9.81f;
            debris.config.damping = 0.1f;
            debris.config.emissiveIntensity = 1.0f;
            debris.config.blendMode = 0; // AlphaBlend
            debris.config.facingMode = 1; // VelocityAligned
            debris.config.stretchFactor = 0.4f;
            debris.config.groundCollision = 1;
            debris.config.restitution = 0.4f;
            debris.config.sizeOverLifeStart = 1.0f;
            debris.config.sizeOverLifePeak = 0.8f;
            debris.config.sizeOverLifeEnd = 0.2f;
            debris.config.sizeOverLifePeakTime = 0.1f;
            debris.burstCount = 120;
            debris.burstMode = true;
            debris.burstPending = true;
            debris.poolSize = 8000;
        }

        // Shockwave smoke: expanding outward ring
        {
            auto& shock = m_emitters.emplace_back();
            shock.name = "Shockwave";
            shock.config = {};
            shock.config.spawnVelocity[0] = 0.0f;
            shock.config.spawnVelocity[1] = 0.0f;
            shock.config.spawnVelocity[2] = 2.0f;
            shock.config.velocityVariation = 1.0f;
            shock.config.spreadAngle = 3.14159f;
            shock.config.minLifetime = 1.0f;
            shock.config.maxLifetime = 3.0f;
            shock.config.initialSize = 0.1f;
            shock.config.spawnShape = 3; // Ring
            shock.config.shapeParam1 = 0.5f; // ring width
            shock.config.spawnRadius = 0.5f;
            shock.config.initialColor[0] = 0.5f;
            shock.config.initialColor[1] = 0.45f;
            shock.config.initialColor[2] = 0.4f;
            shock.config.initialColor[3] = 0.6f;
            shock.config.gravity[0] = 0.0f;
            shock.config.gravity[1] = 0.0f;
            shock.config.gravity[2] = 0.3f;
            shock.config.damping = 0.4f;
            shock.config.emissiveIntensity = 0.0f;
            shock.config.blendMode = 0; // AlphaBlend
            shock.config.curlStrength = 1.5f;
            shock.config.curlFrequency = 0.4f;
            shock.config.curlSpeed = 0.2f;
            shock.config.sizeOverLifeStart = 0.5f;
            shock.config.sizeOverLifePeak = 1.0f;
            shock.config.sizeOverLifeEnd = 1.5f;
            shock.config.sizeOverLifePeakTime = 0.4f;
            shock.config.colorLifeStartR = 0.6f;
            shock.config.colorLifeStartG = 0.55f;
            shock.config.colorLifeStartB = 0.5f;
            shock.config.colorLifeStartA = 0.7f;
            shock.config.colorLifeEndR = 0.3f;
            shock.config.colorLifeEndG = 0.3f;
            shock.config.colorLifeEndB = 0.3f;
            shock.config.colorLifeEndA = 0.0f;
            shock.config.softFadeDistance = 0.6f;
            shock.burstCount = 150;
            shock.burstMode = true;
            shock.burstPending = true;
            shock.poolSize = 10000;
        }
        break;
    }

    case VFXCompoundPreset::Magic: {
        // Core glow: concentrated bright orb
        {
            auto& core = m_emitters.emplace_back();
            core.name = "Core Glow";
            core.config = {};
            core.config.spawnVelocity[0] = 0.0f;
            core.config.spawnVelocity[1] = 0.0f;
            core.config.spawnVelocity[2] = 0.5f;
            core.config.velocityVariation = 0.3f;
            core.config.spreadAngle = 3.14159f;
            core.config.minLifetime = 0.8f;
            core.config.maxLifetime = 1.5f;
            core.config.initialSize = 0.06f;
            core.config.spawnRadius = 0.15f;
            core.config.initialColor[0] = 0.3f;
            core.config.initialColor[1] = 0.5f;
            core.config.initialColor[2] = 1.0f;
            core.config.initialColor[3] = 0.8f;
            core.config.gravity[0] = 0.0f;
            core.config.gravity[1] = 0.0f;
            core.config.gravity[2] = 0.0f;
            core.config.damping = 0.8f;
            core.config.emissiveIntensity = 2.5f;
            core.config.blendMode = 1; // Additive
            core.config.sizeOverLifeStart = 0.5f;
            core.config.sizeOverLifePeak = 1.0f;
            core.config.sizeOverLifeEnd = 0.0f;
            core.config.sizeOverLifePeakTime = 0.3f;
            core.config.colorLifeStartR = 0.4f;
            core.config.colorLifeStartG = 0.6f;
            core.config.colorLifeStartB = 1.0f;
            core.config.colorLifeStartA = 1.0f;
            core.config.colorLifeEndR = 0.2f;
            core.config.colorLifeEndG = 0.3f;
            core.config.colorLifeEndB = 0.8f;
            core.config.colorLifeEndA = 0.0f;
            core.config.softFadeDistance = 0.2f;
            core.burstCount = 150;
            core.spawnRate = 200.0f;
            core.burstMode = false;
            core.burstPending = false;
            core.poolSize = 8000;
        }

        // Orbiting particles: swirling ring around core
        {
            auto& orbit = m_emitters.emplace_back();
            orbit.name = "Orbiting";
            orbit.config = {};
            orbit.config.spawnVelocity[0] = 0.0f;
            orbit.config.spawnVelocity[1] = 0.0f;
            orbit.config.spawnVelocity[2] = 1.0f;
            orbit.config.velocityVariation = 0.5f;
            orbit.config.spreadAngle = 0.8f;
            orbit.config.minLifetime = 1.0f;
            orbit.config.maxLifetime = 2.5f;
            orbit.config.initialSize = 0.03f;
            orbit.config.spawnRadius = 0.8f;
            orbit.config.spawnShape = 3; // Ring
            orbit.config.shapeParam1 = 0.2f; // ring width
            orbit.config.initialColor[0] = 0.5f;
            orbit.config.initialColor[1] = 0.8f;
            orbit.config.initialColor[2] = 1.0f;
            orbit.config.initialColor[3] = 1.0f;
            orbit.config.gravity[0] = 0.0f;
            orbit.config.gravity[1] = 0.0f;
            orbit.config.gravity[2] = 0.0f;
            orbit.config.damping = 0.2f;
            orbit.config.emissiveIntensity = 3.0f;
            orbit.config.blendMode = 1; // Additive
            // Vortex makes particles orbit the core
            orbit.config.vortexAxisDir[0] = 0.0f;
            orbit.config.vortexAxisDir[1] = 0.0f;
            orbit.config.vortexAxisDir[2] = 1.0f;
            orbit.config.vortexTangentialStrength = 6.0f;
            orbit.config.vortexRadialStrength = 1.5f; // inward pull
            orbit.config.vortexMaxRadius = 2.0f;
            orbit.config.sizeOverLifeStart = 1.0f;
            orbit.config.sizeOverLifePeak = 0.8f;
            orbit.config.sizeOverLifeEnd = 0.0f;
            orbit.config.sizeOverLifePeakTime = 0.2f;
            orbit.config.colorLifeStartR = 0.6f;
            orbit.config.colorLifeStartG = 0.9f;
            orbit.config.colorLifeStartB = 1.0f;
            orbit.config.colorLifeStartA = 1.0f;
            orbit.config.colorLifeEndR = 0.2f;
            orbit.config.colorLifeEndG = 0.4f;
            orbit.config.colorLifeEndB = 1.0f;
            orbit.config.colorLifeEndA = 0.0f;
            orbit.config.softFadeDistance = 0.15f;
            orbit.burstCount = 80;
            orbit.spawnRate = 100.0f;
            orbit.burstMode = false;
            orbit.burstPending = false;
            orbit.poolSize = 8000;
        }

        // Dust: ambient sparkle motes
        {
            auto& dust = m_emitters.emplace_back();
            dust.name = "Dust";
            dust.config = {};
            dust.config.spawnVelocity[0] = 0.0f;
            dust.config.spawnVelocity[1] = 0.0f;
            dust.config.spawnVelocity[2] = 0.3f;
            dust.config.velocityVariation = 0.2f;
            dust.config.spreadAngle = 3.14159f;
            dust.config.minLifetime = 2.0f;
            dust.config.maxLifetime = 4.0f;
            dust.config.initialSize = 0.01f;
            dust.config.spawnRadius = 2.0f;
            dust.config.initialColor[0] = 0.6f;
            dust.config.initialColor[1] = 0.8f;
            dust.config.initialColor[2] = 1.0f;
            dust.config.initialColor[3] = 0.5f;
            dust.config.gravity[0] = 0.0f;
            dust.config.gravity[1] = 0.0f;
            dust.config.gravity[2] = 0.1f; // slow float upward
            dust.config.damping = 0.3f;
            dust.config.emissiveIntensity = 1.0f;
            dust.config.blendMode = 1; // Additive
            dust.config.curlStrength = 0.5f;
            dust.config.curlFrequency = 0.2f;
            dust.config.curlSpeed = 0.1f;
            dust.config.sizeOverLifeStart = 0.0f;
            dust.config.sizeOverLifePeak = 1.0f;
            dust.config.sizeOverLifeEnd = 0.0f;
            dust.config.sizeOverLifePeakTime = 0.5f;
            dust.config.colorLifeStartR = 0.5f;
            dust.config.colorLifeStartG = 0.7f;
            dust.config.colorLifeStartB = 1.0f;
            dust.config.colorLifeStartA = 0.0f;
            dust.config.colorLifeEndR = 0.3f;
            dust.config.colorLifeEndG = 0.5f;
            dust.config.colorLifeEndB = 0.8f;
            dust.config.colorLifeEndA = 0.0f;
            dust.config.softFadeDistance = 0.2f;
            dust.burstCount = 60;
            dust.spawnRate = 40.0f;
            dust.burstMode = false;
            dust.burstPending = false;
            dust.poolSize = 5000;
        }
        break;
    }

    default:
        break;
    }

    m_compoundPreset = preset;
    UZ_LOG(App, Info, "VFX: Built compound preset '{}' with {} emitter(s)",
           static_cast<int>(preset), static_cast<int>(m_emitters.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildMeshWorldMatrix
// ─────────────────────────────────────────────────────────────────────────────

Mat4 VFXViewportDemo::BuildMeshWorldMatrix(uint32_t meshIndex) const {
    const auto& xform = m_meshTransforms[meshIndex];

    // Ground plane (index 2) has special handling: scale + rotation + translation
    // as before, but now driven by m_meshTransforms instead of hardcoded values.
    const Mat4 scale = Mat4::Scaling(xform.scale);
    const Mat4 rot = xform.rotation.ToMatrix();
    const Mat4 trans = Mat4::Translation(xform.position);

    // Order: scale * rotation * translation (scale applied first in object space)
    return scale * rot * trans;
}

// ─────────────────────────────────────────────────────────────────────────────
// Outline Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderOutlinePass(IRHICommandList* cmd,
                                         const CBAllocation& perFrameAlloc) {
    if (m_selectedMeshIndex == UINT32_MAX || !m_outlineEnabled) return;

    // Build a temporary DrawCommand + GPUMesh vector for the outline pass.
    // The outline pass expects engine DrawCommand/GPUMesh types, not ProceduralMesh.
    const auto& procMesh = m_meshes[m_selectedMeshIndex];
    if (!procMesh.vertexBuffer.IsValid() || !procMesh.indexBuffer.IsValid()) return;

    DrawCommand dc{};
    dc.worldMatrix = BuildMeshWorldMatrix(m_selectedMeshIndex);
    dc.normalMatrix = m_meshTransforms[m_selectedMeshIndex].rotation.ToMatrix();
    dc.entityId = 0; // Use entityId=0 for the selected mesh
    dc.meshId = 0;   // Index into the temporary meshes vector

    GPUMesh gpuMesh{};
    gpuMesh.vertexBuffer = procMesh.vertexBuffer;
    gpuMesh.indexBuffer = procMesh.indexBuffer;
    gpuMesh.vbSrvIndex = procMesh.vbSrvIndex;
    gpuMesh.indexCount = procMesh.indexCount;
    gpuMesh.vertexCount = 0; // Not used by outline pass
    gpuMesh.indexFormat = RHIIndexFormat::Uint32;

    std::vector<DrawCommand> drawList = { dc };
    std::vector<GPUMesh> meshes = { gpuMesh };

    // Transition depth to read for outline mask pass (outline reads depth via DSV).
    // Depth is already in DepthStencilWrite from the scene pass — outline pass
    // needs it in that same state (it reads via depth test, not SRV).
    m_outlinePass.Draw(cmd, *GetCBRing(), GetCamera(), drawList, meshes,
                       0, // selectedEntityId = 0 (matches dc.entityId above)
                       static_cast<float>(GetWidth()),
                       static_cast<float>(GetHeight()),
                       m_depthBuffer, m_hdrTarget, IsBindless(),
                       perFrameAlloc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderGizmoPass(IRHICommandList* cmd) {
    if (m_selectedMeshIndex == UINT32_MAX) return;
    if (!m_gizmoRenderer.IsInitialized()) return;

    const auto& xform = m_meshTransforms[m_selectedMeshIndex];

    // Begin render pass with HDR target + depth bound.
    // Gizmo renders on top of the scene with depth read/no-write.
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

    // Convert GizmoHitResult to int axis index for the renderer:
    // None=-1, AxisX=0, AxisY=1, AxisZ=2, PlaneXY=3, PlaneXZ=4, PlaneYZ=5, Center=6
    auto hitToAxis = [](GizmoHitResult h) -> int {
        return (h == GizmoHitResult::None) ? -1 : (static_cast<int>(h) - 1);
    };

    m_gizmoRenderer.Draw(cmd, *GetCBRing(), GetCamera(),
                          xform.position, xform.rotation,
                          m_gizmoMode, m_gizmoSpace,
                          hitToAxis(m_gizmoInteraction.GetHoveredAxis()),
                          hitToAxis(m_gizmoInteraction.GetActiveAxis()));

    cmd->EndRenderPass();
}

} // namespace uz
