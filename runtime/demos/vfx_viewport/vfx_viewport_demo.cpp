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

    // ── Point lights (scene lights OR VFX particle lights) ──────────────
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

    // ── VFX particle lights (emitter-position point lights) ──────────────
    // When scene point lights are not active, enabled emitters inject dynamic
    // point lights at their emitter position using their color and emissive
    // intensity. Up to 3 lights total (PerFrameCB limit).
    if (!m_pointLightsEnabled && m_vfxEnabled) {
        uint32_t lightIdx = 0;
        for (const auto& emitter : m_emitters) {
            if (!emitter.enabled || !emitter.config.particleLightEnabled || lightIdx >= 3)
                continue;

            // Pointers to the per-light fields in PerFrameCB (contiguous layout)
            float* posPtr       = nullptr;
            float* colorPtr     = nullptr;
            float* radiusPtr    = nullptr;
            float* intensityPtr = nullptr;

            if (lightIdx == 0) {
                posPtr       = &perFrame.pointLight0PosX;
                radiusPtr    = &perFrame.pointLight0Radius;
                colorPtr     = &perFrame.pointLight0ColorR;
                intensityPtr = &perFrame.pointLight0Intensity;
            } else if (lightIdx == 1) {
                posPtr       = &perFrame.pointLight1PosX;
                radiusPtr    = &perFrame.pointLight1Radius;
                colorPtr     = &perFrame.pointLight1ColorR;
                intensityPtr = &perFrame.pointLight1Intensity;
            } else {
                posPtr       = &perFrame.pointLight2PosX;
                radiusPtr    = &perFrame.pointLight2Radius;
                colorPtr     = &perFrame.pointLight2ColorR;
                intensityPtr = &perFrame.pointLight2Intensity;
            }

            // Position: emitter origin + per-emitter offset
            posPtr[0] = emitter.config.position[0] + emitter.positionOffset.x;
            posPtr[1] = emitter.config.position[1] + emitter.positionOffset.y;
            posPtr[2] = emitter.config.position[2] + emitter.positionOffset.z;
            *radiusPtr = emitter.config.particleLightRadius;

            // Color: emitter initial color scaled by emissive intensity
            colorPtr[0] = emitter.config.initialColor[0];
            colorPtr[1] = emitter.config.initialColor[1];
            colorPtr[2] = emitter.config.initialColor[2];
            *intensityPtr = emitter.config.particleLightIntensity;

            lightIdx++;
        }
        perFrame.numPointLights = lightIdx;
    }

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

// VFX Billboard Rendering
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

