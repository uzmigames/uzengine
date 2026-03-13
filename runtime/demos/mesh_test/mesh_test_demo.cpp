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

#include <DirectXMath.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace DirectX;

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

static constexpr uint32_t kFramesInFlight = 3;
static constexpr float kMaxFPS = 500.f;
static constexpr float kMinFrameTime = 1.f / kMaxFPS;

// ─────────────────────────────────────────────────────────────────────────────
// Normal Matrix Computation
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

// ─────────────────────────────────────────────────────────────────────────────
// Model Matrix from Transform
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Main Demo
// ─────────────────────────────────────────────────────────────────────────────

bool RunMeshTestDemo(const MeshTestConfig& config) {
    const uint32_t frameCount = config.frameCount;

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine — Mesh Test (Lit Cube)";
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
    scDesc.windowHandle = window.GetNativeHandle();
    scDesc.displayHandle = window.GetNativeDisplayHandle();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

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
    uint64_t fenceValues[kFramesInFlight] = {};
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

    // ── Shader Loading ──────────────────────────────────────────────────
    // Force non-bindless on macOS Metal — our embedded MSL shaders use
    // [[stage_in]] with [[attribute(N)]], which requires a vertex descriptor.
#if defined(__APPLE__)
    const bool bindless = false;
#else
    const bool bindless = device->IsBindless();
#endif

#ifdef _WIN32
    // ── Windows: HLSL via ShaderPipeline ─────────────────────────────────
    ShaderCompiler compiler;
    if (!compiler.Init()) {
        UZ_LOG(Renderer, Error, "Failed to init shader compiler");
        return false;
    }

    ShaderPipeline shaderPipeline;
    ShaderPipeline::Config pipelineCfg;
    pipelineCfg.cacheDir = (uz::GetExeDir() / "cache/shaders").string();
    pipelineCfg.targetBackend = bindless ? "dx12_sm66" : "dx12_sm50";
    pipelineCfg.bindless = bindless;
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

    // Use embedded engine shaders (compiled into the binary via uzembed)
    auto vsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshVS),
        EngineShaders::GetSource(EngineShader::MeshVS));
    auto psBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshPS),
        EngineShaders::GetSource(EngineShader::MeshPS));
    if (vsBytecode.empty() || psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to compile mesh shaders from embedded source");
        return false;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "mesh_vs";

    RHIShaderDesc psDesc{};
    psDesc.bytecode = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.entryPoint = "main";
    psDesc.debugName = "mesh_ps";

#elif defined(__APPLE__)
    // ── macOS: embedded MSL source passed directly to CreateShader ───────
    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = kMeshVSSource;
    vsDesc.bytecodeSize = std::strlen(kMeshVSSource);
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.entryPoint = "vertex_main";
    vsDesc.debugName = "mesh_vs_msl";

    RHIShaderDesc psDesc{};
    psDesc.bytecode = kMeshPSSource;
    psDesc.bytecodeSize = std::strlen(kMeshPSSource);
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.entryPoint = "fragment_main";
    psDesc.debugName = "mesh_ps_msl";
#endif

    RHIShaderHandle vsHandle = device->CreateShader(vsDesc);
    RHIShaderHandle psHandle = device->CreateShader(psDesc);

    if (!vsHandle.IsValid() || !psHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create mesh shaders");
        return false;
    }

    // ── Pipeline ─────────────────────────────────────────────────────────
    // SM5 input layout: POSITION(float3) + NORMAL(float3) + TEXCOORD(float2) + TANGENT(float4)
    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
        { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
    };

    RHIGraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader = vsHandle;
    pipeDesc.pixelShader = psHandle;
    pipeDesc.topology = RHIPrimitiveTopology::TriangleList;
    pipeDesc.rasterizer.cullMode = RHICullMode::Back;
    pipeDesc.rasterizer.frontCounterClockwise = false;
    pipeDesc.depthStencil.depthEnable = true;
    pipeDesc.depthStencil.depthWrite = true;
    pipeDesc.renderTargetFormats[0] = swapChain->GetFormat();
    pipeDesc.renderTargetCount = 1;
    pipeDesc.depthStencilFormat = RHIFormat::D32Float;
    pipeDesc.debugName = "MeshPipeline";

    if (!bindless) {
        pipeDesc.inputElements = inputElements;
        pipeDesc.inputElementCount = 4;
        pipeDesc.vertexStride = sizeof(Vertex);
    }

    RHIPipelineHandle pipeline = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create mesh pipeline");
        return false;
    }

    // ── Load Mesh from .uzmesh asset ──────────────────────────────────────
    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    Mesh cubeMesh = LoadMesh(meshPath);
    if (!cubeMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to load cube.uzmesh from '{}'", meshPath);
        return false;
    }
    UZ_LOG(Renderer, Info, "Loaded cube.uzmesh: {} vertices, {} indices, stride={}",
           cubeMesh.GetVertexCount(), cubeMesh.GetIndexCount(), cubeMesh.header.vertexStride);

    // ── Upload Vertex Buffer ──────────────────────────────────────────────
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = cubeMesh.GetVertexDataSize();
    vbDesc.stride = cubeMesh.header.vertexStride;
    vbDesc.usage = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "MeshVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle);
    std::memcpy(vbMapped, cubeMesh.vertexData.data(), cubeMesh.GetVertexDataSize());
    device->UnmapBuffer(vbHandle);

    uint32_t vbSrvIndex = 0;
    if (bindless) {
        vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
    }

    // ── Upload Index Buffer ───────────────────────────────────────────────
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = cubeMesh.GetIndexDataSize();
    ibDesc.stride = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage = RHIBufferUsage::IndexBuffer;
    ibDesc.memory = RHIMemoryType::Upload;
    ibDesc.debugName = "MeshIndexBuffer";

    RHIBufferHandle ibHandle = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle);
    std::memcpy(ibMapped, cubeMesh.indexData.data(), cubeMesh.GetIndexDataSize());
    device->UnmapBuffer(ibHandle);

    // ── Depth Buffer ──────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width = scDesc.width;
    depthDesc.height = scDesc.height;
    depthDesc.format = RHIFormat::D32Float;
    depthDesc.usage = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "DepthBuffer";

    RHITextureHandle depthBuffer = device->CreateTexture(depthDesc);

    // ── CB Ring Allocator ─────────────────────────────────────────────────
    CBRingAllocator cbRing;
    cbRing.Init(device, kFramesInFlight, 64 * 1024);

