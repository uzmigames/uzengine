/// Deferred Rendering Test Demo Implementation — Backend-Agnostic
///
/// Supports two render paths, toggled at runtime with the F key:
///
/// **Deferred** (default):
///   Pass 1 — G-Buffer: 5 cubes -> albedo (RGBA8), normal (RGBA16F),
///             metallic/roughness (RGBA8), depth (D32F).
///   Pass 2 — Lighting (or Debug): fullscreen triangle reads G-Buffer,
///             accumulates 4 coloured point lights + directional sun, or
///             visualises a single G-Buffer channel when debug mode is active.
///
/// **Forward**:
///   Pass 1 — Forward PBR: 5 cubes rendered directly to backbuffer with
///             per-pixel Cook-Torrance lighting in a single pass.
///
/// Both paths share:
///   Grid + Stats overlay composited on the backbuffer, then present barrier.
///
/// All GPU interaction flows through abstract RHI interfaces — no DX12,
/// Vulkan, or Metal types appear in this file.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "deferred_test_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "input.h"
#include "log.h"
#include "vertex.h"
#include "mesh.h"
#include "upload_context.h"
#include "constant_buffers.h"
#include "camera.h"
#include "fps_camera_controller.h"
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "engine_shaders.h"
#include "paths.h"
#include "editor_grid.h"
#include "screenshot_capture.h"
#include "stats_overlay.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "draw_list.h"
#include "passes/gbuffer_pass.h"
#include "passes/lighting_pass.h"
#include "passes/gbuffer_debug_pass.h"
#include "passes/pbr_pass.h"
#include "render_path.h"

#include "stb_image.h"

#include <cstdlib>
#include <cstring>

namespace uz {

// Uses the Renderer log category declared in log.h

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kMaxFPS         = 500.f;
static constexpr float kMinFrameTime   = 1.f / kMaxFPS;
static constexpr int   kCubeCount      = 5;
static constexpr uint32_t kPointLightCount = 4;

// ─────────────────────────────────────────────────────────────────────────────
// Scene data
// ─────────────────────────────────────────────────────────────────────────────

static constexpr Vec3 kCubePositions[kCubeCount] = {
    {-4.f, 0.f, 0.f},
    {-2.f, 0.f, 0.f},
    { 0.f, 0.f, 0.f},
    { 2.f, 0.f, 0.f},
    { 4.f, 0.f, 0.f},
};
static constexpr float kCubeScale = 0.8f;

// ─────────────────────────────────────────────────────────────────────────────
// Texture creation helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Loads an image from disk, uploads it to the GPU, and returns the SRV index.
static uint32_t LoadPBRTexture(IRHIDevice* device, IRHICommandList* cmdList,
                               const char* filename, const char* debugName,
                               RHITextureHandle& outHandle)
{
    std::string path = (uz::GetExeDir() / "assets" / filename).string();
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!pixels) {
        UZ_LOG(Renderer, Warning, "PBR texture not found: '{}' (using default)", filename);
        return 0;
    }

    RHITextureDesc desc{};
    desc.width     = static_cast<uint32_t>(w);
    desc.height    = static_cast<uint32_t>(h);
    desc.format    = RHIFormat::RGBA8Unorm;
    desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    outHandle = device->CreateTexture(desc);
    if (!outHandle.IsValid()) {
        stbi_image_free(pixels);
        UZ_LOG(Renderer, Error, "Failed to create texture: '{}'", debugName);
        return 0;
    }

    uint32_t srvIdx = device->GetTextureSRVIndex(outHandle);

    UploadContext upload(device);
    cmdList->Begin();
    uint64_t dataSize = static_cast<uint64_t>(w) * h * 4;
    upload.UploadTexture(cmdList, outHandle, pixels, dataSize);

    RHIBarrierDesc barrier{};
    barrier.texture     = outHandle;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();
    upload.ReleaseStaging();
    stbi_image_free(pixels);

