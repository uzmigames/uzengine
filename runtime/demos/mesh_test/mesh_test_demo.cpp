/// Mesh Test Demo Implementation — Cross-Platform (Windows + macOS)
///
/// Renders a MeshBuilder::CreateCube() with the standard mesh shaders.
/// Uses Vertex format (48 bytes: position, normal, texcoord, tangent) and
/// PerObjectCB for model/normal matrix transforms.
///
/// On Windows: loads HLSL shaders via ShaderPipeline + ShaderCompiler/FXC.
/// On macOS:   embeds MSL source strings and passes them directly to CreateShader().
///
/// All GPU interaction flows through abstract RHI interfaces — no DX12,
/// Vulkan, or Metal types appear in this file.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "mesh_test_demo.h"
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
#include "paths.h"
#include "stats_overlay.h"

#ifdef _WIN32
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "engine_shaders.h"
#include "editor_grid.h"
#include "screenshot_capture.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Embedded MSL Shaders (macOS only)
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__APPLE__)

/// MSL vertex shader for lit mesh — transforms position by model then viewProj,
/// passes normalWS, texcoord, tangentWS, positionWS to fragment stage.
///
/// The engine stores matrices in row-major order (DirectXMath convention).
/// MSL float4x4 is column-major, so when row-major bytes are read as column-major
/// it is equivalent to transpose(M). HLSL mul(v, M) == transpose(M) * v == MSL_M * v.
///
/// PerFrameCB at buffer(1): viewProj, cameraPos, timeParams.
/// PerObjectCB at buffer(2): model, normalMatrix, materialIndex.
static const char kMeshVSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
    float4 tangent  [[attribute(3)]];
};

struct VertexOut {
    float4 position  [[position]];
    float3 normalWS;
    float2 texcoord;
    float4 tangentWS;
    float3 positionWS;
};

struct PerFrameCB {
    float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
};

struct PerObjectCB {
    float4x4 model;
    float4x4 normalMatrix;
    uint     materialIndex;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant PerFrameCB& perFrame [[buffer(1)]],
                             constant PerObjectCB& perObject [[buffer(2)]]) {
    // Row-major bytes (DirectXMath) read as column-major in MSL = transpose(M).
    // HLSL mul(v, M) == transpose(M) * v == MSL_M * v — no transpose needed.
    VertexOut out;
    float4 worldPos = perObject.model * float4(in.position, 1.0);
    out.position   = perFrame.viewProj * worldPos;
    out.positionWS = worldPos.xyz;
    out.normalWS   = normalize((perObject.normalMatrix * float4(in.normal, 0.0)).xyz);
    out.texcoord   = in.texcoord;
    out.tangentWS  = float4((perObject.model * float4(in.tangent.xyz, 0.0)).xyz, in.tangent.w);
    return out;
}
)msl";

/// MSL pixel shader for lit mesh — N dot L directional lighting with fixed sun.
///
/// Uses white albedo (no texture sampling on macOS for now).
/// Sun direction (0.5, 0.3, 0.8) normalized, ambient = 0.15.
static const char kMeshPSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct FragmentIn {
    float4 position  [[position]];
    float3 normalWS;
    float2 texcoord;
    float4 tangentWS;
    float3 positionWS;
};

fragment float4 fragment_main(FragmentIn in [[stage_in]]) {
    // Fixed directional sun light
    float3 sunDir = normalize(float3(0.5, 0.3, 0.8));
    float3 N = normalize(in.normalWS);

    // N dot L diffuse lighting
    float NdotL = max(dot(N, sunDir), 0.0);

    // White albedo — no texture on macOS for now
    float3 albedo = float3(1.0, 1.0, 1.0);
    float3 ambient = albedo * 0.15;
    float3 diffuse = albedo * NdotL;

    return float4(ambient + diffuse, 1.0);
}
)msl";

#endif // __APPLE__

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Light PerFrame CB — matches mesh_ps.hlsl 768-byte layout at b1
// ─────────────────────────────────────────────────────────────────────────────