#ifdef _WIN32
    // ── Editor Grid (Windows only — depends on ShaderPipeline) ───────────
    EditorGrid editorGrid;
    if (!editorGrid.Init(device, shaderPipeline, swapChain->GetFormat(), RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "Failed to initialize editor grid");
        return false;
    }
#endif

    // ── Stats Overlay ─────────────────────────────────────────────────────
    StatsOverlay statsOverlay;
    bool hasStatsOverlay = false;
    {
        UploadContext upload(device);
        cmdList->Begin();

#ifdef _WIN32
        if (!statsOverlay.Init(device, cmdList, upload, shaderPipeline,
                               swapChain->GetFormat(), winDesc.width, winDesc.height, bindless)) {
            UZ_LOG(Renderer, Error, "Failed to initialize stats overlay");
            return false;
        }
        hasStatsOverlay = true;
#endif
        // macOS: stats overlay skipped for now — depends on ShaderPipeline for
        // overlay text/rect shaders. Will be enabled once MSL overlay shaders
        // are integrated (same pattern as camera_test).

        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
    }

#ifdef _WIN32
    // ── Screenshot Capture (Windows only) ────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        if (!screenshotCapture.Init(device, scDesc.width, scDesc.height)) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            return false;
        }
    }
#endif

    // ── Render Graph Resource Pool (persistent across frames) ────────────
    rg::RgResourcePool rgPool(device);

    // ── Camera ────────────────────────────────────────────────────────────
    Camera camera;
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera.SetPerspective(vFovRad,
        static_cast<float>(winDesc.width) / static_cast<float>(winDesc.height),
        0.1f, 1000.f);
    // Same initial position as camera_test for direct comparison
    camera.SetPosition({0.f, 5.f, 2.5f});
    {
        constexpr float kDegToRad = 3.14159265f / 180.f;
        float yawRad = -90.f * kDegToRad;
        float pitchRad = -15.f * kDegToRad;
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

    // ── Render Loop ───────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto prevTime = std::chrono::high_resolution_clock::now();
    auto startTime = prevTime;

    UZ_LOG(Renderer, Info, "Mesh test demo started — lit cube with mesh shaders");

    while (window.PumpMessages() && window.IsOpen()) {
        // Skip rendering while minimized — swapchain surface is 0x0
        if (window.IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (frameCount > 0 && frameIndex >= frameCount) break;

        // ── Timing ───────────────────────────────────────────────────────
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prevTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        prevTime = now;

        // ── Stats ─────────────────────────────────────────────────────────
        if (hasStatsOverlay) {
            statsOverlay.BeginFrame(dt);
        }

        // ── Input ─────────────────────────────────────────────────────────
        input.BeginFrame();

        // ── Camera Update ─────────────────────────────────────────────────
        cameraCtrl.Update(camera, input, dt);

        // ── Resize ───────────────────────────────────────────────────────
        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            device->DestroyTexture(depthBuffer);
            depthDesc.width = static_cast<uint32_t>(window.GetWidth());
            depthDesc.height = static_cast<uint32_t>(window.GetHeight());
            depthBuffer = device->CreateTexture(depthDesc);
            if (hasStatsOverlay) {
                statsOverlay.SetScreenSize(depthDesc.width, depthDesc.height);
            }
            window.ClearResizedFlag();
            camera.SetAspectRatio(
                static_cast<float>(window.GetWidth()) /
                static_cast<float>(window.GetHeight()));
        }

        // Wait for frame N-2 before reusing its allocator slot
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // ── Upload PerFrameCB ─────────────────────────────────────────────
        cbRing.BeginFrame(frameIndex);

        PerFrameCB perFrame{};
        perFrame.viewProj = camera.GetViewProjection();
        Vec3 camPos = camera.GetPosition();
        perFrame.cameraPos = {camPos.x, camPos.y, camPos.z, 0.f};
        perFrame.timeParams = {totalTime, dt, static_cast<float>(frameIndex), 0.f};

        CBAllocation perFrameAlloc = cbRing.Allocate(sizeof(PerFrameCB));
        std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

        // ── Upload PerObjectCB ────────────────────────────────────────────
        PerObjectCB perObject{};
        perObject.model = BuildModelMatrix({0.f, 0.f, 0.f}, 1.f);
        perObject.normalMatrix = ComputeNormalMatrix(perObject.model);
        perObject.albedoIndex = 0;

        CBAllocation perObjectAlloc = cbRing.Allocate(sizeof(PerObjectCB));
        std::memcpy(perObjectAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

        // ── Record via render graph ──────────────────────────────────────
        bool capturedThisFrame = false;
        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

        rg::RgBuilder rgBuilder(device, &rgPool);
        auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

        // Main 3D pass: clear + mesh + grid + stats overlay.
        rgBuilder.AddPass("MeshScene", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                auto bb = res.GetTexture(bbRes);

                float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
                cmd->ClearRenderTarget(bb, clearColor);
                cmd->ClearDepthStencil(depthBuffer, 1.0f, 0);

                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments[0].texture = bb;
                passDesc.colorAttachments[0].clear = false;
                passDesc.hasDepth = true;
                passDesc.depthAttachment.texture = depthBuffer;
                passDesc.depthAttachment.clearDepth = 1.0f;
                passDesc.depthAttachment.clear = false;
                cmd->BeginRenderPass(passDesc);

                cmd->SetPipeline(pipeline);

                if (bindless) {
                    cmd->SetDescriptorIndex(0, vbSrvIndex);
                } else {
                    cmd->SetVertexBuffer(0, vbHandle, 0);
                }

                cmd->SetRootCBV(1, perFrameAlloc.bufferHandle, perFrameAlloc.offsetBytes,
                                perFrameAlloc.cpuPtr, sizeof(PerFrameCB));
                cmd->SetRootCBV(2, perObjectAlloc.bufferHandle, perObjectAlloc.offsetBytes,
                                perObjectAlloc.cpuPtr, sizeof(PerObjectCB));

                RHIViewport vp{};
                vp.width = static_cast<float>(swapChain->GetWidth());
                vp.height = static_cast<float>(swapChain->GetHeight());
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                RHIScissorRect sc{};
                sc.right = static_cast<int32_t>(swapChain->GetWidth());
                sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
                cmd->SetScissor(sc);

                RHIIndexFormat idxFmt = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32)
                    ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;
                cmd->SetIndexBuffer(ibHandle, idxFmt, 0);
                cmd->DrawIndexed(cubeMesh.GetIndexCount());

#ifdef _WIN32
                editorGrid.Draw(cmd, cbRing, camera);
#endif

                statsOverlay.SetDrawCalls(2);
                statsOverlay.SetVertexCount(cubeMesh.GetVertexCount() + 3);
                statsOverlay.SetTriangleCount(cubeMesh.GetIndexCount() / 3 + 1);
                statsOverlay.Render(cmd, cbRing, frameIndex);

                cmd->EndRenderPass();
            });
        });

        // Screenshot pass (optional).