    UZ_LOG(Renderer, Info, "PBR texture '{}': {}x{}, SRV={}", debugName, w, h, srvIdx);
    return srvIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline creation helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Creates the G-Buffer PSO (mesh_vs + gbuffer_ps, 3 RTs + depth).
static RHIPipelineHandle CreateGBufferPipeline(IRHIDevice* device,
                                               RHIShaderHandle vsHandle,
                                               RHIShaderHandle psHandle,
                                               bool bindless)
{
    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
        { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
    };

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = vsHandle;
    desc.pixelShader   = psHandle;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;
    desc.renderTargetFormats[1]           = RHIFormat::RGBA16Float;
    desc.renderTargetFormats[2]           = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount                = 3;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = "GBufferPipeline";

    if (!bindless) {
        desc.inputElements     = inputElements;
        desc.inputElementCount = 4;
        desc.vertexStride      = sizeof(Vertex);
    }

    return device->CreateGraphicsPipeline(desc);
}

/// Creates the fullscreen lighting PSO (fullscreen_vs + deferred_lighting_ps, 1 RT, no depth).
static RHIPipelineHandle CreateLightingPipeline(IRHIDevice* device,
                                                RHIShaderHandle vsHandle,
                                                RHIShaderHandle psHandle)
{
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = vsHandle;
    desc.pixelShader   = psHandle;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount        = 1;
    desc.debugName                = "LightingPipeline";
    return device->CreateGraphicsPipeline(desc);
}

/// Creates the fullscreen debug visualization PSO.
static RHIPipelineHandle CreateDebugPipeline(IRHIDevice* device,
                                             RHIShaderHandle vsHandle,
                                             RHIShaderHandle psHandle)
{
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = vsHandle;
    desc.pixelShader   = psHandle;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount        = 1;
    desc.debugName                = "GBufferDebugPipeline";
    return device->CreateGraphicsPipeline(desc);
}

/// Creates the forward PBR PSO (mesh_vs + mesh_ps, 1 RT + depth).
static RHIPipelineHandle CreateForwardPipeline(IRHIDevice* device,
                                                RHIShaderHandle vsHandle,
                                                RHIShaderHandle psHandle,
                                                bool bindless)
{
    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
        { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
    };

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = vsHandle;
    desc.pixelShader   = psHandle;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount                = 1;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = "PBRPipeline";

    if (!bindless) {
        desc.inputElements     = inputElements;
        desc.inputElementCount = 4;
        desc.vertexStride      = sizeof(Vertex);
    }

    return device->CreateGraphicsPipeline(desc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

DeferredTestDemo::DeferredTestDemo(const DeferredTestConfig& config)
    : DemoApplication(DemoConfig{
          .title     = "UzEngine \xe2\x80\x94 Deferred Rendering Test",
          .width     = 1280,
          .height    = 720,
          .maxFrames = config.maxFrames,
      })
    , config_(config) {}

DeferredTestDemo::~DeferredTestDemo() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OnInit — Compiles shaders, creates pipelines, loads mesh/textures, G-Buffer
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::OnInit() {
    auto* device  = GetDevice();
    auto* cmdList = GetCommandList();

    // ── Input ────────────────────────────────────────────────────────────
    input_.Init();
    GetWindow()->SetInputSystem(&input_);

    // ── Shader pipeline ──────────────────────────────────────────────────
    bindless_ = IsBindless();

    compiler_ = std::make_unique<ShaderCompiler>();
    if (!compiler_->Init()) {
        UZ_LOG(Renderer, Error, "Failed to init shader compiler");
        return;
    }

    shaderPipeline_ = std::make_unique<ShaderPipeline>();
    ShaderPipeline::Config pipelineCfg;
    pipelineCfg.cacheDir      = (uz::GetExeDir() / "cache/shaders").string();
    pipelineCfg.targetBackend = bindless_ ? "dx12_sm66" : "dx12_sm50";
    pipelineCfg.bindless      = bindless_;
    shaderPipeline_->Init(pipelineCfg);
    shaderPipeline_->SetCompiler(compiler_.get());

    if (!bindless_) {
        compilerFXC_ = std::make_unique<ShaderCompilerFXC>();
        if (!compilerFXC_->Init()) {
            UZ_LOG(Renderer, Error, "Failed to init FXC shader compiler");
            return;
        }
        shaderPipeline_->SetCompilerFXC(compilerFXC_.get());
    }

    CompileShaders();
    CreatePipelines();
    LoadAndUploadMesh();
    LoadPBRTextures();

    // ── G-Buffer textures ────────────────────────────────────────────────
    if (!CreateGBuffer()) {
        UZ_LOG(Renderer, Error, "Failed to create initial G-Buffer textures");
        return;
    }

    // ── CB ring allocator ────────────────────────────────────────────────
    cbRing_.Init(device, 3, 128 * 1024);

    // ── Editor grid ──────────────────────────────────────────────────────
    if (!editorGrid_.Init(device, *shaderPipeline_, RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "Failed to initialize editor grid");
        return;
    }

    // ── Stats overlay ────────────────────────────────────────────────────
    {
        UploadContext upload(device);
        cmdList->Begin();
        if (!statsOverlay_.Init(device, cmdList, upload, *shaderPipeline_,
                                RHIFormat::RGBA8Unorm, GetWidth(), GetHeight(), bindless_)) {
            UZ_LOG(Renderer, Error, "Failed to initialize stats overlay");
            cmdList->End();
            return;
        }
        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
    }

    // ── Screenshot capture ───────────────────────────────────────────────
    screenshotPending_ = !config_.screenshotPath.empty();
    if (screenshotPending_) {
        if (!screenshotCapture_.Init(device, GetWidth(), GetHeight())) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            return;
        }
    }

    // ── Render graph resource pool ───────────────────────────────────────
    rgPool_ = std::make_unique<rg::RgResourcePool>(device);

    // ── Camera ───────────────────────────────────────────────────────────
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect  = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera_.SetPerspective(vFovRad,
        static_cast<float>(GetWidth()) / static_cast<float>(GetHeight()),
        0.1f, 1000.f);
    camera_.SetPosition({0.f, 5.f, 2.f});
    {
        constexpr float kDegToRad = 3.14159265f / 180.f;
        float yawRad   = -90.f * kDegToRad;
        float pitchRad = -20.f * kDegToRad;
        float cosPitch = std::cos(pitchRad);
        Vec3 forward = {
            std::cos(yawRad) * cosPitch,
            std::sin(yawRad) * cosPitch,
            std::sin(pitchRad)
        };
        Vec3 pos = camera_.GetPosition();
        camera_.LookAt(pos + forward, Vec3::Up());
    }

    cameraCtrl_.InitFromCamera(camera_);
    cameraCtrl_.SetMoveSpeed(10.f);

    // ── Render path ──────────────────────────────────────────────────────
    renderPath_ = GetDefaultRenderPath();

    UZ_LOG(Renderer, Info, "Deferred test demo started — 5 cubes, 4 point lights + directional sun "
           "(F=toggle render path, current={})", RenderPathToString(renderPath_));
}

// ─────────────────────────────────────────────────────────────────────────────
// CompileShaders
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::CompileShaders() {
    auto* device = GetDevice();

    auto meshVSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshVS),
        EngineShaders::GetSource(EngineShader::MeshVS));
    auto gbufPSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GbufferPS),
        EngineShaders::GetSource(EngineShader::GbufferPS));
    auto fsVSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::FullscreenVS),
        EngineShaders::GetSource(EngineShader::FullscreenVS));
    auto lightPSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::DeferredLightingPS),
        EngineShaders::GetSource(EngineShader::DeferredLightingPS));
    auto debugPSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GbufferDebugPS),
        EngineShaders::GetSource(EngineShader::GbufferDebugPS));
    auto meshPSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshPS),
        EngineShaders::GetSource(EngineShader::MeshPS));

    if (meshVSBytes.empty() || gbufPSBytes.empty() || fsVSBytes.empty() ||
        lightPSBytes.empty() || debugPSBytes.empty() || meshPSBytes.empty())
    {
        UZ_LOG(Renderer, Error, "Failed to compile one or more deferred shaders");
        return;
    }

    auto makeShader = [&](const std::vector<uint8_t>& bc, RHIShaderStage stage,
                          const char* name) -> RHIShaderHandle {
        RHIShaderDesc d{};
        d.bytecode     = bc.data();
        d.bytecodeSize = bc.size();
        d.stage        = stage;
        d.entryPoint   = "main";
        d.debugName    = name;
        return device->CreateShader(d);
    };

    meshVSHandle_  = makeShader(meshVSBytes,  RHIShaderStage::Vertex, "mesh_vs");
    gbufPSHandle_  = makeShader(gbufPSBytes,  RHIShaderStage::Pixel,  "gbuffer_ps");
    fsVSHandle_    = makeShader(fsVSBytes,    RHIShaderStage::Vertex, "fullscreen_vs");
    lightPSHandle_ = makeShader(lightPSBytes, RHIShaderStage::Pixel,  "deferred_lighting_ps");
    debugPSHandle_ = makeShader(debugPSBytes, RHIShaderStage::Pixel,  "gbuffer_debug_ps");
    meshPSHandle_  = makeShader(meshPSBytes,  RHIShaderStage::Pixel,  "mesh_ps");
}

