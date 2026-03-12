/// DX12 Textured Quad Demo — first textured geometry on screen.
///
/// Renders a quad with a procedural checkerboard texture. Automatically
/// selects SM6.6 bindless or SM5 Input Assembler path based on GPU
/// capabilities. Validates the full texture upload pipeline:
/// staging buffer → CopyBufferToTexture → SRV access.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "textured_quad_demo.h"
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
#include "shader_compiler_fxc.h"
#include "paths.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <d3d12.h>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Procedural Checkerboard Texture
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a 256x256 RGBA8 checkerboard pattern.
///
/// Each tile is 32x32 pixels, alternating between two colors.
/// Returns tightly-packed pixel data (no padding).
static std::vector<uint8_t> GenerateCheckerboard(uint32_t width, uint32_t height,
                                                  uint32_t tileSize) {
    std::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            bool isWhite = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            uint32_t idx = (y * width + x) * 4;
            pixels[idx + 0] = isWhite ? 240 : 40;   // R
            pixels[idx + 1] = isWhite ? 240 : 40;   // G
            pixels[idx + 2] = isWhite ? 240 : 200;  // B — slight blue tint on dark tiles
            pixels[idx + 3] = 255;                   // A
        }
    }
    return pixels;
}

// ─────────────────────────────────────────────────────────────────────────────
// Quad Geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Returns 6 vertices forming a full-screen quad (two triangles).
/// UVs map [0,0] at top-left to [1,1] at bottom-right.
static void GetQuadVertices(SimpleVertex outVerts[6]) {
    // Triangle 1: top-left, top-right, bottom-left
    outVerts[0] = { { -0.8f,  0.8f, 0.0f }, { 1, 1, 1, 1 }, { 0.0f, 0.0f } };
    outVerts[1] = { {  0.8f,  0.8f, 0.0f }, { 1, 1, 1, 1 }, { 1.0f, 0.0f } };
    outVerts[2] = { { -0.8f, -0.8f, 0.0f }, { 1, 1, 1, 1 }, { 0.0f, 1.0f } };
    // Triangle 2: top-right, bottom-right, bottom-left
    outVerts[3] = { {  0.8f,  0.8f, 0.0f }, { 1, 1, 1, 1 }, { 1.0f, 0.0f } };
    outVerts[4] = { {  0.8f, -0.8f, 0.0f }, { 1, 1, 1, 1 }, { 1.0f, 1.0f } };
    outVerts[5] = { { -0.8f, -0.8f, 0.0f }, { 1, 1, 1, 1 }, { 0.0f, 1.0f } };
}

// ─────────────────────────────────────────────────────────────────────────────
// Root constants layout — must match HLSL cbuffer RootConstants : register(b0)
// ─────────────────────────────────────────────────────────────────────────────

struct RootConstants {
    uint32_t vertexBufferIndex;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

// ─────────────────────────────────────────────────────────────────────────────
// Main Demo
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kFramesInFlight = 3;
static constexpr uint32_t kTexWidth = 256;
static constexpr uint32_t kTexHeight = 256;
static constexpr uint32_t kTileSize = 32;

bool RunTexturedQuadDemo(uint32_t frameCount) {
    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine — Textured Quad Demo";
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

    if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

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

    auto* swapChain = static_cast<DX12SwapChain*>(device->CreateSwapChain(scDesc));
    if (!swapChain) {
        UZ_LOG(Renderer, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Command Lists ────────────────────────────────────────────────────
    auto* cmdList = static_cast<DX12CommandList*>(device->CreateCommandList(RHIQueueType::Graphics));
    auto* copyCmdList = static_cast<DX12CommandList*>(device->CreateCommandList(RHIQueueType::Graphics));
    if (!cmdList || !copyCmdList) {
        UZ_LOG(Renderer, Error, "Failed to create command lists");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Fences ───────────────────────────────────────────────────────────
    DX12Fence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = static_cast<DX12Fence*>(device->CreateFence());
        if (!frameFences[i]) {
            UZ_LOG(Renderer, Error, "Failed to create frame fence {}", i);
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
    shaderPipeline.Mount("/demo/textured_quad", shadersDir);

    auto vsBytecode = shaderPipeline.LoadBytecode("/demo/textured_quad/textured_vs.hlsl", "");
    auto psBytecode = shaderPipeline.LoadBytecode("/demo/textured_quad/textured_ps.hlsl", "");
    if (vsBytecode.empty() || psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load shaders via pipeline");
        return false;
    }
    UZ_LOG(Renderer, Info, "Shaders loaded: VS={} bytes, PS={} bytes",
           vsBytecode.size(), psBytecode.size());

    // ── Create Shader Handles ────────────────────────────────────────────
    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "textured_vs";

    RHIShaderDesc psDesc{};
    psDesc.bytecode = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.entryPoint = "main";
    psDesc.debugName = "textured_ps";

    RHIShaderHandle vsHandle = device->CreateShader(vsDesc);
    RHIShaderHandle psHandle = device->CreateShader(psDesc);
    if (!vsHandle.IsValid() || !psHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create shader handles");
        return false;
    }

    // ── Create Pipeline ──────────────────────────────────────────────────
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
    pipeDesc.depthStencil.depthEnable = false;
    pipeDesc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
    pipeDesc.renderTargetCount = 1;
    pipeDesc.debugName = "TexturedQuadPipeline";

    if (!bindless) {
        pipeDesc.inputElements = inputElements;
        pipeDesc.inputElementCount = 3;
        pipeDesc.vertexStride = sizeof(SimpleVertex);
    }

    RHIPipelineHandle pipeline = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create textured quad pipeline");
        return false;
    }
    UZ_LOG(Renderer, Info, "Textured quad pipeline created");

    // ── Create Vertex Buffer ─────────────────────────────────────────────
    SimpleVertex vertices[6];
    GetQuadVertices(vertices);

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(vertices);
    vbDesc.stride = sizeof(SimpleVertex);
    vbDesc.usage = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "QuadVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    if (!vbHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create vertex buffer");
        return false;
    }

    void* mapped = device->MapBuffer(vbHandle);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "Failed to map vertex buffer");
        return false;
    }
    std::memcpy(mapped, vertices, sizeof(vertices));

