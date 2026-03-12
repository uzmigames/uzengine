/// Mesh Test Demo Implementation
///
/// Renders a MeshBuilder::CreateCube() with the standard mesh shaders.
/// Uses Vertex format (48 bytes: position, normal, texcoord, tangent) and
/// PerObjectCB for model/normal matrix transforms.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mesh_test_demo.h"
#include "dx12_device.h"
#include "dx12_swapchain.h"
#include "dx12_command_list.h"
#include "dx12_fence.h"
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

#include <DirectXMath.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <d3d12.h>
#include <thread>

using namespace DirectX;

namespace uz {

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

    // If singular, return identity
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
    deviceDesc.backend = RHIBackend::DX12;
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = true;
#endif

    if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

    auto* device = static_cast<DX12Device*>(CreateRHIDevice(deviceDesc));
    if (!device) {
        UZ_LOG(Renderer, Error, "Failed to create RHI device");
        return false;
    }

    // ── Swapchain ────────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = window.GetHWND();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    auto* swapChain = static_cast<DX12SwapChain*>(device->CreateSwapChain(scDesc));
    if (!swapChain) {
        UZ_LOG(Renderer, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Command List ─────────────────────────────────────────────────────
    auto* cmdList = static_cast<DX12CommandList*>(device->CreateCommandList(RHIQueueType::Graphics));
    if (!cmdList) {
        UZ_LOG(Renderer, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    // ── Fences ───────────────────────────────────────────────────────────
    DX12Fence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = static_cast<DX12Fence*>(device->CreateFence());
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

    // ── Shader Handles ───────────────────────────────────────────────────
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

    RHIShaderHandle vsHandle = device->CreateShader(vsDesc);
    RHIShaderHandle psHandle = device->CreateShader(psDesc);

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
    pipeDesc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
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

    // ── Load Mesh from .uzmesh asset ───────────────────────────────────
    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    Mesh cubeMesh = LoadMesh(meshPath);
    if (!cubeMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to load cube.uzmesh from '{}'", meshPath);
        return false;
    }
    UZ_LOG(Renderer, Info, "Loaded cube.uzmesh: {} vertices, {} indices, stride={}",
           cubeMesh.GetVertexCount(), cubeMesh.GetIndexCount(), cubeMesh.header.vertexStride);

    // ── Upload Vertex Buffer ─────────────────────────────────────────────
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = cubeMesh.GetVertexDataSize();
    vbDesc.stride = cubeMesh.header.vertexStride;
    vbDesc.usage = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "MeshVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle);
    std::memcpy(vbMapped, cubeMesh.vertexData.data(), cubeMesh.GetVertexDataSize());

    uint32_t vbSrvIndex = 0;
    if (bindless) {
        vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
    }

    // ── Upload Index Buffer ──────────────────────────────────────────────
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = cubeMesh.GetIndexDataSize();
    ibDesc.stride = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage = RHIBufferUsage::IndexBuffer;
    ibDesc.memory = RHIMemoryType::Upload;
    ibDesc.debugName = "MeshIndexBuffer";

    RHIBufferHandle ibHandle = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle);
    std::memcpy(ibMapped, cubeMesh.indexData.data(), cubeMesh.GetIndexDataSize());

    // ── Depth Buffer ─────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width = scDesc.width;
    depthDesc.height = scDesc.height;
    depthDesc.format = RHIFormat::D32Float;
    depthDesc.usage = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "DepthBuffer";

    RHITextureHandle depthBuffer = device->CreateTexture(depthDesc);

    // ── CB Ring Allocator ────────────────────────────────────────────────
    CBRingAllocator cbRing;
    cbRing.Init(device, kFramesInFlight, 64 * 1024);

    // ── Editor Grid ──────────────────────────────────────────────────────
    EditorGrid editorGrid;
    if (!editorGrid.Init(device, shaderPipeline, RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "Failed to initialize editor grid");
        return false;
    }

    // ── Stats Overlay ────────────────────────────────────────────────────
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

    // ── Screenshot Capture ───────────────────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        if (!screenshotCapture.Init(device, scDesc.width, scDesc.height)) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            return false;
        }
    }

    // ── Camera ───────────────────────────────────────────────────────────
    Camera camera;
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera.SetPerspective(vFovRad,
        static_cast<float>(winDesc.width) / static_cast<float>(winDesc.height),
        0.1f, 1000.f);
    // Same camera as camera_test for direct comparison
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

    // ── Render Loop ──────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto prevTime = std::chrono::high_resolution_clock::now();
    auto startTime = prevTime;

    UZ_LOG(Renderer, Info, "Mesh test demo started — lit cube with mesh shaders");

    while (window.PumpMessages() && window.IsOpen()) {
        if (frameCount > 0 && frameIndex >= frameCount) break;

        // ── Timing ───────────────────────────────────────────────────────
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prevTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        prevTime = now;

        // ── Stats ────────────────────────────────────────────────────────
        statsOverlay.BeginFrame(dt);

        // ── Input ────────────────────────────────────────────────────────
        input.BeginFrame();

        // ── Camera Update ────────────────────────────────────────────────
        cameraCtrl.Update(camera, input, dt);

        // Handle resize
        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            device->DestroyTexture(depthBuffer);
            depthDesc.width = static_cast<uint32_t>(window.GetWidth());
            depthDesc.height = static_cast<uint32_t>(window.GetHeight());
            depthBuffer = device->CreateTexture(depthDesc);
            statsOverlay.SetScreenSize(depthDesc.width, depthDesc.height);
            window.ClearResizedFlag();
            camera.SetAspectRatio(
                static_cast<float>(window.GetWidth()) /
                static_cast<float>(window.GetHeight()));
        }

        // Wait for frame N-2
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // ── Upload PerFrameCB ────────────────────────────────────────────
        cbRing.BeginFrame(frameIndex);

        PerFrameCB perFrame{};
        perFrame.viewProj = camera.GetViewProjection();
        Vec3 camPos = camera.GetPosition();
        perFrame.cameraPos = {camPos.x, camPos.y, camPos.z, 0.f};
        perFrame.timeParams = {totalTime, dt, static_cast<float>(frameIndex), 0.f};

        CBAllocation perFrameAlloc = cbRing.Allocate(sizeof(PerFrameCB));
        std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

        // ── Upload PerObjectCB ───────────────────────────────────────────
        PerObjectCB perObject{};
        perObject.model = BuildModelMatrix({0.f, 0.f, 0.f}, 1.f);
        perObject.normalMatrix = ComputeNormalMatrix(perObject.model);
        perObject.materialIndex = 0;

        CBAllocation perObjectAlloc = cbRing.Allocate(sizeof(PerObjectCB));
        std::memcpy(perObjectAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

        // ── Record ───────────────────────────────────────────────────────
        cmdList->Begin();

        ID3D12Resource* backBuffer = swapChain->GetCurrentBackBufferResource();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain->GetCurrentRTV();

        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
        cmdList->ClearRTV(rtv, clearColor);
        cmdList->ClearDepthStencil(depthBuffer, 1.0f, 0);

        auto* depthTex = device->GetTexturePool().Get(depthBuffer.index, depthBuffer.generation);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = device->GetDescriptorAllocator(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV).CpuHandle(depthTex->dsvIndex);
        cmdList->GetNative()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        // ── Draw Mesh ────────────────────────────────────────────────────
        cmdList->SetPipeline(pipeline);
        cmdList->GetNative()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Bind vertex data
        if (bindless) {
            cmdList->SetDescriptorIndex(0, vbSrvIndex);
        } else {
            cmdList->SetVertexBuffer(0, vbHandle, 0);
        }

        // Root CBV: per-frame CB at b1
        cmdList->SetRootCBV(1, perFrameAlloc.gpuAddr);

        // Root CBV: per-object CB at b2
        cmdList->SetRootCBV(2, perObjectAlloc.gpuAddr);

        RHIViewport vp{};
        vp.width = static_cast<float>(swapChain->GetWidth());
        vp.height = static_cast<float>(swapChain->GetHeight());
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = swapChain->GetWidth();
        sc.bottom = swapChain->GetHeight();
        cmdList->SetScissor(sc);

        // Bind index buffer and draw indexed
        RHIIndexFormat idxFmt = (cubeMesh.header.indexFormat == MeshIndexFormat::UInt32)
            ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;
        cmdList->SetIndexBuffer(ibHandle, idxFmt, 0);
        cmdList->DrawIndexed(cubeMesh.GetIndexCount());

        // ── Editor Grid ──────────────────────────────────────────────────
        editorGrid.Draw(cmdList, cbRing, camera);

        // ── Stats Overlay ────────────────────────────────────────────────
        statsOverlay.SetDrawCalls(2); // Mesh + grid
        statsOverlay.SetVertexCount(cubeMesh.GetVertexCount() + 3);
        statsOverlay.SetTriangleCount(cubeMesh.GetIndexCount() / 3 + 1);

        cmdList->GetNative()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        statsOverlay.Render(cmdList, cbRing, frameIndex);

        // ── Screenshot ───────────────────────────────────────────────────
        bool capturedThisFrame = false;
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            screenshotCapture.CaptureBackBuffer(cmdList, swapChain);
            capturedThisFrame = true;
        }

        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        cmdList->End();
        cmdList->Execute();

        swapChain->Present(false);

        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        device->GetDirectQueue()->Signal(
            frameFences[allocFrame]->GetNativeFence(), globalFenceValue);

        // ── Save screenshot after GPU completes ──────────────────────────
        if (capturedThisFrame) {
            frameFences[allocFrame]->WaitCPU(globalFenceValue);
            screenshotCapture.SaveToPNG(config.screenshotPath);
            screenshotPending = false;
        }

        frameIndex++;

        // ── FPS Cap ──────────────────────────────────────────────────────
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(frameEnd - now).count();
        if (elapsed < kMinFrameTime) {
            auto sleepUs = std::chrono::microseconds(
                static_cast<int64_t>((kMinFrameTime - elapsed) * 1e6f));
            std::this_thread::sleep_for(sleepUs);
        }
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.f;

    UZ_LOG(Renderer, Info, "Mesh test demo: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.f / avgMs : 0.f);

    screenshotCapture.Shutdown();
    statsOverlay.Shutdown();
    editorGrid.Shutdown();
    cbRing.Shutdown();
    shaderPipeline.Shutdown();
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