// ─────────────────────────────────────────────────────────────────────────────
// CreatePipelines
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::CreatePipelines() {
    auto* device = GetDevice();

    gbufPipeline_    = CreateGBufferPipeline(device, meshVSHandle_, gbufPSHandle_, bindless_);
    lightPipeline_   = CreateLightingPipeline(device, fsVSHandle_, lightPSHandle_);
    debugPipeline_   = CreateDebugPipeline(device, fsVSHandle_, debugPSHandle_);
    forwardPipeline_ = CreateForwardPipeline(device, meshVSHandle_, meshPSHandle_, bindless_);

    if (!gbufPipeline_.IsValid() || !lightPipeline_.IsValid() ||
        !debugPipeline_.IsValid() || !forwardPipeline_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create one or more PSOs");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadAndUploadMesh
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::LoadAndUploadMesh() {
    auto* device = GetDevice();

    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    Mesh cubeMesh = LoadMesh(meshPath);
    if (!cubeMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to load cube.uzmesh from '{}'", meshPath);
        return;
    }
    UZ_LOG(Renderer, Info, "Loaded cube.uzmesh: {} vertices, {} indices, stride={}",
           cubeMesh.GetVertexCount(), cubeMesh.GetIndexCount(), cubeMesh.header.vertexStride);

    // ── Vertex buffer ────────────────────────────────────────────────────
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = cubeMesh.GetVertexDataSize();
    vbDesc.stride    = cubeMesh.header.vertexStride;
    vbDesc.usage     = bindless_ ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory    = RHIMemoryType::Upload;
    vbDesc.debugName = "DeferredVB";

    vbHandle_ = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle_);
    std::memcpy(vbMapped, cubeMesh.vertexData.data(), cubeMesh.GetVertexDataSize());
    device->UnmapBuffer(vbHandle_);

    vbSrvIndex_ = bindless_ ? device->GetBufferSRVIndex(vbHandle_) : 0u;

    // ── Index buffer ─────────────────────────────────────────────────────
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = cubeMesh.GetIndexDataSize();
    ibDesc.stride    = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage     = RHIBufferUsage::IndexBuffer;
    ibDesc.memory    = RHIMemoryType::Upload;
    ibDesc.debugName = "DeferredIB";

    ibHandle_ = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle_);
    std::memcpy(ibMapped, cubeMesh.indexData.data(), cubeMesh.GetIndexDataSize());
    device->UnmapBuffer(ibHandle_);

    indexFormat_ = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32)
        ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;
    indexCount_  = cubeMesh.GetIndexCount();
    vertexCount_ = cubeMesh.GetVertexCount();
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadPBRTextures
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::LoadPBRTextures() {
    auto* device  = GetDevice();
    auto* cmdList = GetCommandList();

    pbrTex_.albedoSrv    = LoadPBRTexture(device, cmdList, "Stonebricks_BaseColor.jpg", "StonebricksBaseColor",  pbrTex_.handles[0]);
    pbrTex_.normalSrv    = LoadPBRTexture(device, cmdList, "Stonebricks_Normal.jpg",    "StonebricksNormal",     pbrTex_.handles[1]);
    pbrTex_.roughnessSrv = LoadPBRTexture(device, cmdList, "Stonebricks_Roughness.jpg", "StonebricksRoughness",  pbrTex_.handles[2]);
    pbrTex_.metallicSrv  = LoadPBRTexture(device, cmdList, "Stonebricks_Metallic.jpg",  "StonebricksMetallic",   pbrTex_.handles[3]);
    pbrTex_.aoSrv        = LoadPBRTexture(device, cmdList, "Stonebricks_AO.jpg",        "StonebricksAO",         pbrTex_.handles[4]);
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateGBuffer / DestroyGBuffer
// ─────────────────────────────────────────────────────────────────────────────

bool DeferredTestDemo::CreateGBuffer() {
    auto* device = GetDevice();
    const uint32_t w = GetWidth();
    const uint32_t h = GetHeight();

    auto create = [&](RHIFormat fmt, RHITextureUsage usage, const char* name,
                      RHITextureHandle& handle, uint32_t& srvIdx) -> bool {
        RHITextureDesc desc{};
        desc.width     = w;
        desc.height    = h;
        desc.format    = fmt;
        desc.usage     = usage;
        desc.debugName = name;
        handle = device->CreateTexture(desc);
        if (!handle.IsValid()) {
            UZ_LOG(Renderer, Error, "Failed to create G-Buffer texture: '{}'", name);
            return false;
        }
        srvIdx = device->GetTextureSRVIndex(handle);
        return true;
    };

    const RHITextureUsage kColorUsage = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
    const RHITextureUsage kDepthUsage = RHITextureUsage::DepthStencil   | RHITextureUsage::ShaderResource;

    if (!create(RHIFormat::RGBA8Unorm,   kColorUsage, "GBuffer_Albedo",    gbuf_.albedo,     gbuf_.albedoSrv))    return false;
    if (!create(RHIFormat::RGBA16Float,  kColorUsage, "GBuffer_Normal",    gbuf_.normal,     gbuf_.normalSrv))    return false;
    if (!create(RHIFormat::RGBA8Unorm,   kColorUsage, "GBuffer_MetalRough",gbuf_.metalRough, gbuf_.metalRoughSrv))return false;
    if (!create(RHIFormat::D32Float,     kDepthUsage, "GBuffer_Depth",     gbuf_.depth,      gbuf_.depthSrv))     return false;

    return true;
}

void DeferredTestDemo::DestroyGBuffer() {
    auto* device = GetDevice();
    if (gbuf_.albedo.IsValid())     device->DestroyTexture(gbuf_.albedo);
    if (gbuf_.normal.IsValid())     device->DestroyTexture(gbuf_.normal);
    if (gbuf_.metalRough.IsValid()) device->DestroyTexture(gbuf_.metalRough);
    if (gbuf_.depth.IsValid())      device->DestroyTexture(gbuf_.depth);
    gbuf_ = {};
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize — Recreate size-dependent resources
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::OnResize(uint32_t width, uint32_t height) {
    DestroyGBuffer();
    if (!CreateGBuffer()) {
        UZ_LOG(Renderer, Error, "Failed to recreate G-Buffer textures on resize");
        return;
    }

    statsOverlay_.SetScreenSize(width, height);
    camera_.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame — Record one frame via the render graph
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::OnFrame(float dt) {
    auto* device    = GetDevice();
    auto* cmdList   = GetCommandList();
    auto* swapChain = GetSwapChain();
    const uint32_t frameIndex = GetFrameIndex();

    totalTime_ += dt;
    statsOverlay_.BeginFrame(dt);

    // ── Input ────────────────────────────────────────────────────────────
    input_.BeginFrame();

    // Debug mode toggle: '0' = off, '1'-'7' = G-Buffer channels 0-6
    if (input_.IsKeyJustPressed(Key::Num0)) {
        debugModeValue_ = static_cast<uint32_t>(GBufferDebugMode::Count);
    }
    for (uint32_t k = 0; k < 7; ++k) {
        if (input_.IsKeyJustPressed(static_cast<Key>(static_cast<uint32_t>(Key::Num1) + k))) {
            debugModeValue_ = k;
        }
    }

    // Render path toggle: F switches between Deferred and Forward
    if (input_.IsKeyJustPressed(Key::F)) {
        renderPath_ = (renderPath_ == RenderPath::Deferred)
            ? RenderPath::Forward : RenderPath::Deferred;
        UZ_LOG(Renderer, Info, "Render path switched to: {}",
               RenderPathToString(renderPath_));
    }

    // ── Camera update ────────────────────────────────────────────────────
    cameraCtrl_.Update(camera_, input_, dt);

    cbRing_.BeginFrame(frameIndex);

    // ── PerFrameCB ───────────────────────────────────────────────────────
    PerFrameCB perFrame{};
    perFrame.viewProj    = camera_.GetViewProjection();
    perFrame.invViewProj = perFrame.viewProj.Inverse();
    Vec3 camPos          = camera_.GetPosition();
    perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams  = {totalTime_, dt, static_cast<float>(frameIndex), 0.f};

    // Warm directional sun
    Vec3 sunDir           = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
    perFrame.lightDir     = {sunDir.x, sunDir.y, sunDir.z, 0.f};
    perFrame.lightColor   = {3.0f, 2.85f, 2.7f, 0.f};
    perFrame.ambientColor = {0.03f, 0.03f, 0.05f, 0.f};

    CBAllocation perFrameAlloc = cbRing_.Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── LightingCB — 4 coloured point lights ────────────────────────────
    LightingCB lightingData{};
    lightingData.pointLightCount = kPointLightCount;
    lightingData.pointLights[0] = { {-3.f, 2.f,  2.f}, 8.f,  {4.f, 0.2f, 0.2f}, 0.f };
    lightingData.pointLights[1] = { { 0.f, 3.f, -2.f}, 10.f, {0.2f, 4.f, 0.2f}, 0.f };
    lightingData.pointLights[2] = { { 3.f, 2.f,  2.f}, 8.f,  {0.2f, 0.2f, 4.f}, 0.f };
    lightingData.pointLights[3] = { { 0.f, 1.f,  0.f}, 15.f, {3.f,  3.f,  3.f}, 0.f };

    CBAllocation lightingAlloc = cbRing_.Allocate(sizeof(LightingCB));
    std::memcpy(lightingAlloc.cpuPtr, &lightingData, sizeof(LightingCB));

    // ── Build draw list ──────────────────────────────────────────────────
    DrawList drawList;
    drawList.Clear();
    for (int i = 0; i < kCubeCount; ++i) {
        DrawCommand cmd{};
        cmd.sortKey     = SortKey::Opaque(0, 0, 0.f, static_cast<uint32_t>(i));
        cmd.worldMatrix  = Mat4::TRS(kCubePositions[i], kCubeScale);
        cmd.normalMatrix = cmd.worldMatrix.NormalMatrix();
        cmd.meshId             = 0;
        cmd.albedoSrvIndex     = pbrTex_.albedoSrv;
        cmd.normalSrvIndex     = pbrTex_.normalSrv;
        cmd.roughnessSrvIndex  = pbrTex_.roughnessSrv;
        cmd.metallicSrvIndex   = pbrTex_.metallicSrv;
        drawList.Add(cmd);
    }
    drawList.Sort();

    // ── Record frame via render graph ────────────────────────────────────
    bool capturedThisFrame = false;

    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
    rg::RgBuilder rgBuilder(device, rgPool_.get());
    auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

    auto albedoRes     = rgBuilder.ImportTexture("GBuf_Albedo",    gbuf_.albedo,     RHIResourceState::ShaderResource);
    auto normalRes     = rgBuilder.ImportTexture("GBuf_Normal",    gbuf_.normal,     RHIResourceState::ShaderResource);
    auto metalRoughRes = rgBuilder.ImportTexture("GBuf_MetalRough",gbuf_.metalRough, RHIResourceState::ShaderResource);
    auto depthRes      = rgBuilder.ImportTexture("GBuf_Depth",     gbuf_.depth,      RHIResourceState::ShaderResource);

    if (renderPath_ == RenderPath::Deferred) {
        // ── Deferred path: G-Buffer + lighting/debug ─────────────────
        {
            GBufferPassDesc gbDesc{};
            gbDesc.albedoTarget         = albedoRes;
            gbDesc.normalTarget         = normalRes;
            gbDesc.metalRoughTarget     = metalRoughRes;
            gbDesc.depthBuffer          = depthRes;
            gbDesc.depthTexture         = gbuf_.depth;
            gbDesc.drawList             = &drawList;
            gbDesc.cbAllocator          = &cbRing_;
            gbDesc.perFrameAlloc        = perFrameAlloc;
            gbDesc.pipeline             = gbufPipeline_;
            gbDesc.vertexBufferSrvIndex = vbSrvIndex_;
            gbDesc.vertexBuffer         = vbHandle_;
            gbDesc.indexBuffer          = ibHandle_;
            gbDesc.indexFormat          = indexFormat_;
            gbDesc.indexCount           = indexCount_;
            gbDesc.bindless             = bindless_;
            gbDesc.viewportWidth        = static_cast<float>(swapChain->GetWidth());
            gbDesc.viewportHeight       = static_cast<float>(swapChain->GetHeight());
            AddGBufferPass(rgBuilder, gbDesc);
        }

        const bool debugActive = (debugModeValue_ < static_cast<uint32_t>(GBufferDebugMode::Count));

        if (!debugActive) {
            LightingPassDesc lightDesc{};
            lightDesc.backbuffer          = bbRes;
            lightDesc.albedoGBuffer       = albedoRes;
            lightDesc.normalGBuffer       = normalRes;
            lightDesc.metalRoughGBuffer   = metalRoughRes;
            lightDesc.depthGBuffer        = depthRes;
            lightDesc.perFrameAlloc       = perFrameAlloc;
            lightDesc.lightingAlloc       = lightingAlloc;
            lightDesc.pipeline            = lightPipeline_;
            lightDesc.albedoSrvIndex      = gbuf_.albedoSrv;
            lightDesc.normalSrvIndex      = gbuf_.normalSrv;
            lightDesc.metalRoughSrvIndex  = gbuf_.metalRoughSrv;
            lightDesc.depthSrvIndex       = gbuf_.depthSrv;
            lightDesc.albedoTexture       = gbuf_.albedo;
            lightDesc.normalTexture       = gbuf_.normal;
            lightDesc.metalRoughTexture   = gbuf_.metalRough;
            lightDesc.depthTextureForSrv  = gbuf_.depth;
            lightDesc.bindless            = bindless_;
            lightDesc.viewportWidth       = static_cast<float>(swapChain->GetWidth());
            lightDesc.viewportHeight      = static_cast<float>(swapChain->GetHeight());
            AddLightingPass(rgBuilder, lightDesc);
        } else {
            GBufferDebugPassDesc dbgDesc{};
            dbgDesc.backbuffer         = bbRes;
            dbgDesc.albedoGBuffer      = albedoRes;
            dbgDesc.normalGBuffer      = normalRes;
            dbgDesc.metalRoughGBuffer  = metalRoughRes;
            dbgDesc.depthGBuffer       = depthRes;
            dbgDesc.perFrameAlloc      = perFrameAlloc;
            struct DebugCBData { uint32_t mode; uint32_t pad[3]; };
            CBAllocation debugAlloc = cbRing_.Allocate(sizeof(DebugCBData));
            DebugCBData debugCBData{debugModeValue_, {0, 0, 0}};
            std::memcpy(debugAlloc.cpuPtr, &debugCBData, sizeof(DebugCBData));
            dbgDesc.debugAlloc         = debugAlloc;
            dbgDesc.pipeline           = debugPipeline_;
            dbgDesc.mode               = static_cast<GBufferDebugMode>(debugModeValue_);
            dbgDesc.albedoSrvIndex     = gbuf_.albedoSrv;
            dbgDesc.normalSrvIndex     = gbuf_.normalSrv;
            dbgDesc.metalRoughSrvIndex = gbuf_.metalRoughSrv;
            dbgDesc.depthSrvIndex      = gbuf_.depthSrv;
            dbgDesc.albedoTexture      = gbuf_.albedo;
            dbgDesc.normalTexture      = gbuf_.normal;
            dbgDesc.metalRoughTexture  = gbuf_.metalRough;
            dbgDesc.depthTextureForSrv = gbuf_.depth;
            dbgDesc.bindless           = bindless_;
            dbgDesc.viewportWidth      = static_cast<float>(swapChain->GetWidth());
            dbgDesc.viewportHeight     = static_cast<float>(swapChain->GetHeight());
            AddGBufferDebugPass(rgBuilder, dbgDesc);
        }
    } else {
        // ── Forward path: single-pass PBR directly to backbuffer ─────
        PBRPassDesc fwdDesc{};
        fwdDesc.backbuffer           = bbRes;
        fwdDesc.depthBuffer          = depthRes;
        fwdDesc.depthTexture         = gbuf_.depth;
        fwdDesc.drawList             = &drawList;
        fwdDesc.cbAllocator          = &cbRing_;
        fwdDesc.perFrameAlloc        = perFrameAlloc;
        fwdDesc.pipeline             = forwardPipeline_;
        fwdDesc.vertexBufferSrvIndex = vbSrvIndex_;
        fwdDesc.vertexBuffer         = vbHandle_;
        fwdDesc.indexBuffer          = ibHandle_;
        fwdDesc.indexFormat          = indexFormat_;
        fwdDesc.indexCount           = indexCount_;
        fwdDesc.bindless             = bindless_;
        fwdDesc.viewportWidth        = static_cast<float>(swapChain->GetWidth());
        fwdDesc.viewportHeight       = static_cast<float>(swapChain->GetHeight());
        AddPBRPass(rgBuilder, fwdDesc);
    }

    // Grid + stats overlay pass
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
            vp.width    = static_cast<float>(swapChain->GetWidth());
            vp.height   = static_cast<float>(swapChain->GetHeight());
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
            cmd->SetScissor(sc);

            editorGrid_.Draw(cmd, cbRing_, camera_);

            statsOverlay_.SetDrawCalls(kCubeCount + 1);
            statsOverlay_.SetVertexCount(
                static_cast<uint32_t>(kCubeCount) * vertexCount_ + 3);
            statsOverlay_.SetTriangleCount(
                static_cast<uint32_t>(kCubeCount) * (indexCount_ / 3) + 1);
            statsOverlay_.Render(cmd, cbRing_, frameIndex);

            cmd->EndRenderPass();
        });
    });

    // Screenshot pass (optional)
    if (screenshotPending_ && !screenshotCaptured_ && frameIndex == config_.screenshotFrame) {
        rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
            pb.Read(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                screenshotCapture_.CaptureBackBuffer(cmdList, swapChain);
                capturedThisFrame = true;
            });
        });
    }

    // Present transition
    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // NOTE: base class handles cmdList->End(), Execute(), Present(), and fence signaling.
    // However, the base class calls Begin() before OnFrame and End()+Execute()+Present()
    // after OnFrame. The render graph already recorded into the open command list.
    // We do NOT call End/Execute/Present here — the base class does that.

    // Save screenshot if captured this frame — must wait for GPU completion.
    // The base class signals the fence after present, so we defer to next frame
    // or handle it in OnShutdown. For simplicity, we do a device WaitIdle here.
    if (capturedThisFrame) {
        // The base class hasn't submitted yet, so we can't wait on the fence.
        // Instead mark for deferred save — the screenshot readback happens during
        // the next WaitIdle. We'll save in OnShutdown or on the next frame.
        screenshotCaptured_ = true;
    }

    // Handle deferred screenshot save from previous frame
    if (screenshotCaptured_ && screenshotPending_ && frameIndex > config_.screenshotFrame) {
        screenshotCapture_.SaveToPNG(config_.screenshotPath);
        screenshotPending_ = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown — Release all GPU resources
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTestDemo::OnShutdown() {
    auto* device = GetDevice();

    // Save any pending screenshot before releasing resources
    if (screenshotCaptured_ && screenshotPending_) {
        screenshotCapture_.SaveToPNG(config_.screenshotPath);
        screenshotPending_ = false;
    }

    screenshotCapture_.Shutdown();
    statsOverlay_.Shutdown();
    editorGrid_.Shutdown();
    cbRing_.Shutdown();

    if (shaderPipeline_) {
        shaderPipeline_->Shutdown();
    }

    DestroyGBuffer();
    rgPool_.reset();

    for (auto& h : pbrTex_.handles) {
        if (h.IsValid()) device->DestroyTexture(h);
    }
    pbrTex_ = {};

    if (ibHandle_.IsValid())  device->DestroyBuffer(ibHandle_);
    if (vbHandle_.IsValid())  device->DestroyBuffer(vbHandle_);

    if (forwardPipeline_.IsValid()) device->DestroyPipeline(forwardPipeline_);
    if (debugPipeline_.IsValid())   device->DestroyPipeline(debugPipeline_);
    if (lightPipeline_.IsValid())   device->DestroyPipeline(lightPipeline_);
    if (gbufPipeline_.IsValid())    device->DestroyPipeline(gbufPipeline_);

    if (meshPSHandle_.IsValid())  device->DestroyShader(meshPSHandle_);
    if (debugPSHandle_.IsValid()) device->DestroyShader(debugPSHandle_);
    if (lightPSHandle_.IsValid()) device->DestroyShader(lightPSHandle_);
    if (fsVSHandle_.IsValid())    device->DestroyShader(fsVSHandle_);
    if (gbufPSHandle_.IsValid())  device->DestroyShader(gbufPSHandle_);
    if (meshVSHandle_.IsValid())  device->DestroyShader(meshVSHandle_);

    UZ_LOG(Renderer, Info, "Deferred test demo shutdown complete");
}

} // namespace uz
