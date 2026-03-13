/// PBR Test Demo Implementation — Backend-Agnostic
///
/// Supports two render paths, toggled at runtime with the F key:
///
/// **Deferred** (default):
///   Pass 1 — G-Buffer: 5 cubes + ground plane → albedo (RGBA8), normal (RGBA16F),
///             metallic/roughness (RGBA8), depth (D32F).
///   Pass 2 — Lighting (or Debug): fullscreen triangle reads G-Buffer,
///             accumulates 4 coloured point lights + directional sun, or
///             visualises a single G-Buffer channel when debug mode is active.
///
/// **Forward**:
///   Pass 1 — Forward PBR: 5 cubes + ground plane rendered directly to backbuffer with
///             per-pixel Cook-Torrance lighting in a single pass.
///
/// Both paths share:
///   Grid + Stats overlay composited on the backbuffer, then present barrier.
///
/// All GPU interaction flows through abstract RHI interfaces — no DX12,
/// Vulkan, or Metal types appear in this file.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pbr_test_demo.h"
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
#include "passes/shadow_pass.h"
#include "shadows/cascade.h"
#include "render_path.h"

#include "stb_image.h"

#include <DirectXMath.h>
#include <chrono>
#include <cstdlib>
#include <cstring>

using namespace DirectX;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int      kCubeCount       = 5;
static constexpr uint32_t kPointLightCount = 4;
static constexpr uint32_t kShadowResolution = 2048;
static constexpr uint32_t kShadowAtlasSize  = kShadowResolution * 2;

// ─────────────────────────────────────────────────────────────────────────────
// Matrix helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Computes transpose(inverse(model)) for correct normal transformation.
static Mat4 ComputeNormalMatrix(const Mat4& model) {
    XMFLOAT4X4 xmModel;
    std::memcpy(&xmModel, &model, sizeof(Mat4));

    XMMATRIX m = XMLoadFloat4x4(&xmModel);
    XMVECTOR det;
    XMMATRIX inv = XMMatrixInverse(&det, m);

    if (XMVectorGetX(det) == 0.f) {
        return Mat4::Identity();
    }

    XMMATRIX normalMat = XMMatrixTranspose(inv);

    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, normalMat);

    Mat4 out;
    std::memcpy(&out, &result, sizeof(Mat4));
    return out;
}

/// Builds a model matrix from position and uniform scale.
static Mat4 BuildModelMatrix(Vec3 position, float scale) {
    XMMATRIX s = XMMatrixScaling(scale, scale, scale);
    XMMATRIX t = XMMatrixTranslation(position.x, position.y, position.z);
    XMMATRIX m = XMMatrixMultiply(s, t);

    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, m);

    Mat4 out;
    std::memcpy(&out, &result, sizeof(Mat4));
    return out;
}

/// Builds a model matrix with non-uniform scale for objects like the ground plane.
static Mat4 BuildModelMatrixNonUniform(Vec3 position, Vec3 scale) {
    XMMATRIX s = XMMatrixScaling(scale.x, scale.y, scale.z);
    XMMATRIX t = XMMatrixTranslation(position.x, position.y, position.z);
    XMMATRIX m = XMMatrixMultiply(s, t);

    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, m);

    Mat4 out;
    std::memcpy(&out, &result, sizeof(Mat4));
    return out;
}

/// Computes the inverse of a view-projection matrix for world-position reconstruction.
static Mat4 ComputeInvViewProj(const Mat4& viewProj) {
    return viewProj.Inverse();
}

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