    uint32_t vbSrvIndex = 0;
    if (bindless) {
        vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
        if (vbSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "Vertex buffer has no SRV");
            return false;
        }
        UZ_LOG(Renderer, Info, "Vertex buffer SRV index: {}", vbSrvIndex);
    }

    // ── Create Texture ───────────────────────────────────────────────────
    RHITextureDesc texDesc{};
    texDesc.width = kTexWidth;
    texDesc.height = kTexHeight;
    texDesc.format = RHIFormat::RGBA8Unorm;
    texDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    texDesc.debugName = "CheckerboardTexture";

    RHITextureHandle texHandle = device->CreateTexture(texDesc);
    if (!texHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create checkerboard texture");
        return false;
    }

    uint32_t texSrvIndex = device->GetTextureSRVIndex(texHandle);
    if (texSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "Texture has no SRV");
        return false;
    }
    UZ_LOG(Renderer, Info, "Texture SRV index: {}", texSrvIndex);

    // ── Upload Texture ───────────────────────────────────────────────────
    auto checkerPixels = GenerateCheckerboard(kTexWidth, kTexHeight, kTileSize);

    {
        UploadContext upload(device);
        copyCmdList->Begin();

        bool uploaded = upload.UploadTexture(
            copyCmdList, texHandle,
            checkerPixels.data(), checkerPixels.size());
        if (!uploaded) {
            UZ_LOG(Renderer, Error, "Failed to upload checkerboard texture");
            return false;
        }

        // Transition texture from CopyDst to ShaderResource
        RHIBarrierDesc barrier{};
        barrier.texture = texHandle;
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter = RHIResourceState::ShaderResource;
        copyCmdList->ResourceBarrier(&barrier, 1);

        copyCmdList->End();
        copyCmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
    }
    UZ_LOG(Renderer, Info, "Checkerboard texture uploaded ({}x{})", kTexWidth, kTexHeight);

    // ── Create Sampler ───────────────────────────────────────────────────
    RHISamplerDesc samplerDesc{};
    samplerDesc.filter = RHIFilterMode::Linear;
    samplerDesc.addressU = RHIAddressMode::Wrap;
    samplerDesc.addressV = RHIAddressMode::Wrap;
    samplerDesc.debugName = "LinearWrapSampler";

    RHISamplerHandle samplerHandle = device->CreateSampler(samplerDesc);
    if (!samplerHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create sampler");
        return false;
    }

    uint32_t samplerIdx = device->GetSamplerIndex(samplerHandle);
    if (samplerIdx == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "Sampler has no heap index");
        return false;
    }
    UZ_LOG(Renderer, Info, "Sampler index: {}", samplerIdx);

    // ── Root Constants (SM6.6 bindless only) ────────────────────────────
    RootConstants rootConsts{};
    if (bindless) {
        rootConsts.vertexBufferIndex = vbSrvIndex;
        rootConsts.textureIndex = texSrvIndex;
        rootConsts.samplerIndex = samplerIdx;
    }

    // ── Render Loop ──────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    UZ_LOG(Renderer, Info, "Starting textured quad render loop (frames={})",
           frameCount == 0 ? -1 : static_cast<int>(frameCount));

    while (window.PumpMessages() && window.IsOpen()) {
        if (frameCount > 0 && frameIndex >= frameCount) break;

        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            window.ClearResizedFlag();
        }

        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        cmdList->Begin();

        ID3D12Resource* backBuffer = swapChain->GetCurrentBackBufferResource();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain->GetCurrentRTV();

        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        float clearColor[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
        cmdList->ClearRTV(rtv, clearColor);
        cmdList->GetNative()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        cmdList->SetPipeline(pipeline);
        cmdList->GetNative()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Bind vertex data and texture
        if (bindless) {
            cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rootConsts), 3, 0);
        } else {
            cmdList->SetVertexBuffer(0, vbHandle, 0);
            cmdList->SetDescriptorTable(3, texSrvIndex);
        }

        RHIViewport vp{};
        vp.width = static_cast<float>(swapChain->GetWidth());
        vp.height = static_cast<float>(swapChain->GetHeight());
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = swapChain->GetWidth();
        sc.bottom = swapChain->GetHeight();
        cmdList->SetScissor(sc);

        // Draw 6 vertices (2 triangles = quad)
        cmdList->Draw(6);

        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        cmdList->End();
        cmdList->Execute();

        swapChain->Present(false);

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

    UZ_LOG(Renderer, Info, "Textured quad demo finished: {} frames, {:.1f}ms total, "
           "{:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.0f / avgMs : 0.0f);

    shaderPipeline.Shutdown();
    device->DestroySampler(samplerHandle);
    device->DestroyTexture(texHandle);
    device->DestroyBuffer(vbHandle);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vsHandle);
    device->DestroyShader(psHandle);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
    device->DestroyCommandList(copyCmdList);
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return true;
}

} // namespace uz
