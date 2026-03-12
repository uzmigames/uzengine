/// Camera Test Demo Implementation — Backend-Agnostic
///
/// Renders a colored cube in 3D with FPS camera controls.
/// WASD to move, right-click + mouse to look, Shift to sprint, Space/Ctrl up/down.
/// Uses only abstract RHI interfaces — no DX12, Vulkan, or Metal types.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "camera_test_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "input.h"
#include "log.h"
#include "vertex.h"
#include "upload_context.h"
#include "constant_buffers.h"
#include "camera.h"
#include "fps_camera_controller.h"
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "paths.h"
#include "editor_grid.h"
#include "screenshot_capture.h"
#include "dx12_device.h"
#include "dx12_command_list.h"
#include "dx12_swapchain.h"
#include "stats_overlay.h"

#include <DirectXMath.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace DirectX;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Cube Geometry — 36 vertices (6 faces × 2 triangles × 3 verts)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kCubeVertexCount = 36;
static constexpr float kMaxFPS = 500.f;
static constexpr float kMinFrameTime = 1.f / kMaxFPS;

/// Fills outVerts with a unit cube centered at origin. Each face has a distinct color.
///
/// # Arguments
/// * `outVerts` — Array of exactly `kCubeVertexCount` (36) vertices to fill.
///
/// # Face colors
/// +X=red, -X=cyan, +Y=green, -Y=magenta, +Z=blue, -Z=yellow
/// Cube bottom sits on Z=0, top at Z=1.
static void GetCubeVertices(SimpleVertex outVerts[kCubeVertexCount]) {
    const Vec4 red     = {1, 0, 0, 1};
    const Vec4 cyan    = {0, 1, 1, 1};
    const Vec4 green   = {0, 1, 0, 1};
    const Vec4 magenta = {1, 0, 1, 1};
    const Vec4 blue    = {0, 0, 1, 1};
    const Vec4 yellow  = {1, 1, 0, 1};

    // Half-extent; cube sits on Z=0 ground plane (bottom at Z=0, top at Z=1)
    constexpr float h = 0.5f;
    constexpr float zOff = h;
    const Vec2 uv00 = {0, 0}, uv10 = {1, 0}, uv01 = {0, 1}, uv11 = {1, 1};

    uint32_t i = 0;
    auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec4 col) {
        a.z += zOff; b.z += zOff; c.z += zOff; d.z += zOff;
        outVerts[i++] = {a, col, uv00};
        outVerts[i++] = {b, col, uv10};
        outVerts[i++] = {c, col, uv11};
        outVerts[i++] = {a, col, uv00};
        outVerts[i++] = {c, col, uv11};
        outVerts[i++] = {d, col, uv01};
    };

    // +X face (forward)
    quad({h,-h, h}, {h,-h,-h}, {h, h,-h}, {h, h, h}, red);
    // -X face (back)
    quad({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h}, cyan);
    // +Y face (right)
    quad({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h}, green);
    // -Y face (left)
    quad({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h}, magenta);
    // +Z face (top)
    quad({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}, blue);
    // -Z face (bottom)
    quad({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h}, yellow);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Demo
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kFramesInFlight = 3;