/// Creates a procedural checker texture (dark/light grey squares).
static uint32_t CreateCheckerTexture(IRHIDevice* device, IRHICommandList* cmdList,
                                     uint32_t size, uint32_t tileCount,
                                     RHITextureHandle& outHandle)
{
    std::vector<uint8_t> pixels(size * size * 4);
    const uint32_t tileSize = size / tileCount;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            bool dark = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            uint8_t val = dark ? 140u : 200u;
            uint32_t idx = (y * size + x) * 4;
            pixels[idx + 0] = val;
            pixels[idx + 1] = val;
            pixels[idx + 2] = val;
            pixels[idx + 3] = 255u;
        }
    }

    RHITextureDesc desc{};
    desc.width     = size;
    desc.height    = size;
    desc.format    = RHIFormat::RGBA8Unorm;
    desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = "CheckerTexture";

    outHandle = device->CreateTexture(desc);
    if (!outHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create checker texture");
        return 0;
    }

    uint32_t srvIdx = device->GetTextureSRVIndex(outHandle);

    UploadContext upload(device);
    cmdList->Begin();
    uint64_t dataSize = static_cast<uint64_t>(size) * size * 4;
    upload.UploadTexture(cmdList, outHandle, pixels.data(), dataSize);

    RHIBarrierDesc barrier{};
    barrier.texture     = outHandle;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();
    upload.ReleaseStaging();

    UZ_LOG(Renderer, Info, "Checker texture: {}x{}, {} tiles, SRV={}", size, size, tileCount, srvIdx);
    return srvIdx;
}

