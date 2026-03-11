/// DX12 Triangle Demo — first geometry on screen.
///
/// Creates a colored triangle via SM6.6 bindless vertex fetch.
/// Vertex data is stored in a StructuredBuffer, accessed by SRV index
/// pushed as a root constant. No Input Assembler vertex layout is used.
///
/// Shaders are loaded through the ShaderPipeline (VFS → cache → DXC),
/// not compiled directly. This validates the full asset pipeline path.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "triangle_demo.h"
#include "dx12_device.h"
#include "dx12_swapchain.h"
#include "dx12_command_list.h"
#include "dx12_fence.h"
#include "platform_window.h"
#include "log.h"
#include "vertex.h"
#include "upload_context.h"
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "paths.h"

#include <chrono>
#include <d3d12.h>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Triangle Geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the 3 vertices of a colored triangle in NDC.
static void GetTriangleVertices(SimpleVertex outVerts[3]) {
    outVerts[0] = { {  0.0f,  0.5f, 0.0f }, { 1, 0, 0, 1 }, { 0.5f, 0.0f } };  // Top — red
    outVerts[1] = { {  0.5f, -0.5f, 0.0f }, { 0, 1, 0, 1 }, { 1.0f, 1.0f } };  // Bottom-right — green
    outVerts[2] = { { -0.5f, -0.5f, 0.0f }, { 0, 0, 1, 1 }, { 0.0f, 1.0f } };  // Bottom-left — blue
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Demo
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kFramesInFlight = 3;

bool RunTriangleDemo(uint32_t frameCount) {
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
    deviceDesc.backend = RHIBackend::DX12;
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = true;
#endif

    auto* rhiDevice = CreateRHIDevice(deviceDesc);
    if (!rhiDevice) {
        UZ_LOG(Renderer, Error, "Failed to create RHI device");
        return false;
    }
    auto* device = static_cast<DX12Device*>(rhiDevice);

    // ── Swapchain ────────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = window.GetHWND();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    auto* rhiSwapChain = device->CreateSwapChain(scDesc);
    if (!rhiSwapChain) {
        UZ_LOG(Renderer, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }
    auto* swapChain = static_cast<DX12SwapChain*>(rhiSwapChain);

    // ── Command List ─────────────────────────────────────────────────────
    auto* rhiCmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!rhiCmdList) {
        UZ_LOG(Renderer, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }
    auto* cmdList = static_cast<DX12CommandList*>(rhiCmdList);

    // ── Fences ───────────────────────────────────────────────────────────
    DX12Fence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = static_cast<DX12Fence*>(device->CreateFence());
        if (!frameFences[i]) {
            UZ_LOG(Renderer, Error, "Failed to create frame fence {}", i);
            for (uint32_t j = 0; j < i; ++j) device->DestroyFence(frameFences[j]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
    }

    // ── Shader Pipeline (VFS → cache → DXC) ────────────────────────────
    ShaderCompiler compiler;
    if (!compiler.Init()) {
        UZ_LOG(Renderer, Error, "Failed to init shader compiler");
        return false;
    }

    ShaderPipeline shaderPipeline;
    ShaderPipeline::Config pipelineCfg;
    pipelineCfg.cacheDir = (uz::GetExeDir() / "cache/shaders").string();
    pipelineCfg.targetBackend = "dx12_sm66";
    shaderPipeline.Init(pipelineCfg);
    shaderPipeline.SetCompiler(&compiler);

    // Mount demo shaders: exe is at zig-out/bin/, shaders at shaders/
    std::string shadersDir = (uz::GetExeDir() / "../../shaders").string();
    shaderPipeline.Mount("/demo/triangle", shadersDir);

    // Load bytecode through the pipeline (VFS read → cache check → compile if needed)
    auto vsBytecode = shaderPipeline.LoadBytecode("/demo/triangle/simple_vs.hlsl", "");
    if (vsBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load VS via ShaderPipeline");
        return false;
    }

    auto psBytecode = shaderPipeline.LoadBytecode("/demo/triangle/simple_ps.hlsl", "");
    if (psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load PS via ShaderPipeline");
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
        return false;
    }

    // ── Create Pipeline ──────────────────────────────────────────────────
    RHIGraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader = vsHandle;
    pipeDesc.pixelShader = psHandle;
    pipeDesc.topology = RHIPrimitiveTopology::TriangleList;
    pipeDesc.rasterizer.cullMode = RHICullMode::None;  // No backface culling for triangle
    pipeDesc.depthStencil.depthEnable = false;          // No depth buffer for now
    pipeDesc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
    pipeDesc.renderTargetCount = 1;
    pipeDesc.debugName = "TrianglePipeline";

    RHIPipelineHandle pipeline = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create triangle pipeline");
        return false;
    }
    UZ_LOG(Renderer, Info, "Triangle pipeline created successfully");

    // ── Create Vertex Buffer (StructuredBuffer for bindless) ─────────────
    SimpleVertex vertices[3];
    GetTriangleVertices(vertices);

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(vertices);
    vbDesc.stride = sizeof(SimpleVertex);
    vbDesc.usage = RHIBufferUsage::StructuredBuffer;
    vbDesc.memory = RHIMemoryType::Upload;  // Small data, skip staging for simplicity
    vbDesc.debugName = "TriangleVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    if (!vbHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create vertex buffer");
        return false;
    }

    // Copy vertex data into upload buffer
    void* mapped = device->MapBuffer(vbHandle);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "Failed to map vertex buffer");
        return false;
    }
    std::memcpy(mapped, vertices, sizeof(vertices));

    uint32_t vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
    if (vbSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "Vertex buffer has no SRV — check StructuredBuffer usage flag");
        return false;
    }
    UZ_LOG(Renderer, Info, "Vertex buffer SRV index: {}", vbSrvIndex);

    // ── Render Loop ──────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    UZ_LOG(Renderer, Info, "Starting triangle render loop (frames={})",
           frameCount == 0 ? -1 : static_cast<int>(frameCount));

    while (window.PumpMessages() && window.IsOpen()) {
        if (frameCount > 0 && frameIndex >= frameCount) break;

        // Handle resize
        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            window.ClearResizedFlag();
        }

        // Wait for frame N-2
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // Record
        cmdList->Begin();

        ID3D12Resource* backBuffer = swapChain->GetCurrentBackBufferResource();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain->GetCurrentRTV();

        // Barrier: Present → RenderTarget
        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        // Clear to dark gray
        float clearColor[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
        cmdList->ClearRTV(rtv, clearColor);

        // Set render target
        cmdList->GetNative()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        // Set pipeline
        cmdList->SetPipeline(pipeline);

        // Set topology
        cmdList->GetNative()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Push vertex buffer SRV index as root constant
        cmdList->SetDescriptorIndex(0, vbSrvIndex);

        // Set viewport and scissor
        RHIViewport vp{};
        vp.width = static_cast<float>(swapChain->GetWidth());
        vp.height = static_cast<float>(swapChain->GetHeight());
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = swapChain->GetWidth();
        sc.bottom = swapChain->GetHeight();
        cmdList->SetScissor(sc);

        // Draw 3 vertices (vertex shader fetches from StructuredBuffer)
        cmdList->Draw(3);

        // Barrier: RenderTarget → Present
        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        cmdList->End();
        cmdList->Execute();

        swapChain->Present(false);

        // Signal fence
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        device->GetDirectQueue()->Signal(
            frameFences[allocFrame]->GetNativeFence(), globalFenceValue);

        frameIndex++;
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.0f;

    UZ_LOG(Renderer, Info, "Triangle demo finished: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.0f / avgMs : 0.0f);

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