/// Single light for the multi-light forward CB (64 bytes per light).
/// Matches LightData in light_types.hlsli.
struct LightDataGPU {
    Vec3  positionOrDir;    ///< Direction toward light (directional) or world position (point/spot).
    float range;            ///< Attenuation range (point/spot). Unused for directional.
    Vec3  color;            ///< Light color * intensity (linear HDR).
    float lightType;        ///< 0 = directional, 1 = point, 2 = spot.
    Vec3  spotDirection;    ///< Spot light cone direction. Unused for directional/point.
    float innerConeCos;     ///< cos(innerConeAngle) for spot lights.
    float outerConeCos;     ///< cos(outerConeAngle) for spot lights.
    float _pad0 = 0.f;
    float _pad1 = 0.f;
    float _pad2 = 0.f;
};

static_assert(sizeof(LightDataGPU) == 64, "LightDataGPU must be 64 bytes for mesh_ps.hlsl");

/// Maximum lights supported by the multi-light forward CB.
static constexpr uint32_t kMeshMaxLights = 8;

/// Per-frame constant buffer matching the 768-byte layout expected by mesh_ps.hlsl.
///
/// This is a demo-local struct — NOT the same as the 256-byte PerFrameCB in
/// constant_buffers.h, which is used by other passes (gbuffer, deferred, etc.).
struct PerFrameMultiLightCB {
    Mat4         viewProj;                     ///< 64B — combined view-projection matrix.
    Vec4         cameraPos;                    ///< 16B — xyz = camera world position, w = 0.
    Vec4         timeParams;                   ///< 16B — x = totalTime, y = dt, z = frame, w = 0.
    Vec4         ambientColor;                 ///< 16B — xyz = ambient light color, w = 0.
    uint32_t     lightCount = 0;               ///< 4B  — number of active lights.
    uint32_t     _pad0a = 0;                   ///< 4B  — padding.
    uint32_t     _pad0b = 0;                   ///< 4B  — padding.
    uint32_t     _pad0c = 0;                   ///< 4B  — padding.
    LightDataGPU lights[kMeshMaxLights];       ///< 512B — light array (8 * 64).
    Vec4         _pad1[8] = {};                ///< 128B — trailing padding.
};

static_assert(sizeof(PerFrameMultiLightCB) == 768,
              "PerFrameMultiLightCB must be 768 bytes for mesh_ps.hlsl multi-light CB");

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

MeshTestDemo::MeshTestDemo(const MeshTestConfig& config)
    : DemoApplication(DemoConfig{
        .title  = "UzEngine \xe2\x80\x94 Mesh Test (Lit Cube)",
        .width  = 1280,
        .height = 720,
        .maxFrames = config.frameCount,
      })
    , config_(config)
{}

MeshTestDemo::~MeshTestDemo() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OnInit — Create all rendering resources
// ─────────────────────────────────────────────────────────────────────────────

void MeshTestDemo::OnInit() {
    startTime_ = std::chrono::high_resolution_clock::now();

    auto* device  = GetDevice();
    auto* cmdList = GetCommandList();

    // Force non-bindless on macOS Metal — embedded MSL uses [[stage_in]]
#if defined(__APPLE__)
    bindless_ = false;
#else
    bindless_ = device->IsBindless();
#endif

    // ── Input ────────────────────────────────────────────────────────────
    input_ = std::make_unique<InputSystem>();
    input_->Init();
    GetWindow()->SetInputSystem(input_.get());

    // ── Shaders & Pipeline ──────────────────────────────────────────────
    CompileShaders();
    CreatePipeline();

    // ── Mesh ─────────────────────────────────────────────────────────────
    LoadMeshData();

    // ── Depth Buffer ─────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width  = GetWidth();
    depthDesc.height = GetHeight();
    depthDesc.format = RHIFormat::D32Float;
    depthDesc.usage  = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "DepthBuffer";
    depthBuffer_ = device->CreateTexture(depthDesc);

    // ── CB Ring Allocator ────────────────────────────────────────────────
    cbRing_ = std::make_unique<CBRingAllocator>();
    cbRing_->Init(device, 3, 64 * 1024);

#ifdef _WIN32
    // ── Editor Grid ──────────────────────────────────────────────────────
    editorGrid_ = std::make_unique<EditorGrid>();
    if (!editorGrid_->Init(device, *shaderPipeline_, GetSwapChain()->GetFormat(), RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "Failed to initialize editor grid");
    }
#endif

    // ── Stats Overlay ────────────────────────────────────────────────────
    statsOverlay_ = std::make_unique<StatsOverlay>();
    {
        UploadContext upload(device);
        cmdList->Begin();

#ifdef _WIN32
        if (!statsOverlay_->Init(device, cmdList, upload, *shaderPipeline_,
                                  GetSwapChain()->GetFormat(), GetWidth(), GetHeight(), bindless_)) {
            UZ_LOG(Renderer, Error, "Failed to initialize stats overlay");
        }
#endif

        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
    }

#ifdef _WIN32
    // ── Screenshot Capture ───────────────────────────────────────────────
    screenshotPending_ = !config_.screenshotPath.empty();
    if (screenshotPending_) {
        screenshotCapture_ = std::make_unique<ScreenshotCapture>();
        if (!screenshotCapture_->Init(device, GetWidth(), GetHeight())) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
        }
    }