/// Creates all four G-Buffer textures at the given resolution.
static bool CreateGBufferTextures(IRHIDevice* device, uint32_t width, uint32_t height,
                                  GBufferTextures& out)
{
    auto create = [&](RHIFormat fmt, RHITextureUsage usage, const char* name,
                      RHITextureHandle& handle, uint32_t& srvIdx) -> bool {
        RHITextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
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

    if (!create(RHIFormat::RGBA8Unorm,   kColorUsage, "GBuffer_Albedo",    out.albedo,     out.albedoSrv))    return false;
    if (!create(RHIFormat::RGBA16Float,  kColorUsage, "GBuffer_Normal",    out.normal,     out.normalSrv))    return false;
    if (!create(RHIFormat::RGBA8Unorm,   kColorUsage, "GBuffer_MetalRough",out.metalRough, out.metalRoughSrv))return false;
    if (!create(RHIFormat::D32Float,     kDepthUsage, "GBuffer_Depth",     out.depth,      out.depthSrv))     return false;

    return true;
}

/// Destroys all G-Buffer textures through the device.
static void DestroyGBufferTextures(IRHIDevice* device, GBufferTextures& gbuf) {
    if (gbuf.albedo.IsValid())     device->DestroyTexture(gbuf.albedo);
    if (gbuf.normal.IsValid())     device->DestroyTexture(gbuf.normal);
    if (gbuf.metalRough.IsValid()) device->DestroyTexture(gbuf.metalRough);
    if (gbuf.depth.IsValid())      device->DestroyTexture(gbuf.depth);
    gbuf = {};
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
// PBRTestDemo — Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

PBRTestDemo::PBRTestDemo(const PBRTestConfig& config)
    : DemoApplication(DemoConfig{
        .title  = "UzEngine \xe2\x80\x94 PBR Test (Cook-Torrance)",
        .width  = 1280,
        .height = 720,
        .maxFrames = config.frameCount,
        .vsync  = false,
    })
    , config_(config)
{}

PBRTestDemo::~PBRTestDemo() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OnInit — Create all rendering resources
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnInit() {
    startTime_ = std::chrono::high_resolution_clock::now();

    auto* device = GetDevice();
    bindless_ = device->IsBindless();

    // ── Input ────────────────────────────────────────────────────────────
    input_ = std::make_unique<InputSystem>();
    input_->Init();
    GetWindow()->SetInputSystem(input_.get());

    CompileShaders();
    CreatePipelines();
    LoadMeshData();
    LoadTextures();
    CreateGBuffer();

    // ── Shadow atlas texture ─────────────────────────────────────────────
    RHITextureDesc shadowAtlasDesc{};
    shadowAtlasDesc.width     = kShadowAtlasSize;
    shadowAtlasDesc.height    = kShadowAtlasSize;
    shadowAtlasDesc.format    = RHIFormat::D32Float;
    shadowAtlasDesc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    shadowAtlasDesc.debugName = "ShadowAtlas";
    shadowAtlas_    = device->CreateTexture(shadowAtlasDesc);
    shadowAtlasSrv_ = device->GetTextureSRVIndex(shadowAtlas_);

    // ── CB ring allocator ────────────────────────────────────────────────
    cbRing_ = std::make_unique<CBRingAllocator>();
    cbRing_->Init(device, 3, 128 * 1024);

    // ── Editor Grid ──────────────────────────────────────────────────────
    editorGrid_ = std::make_unique<EditorGrid>();
    if (!editorGrid_->Init(device, *shaderPipeline_, RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "Failed to initialize editor grid");
        return;
    }

    // ── Stats Overlay ────────────────────────────────────────────────────
    statsOverlay_ = std::make_unique<StatsOverlay>();
    {
        auto* cmdList = GetCommandList();
        UploadContext upload(device);
        cmdList->Begin();
        if (!statsOverlay_->Init(device, cmdList, upload, *shaderPipeline_,
                                 RHIFormat::RGBA8Unorm, GetWidth(), GetHeight(), bindless_)) {
            UZ_LOG(Renderer, Error, "Failed to initialize stats overlay");
            return;
        }
        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
    }

    // ── Screenshot Capture ───────────────────────────────────────────────
    screenshotPending_ = !config_.screenshotPath.empty();
    if (screenshotPending_) {
        screenshotCapture_ = std::make_unique<ScreenshotCapture>();
        if (!screenshotCapture_->Init(device, GetWidth(), GetHeight())) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            return;
        }
    }

    // ── Render Graph Resource Pool ───────────────────────────────────────
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

    // ── Render state ─────────────────────────────────────────────────────
    debugModeValue_ = static_cast<uint32_t>(GBufferDebugMode::Count);
    renderPath_ = GetDefaultRenderPath();

    UZ_LOG(Renderer, Info, "PBR test demo started — 5 cubes, {} point lights + directional sun "
           "(F=toggle render path, current={})", kPointLightCount, RenderPathToString(renderPath_));
}

// ─────────────────────────────────────────────────────────────────────────────
// CompileShaders
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::CompileShaders() {
    auto* device = GetDevice();

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

    auto meshVSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshVS),
        EngineShaders::GetSource(EngineShader::MeshVS));
    auto meshPSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshPS),
        EngineShaders::GetSource(EngineShader::MeshPS));
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
    auto shadowVSBytes = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::ShadowDepthVS),
        EngineShaders::GetSource(EngineShader::ShadowDepthVS));

    if (meshVSBytes.empty() || meshPSBytes.empty() || gbufPSBytes.empty() ||
        fsVSBytes.empty() || lightPSBytes.empty() || debugPSBytes.empty() ||
        shadowVSBytes.empty())
    {
        UZ_LOG(Renderer, Error, "Failed to compile one or more shaders");
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

    meshVSHandle_   = makeShader(meshVSBytes,   RHIShaderStage::Vertex, "mesh_vs");
    meshPSHandle_   = makeShader(meshPSBytes,   RHIShaderStage::Pixel,  "mesh_ps");
    gbufPSHandle_   = makeShader(gbufPSBytes,   RHIShaderStage::Pixel,  "gbuffer_ps");
    fsVSHandle_     = makeShader(fsVSBytes,     RHIShaderStage::Vertex, "fullscreen_vs");
    lightPSHandle_  = makeShader(lightPSBytes,  RHIShaderStage::Pixel,  "deferred_lighting_ps");
    debugPSHandle_  = makeShader(debugPSBytes,  RHIShaderStage::Pixel,  "gbuffer_debug_ps");
    shadowVSHandle_ = makeShader(shadowVSBytes, RHIShaderStage::Vertex, "shadow_depth_vs");
}

