/// Triangle Demo Implementation — Backend-Agnostic
///
/// Renders a colored RGB triangle using only abstract RHI interfaces.
/// Supports SM6.6 bindless (StructuredBuffer + SRV index root constant)
/// and SM5 Input Assembler fallback (VertexBuffer + input layout).
/// The code never touches DX12/DX11 types directly.

#include "triangle_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "log.h"
#include "vertex.h"
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "paths.h"
#include "screenshot_capture.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Triangle Geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Fills `outVerts` with the 3 vertices of a colored NDC triangle.
///
/// Winding order is clockwise (top, bottom-right, bottom-left).
/// Colors: red (top), green (bottom-right), blue (bottom-left).
static void GetTriangleVertices(SimpleVertex outVerts[3]) {
    outVerts[0] = { {  0.0f,  0.5f, 0.0f }, { 1, 0, 0, 1 }, { 0.5f, 0.0f } };  // Top — red
    outVerts[1] = { {  0.5f, -0.5f, 0.0f }, { 0, 1, 0, 1 }, { 1.0f, 1.0f } };  // Bottom-right — green
    outVerts[2] = { { -0.5f, -0.5f, 0.0f }, { 0, 0, 1, 1 }, { 0.0f, 1.0f } };  // Bottom-left — blue
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Demo
// ─────────────────────────────────────────────────────────────────────────────

/// Number of frames in flight for triple-buffered frame pacing.
static constexpr uint32_t kFramesInFlight = 3;

bool RunTriangleDemo(const TriangleDemoConfig& config) {
    const RHIBackend backend = config.backend;
    const uint32_t frameCount = config.frameCount;

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine — Triangle Demo";
    winDesc.width = 1280;
    winDesc.height = 720;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(Renderer, Error, "Failed to create window");
        return false;
    }
    window.Show();

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = backend;
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = true;
#endif

    // UZ_FORCE_SM5=1 forces the FXC/SM5 path regardless of hardware capability.
    if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

    IRHIDevice* device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(Renderer, Error, "Failed to create RHI device (backend={})",
               static_cast<int>(backend));
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

    // ── Per-frame Fences ─────────────────────────────────────────────────
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

    // ── Shader Pipeline (VFS -> cache -> DXC/FXC) ────────────────────────
    // Use bindless (SM6.6) if the device supports it, otherwise fall back to
    // the SM5 Input Assembler path compiled by FXC.
    const bool bindless = device->IsBindless();

    // SM6.6 bindless -> DXC produces DXIL; SM5 -> FXC produces DXBC.
    // Both DX12 and DX11 SM5 consume DXBC from FXC, so "dx12_sm50" works for both.
    const std::string targetBackend = bindless ? "dx12_sm66" : "dx12_sm50";

    ShaderCompiler compiler;
    if (!compiler.Init()) {
        UZ_LOG(Renderer, Error, "Failed to init DXC shader compiler");
        for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    ShaderPipeline shaderPipeline;
    ShaderPipeline::Config pipelineCfg;
    pipelineCfg.cacheDir = (uz::GetExeDir() / "cache/shaders").string();
    pipelineCfg.targetBackend = targetBackend;
    pipelineCfg.bindless = bindless;
    shaderPipeline.Init(pipelineCfg);
    shaderPipeline.SetCompiler(&compiler);

    ShaderCompilerFXC compilerFXC;
    if (!bindless) {
        if (!compilerFXC.Init()) {
            UZ_LOG(Renderer, Error, "Failed to init FXC shader compiler");
            shaderPipeline.Shutdown();
            for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
        shaderPipeline.SetCompilerFXC(&compilerFXC);
    }

    // Exe is at zig-out/bin/; shaders live at shaders/ relative to the repo root.
    const std::string shadersDir = (uz::GetExeDir() / "../../shaders").string();
    shaderPipeline.Mount("/demo/triangle", shadersDir);

    auto vsBytecode = shaderPipeline.LoadBytecode("/demo/triangle/simple_vs.hlsl", "");
    if (vsBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load VS via ShaderPipeline");
        shaderPipeline.Shutdown();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    auto psBytecode = shaderPipeline.LoadBytecode("/demo/triangle/simple_ps.hlsl", "");
    if (psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load PS via ShaderPipeline");
        shaderPipeline.Shutdown();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    UZ_LOG(Renderer, Info, "Shaders loaded via pipeline: VS={} bytes, PS={} bytes",
           vsBytecode.size(), psBytecode.size());

    // ── Create Shader Handles ────────────────────────────────────────────
    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "simple_vs";

    RHIShaderDesc psDesc{};
    psDesc.bytecode = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.entryPoint = "main";
    psDesc.debugName = "simple_ps";

    RHIShaderHandle vsHandle = device->CreateShader(vsDesc);
    RHIShaderHandle psHandle = device->CreateShader(psDesc);

    if (!vsHandle.IsValid() || !psHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create shader handles");
        shaderPipeline.Shutdown();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    // ── Create Pipeline ──────────────────────────────────────────────────
    // SM5 input layout: POSITION(float3) + COLOR(float4) + TEXCOORD(float2).
    // Only populated when not in bindless mode.
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
    pipeDesc.depthStencil.depthEnable = false;
    pipeDesc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
    pipeDesc.renderTargetCount = 1;
    pipeDesc.debugName = "TrianglePipeline";

    if (!bindless) {
        // SM5 fallback: provide explicit vertex layout for the Input Assembler.
        pipeDesc.inputElements = inputElements;
        pipeDesc.inputElementCount = 3;
        pipeDesc.vertexStride = sizeof(SimpleVertex);
    }

    RHIPipelineHandle pipeline = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create triangle pipeline");
        device->DestroyShader(vsHandle);
        device->DestroyShader(psHandle);
        shaderPipeline.Shutdown();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }
    UZ_LOG(Renderer, Info, "Triangle pipeline created (bindless={})", bindless);

    // ── Create Vertex Buffer ─────────────────────────────────────────────
    SimpleVertex vertices[3];
    GetTriangleVertices(vertices);

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(vertices);
    vbDesc.stride = sizeof(SimpleVertex);
    // SM6.6: StructuredBuffer accessed by SRV index via root constant.
    // SM5: plain VertexBuffer bound through the Input Assembler.
    vbDesc.usage = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "TriangleVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    if (!vbHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create vertex buffer");
        device->DestroyPipeline(pipeline);
        device->DestroyShader(vsHandle);
        device->DestroyShader(psHandle);
        shaderPipeline.Shutdown();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    void* mapped = device->MapBuffer(vbHandle);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "Failed to map vertex buffer");
        device->DestroyBuffer(vbHandle);
        device->DestroyPipeline(pipeline);
        device->DestroyShader(vsHandle);
        device->DestroyShader(psHandle);
        shaderPipeline.Shutdown();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }
    std::memcpy(mapped, vertices, sizeof(vertices));
    device->UnmapBuffer(vbHandle);

    uint32_t vbSrvIndex = 0;
    if (bindless) {
        vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
        if (vbSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "Vertex buffer has no SRV — check StructuredBuffer usage flag");
            device->DestroyBuffer(vbHandle);
            device->DestroyPipeline(pipeline);
            device->DestroyShader(vsHandle);
            device->DestroyShader(psHandle);
            shaderPipeline.Shutdown();
            for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
        UZ_LOG(Renderer, Info, "Vertex buffer SRV index: {}", vbSrvIndex);
    }

    // ── Screenshot Capture ────────────────────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        if (!screenshotCapture.Init(device, scDesc.width, scDesc.height)) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            screenshotPending = false;
        }
    }

    // ── Render Loop ──────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    UZ_LOG(Renderer, Info, "Starting triangle render loop (frames={})",
           frameCount == 0 ? -1 : static_cast<int>(frameCount));

    while (window.PumpMessages() && window.IsOpen()) {
        if (frameCount > 0 && frameIndex >= frameCount) break;

        // Handle resize — must drain the GPU before touching swapchain resources.
        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            window.ClearResizedFlag();
        }

        // Wait for frame N-2 before reusing its command allocator.
        const uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

        // Barrier: Present -> RenderTarget
        RHIBarrierDesc toRenderTarget{};
        toRenderTarget.texture = backBuffer;
        toRenderTarget.stateBefore = RHIResourceState::Present;
        toRenderTarget.stateAfter = RHIResourceState::RenderTarget;
        cmdList->ResourceBarrier(&toRenderTarget, 1);

        // Clear to dark charcoal background before drawing the triangle.
        const float clearColor[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
        cmdList->ClearRenderTarget(backBuffer, clearColor);

        // Begin render pass — sets the render target for subsequent draw calls.
        RHIRenderPassDesc rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments[0].texture = backBuffer;
        // Already cleared above; no load-clear needed here.
        rpDesc.colorAttachments[0].clear = false;
        cmdList->BeginRenderPass(rpDesc);

        cmdList->SetPipeline(pipeline);

        // Bind vertex data: SRV index root constant (SM6.6) or IA vertex buffer (SM5).
        if (bindless) {
            cmdList->SetDescriptorIndex(0, vbSrvIndex);
        } else {
            cmdList->SetVertexBuffer(0, vbHandle, 0);
        }

        RHIViewport vp{};
        vp.width = static_cast<float>(swapChain->GetWidth());
        vp.height = static_cast<float>(swapChain->GetHeight());
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = static_cast<int32_t>(swapChain->GetWidth());
        sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
        cmdList->SetScissor(sc);

        cmdList->Draw(3);

        cmdList->EndRenderPass();

        // ── Screenshot ────────────────────────────────────────────────────
        bool capturedThisFrame = false;
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            screenshotCapture.CaptureBackBuffer(cmdList, swapChain);
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

        // Signal fence for this frame via the abstract interface.
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
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    const float totalMs =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    const float avgMs =
        frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.0f;

    UZ_LOG(Renderer, Info,
           "Triangle demo finished: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.0f / avgMs : 0.0f);

    screenshotCapture.Shutdown();
    shaderPipeline.Shutdown();
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