#endif

    // ── Render Graph Resource Pool ───────────────────────────────────────
    rgPool_ = std::make_unique<rg::RgResourcePool>(device);

    // ── Camera ───────────────────────────────────────────────────────────
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect  = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera_.SetPerspective(vFovRad,
        static_cast<float>(GetWidth()) / static_cast<float>(GetHeight()),
        0.1f, 1000.f);
    // Same initial position as camera_test for direct comparison
    camera_.SetPosition({0.f, 5.f, 2.5f});
    {
        constexpr float kDegToRad = 3.14159265f / 180.f;
        float yawRad   = -90.f * kDegToRad;
        float pitchRad = -15.f * kDegToRad;
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

    UZ_LOG(Renderer, Info, "Mesh test demo started — lit cube with mesh shaders");
}

// ─────────────────────────────────────────────────────────────────────────────
// CompileShaders
// ─────────────────────────────────────────────────────────────────────────────

void MeshTestDemo::CompileShaders() {
    auto* device = GetDevice();

#ifdef _WIN32
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

    auto vsBytecode = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshVS),
        EngineShaders::GetSource(EngineShader::MeshVS));
    auto psBytecode = shaderPipeline_->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshPS),
        EngineShaders::GetSource(EngineShader::MeshPS));
    if (vsBytecode.empty() || psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to compile mesh shaders from embedded source");
        return;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode     = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage        = RHIShaderStage::Vertex;
    vsDesc.entryPoint   = "main";
    vsDesc.debugName    = "mesh_vs";

    RHIShaderDesc psDesc{};
    psDesc.bytecode     = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage        = RHIShaderStage::Pixel;
    psDesc.entryPoint   = "main";
    psDesc.debugName    = "mesh_ps";

#elif defined(__APPLE__)
    RHIShaderDesc vsDesc{};
    vsDesc.bytecode     = kMeshVSSource;
    vsDesc.bytecodeSize = std::strlen(kMeshVSSource);
    vsDesc.stage        = RHIShaderStage::Vertex;
    vsDesc.entryPoint   = "vertex_main";
    vsDesc.debugName    = "mesh_vs_msl";

    RHIShaderDesc psDesc{};
    psDesc.bytecode     = kMeshPSSource;
    psDesc.bytecodeSize = std::strlen(kMeshPSSource);
    psDesc.stage        = RHIShaderStage::Pixel;
    psDesc.entryPoint   = "fragment_main";
    psDesc.debugName    = "mesh_ps_msl";
#endif

    vsHandle_ = device->CreateShader(vsDesc);
    psHandle_ = device->CreateShader(psDesc);

    if (!vsHandle_.IsValid() || !psHandle_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create mesh shaders");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CreatePipeline
// ─────────────────────────────────────────────────────────────────────────────

void MeshTestDemo::CreatePipeline() {
    auto* device = GetDevice();

    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
        { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
    };

    RHIGraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader                 = vsHandle_;
    pipeDesc.pixelShader                  = psHandle_;
    pipeDesc.topology                     = RHIPrimitiveTopology::TriangleList;
    pipeDesc.rasterizer.cullMode          = RHICullMode::Back;
    pipeDesc.rasterizer.frontCounterClockwise = false;
    pipeDesc.depthStencil.depthEnable     = true;
    pipeDesc.depthStencil.depthWrite      = true;
    pipeDesc.renderTargetFormats[0]       = GetSwapChain()->GetFormat();
    pipeDesc.renderTargetCount            = 1;
    pipeDesc.depthStencilFormat           = RHIFormat::D32Float;
    pipeDesc.debugName                    = "MeshPipeline";

    if (!bindless_) {
        pipeDesc.inputElements     = inputElements;
        pipeDesc.inputElementCount = 4;
        pipeDesc.vertexStride      = sizeof(Vertex);
    }

    pipeline_ = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create mesh pipeline");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMeshData
// ─────────────────────────────────────────────────────────────────────────────

void MeshTestDemo::LoadMeshData() {
    auto* device = GetDevice();

    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    auto loaded = LoadMesh(meshPath);
    if (!loaded.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to load cube.uzmesh from '{}'", meshPath);
        return;
    }
    UZ_LOG(Renderer, Info, "Loaded cube.uzmesh: {} vertices, {} indices, stride={}",
           loaded.GetVertexCount(), loaded.GetIndexCount(), loaded.header.vertexStride);

    cubeMesh_ = std::make_unique<Mesh>(std::move(loaded));

    // Upload vertex buffer
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes  = cubeMesh_->GetVertexDataSize();
    vbDesc.stride     = cubeMesh_->header.vertexStride;
    vbDesc.usage      = bindless_ ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory     = RHIMemoryType::Upload;
    vbDesc.debugName  = "MeshVertexBuffer";

    vbHandle_ = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle_);
    std::memcpy(vbMapped, cubeMesh_->vertexData.data(), cubeMesh_->GetVertexDataSize());
    device->UnmapBuffer(vbHandle_);

    if (bindless_) {
        vbSrvIndex_ = device->GetBufferSRVIndex(vbHandle_);
    }

    // Upload index buffer
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes  = cubeMesh_->GetIndexDataSize();
    ibDesc.stride     = (cubeMesh_->header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage      = RHIBufferUsage::IndexBuffer;
    ibDesc.memory     = RHIMemoryType::Upload;
    ibDesc.debugName  = "MeshIndexBuffer";

    ibHandle_ = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle_);
    std::memcpy(ibMapped, cubeMesh_->indexData.data(), cubeMesh_->GetIndexDataSize());
    device->UnmapBuffer(ibHandle_);

    indexFormat_ = (cubeMesh_->header.indexFormat == MeshIndexFormat::UInt32)
        ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize
// ─────────────────────────────────────────────────────────────────────────────

void MeshTestDemo::OnResize(uint32_t width, uint32_t height) {
    auto* device = GetDevice();

    // Recreate depth buffer at new dimensions
    device->DestroyTexture(depthBuffer_);
    RHITextureDesc depthDesc{};
    depthDesc.width     = width;
    depthDesc.height    = height;
    depthDesc.format    = RHIFormat::D32Float;
    depthDesc.usage     = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "DepthBuffer";
    depthBuffer_ = device->CreateTexture(depthDesc);

    if (statsOverlay_) {
        statsOverlay_->SetScreenSize(width, height);
    }

    camera_.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame
// ─────────────────────────────────────────────────────────────────────────────

void MeshTestDemo::OnFrame(float dt) {
    auto* device    = GetDevice();
    auto* cmdList   = GetCommandList();
    auto* swapChain = GetSwapChain();

    auto now = std::chrono::high_resolution_clock::now();
    totalTime_ = std::chrono::duration<float>(now - startTime_).count();

    // ── Stats ────────────────────────────────────────────────────────────
    if (statsOverlay_) {
        statsOverlay_->BeginFrame(dt);
    }

    // ── Input ────────────────────────────────────────────────────────────
    input_->BeginFrame();

    // ── Camera Update ────────────────────────────────────────────────────
    cameraCtrl_.Update(camera_, *input_, dt);

    // ── Upload PerFrameCB ────────────────────────────────────────────────
    cbRing_->BeginFrame(GetFrameIndex());

    PerFrameMultiLightCB perFrame{};
    perFrame.viewProj = camera_.GetViewProjection();
    Vec3 camPos = camera_.GetPosition();
    perFrame.cameraPos  = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams = {totalTime_, dt, static_cast<float>(GetFrameIndex()), 0.f};
    perFrame.ambientColor = {0.08f, 0.08f, 0.1f, 0.f};

    // Directional light — direction toward light, white at intensity 3
    perFrame.lightCount = 1;
    LightDataGPU& sun = perFrame.lights[0];
    Vec3 rawDir = {0.5f, 1.0f, 0.3f};
    float len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
    sun.positionOrDir = {rawDir.x / len, rawDir.y / len, rawDir.z / len};
    sun.range     = 0.f;
    sun.color     = {3.0f, 3.0f, 3.0f};
    sun.lightType = 0.f;

    CBAllocation perFrameAlloc = cbRing_->Allocate(sizeof(PerFrameMultiLightCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameMultiLightCB));

    // ── Upload PerObjectCB ───────────────────────────────────────────────
    PerObjectCB perObject{};
    perObject.model        = Mat4::TRS({0.f, 0.f, 0.f}, 1.f);
    perObject.normalMatrix = perObject.model.NormalMatrix();
    perObject.albedoIndex  = 0;

    CBAllocation perObjectAlloc = cbRing_->Allocate(sizeof(PerObjectCB));
    std::memcpy(perObjectAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

    // ── Record via render graph ──────────────────────────────────────────
    bool capturedThisFrame = false;

    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

    rg::RgBuilder rgBuilder(device, rgPool_.get());
    auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

    // Main 3D pass: clear + mesh + grid + stats overlay.
    rgBuilder.AddPass("MeshScene", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::RenderTarget);
        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto bb = res.GetTexture(bbRes);

            float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
            cmd->ClearRenderTarget(bb, clearColor);
            cmd->ClearDepthStencil(depthBuffer_, 1.0f, 0);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth                    = true;
            passDesc.depthAttachment.texture     = depthBuffer_;
            passDesc.depthAttachment.clearDepth  = 1.0f;
            passDesc.depthAttachment.clear       = false;
            cmd->BeginRenderPass(passDesc);

            cmd->SetPipeline(pipeline_);

            if (bindless_) {
                cmd->SetDescriptorIndex(0, vbSrvIndex_);
            } else {
                cmd->SetVertexBuffer(0, vbHandle_, 0);
            }

            cmd->SetRootCBV(1, perFrameAlloc.bufferHandle, perFrameAlloc.offsetBytes,
                            perFrameAlloc.cpuPtr, sizeof(PerFrameMultiLightCB));
            cmd->SetRootCBV(2, perObjectAlloc.bufferHandle, perObjectAlloc.offsetBytes,
                            perObjectAlloc.cpuPtr, sizeof(PerObjectCB));

            RHIViewport vp{};
            vp.width    = static_cast<float>(swapChain->GetWidth());
            vp.height   = static_cast<float>(swapChain->GetHeight());
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
            cmd->SetScissor(sc);

            cmd->SetIndexBuffer(ibHandle_, indexFormat_, 0);
            cmd->DrawIndexed(cubeMesh_->GetIndexCount());

#ifdef _WIN32
            if (editorGrid_) {
                editorGrid_->Draw(cmd, *cbRing_, camera_);
            }
#endif

            if (statsOverlay_) {
                statsOverlay_->SetDrawCalls(2);
                statsOverlay_->SetVertexCount(cubeMesh_->GetVertexCount() + 3);
                statsOverlay_->SetTriangleCount(cubeMesh_->GetIndexCount() / 3 + 1);
                statsOverlay_->Render(cmd, *cbRing_, GetFrameIndex());
            }

            cmd->EndRenderPass();
        });
    });

    // Screenshot pass (optional).
#ifdef _WIN32
    if (screenshotPending_ && GetFrameIndex() == config_.screenshotFrame) {
        rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
            pb.Read(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                screenshotCapture_->CaptureBackBuffer(cmdList, swapChain);
                capturedThisFrame = true;
            });
        });
    }
#endif

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

#ifdef _WIN32
    // Save screenshot after GPU completes
    if (capturedThisFrame && screenshotCapture_) {
        device->WaitIdle();
        screenshotCapture_->SaveToPNG(config_.screenshotPath);
        screenshotPending_ = false;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown
// ─────────────────────────────────────────────────────────────────────────────

void MeshTestDemo::OnShutdown() {
    auto* device = GetDevice();

    if (screenshotCapture_) screenshotCapture_->Shutdown();
    if (statsOverlay_) statsOverlay_->Shutdown();
    if (editorGrid_) editorGrid_->Shutdown();
    if (shaderPipeline_) shaderPipeline_->Shutdown();

    cbRing_->Shutdown();

    device->DestroyTexture(depthBuffer_);
    device->DestroyBuffer(ibHandle_);
    device->DestroyBuffer(vbHandle_);
    device->DestroyPipeline(pipeline_);
    device->DestroyShader(vsHandle_);
    device->DestroyShader(psHandle_);
}

} // namespace uz