// ─────────────────────────────────────────────────────────────────────────────
// CreatePipelines
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::CreatePipelines() {
    auto* device = GetDevice();

    gbufPipeline_    = CreateGBufferPipeline(device, meshVSHandle_, gbufPSHandle_, bindless_);
    lightPipeline_   = CreateLightingPipeline(device, fsVSHandle_, lightPSHandle_);
    debugPipeline_   = CreateDebugPipeline(device, fsVSHandle_, debugPSHandle_);
    forwardPipeline_ = CreateForwardPipeline(device, meshVSHandle_, meshPSHandle_, bindless_);

    // Shadow depth PSO — vertex shader only, depth-only output, no pixel shader
    {
        RHIInputElementDesc inputElements[] = {
            { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
            { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
            { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
            { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
        };

        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader  = shadowVSHandle_;
        desc.topology      = RHIPrimitiveTopology::TriangleList;
        desc.rasterizer.cullMode              = RHICullMode::Back;
        desc.rasterizer.frontCounterClockwise = false;
        desc.depthStencil.depthEnable         = true;
        desc.depthStencil.depthWrite          = true;
        desc.renderTargetCount                = 0;
        desc.depthStencilFormat               = RHIFormat::D32Float;
        desc.debugName                        = "ShadowDepthPipeline";

        if (!bindless_) {
            desc.inputElements     = inputElements;
            desc.inputElementCount = 4;
        }

        shadowPipeline_ = device->CreateGraphicsPipeline(desc);
    }

    if (!gbufPipeline_.IsValid() || !lightPipeline_.IsValid() ||
        !debugPipeline_.IsValid() || !forwardPipeline_.IsValid() ||
        !shadowPipeline_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create one or more PSOs");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMeshData
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::LoadMeshData() {
    auto* device = GetDevice();

    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    auto mesh = std::make_unique<Mesh>(LoadMesh(meshPath));
    if (!mesh->IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to load cube.uzmesh from '{}'", meshPath);
        return;
    }
    UZ_LOG(Renderer, Info, "Loaded cube.uzmesh: {} vertices, {} indices, stride={}",
           mesh->GetVertexCount(), mesh->GetIndexCount(), mesh->header.vertexStride);

    // Upload vertex buffer
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = mesh->GetVertexDataSize();
    vbDesc.stride    = mesh->header.vertexStride;
    vbDesc.usage     = bindless_ ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory    = RHIMemoryType::Upload;
    vbDesc.debugName = "PBRVertexBuffer";

    vbHandle_ = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle_);
    std::memcpy(vbMapped, mesh->vertexData.data(), mesh->GetVertexDataSize());
    device->UnmapBuffer(vbHandle_);

    vbSrvIndex_ = bindless_ ? device->GetBufferSRVIndex(vbHandle_) : 0u;

    // Upload index buffer
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = mesh->GetIndexDataSize();
    ibDesc.stride    = (mesh->header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage     = RHIBufferUsage::IndexBuffer;
    ibDesc.memory    = RHIMemoryType::Upload;
    ibDesc.debugName = "PBRIndexBuffer";

    ibHandle_ = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle_);
    std::memcpy(ibMapped, mesh->indexData.data(), mesh->GetIndexDataSize());
    device->UnmapBuffer(ibHandle_);

    indexFormat_ = (mesh->header.indexFormat == MeshIndexFormat::UInt32)
        ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;

    cubeMesh_ = std::move(mesh);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadTextures
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::LoadTextures() {
    auto* device  = GetDevice();
    auto* cmdList = GetCommandList();

    pbrTex_.albedoSrv    = LoadPBRTexture(device, cmdList, "Stonebricks_BaseColor.jpg", "StonebricksBaseColor",  pbrTex_.handles[0]);
    pbrTex_.normalSrv    = LoadPBRTexture(device, cmdList, "Stonebricks_Normal.jpg",    "StonebricksNormal",     pbrTex_.handles[1]);
    pbrTex_.roughnessSrv = LoadPBRTexture(device, cmdList, "Stonebricks_Roughness.jpg", "StonebricksRoughness",  pbrTex_.handles[2]);
    pbrTex_.metallicSrv  = LoadPBRTexture(device, cmdList, "Stonebricks_Metallic.jpg",  "StonebricksMetallic",   pbrTex_.handles[3]);
    pbrTex_.aoSrv        = LoadPBRTexture(device, cmdList, "Stonebricks_AO.jpg",        "StonebricksAO",         pbrTex_.handles[4]);

    checkerSrv_ = CreateCheckerTexture(device, cmdList, 512, 16, checkerHandle_);
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateGBuffer / RecreateGBuffer
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::CreateGBuffer() {
    if (!CreateGBufferTextures(GetDevice(), GetWidth(), GetHeight(), gbuf_)) {
        UZ_LOG(Renderer, Error, "Failed to create G-Buffer textures");
    }
}

void PBRTestDemo::RecreateGBuffer(uint32_t width, uint32_t height) {
    DestroyGBufferTextures(GetDevice(), gbuf_);
    if (!CreateGBufferTextures(GetDevice(), width, height, gbuf_)) {
        UZ_LOG(Renderer, Error, "Failed to recreate G-Buffer textures on resize");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnResize(uint32_t width, uint32_t height) {
    RecreateGBuffer(width, height);
    statsOverlay_->SetScreenSize(width, height);
    camera_.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame — Record one frame's rendering commands
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnFrame(float dt) {
    auto* device    = GetDevice();
    auto* cmdList   = GetCommandList();
    auto* swapChain = GetSwapChain();

    auto now = std::chrono::high_resolution_clock::now();
    totalTime_ = std::chrono::duration<float>(now - startTime_).count();

    // ── Stats ────────────────────────────────────────────────────────────
    statsOverlay_->BeginFrame(dt);

    // ── Input ────────────────────────────────────────────────────────────
    input_->BeginFrame();

    // Debug mode toggle: '0' = off, '1'-'7' = G-Buffer channels 0-6
    if (input_->IsKeyJustPressed(Key::Num0)) {
        debugModeValue_ = static_cast<uint32_t>(GBufferDebugMode::Count);
    }
    for (uint32_t k = 0; k < 7; ++k) {
        if (input_->IsKeyJustPressed(static_cast<Key>(static_cast<uint32_t>(Key::Num1) + k))) {
            debugModeValue_ = k;
        }
    }

    // Render path toggle: F switches between Deferred and Forward
    if (input_->IsKeyJustPressed(Key::F)) {
        renderPath_ = (renderPath_ == RenderPath::Deferred)
            ? RenderPath::Forward : RenderPath::Deferred;
        UZ_LOG(Renderer, Info, "Render path switched to: {}",
               RenderPathToString(renderPath_));
    }

    // ── Camera Update ────────────────────────────────────────────────────
    cameraCtrl_.Update(camera_, *input_, dt);

    // ── Upload PerFrameCB ────────────────────────────────────────────────
    cbRing_->BeginFrame(GetFrameIndex());

    PerFrameCB perFrame{};
    perFrame.viewProj    = camera_.GetViewProjection();
    perFrame.invViewProj = ComputeInvViewProj(perFrame.viewProj);
    Vec3 camPos          = camera_.GetPosition();
    perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams  = {totalTime_, dt, static_cast<float>(GetFrameIndex()), 0.f};

    Vec3 sunDir           = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
    perFrame.lightDir     = {sunDir.x, sunDir.y, sunDir.z, 0.f};
    perFrame.lightColor   = {3.0f, 2.85f, 2.7f, 0.f};
    perFrame.ambientColor = {0.03f, 0.03f, 0.05f, 0.f};

    CBAllocation perFrameAlloc = cbRing_->Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── LightingCB — 4 coloured point lights ────────────────────────────
    LightingCB lightingData{};
    lightingData.pointLightCount = kPointLightCount;
    lightingData.pointLights[0] = { {-3.f, 2.f,  2.f}, 8.f,  {4.f, 0.2f, 0.2f}, 0.f };
    lightingData.pointLights[1] = { { 0.f, 3.f, -2.f}, 10.f, {0.2f, 4.f, 0.2f}, 0.f };
    lightingData.pointLights[2] = { { 3.f, 2.f,  2.f}, 8.f,  {0.2f, 0.2f, 4.f}, 0.f };
    lightingData.pointLights[3] = { { 0.f, 1.f,  0.f}, 15.f, {3.f,  3.f,  3.f}, 0.f };

    CBAllocation lightingAlloc = cbRing_->Allocate(sizeof(LightingCB));
    std::memcpy(lightingAlloc.cpuPtr, &lightingData, sizeof(LightingCB));

    // ── Compute shadow cascades ──────────────────────────────────────────
    ShadowConfig shadowCfg{};
    shadowCfg.cascadeCount = 4;
    shadowCfg.resolution   = kShadowResolution;
    shadowCfg.maxDistance   = 50.0f;
    shadowCfg.splitLambda  = 0.75f;
    shadowCfg.normalBias   = 0.02f;
    shadowCfg.depthBias    = 0.005f;

    ShadowCascade cascades[kMaxShadowCascades];
    Vec3 lightShineDir = Vec3{-sunDir.x, -sunDir.y, -sunDir.z};
    ComputeCascades(camera_.GetView(), camera_.GetProjection(), lightShineDir, shadowCfg, cascades);

    // ── Upload ShadowDataCB ──────────────────────────────────────────────
    ShadowDataCB shadowData{};
    for (uint32_t i = 0; i < shadowCfg.cascadeCount; ++i) {
        shadowData.cascadeViewProj[i] = cascades[i].viewProj;
    }
    shadowData.cascadeSplits   = {cascades[0].splitFar, cascades[1].splitFar,
                                  cascades[2].splitFar, cascades[3].splitFar};
    shadowData.shadowAtlasIndex = shadowAtlasSrv_;
    shadowData.cascadeCount     = shadowCfg.cascadeCount;
    shadowData.normalBias       = shadowCfg.normalBias;
    shadowData.depthBias        = shadowCfg.depthBias;
    shadowData.shadowMapSize    = static_cast<float>(kShadowResolution);

    CBAllocation shadowAlloc = cbRing_->Allocate(sizeof(ShadowDataCB));
    std::memcpy(shadowAlloc.cpuPtr, &shadowData, sizeof(ShadowDataCB));

    // ── Build draw list ──────────────────────────────────────────────────
    DrawList drawList;
    drawList.Clear();
    for (int i = 0; i < kCubeCount; ++i) {
        DrawCommand cmd{};
        cmd.sortKey      = SortKey::Opaque(0, 0, 0.f, static_cast<uint32_t>(i));
        cmd.worldMatrix  = BuildModelMatrix(kCubePositions[i], kCubeScale);
        cmd.normalMatrix = ComputeNormalMatrix(cmd.worldMatrix);
        cmd.meshId             = 0;
        cmd.albedoSrvIndex     = pbrTex_.albedoSrv;
        cmd.normalSrvIndex     = pbrTex_.normalSrv;
        cmd.roughnessSrvIndex  = pbrTex_.roughnessSrv;
        cmd.metallicSrvIndex   = pbrTex_.metallicSrv;
        drawList.Add(cmd);
    }
    {
        DrawCommand ground{};
        ground.sortKey      = SortKey::Opaque(0, 0, 0.f, static_cast<uint32_t>(kCubeCount));
        ground.worldMatrix  = BuildModelMatrixNonUniform(
            Vec3{0.f, 0.f, -0.82f},
            Vec3{15.f, 15.f, 0.02f});
        ground.normalMatrix      = ComputeNormalMatrix(ground.worldMatrix);
        ground.meshId            = 0;
        ground.albedoSrvIndex    = checkerSrv_;
        ground.normalSrvIndex    = 0;
        ground.roughnessSrvIndex = 0;
        ground.metallicSrvIndex  = 0;
        drawList.Add(ground);
    }
    drawList.Sort();

    // ── Record frame via render graph ────────────────────────────────────
    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
    rg::RgBuilder rgBuilder(device, rgPool_.get());
    auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

    auto albedoRes     = rgBuilder.ImportTexture("GBuf_Albedo",     gbuf_.albedo,     RHIResourceState::ShaderResource);
    auto normalRes     = rgBuilder.ImportTexture("GBuf_Normal",     gbuf_.normal,     RHIResourceState::ShaderResource);
    auto metalRoughRes = rgBuilder.ImportTexture("GBuf_MetalRough", gbuf_.metalRough, RHIResourceState::ShaderResource);
    auto depthRes      = rgBuilder.ImportTexture("GBuf_Depth",      gbuf_.depth,      RHIResourceState::ShaderResource);

    // ── Shadow pass ──────────────────────────────────────────────────────
    auto shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", shadowAtlas_, RHIResourceState::ShaderResource);
    {
        ShadowPassDesc shadowDesc{};
        shadowDesc.shadowAtlas          = shadowAtlasRes;
        shadowDesc.shadowAtlasTexture   = shadowAtlas_;
        shadowDesc.drawList             = &drawList;
        shadowDesc.cbAllocator          = cbRing_.get();
        shadowDesc.pipeline             = shadowPipeline_;
        shadowDesc.vertexBufferSrvIndex = vbSrvIndex_;
        shadowDesc.vertexBuffer         = vbHandle_;
        shadowDesc.indexBuffer          = ibHandle_;
        shadowDesc.indexFormat          = indexFormat_;
        shadowDesc.indexCount           = cubeMesh_->GetIndexCount();
        shadowDesc.bindless             = bindless_;
        for (uint32_t i = 0; i < shadowCfg.cascadeCount; ++i) {
            shadowDesc.cascades[i] = cascades[i];
        }
        shadowDesc.cascadeCount     = shadowCfg.cascadeCount;
        shadowDesc.atlasResolution  = kShadowResolution;
        AddShadowPass(rgBuilder, shadowDesc);
    }

    if (renderPath_ == RenderPath::Deferred) {
        // ── Deferred path: G-Buffer + lighting/debug ─────────────────────
        {
            GBufferPassDesc gbDesc{};
            gbDesc.albedoTarget         = albedoRes;
            gbDesc.normalTarget         = normalRes;
            gbDesc.metalRoughTarget     = metalRoughRes;
            gbDesc.depthBuffer          = depthRes;
            gbDesc.depthTexture         = gbuf_.depth;
            gbDesc.drawList             = &drawList;
            gbDesc.cbAllocator          = cbRing_.get();
            gbDesc.perFrameAlloc        = perFrameAlloc;
            gbDesc.pipeline             = gbufPipeline_;
            gbDesc.vertexBufferSrvIndex = vbSrvIndex_;
            gbDesc.vertexBuffer         = vbHandle_;
            gbDesc.indexBuffer          = ibHandle_;
            gbDesc.indexFormat          = indexFormat_;
            gbDesc.indexCount           = cubeMesh_->GetIndexCount();
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
            lightDesc.shadowAlloc         = shadowAlloc;
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
            CBAllocation debugAlloc = cbRing_->Allocate(sizeof(DebugCBData));
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
        // ── Forward path: single-pass PBR directly to backbuffer ─────────
        PBRPassDesc fwdDesc{};
        fwdDesc.backbuffer           = bbRes;
        fwdDesc.depthBuffer          = depthRes;
        fwdDesc.depthTexture         = gbuf_.depth;
        fwdDesc.drawList             = &drawList;
        fwdDesc.cbAllocator          = cbRing_.get();
        fwdDesc.perFrameAlloc        = perFrameAlloc;
        fwdDesc.pipeline             = forwardPipeline_;
        fwdDesc.vertexBufferSrvIndex = vbSrvIndex_;
        fwdDesc.vertexBuffer         = vbHandle_;
        fwdDesc.indexBuffer          = ibHandle_;
        fwdDesc.indexFormat          = indexFormat_;
        fwdDesc.indexCount           = cubeMesh_->GetIndexCount();
        fwdDesc.bindless             = bindless_;
        fwdDesc.viewportWidth        = static_cast<float>(swapChain->GetWidth());
        fwdDesc.viewportHeight       = static_cast<float>(swapChain->GetHeight());
        AddPBRPass(rgBuilder, fwdDesc);
    }

    // ── Overlay pass (grid + stats) ──────────────────────────────────────
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

            editorGrid_->Draw(cmd, *cbRing_, camera_);

            statsOverlay_->SetDrawCalls(kCubeCount + 1);
            statsOverlay_->SetVertexCount(
                static_cast<uint32_t>(kCubeCount) * cubeMesh_->GetVertexCount() + 3);
            statsOverlay_->SetTriangleCount(
                static_cast<uint32_t>(kCubeCount) * (cubeMesh_->GetIndexCount() / 3) + 1);
            statsOverlay_->Render(cmd, *cbRing_, GetFrameIndex());

            cmd->EndRenderPass();
        });
    });

    // ── Screenshot pass (optional) ───────────────────────────────────────
    if (screenshotPending_ && GetFrameIndex() == config_.screenshotFrame) {
        rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
            pb.Read(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                screenshotCapture_->CaptureBackBuffer(cmdList, swapChain);
                screenshotSaveDeferred_ = true;
            });
        });
    }

    // ── Present transition ───────────────────────────────────────────────
    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // ── Save screenshot after this frame completes on GPU ────────────────
    // The base class will End()/Execute()/Present()/Signal() after OnFrame
    // returns, so we defer the save to the next frame when the fence is waited.
    if (screenshotSaveDeferred_) {
        // GPU completed previous frame's capture (fence waited by base class)
        GetDevice()->WaitIdle();
        screenshotCapture_->SaveToPNG(config_.screenshotPath);
        screenshotPending_ = false;
        screenshotSaveDeferred_ = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown — Release all GPU resources
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnShutdown() {
    auto* device = GetDevice();

    if (screenshotCapture_) screenshotCapture_->Shutdown();
    if (statsOverlay_) statsOverlay_->Shutdown();
    if (editorGrid_) editorGrid_->Shutdown();
    if (cbRing_) cbRing_->Shutdown();
    if (shaderPipeline_) shaderPipeline_->Shutdown();

    if (shadowAtlas_.IsValid()) device->DestroyTexture(shadowAtlas_);
    DestroyGBufferTextures(device, gbuf_);

    for (auto& h : pbrTex_.handles) {
        if (h.IsValid()) device->DestroyTexture(h);
    }
    if (checkerHandle_.IsValid()) device->DestroyTexture(checkerHandle_);
    if (ibHandle_.IsValid())  device->DestroyBuffer(ibHandle_);
    if (vbHandle_.IsValid())  device->DestroyBuffer(vbHandle_);
    if (shadowPipeline_.IsValid())  device->DestroyPipeline(shadowPipeline_);
    if (forwardPipeline_.IsValid()) device->DestroyPipeline(forwardPipeline_);
    if (debugPipeline_.IsValid())   device->DestroyPipeline(debugPipeline_);
    if (lightPipeline_.IsValid())   device->DestroyPipeline(lightPipeline_);
    if (gbufPipeline_.IsValid())    device->DestroyPipeline(gbufPipeline_);
    if (shadowVSHandle_.IsValid())  device->DestroyShader(shadowVSHandle_);
    if (meshPSHandle_.IsValid())    device->DestroyShader(meshPSHandle_);
    if (debugPSHandle_.IsValid())   device->DestroyShader(debugPSHandle_);
    if (lightPSHandle_.IsValid())   device->DestroyShader(lightPSHandle_);
    if (fsVSHandle_.IsValid())      device->DestroyShader(fsVSHandle_);
    if (gbufPSHandle_.IsValid())    device->DestroyShader(gbufPSHandle_);
    if (meshVSHandle_.IsValid())    device->DestroyShader(meshVSHandle_);
}

} // namespace uz