bool RunCameraTestDemo(const CameraTestConfig& config) {
    const uint32_t frameCount = config.frameCount;

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine — Camera Test (WASD + RMB Look)";
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
    scDesc.windowHandle = window.GetHWND();
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

    std::string shadersDir = (uz::GetExeDir() / "../../shaders").string();
    shaderPipeline.Mount("/demo/camera", shadersDir);

    auto vsBytecode = shaderPipeline.LoadBytecode("/demo/camera/camera_vs.hlsl", "");
    auto psBytecode = shaderPipeline.LoadBytecode("/demo/camera/camera_ps.hlsl", "");
    if (vsBytecode.empty() || psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load camera shaders");
        return false;
    }

    // ── Shader Handles ───────────────────────────────────────────────────
    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "camera_vs";

    RHIShaderDesc psDesc{};
    psDesc.bytecode = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.entryPoint = "main";
    psDesc.debugName = "camera_ps";

    RHIShaderHandle vsHandle = device->CreateShader(vsDesc);
    RHIShaderHandle psHandle = device->CreateShader(psDesc);

    // ── Pipeline ─────────────────────────────────────────────────────────
    // SM5 input layout: POSITION(float3) + COLOR(float4) + TEXCOORD(float2)
    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Color,    0, RHIFormat::RGBA32Float, 0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 28 },
    };

    RHIGraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader = vsHandle;
    pipeDesc.pixelShader = psHandle;
    pipeDesc.topology = RHIPrimitiveTopology::TriangleList;
    pipeDesc.rasterizer.cullMode = RHICullMode::None;
    pipeDesc.depthStencil.depthEnable = true;
    pipeDesc.depthStencil.depthWrite = true;
    pipeDesc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
    pipeDesc.renderTargetCount = 1;
    pipeDesc.depthStencilFormat = RHIFormat::D32Float;
    pipeDesc.debugName = "CameraCubePipeline";

    if (!bindless) {
        pipeDesc.inputElements = inputElements;
        pipeDesc.inputElementCount = 3;
        pipeDesc.vertexStride = sizeof(SimpleVertex);
    }

    RHIPipelineHandle pipeline = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create pipeline");
        return false;
    }

    // ── Vertex Buffer ────────────────────────────────────────────────────
    SimpleVertex cubeVerts[kCubeVertexCount];
    GetCubeVertices(cubeVerts);

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(cubeVerts);
    vbDesc.stride = sizeof(SimpleVertex);
    vbDesc.usage = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "CubeVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    void* mapped = device->MapBuffer(vbHandle);
    std::memcpy(mapped, cubeVerts, sizeof(cubeVerts));

    uint32_t vbSrvIndex = 0;
    if (bindless) {
        vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
    }

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

    // ── Stats Overlay ─────────────────────────────────────────────────
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

    // ── Screenshot Capture ─────────────────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        // Screenshot capture is DX12-only for now (uses DX12 readback APIs)
        auto* dx12Dev = dynamic_cast<DX12Device*>(device);
        if (!dx12Dev) {
            UZ_LOG(Renderer, Warning, "Screenshot capture only supported on DX12 backend");
            screenshotPending = false;
        } else if (!screenshotCapture.Init(dx12Dev, scDesc.width, scDesc.height)) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            return false;
        }
    }

    // ── Camera (UE5 default viewport values, scaled to meters) ─────────
    Camera camera;
    // UE5: 90° horizontal FOV → ~59° vertical FOV at 16:9
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera.SetPerspective(vFovRad,
        static_cast<float>(winDesc.width) / static_cast<float>(winDesc.height),
        0.1f, 1000.f);
    // UE5 default: Position(0, 1024, 512)cm → meters: (0, 10.24, 5.12)
    // Scaled down for our scene: (0, 5, 2.5)
    // Yaw=-90° from +X = looking along -Y, Pitch=-15° = slightly downward
    camera.SetPosition({0.f, 5.f, 2.5f});

    constexpr float kPitchDeg = -15.f;
    constexpr float kYawDeg = -90.f;
    constexpr float kDegToRad = 3.14159265f / 180.f;
    {
        // Build forward direction from Euler angles in Z-up space
        float yawRad = kYawDeg * kDegToRad;
        float pitchRad = kPitchDeg * kDegToRad;
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

    UZ_LOG(Renderer, Info, "Camera test demo started — WASD move, RMB+mouse look, Shift sprint");

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
            // Recreate depth buffer at new size
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

        // Wait for frame N-2 before reusing its allocator
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

        CBAllocation cbAlloc = cbRing.Allocate(sizeof(PerFrameCB));
        std::memcpy(cbAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

        // ── Record ───────────────────────────────────────────────────────
        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

        // Barrier: Present -> RenderTarget
        RHIBarrierDesc toRenderTarget{};
        toRenderTarget.texture = backBuffer;
        toRenderTarget.stateBefore = RHIResourceState::Present;
        toRenderTarget.stateAfter = RHIResourceState::RenderTarget;
        cmdList->ResourceBarrier(&toRenderTarget, 1);

        // Clear render target and depth
        float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
        cmdList->ClearRenderTarget(backBuffer, clearColor);
        cmdList->ClearDepthStencil(depthBuffer, 1.0f, 0);

        // Begin render pass with depth attachment — binds both color and DSV
        RHIRenderPassDesc rpDesc{};
        rpDesc.colorAttachments[0].texture = backBuffer;
        rpDesc.colorAttachmentCount = 1;
        rpDesc.hasDepth = true;
        rpDesc.depthAttachment.texture = depthBuffer;
        cmdList->BeginRenderPass(rpDesc);

        cmdList->SetPipeline(pipeline);

        // Bind vertex data
        if (bindless) {
            cmdList->SetDescriptorIndex(0, vbSrvIndex);
        } else {
            cmdList->SetVertexBuffer(0, vbHandle, 0);
        }

        // Root CBV: per-frame CB at b1 (root param index 1)
        cmdList->SetRootCBV(1, cbAlloc.gpuAddr);

        RHIViewport vp{};
        vp.width = static_cast<float>(swapChain->GetWidth());
        vp.height = static_cast<float>(swapChain->GetHeight());
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = swapChain->GetWidth();
        sc.bottom = swapChain->GetHeight();
        cmdList->SetScissor(sc);

        cmdList->Draw(kCubeVertexCount);

        // ── Editor Grid (drawn after opaque geometry, uses alpha blend) ──
        editorGrid.Draw(cmdList, cbRing, camera);

        cmdList->EndRenderPass();

        // ── Stats Overlay (drawn last on top, no depth binding needed) ───
        statsOverlay.SetDrawCalls(2); // Cube + grid
        statsOverlay.SetVertexCount(kCubeVertexCount + 3);
        statsOverlay.SetTriangleCount(kCubeVertexCount / 3 + 1);

        // Overlay render pass: color only, no depth — 2D UI sits on top of 3D
        RHIRenderPassDesc overlayRpDesc{};
        overlayRpDesc.colorAttachments[0].texture = backBuffer;
        overlayRpDesc.colorAttachmentCount = 1;
        overlayRpDesc.hasDepth = false;
        cmdList->BeginRenderPass(overlayRpDesc);
        statsOverlay.Render(cmdList, cbRing, frameIndex);
        cmdList->EndRenderPass();

        // ── Screenshot Capture (if requested on this frame) ─────────────
        bool capturedThisFrame = false;
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            screenshotCapture.CaptureBackBuffer(
                dynamic_cast<DX12CommandList*>(cmdList),
                dynamic_cast<DX12SwapChain*>(swapChain));
            capturedThisFrame = true;
        }

        // Barrier: RenderTarget -> Present
        RHIBarrierDesc toPresent{};
        toPresent.texture = backBuffer;
        toPresent.stateBefore = RHIResourceState::RenderTarget;
        toPresent.stateAfter = RHIResourceState::Present;
        cmdList->ResourceBarrier(&toPresent, 1);

        cmdList->End();
        cmdList->Execute();

        swapChain->Present(false);

        // Signal fence via abstract interface
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);

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

    UZ_LOG(Renderer, Info,
           "Camera test demo: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.f / avgMs : 0.f);

    screenshotCapture.Shutdown();
    statsOverlay.Shutdown();
    editorGrid.Shutdown();
    cbRing.Shutdown();
    shaderPipeline.Shutdown();
    device->DestroyTexture(depthBuffer);
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