#ifdef _WIN32
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
                pb.Read(bbRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                    screenshotCapture.CaptureBackBuffer(cmdList, swapChain);
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

        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);

#ifdef _WIN32
        // ── Save screenshot after GPU completes ───────────────────────────
        if (capturedThisFrame) {
            frameFences[allocFrame]->WaitCPU(globalFenceValue);
            screenshotCapture.SaveToPNG(config.screenshotPath);
            screenshotPending = false;
        }
#endif

        frameIndex++;

        // ── FPS Cap ───────────────────────────────────────────────────────
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(frameEnd - now).count();
        if (elapsed < kMinFrameTime) {
            auto sleepUs = std::chrono::microseconds(
                static_cast<int64_t>((kMinFrameTime - elapsed) * 1e6f));
            std::this_thread::sleep_for(sleepUs);
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.f;

    UZ_LOG(Renderer, Info, "Mesh test demo: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.f / avgMs : 0.f);

    if (hasStatsOverlay) {
        statsOverlay.Shutdown();
    }
#ifdef _WIN32
    screenshotCapture.Shutdown();
    editorGrid.Shutdown();
    shaderPipeline.Shutdown();
#endif
    cbRing.Shutdown();
    device->DestroyTexture(depthBuffer);
    device->DestroyBuffer(ibHandle);
    device->DestroyBuffer(vbHandle);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vsHandle);
    device->DestroyShader(psHandle);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return true;
}

} // namespace uz
