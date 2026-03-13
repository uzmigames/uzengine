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
#include <thread>

using namespace DirectX;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kFramesInFlight  = 3;
static constexpr float    kMaxFPS          = 500.f;
static constexpr float    kMinFrameTime    = 1.f / kMaxFPS;
static constexpr int      kCubeCount       = 5;
static constexpr uint32_t kPointLightCount = 4;
static constexpr uint32_t kShadowResolution = 2048;
static constexpr uint32_t kShadowAtlasSize  = kShadowResolution * 2; // 4096x4096 atlas for 2x2 cascade layout

// ─────────────────────────────────────────────────────────────────────────────
// Matrix helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Computes transpose(inverse(model)) for correct normal transformation.
///
/// Uses DirectXMath SIMD for the matrix inverse and transpose.
/// Falls back to identity if the matrix is singular.
static Mat4 ComputeNormalMatrix(const Mat4& model) {
    XMFLOAT4X4 xmModel;
    std::memcpy(&xmModel, &model, sizeof(Mat4));

    XMMATRIX m = XMLoadFloat4x4(&xmModel);
    XMVECTOR det;
    XMMATRIX inv = XMMatrixInverse(&det, m);

    // Singular matrix — normal transform undefined, return identity
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
///
/// Scale is applied per-axis before translation, allowing flattened shapes
/// (e.g., a cube squashed into a thin slab).
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
///
/// The lighting pass uses `invViewProj` to reconstruct world-space positions from
/// screen-space UV + sampled depth. Returns identity if the matrix is singular.
static Mat4 ComputeInvViewProj(const Mat4& viewProj) {
    return viewProj.Inverse();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene data
// ─────────────────────────────────────────────────────────────────────────────

/// World-space positions for the 5-cube row, evenly spaced along X.
///
/// Spacing of 2 units with scale 0.8 leaves ~1.2 units of clearance between cubes.
static constexpr Vec3 kCubePositions[kCubeCount] = {
    {-4.f, 0.f, 0.f},
    {-2.f, 0.f, 0.f},
    { 0.f, 0.f, 0.f},
    { 2.f, 0.f, 0.f},
    { 4.f, 0.f, 0.f},
};
static constexpr float kCubeScale = 0.8f;

// ─────────────────────────────────────────────────────────────────────────────
// PBR texture bundle (per-material maps)
// ─────────────────────────────────────────────────────────────────────────────

/// GPU-resident PBR texture handles and their SRV descriptor indices.
struct PBRTextures {
    RHITextureHandle handles[5] = {};
    uint32_t albedoSrv    = 0;
    uint32_t normalSrv    = 0;
    uint32_t roughnessSrv = 0;
    uint32_t metallicSrv  = 0;
    uint32_t aoSrv        = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// G-Buffer transient textures (recreated on resize)
// ─────────────────────────────────────────────────────────────────────────────

/// Physical G-Buffer texture handles used across G-Buffer and lighting passes.
///
/// The render graph declares these as transient but the physical textures are
/// maintained externally so their SRV indices remain stable within the frame.
struct GBufferTextures {
    RHITextureHandle albedo;      ///< RGBA8Unorm — base colour + alpha
    RHITextureHandle normal;      ///< RGBA16Float — world-space normals
    RHITextureHandle metalRough;  ///< RGBA8Unorm — R=metallic, G=roughness
    RHITextureHandle depth;       ///< D32Float — primary depth buffer

    uint32_t albedoSrv     = 0;
    uint32_t normalSrv     = 0;
    uint32_t metalRoughSrv = 0;
    uint32_t depthSrv      = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture creation helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Loads an image from disk, uploads it to the GPU, and returns the SRV index.
///
/// # Arguments
/// * `device`    — RHI device. Must outlive the returned texture handle.
/// * `cmdList`   — Open command list used for the upload barrier.
/// * `filename`  — Filename relative to `assets/` next to the executable.
/// * `debugName` — Name shown in GPU validation layers.
/// * `outHandle` — Populated with the created GPU texture handle.
///
/// # Returns
/// Non-zero SRV descriptor index on success, 0 if the texture file was not
/// found or GPU texture creation failed. Shaders treat 0 as "use default value".
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
///
/// Generates a `size x size` RGBA8 texture on the CPU, uploads it to GPU,
/// and returns the SRV heap index. Used for the ground plane.
///
/// # Parameters
/// * `device` — RHI device for resource creation.
/// * `cmdList` — Command list for the staging upload.
/// * `size` — Texture resolution (width = height).
/// * `tileCount` — Number of checker tiles per axis (e.g., 16 = 16x16 grid).
/// * `outHandle` — Receives the created texture handle.
///
/// # Returns
/// SRV heap index on success, 0 on failure.
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
///
/// Depth is created with `DepthStencil | ShaderResource` usage so it can be
/// read in the lighting pass. Returns false if any texture creation fails.
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
///
/// # Returns
/// Valid pipeline handle on success, invalid handle on shader or PSO creation failure.
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
    // Three G-Buffer render targets
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;   // albedo
    desc.renderTargetFormats[1]           = RHIFormat::RGBA16Float;  // normals
    desc.renderTargetFormats[2]           = RHIFormat::RGBA8Unorm;   // metallic/roughness
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
///
/// # Returns
/// Valid pipeline handle on success, invalid handle on failure.
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

/// Creates the fullscreen debug visualization PSO (fullscreen_vs + gbuffer_debug_ps, 1 RT, no depth).
///
/// # Returns
/// Valid pipeline handle on success, invalid handle on failure.
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
///
/// Uses the same vertex input layout as the G-Buffer pipeline but outputs
/// to a single RGBA8Unorm render target with forward Cook-Torrance lighting.
///
/// # Returns
/// Valid pipeline handle on success, invalid handle on shader or PSO creation failure.
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
// Main Demo
// ─────────────────────────────────────────────────────────────────────────────

bool RunPBRTestDemo(const PBRTestConfig& config) {
    const uint32_t frameCount = config.frameCount;

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine \xe2\x80\x94 PBR Test (Cook-Torrance)";
    winDesc.width = 1280;
    winDesc.height = 720;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(Renderer, Error, "Failed to create window");
        return false;
    }
    window.Show();

    // ── Input ────────────────────────────────────────────────────────────
    InputSystem input;
    input.Init();
    window.SetInputSystem(&input);

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = config.backend;
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = true;
#endif

    if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

    IRHIDevice* device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(Renderer, Error, "Failed to create RHI device (backend={})",
               static_cast<int>(config.backend));
        return false;
    }

    // ── Swapchain ────────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle  = window.GetNativeHandle();
    scDesc.displayHandle = window.GetNativeDisplayHandle();
    scDesc.width         = static_cast<uint32_t>(window.GetWidth());
    scDesc.height        = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount   = kFramesInFlight;
    scDesc.format        = RHIFormat::RGBA8Unorm;
    scDesc.vsync         = false;

    IRHISwapChain* swapChain = device->CreateSwapChain(scDesc);
    if (!swapChain) {
        UZ_LOG(Renderer, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Command List ─────────────────────────────────────────────────────
    IRHICommandList* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(Renderer, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    // ── Fences ───────────────────────────────────────────────────────────
    IRHIFence* frameFences[kFramesInFlight] = {};
    uint64_t   fenceValues[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = device->CreateFence();
        if (!frameFences[i]) {
            UZ_LOG(Renderer, Error, "Failed to create frame fence {}", i);
            for (uint32_t j = 0; j < i; ++j) device->DestroyFence(frameFences[j]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
    }

    // ── Shader Pipeline ──────────────────────────────────────────────────
    const bool bindless = device->IsBindless();

    ShaderCompiler compiler;
    if (!compiler.Init()) {
        UZ_LOG(Renderer, Error, "Failed to init shader compiler");
        return false;
    }

    ShaderPipeline shaderPipeline;
    ShaderPipeline::Config pipelineCfg;
    pipelineCfg.cacheDir      = (uz::GetExeDir() / "cache/shaders").string();
    pipelineCfg.targetBackend = bindless ? "dx12_sm66" : "dx12_sm50";
    pipelineCfg.bindless      = bindless;
    shaderPipeline.Init(pipelineCfg);
    shaderPipeline.SetCompiler(&compiler);

    ShaderCompilerFXC compilerFXC;
    if (!bindless) {
        if (!compilerFXC.Init()) {
            UZ_LOG(Renderer, Error, "Failed to init FXC shader compiler");
            return false;
        }
        shaderPipeline.SetCompilerFXC(&compilerFXC);
    }

    // ── Compile shaders from embedded sources ────────────────────────────
    auto meshVSBytes = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshVS),
        EngineShaders::GetSource(EngineShader::MeshVS));
    auto meshPSBytes = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshPS),
        EngineShaders::GetSource(EngineShader::MeshPS));
    auto gbufPSBytes = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GbufferPS),
        EngineShaders::GetSource(EngineShader::GbufferPS));
    auto fsVSBytes = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::FullscreenVS),
        EngineShaders::GetSource(EngineShader::FullscreenVS));
    auto lightPSBytes = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::DeferredLightingPS),
        EngineShaders::GetSource(EngineShader::DeferredLightingPS));
    auto debugPSBytes = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GbufferDebugPS),
        EngineShaders::GetSource(EngineShader::GbufferDebugPS));

    auto shadowVSBytes = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::ShadowDepthVS),
        EngineShaders::GetSource(EngineShader::ShadowDepthVS));

    if (meshVSBytes.empty() || meshPSBytes.empty() || gbufPSBytes.empty() ||
        fsVSBytes.empty() || lightPSBytes.empty() || debugPSBytes.empty() ||
        shadowVSBytes.empty())
    {
        UZ_LOG(Renderer, Error, "Failed to compile one or more shaders");
        return false;
    }

    // ── Shader handles ───────────────────────────────────────────────────
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

    RHIShaderHandle meshVSHandle  = makeShader(meshVSBytes,  RHIShaderStage::Vertex, "mesh_vs");
    RHIShaderHandle meshPSHandle  = makeShader(meshPSBytes,  RHIShaderStage::Pixel,  "mesh_ps");
    RHIShaderHandle gbufPSHandle  = makeShader(gbufPSBytes,  RHIShaderStage::Pixel,  "gbuffer_ps");
    RHIShaderHandle fsVSHandle    = makeShader(fsVSBytes,    RHIShaderStage::Vertex, "fullscreen_vs");
    RHIShaderHandle lightPSHandle = makeShader(lightPSBytes, RHIShaderStage::Pixel,  "deferred_lighting_ps");
    RHIShaderHandle debugPSHandle = makeShader(debugPSBytes, RHIShaderStage::Pixel,  "gbuffer_debug_ps");
    RHIShaderHandle shadowVSHandle = makeShader(shadowVSBytes, RHIShaderStage::Vertex, "shadow_depth_vs");

    // ── PSOs ─────────────────────────────────────────────────────────────
    RHIPipelineHandle gbufPipeline    = CreateGBufferPipeline(device, meshVSHandle, gbufPSHandle, bindless);
    RHIPipelineHandle lightPipeline   = CreateLightingPipeline(device, fsVSHandle, lightPSHandle);
    RHIPipelineHandle debugPipeline   = CreateDebugPipeline(device, fsVSHandle, debugPSHandle);
    RHIPipelineHandle forwardPipeline = CreateForwardPipeline(device, meshVSHandle, meshPSHandle, bindless);

    // Shadow depth PSO — vertex shader only, depth-only output, no pixel shader
    RHIPipelineHandle shadowPipeline;
    {
        RHIInputElementDesc inputElements[] = {
            { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
            { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
            { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
            { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
        };

        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader  = shadowVSHandle;
        // No pixel shader — depth-only pass
        desc.topology      = RHIPrimitiveTopology::TriangleList;
        desc.rasterizer.cullMode              = RHICullMode::Back;
        desc.rasterizer.frontCounterClockwise = false;
        desc.depthStencil.depthEnable         = true;
        desc.depthStencil.depthWrite          = true;
        desc.renderTargetCount                = 0;
        desc.depthStencilFormat               = RHIFormat::D32Float;
        desc.debugName                        = "ShadowDepthPipeline";

        if (!bindless) {
            desc.inputElements     = inputElements;
            desc.inputElementCount = 4;
        }

        shadowPipeline = device->CreateGraphicsPipeline(desc);
    }

    if (!gbufPipeline.IsValid() || !lightPipeline.IsValid() ||
        !debugPipeline.IsValid() || !forwardPipeline.IsValid() ||
        !shadowPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create one or more PSOs");
        return false;
    }

    // ── Load cube mesh ────────────────────────────────────────────────────
    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    Mesh cubeMesh = LoadMesh(meshPath);
    if (!cubeMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to load cube.uzmesh from '{}'", meshPath);
        return false;
    }
    UZ_LOG(Renderer, Info, "Loaded cube.uzmesh: {} vertices, {} indices, stride={}",
           cubeMesh.GetVertexCount(), cubeMesh.GetIndexCount(), cubeMesh.header.vertexStride);

    // ── Upload vertex buffer ──────────────────────────────────────────────
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = cubeMesh.GetVertexDataSize();
    vbDesc.stride    = cubeMesh.header.vertexStride;
    vbDesc.usage     = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory    = RHIMemoryType::Upload;
    vbDesc.debugName = "PBRVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle);
    std::memcpy(vbMapped, cubeMesh.vertexData.data(), cubeMesh.GetVertexDataSize());
    device->UnmapBuffer(vbHandle);

    uint32_t vbSrvIndex = bindless ? device->GetBufferSRVIndex(vbHandle) : 0u;

    // ── Upload index buffer ───────────────────────────────────────────────
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = cubeMesh.GetIndexDataSize();
    ibDesc.stride    = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage     = RHIBufferUsage::IndexBuffer;
    ibDesc.memory    = RHIMemoryType::Upload;
    ibDesc.debugName = "PBRIndexBuffer";

    RHIBufferHandle ibHandle = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle);
    std::memcpy(ibMapped, cubeMesh.indexData.data(), cubeMesh.GetIndexDataSize());
    device->UnmapBuffer(ibHandle);

    const RHIIndexFormat indexFormat = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32)
        ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;

    // ── Load PBR textures ─────────────────────────────────────────────────
    PBRTextures pbrTex;
    pbrTex.albedoSrv    = LoadPBRTexture(device, cmdList, "Stonebricks_BaseColor.jpg", "StonebricksBaseColor",  pbrTex.handles[0]);
    pbrTex.normalSrv    = LoadPBRTexture(device, cmdList, "Stonebricks_Normal.jpg",    "StonebricksNormal",     pbrTex.handles[1]);
    pbrTex.roughnessSrv = LoadPBRTexture(device, cmdList, "Stonebricks_Roughness.jpg", "StonebricksRoughness",  pbrTex.handles[2]);
    pbrTex.metallicSrv  = LoadPBRTexture(device, cmdList, "Stonebricks_Metallic.jpg",  "StonebricksMetallic",   pbrTex.handles[3]);
    pbrTex.aoSrv        = LoadPBRTexture(device, cmdList, "Stonebricks_AO.jpg",        "StonebricksAO",         pbrTex.handles[4]);

    // ── Checker texture for ground plane ─────────────────────────────────
    RHITextureHandle checkerHandle;
    uint32_t checkerSrv = CreateCheckerTexture(device, cmdList, 512, 16, checkerHandle);

    // ── G-Buffer textures ─────────────────────────────────────────────────
    GBufferTextures gbuf;
    if (!CreateGBufferTextures(device, scDesc.width, scDesc.height, gbuf)) {
        return false;
    }

    // ── Shadow atlas texture ─────────────────────────────────────────────
    RHITextureDesc shadowAtlasDesc{};
    shadowAtlasDesc.width     = kShadowAtlasSize;
    shadowAtlasDesc.height    = kShadowAtlasSize;
    shadowAtlasDesc.format    = RHIFormat::D32Float;
    shadowAtlasDesc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    shadowAtlasDesc.debugName = "ShadowAtlas";
    RHITextureHandle shadowAtlas    = device->CreateTexture(shadowAtlasDesc);
    uint32_t         shadowAtlasSrv = device->GetTextureSRVIndex(shadowAtlas);

    // ── CB ring allocator ─────────────────────────────────────────────────
    CBRingAllocator cbRing;
    // Extra capacity for LightingCB (2304 bytes) + PerFrameCB (256) + 5*PerObjectCB (1280)
    cbRing.Init(device, kFramesInFlight, 128 * 1024);

    // ── Editor Grid ───────────────────────────────────────────────────────
    EditorGrid editorGrid;
    if (!editorGrid.Init(device, shaderPipeline, RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "Failed to initialize editor grid");
        return false;
    }

    // ── Stats Overlay ─────────────────────────────────────────────────────
    StatsOverlay statsOverlay;
    {
        UploadContext upload(device);
        cmdList->Begin();
        if (!statsOverlay.Init(device, cmdList, upload, shaderPipeline,
                               RHIFormat::RGBA8Unorm, winDesc.width, winDesc.height, bindless)) {
            UZ_LOG(Renderer, Error, "Failed to initialize stats overlay");
            return false;
        }
        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
    }

    // ── Screenshot Capture ────────────────────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        if (!screenshotCapture.Init(device, scDesc.width, scDesc.height)) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            return false;
        }
    }

    // ── Render Graph Resource Pool (persistent across frames) ────────────
    rg::RgResourcePool rgPool(device);

    // ── Camera ────────────────────────────────────────────────────────────
    Camera camera;
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect  = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera.SetPerspective(vFovRad,
        static_cast<float>(winDesc.width) / static_cast<float>(winDesc.height),
        0.1f, 1000.f);

    // Position camera above and slightly in front to view the 5-cube row clearly
    camera.SetPosition({0.f, 5.f, 2.f});
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
        Vec3 pos = camera.GetPosition();
        camera.LookAt(pos + forward, Vec3::Up());
    }

    FPSCameraController cameraCtrl;
    cameraCtrl.InitFromCamera(camera);
    cameraCtrl.SetMoveSpeed(10.f);

    // ── Debug mode — which G-Buffer channel to visualise (Count = off) ───
    uint32_t debugModeValue = static_cast<uint32_t>(GBufferDebugMode::Count);

    // ── Render path (F key toggles Deferred/Forward) ─────────────────────
    RenderPath renderPath = GetDefaultRenderPath();

    // ── Render Loop ───────────────────────────────────────────────────────
    uint32_t frameIndex       = 0;
    uint64_t globalFenceValue = 0;
    auto     prevTime         = std::chrono::high_resolution_clock::now();
    auto     startTime        = prevTime;

    UZ_LOG(Renderer, Info, "PBR test demo started — 5 cubes, {} point lights + directional sun "
           "(F=toggle render path, current={})", kPointLightCount, RenderPathToString(renderPath));

    while (window.PumpMessages() && window.IsOpen()) {
        // Skip rendering while minimized — swapchain surface is 0x0
        if (window.IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (frameCount > 0 && frameIndex >= frameCount) break;

        // ── Timing ───────────────────────────────────────────────────────
        auto  now       = std::chrono::high_resolution_clock::now();
        float dt        = std::chrono::duration<float>(now - prevTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        prevTime = now;

        // ── Stats ─────────────────────────────────────────────────────────
        statsOverlay.BeginFrame(dt);

        // ── Input ─────────────────────────────────────────────────────────
        input.BeginFrame();

        // Debug mode toggle: '0' = off, '1'-'7' = G-Buffer channels 0-6
        if (input.IsKeyJustPressed(Key::Num0)) {
            debugModeValue = static_cast<uint32_t>(GBufferDebugMode::Count);
        }
        for (uint32_t k = 0; k < 7; ++k) {
            if (input.IsKeyJustPressed(static_cast<Key>(static_cast<uint32_t>(Key::Num1) + k))) {
                debugModeValue = k;
            }
        }

        // Render path toggle: F switches between Deferred and Forward
        if (input.IsKeyJustPressed(Key::F)) {
            renderPath = (renderPath == RenderPath::Deferred)
                ? RenderPath::Forward : RenderPath::Deferred;
            UZ_LOG(Renderer, Info, "Render path switched to: {}",
                   RenderPathToString(renderPath));
        }

        // ── Camera Update ─────────────────────────────────────────────────
        cameraCtrl.Update(camera, input, dt);

        // ── Resize ───────────────────────────────────────────────────────
        if (window.WasResized()) {
            device->WaitIdle();
            const uint32_t newW = static_cast<uint32_t>(window.GetWidth());
            const uint32_t newH = static_cast<uint32_t>(window.GetHeight());

            swapChain->Resize(newW, newH);

            // Recreate all G-Buffer textures at the new resolution
            DestroyGBufferTextures(device, gbuf);
            if (!CreateGBufferTextures(device, newW, newH, gbuf)) {
                UZ_LOG(Renderer, Error, "Failed to recreate G-Buffer textures on resize");
                break;
            }

            statsOverlay.SetScreenSize(newW, newH);
            window.ClearResizedFlag();
            camera.SetAspectRatio(static_cast<float>(newW) / static_cast<float>(newH));
        }

        // Wait for the oldest in-flight frame before reusing its ring slot
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // ── Upload PerFrameCB ─────────────────────────────────────────────
        cbRing.BeginFrame(frameIndex);

        PerFrameCB perFrame{};
        perFrame.viewProj    = camera.GetViewProjection();
        perFrame.invViewProj = ComputeInvViewProj(perFrame.viewProj);
        Vec3 camPos          = camera.GetPosition();
        perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
        perFrame.timeParams  = {totalTime, dt, static_cast<float>(frameIndex), 0.f};

        // PBR lighting — warm directional sun + very dark blue-tinted ambient
        Vec3 sunDir           = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
        perFrame.lightDir     = {sunDir.x, sunDir.y, sunDir.z, 0.f};
        perFrame.lightColor   = {3.0f, 2.85f, 2.7f, 0.f};
        perFrame.ambientColor = {0.03f, 0.03f, 0.05f, 0.f};

        CBAllocation perFrameAlloc = cbRing.Allocate(sizeof(PerFrameCB));
        std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

        // ── LightingCB — 4 coloured point lights ─────────────────────────
        // Colours chosen to make each G-Buffer channel clearly distinguishable
        // under the deferred lighting pass.
        LightingCB lightingData{};
        lightingData.pointLightCount = kPointLightCount;
        lightingData.pointLights[0] = { {-3.f, 2.f,  2.f}, 8.f,  {4.f, 0.2f, 0.2f}, 0.f }; // red
        lightingData.pointLights[1] = { { 0.f, 3.f, -2.f}, 10.f, {0.2f, 4.f, 0.2f}, 0.f }; // green
        lightingData.pointLights[2] = { { 3.f, 2.f,  2.f}, 8.f,  {0.2f, 0.2f, 4.f}, 0.f }; // blue
        lightingData.pointLights[3] = { { 0.f, 1.f,  0.f}, 15.f, {3.f,  3.f,  3.f}, 0.f }; // white fill

        CBAllocation lightingAlloc = cbRing.Allocate(sizeof(LightingCB));
        std::memcpy(lightingAlloc.cpuPtr, &lightingData, sizeof(LightingCB));

        // ── Compute shadow cascades ──────────────────────────────────────
        ShadowConfig shadowCfg{};
        shadowCfg.cascadeCount = 4;
        shadowCfg.resolution   = kShadowResolution;
        shadowCfg.maxDistance   = 50.0f;
        shadowCfg.splitLambda  = 0.75f;
        shadowCfg.normalBias   = 0.02f;
        shadowCfg.depthBias    = 0.005f;

        ShadowCascade cascades[kMaxShadowCascades];
        // sunDir points toward the sun; ComputeCascades expects the direction
        // the light shines (away from the sun), so negate it.
        Vec3 lightShineDir = Vec3{-sunDir.x, -sunDir.y, -sunDir.z};
        ComputeCascades(camera.GetView(), camera.GetProjection(), lightShineDir, shadowCfg, cascades);

        // ── Upload ShadowDataCB ──────────────────────────────────────────
        ShadowDataCB shadowData{};
        for (uint32_t i = 0; i < shadowCfg.cascadeCount; ++i) {
            shadowData.cascadeViewProj[i] = cascades[i].viewProj;
        }
        shadowData.cascadeSplits   = {cascades[0].splitFar, cascades[1].splitFar,
                                      cascades[2].splitFar, cascades[3].splitFar};
        shadowData.shadowAtlasIndex = shadowAtlasSrv;
        shadowData.cascadeCount     = shadowCfg.cascadeCount;
        shadowData.normalBias       = shadowCfg.normalBias;
        shadowData.depthBias        = shadowCfg.depthBias;
        shadowData.shadowMapSize    = static_cast<float>(kShadowResolution);

        CBAllocation shadowAlloc = cbRing.Allocate(sizeof(ShadowDataCB));
        std::memcpy(shadowAlloc.cpuPtr, &shadowData, sizeof(ShadowDataCB));

        // ── Build draw list for G-Buffer / forward pass ──────────────────
        DrawList drawList;
        drawList.Clear();
        for (int i = 0; i < kCubeCount; ++i) {
            DrawCommand cmd{};
            cmd.sortKey      = SortKey::Opaque(0, 0, 0.f, static_cast<uint32_t>(i));
            cmd.worldMatrix  = BuildModelMatrix(kCubePositions[i], kCubeScale);
            cmd.normalMatrix = ComputeNormalMatrix(cmd.worldMatrix);
            cmd.meshId             = 0;
            cmd.albedoSrvIndex     = pbrTex.albedoSrv;
            cmd.normalSrvIndex     = pbrTex.normalSrv;
            cmd.roughnessSrvIndex  = pbrTex.roughnessSrv;
            cmd.metallicSrvIndex   = pbrTex.metallicSrv;
            drawList.Add(cmd);
        }
        // Ground plane — a cube flattened into a thin slab so deferred
        // lighting is visible on the floor beneath the cubes.
        {
            DrawCommand ground{};
            ground.sortKey      = SortKey::Opaque(0, 0, 0.f, static_cast<uint32_t>(kCubeCount));
            ground.worldMatrix  = BuildModelMatrixNonUniform(
                Vec3{0.f, 0.f, -0.82f},  // just below cubes (Z-up: cubes at Z=0, halfExtent=0.8)
                Vec3{15.f, 15.f, 0.02f}); // wide on XY, thin on Z
            ground.normalMatrix      = ComputeNormalMatrix(ground.worldMatrix);
            ground.meshId            = 0;
            ground.albedoSrvIndex    = checkerSrv;
            ground.normalSrvIndex    = 0;
            ground.roughnessSrvIndex = 0;
            ground.metallicSrvIndex  = 0;
            drawList.Add(ground);
        }

        drawList.Sort();

        // ── Record frame via render graph ─────────────────────────────────
        bool capturedThisFrame = false;
        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
        rg::RgBuilder rgBuilder(device, &rgPool);
        auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

        // Import G-Buffer textures and depth — always imported so the render
        // graph can schedule barriers regardless of which path is active.
        auto albedoRes     = rgBuilder.ImportTexture("GBuf_Albedo",     gbuf.albedo,     RHIResourceState::ShaderResource);
        auto normalRes     = rgBuilder.ImportTexture("GBuf_Normal",     gbuf.normal,     RHIResourceState::ShaderResource);
        auto metalRoughRes = rgBuilder.ImportTexture("GBuf_MetalRough", gbuf.metalRough, RHIResourceState::ShaderResource);
        auto depthRes      = rgBuilder.ImportTexture("GBuf_Depth",      gbuf.depth,      RHIResourceState::ShaderResource);

        // ── Shadow pass (runs before lighting in both paths) ─────────
        auto shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", shadowAtlas, RHIResourceState::ShaderResource);
        {
            ShadowPassDesc shadowDesc{};
            shadowDesc.shadowAtlas          = shadowAtlasRes;
            shadowDesc.shadowAtlasTexture   = shadowAtlas;
            shadowDesc.drawList             = &drawList;
            shadowDesc.cbAllocator          = &cbRing;
            shadowDesc.pipeline             = shadowPipeline;
            shadowDesc.vertexBufferSrvIndex = vbSrvIndex;
            shadowDesc.vertexBuffer         = vbHandle;
            shadowDesc.indexBuffer          = ibHandle;
            shadowDesc.indexFormat          = indexFormat;
            shadowDesc.indexCount           = cubeMesh.GetIndexCount();
            shadowDesc.bindless             = bindless;
            for (uint32_t i = 0; i < shadowCfg.cascadeCount; ++i) {
                shadowDesc.cascades[i] = cascades[i];
            }
            shadowDesc.cascadeCount     = shadowCfg.cascadeCount;
            shadowDesc.atlasResolution  = kShadowResolution;
            AddShadowPass(rgBuilder, shadowDesc);
        }

        if (renderPath == RenderPath::Deferred) {
            // ── Deferred path: G-Buffer + lighting/debug ─────────────────
            {
                GBufferPassDesc gbDesc{};
                gbDesc.albedoTarget         = albedoRes;
                gbDesc.normalTarget         = normalRes;
                gbDesc.metalRoughTarget     = metalRoughRes;
                gbDesc.depthBuffer          = depthRes;
                gbDesc.depthTexture         = gbuf.depth;
                gbDesc.drawList             = &drawList;
                gbDesc.cbAllocator          = &cbRing;
                gbDesc.perFrameAlloc        = perFrameAlloc;
                gbDesc.pipeline             = gbufPipeline;
                gbDesc.vertexBufferSrvIndex = vbSrvIndex;
                gbDesc.vertexBuffer         = vbHandle;
                gbDesc.indexBuffer          = ibHandle;
                gbDesc.indexFormat          = indexFormat;
                gbDesc.indexCount           = cubeMesh.GetIndexCount();
                gbDesc.bindless             = bindless;
                gbDesc.viewportWidth        = static_cast<float>(swapChain->GetWidth());
                gbDesc.viewportHeight       = static_cast<float>(swapChain->GetHeight());
                AddGBufferPass(rgBuilder, gbDesc);
            }

            const bool debugActive = (debugModeValue < static_cast<uint32_t>(GBufferDebugMode::Count));

            if (!debugActive) {
                LightingPassDesc lightDesc{};
                lightDesc.backbuffer          = bbRes;
                lightDesc.albedoGBuffer       = albedoRes;
                lightDesc.normalGBuffer       = normalRes;
                lightDesc.metalRoughGBuffer   = metalRoughRes;
                lightDesc.depthGBuffer        = depthRes;
                lightDesc.perFrameAlloc       = perFrameAlloc;
                lightDesc.lightingAlloc       = lightingAlloc;
                lightDesc.pipeline            = lightPipeline;
                lightDesc.albedoSrvIndex      = gbuf.albedoSrv;
                lightDesc.normalSrvIndex      = gbuf.normalSrv;
                lightDesc.metalRoughSrvIndex  = gbuf.metalRoughSrv;
                lightDesc.depthSrvIndex       = gbuf.depthSrv;
                lightDesc.albedoTexture       = gbuf.albedo;
                lightDesc.normalTexture       = gbuf.normal;
                lightDesc.metalRoughTexture   = gbuf.metalRough;
                lightDesc.depthTextureForSrv  = gbuf.depth;
                lightDesc.shadowAlloc         = shadowAlloc;
                lightDesc.bindless            = bindless;
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
                CBAllocation debugAlloc = cbRing.Allocate(sizeof(DebugCBData));
                DebugCBData debugCBData{static_cast<uint32_t>(debugModeValue), {0, 0, 0}};
                std::memcpy(debugAlloc.cpuPtr, &debugCBData, sizeof(DebugCBData));
                dbgDesc.debugAlloc         = debugAlloc;
                dbgDesc.pipeline           = debugPipeline;
                dbgDesc.mode               = static_cast<GBufferDebugMode>(debugModeValue);
                dbgDesc.albedoSrvIndex     = gbuf.albedoSrv;
                dbgDesc.normalSrvIndex     = gbuf.normalSrv;
                dbgDesc.metalRoughSrvIndex = gbuf.metalRoughSrv;
                dbgDesc.depthSrvIndex      = gbuf.depthSrv;
                dbgDesc.albedoTexture      = gbuf.albedo;
                dbgDesc.normalTexture      = gbuf.normal;
                dbgDesc.metalRoughTexture  = gbuf.metalRough;
                dbgDesc.depthTextureForSrv = gbuf.depth;
                dbgDesc.bindless           = bindless;
                dbgDesc.viewportWidth      = static_cast<float>(swapChain->GetWidth());
                dbgDesc.viewportHeight     = static_cast<float>(swapChain->GetHeight());
                AddGBufferDebugPass(rgBuilder, dbgDesc);
            }
        } else {
            // ── Forward path: single-pass PBR directly to backbuffer ─────
            PBRPassDesc fwdDesc{};
            fwdDesc.backbuffer           = bbRes;
            fwdDesc.depthBuffer          = depthRes;
            fwdDesc.depthTexture         = gbuf.depth;
            fwdDesc.drawList             = &drawList;
            fwdDesc.cbAllocator          = &cbRing;
            fwdDesc.perFrameAlloc        = perFrameAlloc;
            fwdDesc.pipeline             = forwardPipeline;
            fwdDesc.vertexBufferSrvIndex = vbSrvIndex;
            fwdDesc.vertexBuffer         = vbHandle;
            fwdDesc.indexBuffer          = ibHandle;
            fwdDesc.indexFormat          = indexFormat;
            fwdDesc.indexCount           = cubeMesh.GetIndexCount();
            fwdDesc.bindless             = bindless;
            fwdDesc.viewportWidth        = static_cast<float>(swapChain->GetWidth());
            fwdDesc.viewportHeight       = static_cast<float>(swapChain->GetHeight());
            AddPBRPass(rgBuilder, fwdDesc);
        }

        // Grid + stats overlay pass (composited on top of the lit backbuffer).
        // The grid needs depth testing against the G-Buffer depth so it doesn't
        // draw over the meshes.
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

                editorGrid.Draw(cmd, cbRing, camera);

                statsOverlay.SetDrawCalls(kCubeCount + 1 /*grid*/);
                statsOverlay.SetVertexCount(
                    static_cast<uint32_t>(kCubeCount) * cubeMesh.GetVertexCount() + 3);
                statsOverlay.SetTriangleCount(
                    static_cast<uint32_t>(kCubeCount) * (cubeMesh.GetIndexCount() / 3) + 1);
                statsOverlay.Render(cmd, cbRing, frameIndex);

                cmd->EndRenderPass();
            });
        });

        // Screenshot pass (optional).
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
                pb.Read(bbRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                    screenshotCapture.CaptureBackBuffer(cmdList, swapChain);
                    capturedThisFrame = true;
                });
            });
        }

        // Present transition pass.
        rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::Present);
            pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
        });

        rgBuilder.Compile();
        rgBuilder.Execute(cmdList);

        cmdList->End();
        cmdList->Execute();

        swapChain->Present(false);

        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);

        // ── Save screenshot after GPU completes ───────────────────────────
        if (capturedThisFrame) {
            frameFences[allocFrame]->WaitCPU(globalFenceValue);
            screenshotCapture.SaveToPNG(config.screenshotPath);
            screenshotPending = false;
        }

        frameIndex++;

        // ── FPS Cap ───────────────────────────────────────────────────────
        auto  frameEnd = std::chrono::high_resolution_clock::now();
        float elapsed  = std::chrono::duration<float>(frameEnd - now).count();
        if (elapsed < kMinFrameTime) {
            auto sleepUs = std::chrono::microseconds(
                static_cast<int64_t>((kMinFrameTime - elapsed) * 1e6f));
            std::this_thread::sleep_for(sleepUs);
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
    device->WaitIdle();

    auto  endTime  = std::chrono::high_resolution_clock::now();
    float totalMs  = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs    = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.f;

    UZ_LOG(Renderer, Info,
           "PBR test demo: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.f / avgMs : 0.f);

    screenshotCapture.Shutdown();
    statsOverlay.Shutdown();
    editorGrid.Shutdown();
    cbRing.Shutdown();
    shaderPipeline.Shutdown();

    if (shadowAtlas.IsValid()) device->DestroyTexture(shadowAtlas);
    DestroyGBufferTextures(device, gbuf);

    for (auto& h : pbrTex.handles) {
        if (h.IsValid()) device->DestroyTexture(h);
    }
    if (checkerHandle.IsValid()) device->DestroyTexture(checkerHandle);
    device->DestroyBuffer(ibHandle);
    device->DestroyBuffer(vbHandle);
    device->DestroyPipeline(shadowPipeline);
    device->DestroyPipeline(forwardPipeline);
    device->DestroyPipeline(debugPipeline);
    device->DestroyPipeline(lightPipeline);
    device->DestroyPipeline(gbufPipeline);
    device->DestroyShader(shadowVSHandle);
    device->DestroyShader(meshPSHandle);
    device->DestroyShader(debugPSHandle);
    device->DestroyShader(lightPSHandle);
    device->DestroyShader(fsVSHandle);
    device->DestroyShader(gbufPSHandle);
    device->DestroyShader(meshVSHandle);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return true;
}

} // namespace uz
