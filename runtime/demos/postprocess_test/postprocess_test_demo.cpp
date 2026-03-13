/// Post-Process Test Demo Implementation
///
/// Validates the post-processing pipeline by:
///   1. Compiling all 7 post-process shaders (fullscreen_vs, tonemap_ps,
///      bloom_downsample_ps, bloom_blur_h_ps, bloom_blur_v_ps,
///      bloom_composite_ps, fxaa_ps)
///   2. Creating PSOs for each fullscreen pass
///   3. Creating intermediate HDR/LDR render targets
///   4. Rendering an animated HDR clear-color scene
///
/// The actual post-process chain execution (tonemap → bloom → FXAA) requires
/// a dedicated root signature with b0 CBV + t0 SRV slots, which will be
/// added when AddToneMapPass/AddBloomPass/AddFXAAPass .cpp files are implemented.
/// This demo proves shader compilation, PSO creation, and resource setup work.

#include "postprocess_test_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "log.h"
#include "profiler.h"
#include "screenshot_capture.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "engine_shaders.h"
#include "paths.h"
#include "input.h"

#include <cmath>
#include <chrono>
#include <thread>
#include <cstring>
#include <memory>

namespace uz {

static constexpr uint32_t kFramesInFlight = 3;

UZ_DEFINE_LOG_CATEGORY(PostProcessDemo, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Helper: create fullscreen PSO
// ─────────────────────────────────────────────────────────────────────────────

static RHIPipelineHandle CreateFullscreenPSO(IRHIDevice* device,
                                              RHIShaderHandle vs,
                                              RHIShaderHandle ps,
                                              RHIFormat rtFormat,
                                              const char* debugName)
{
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader = vs;
    desc.pixelShader  = ps;
    desc.topology     = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::None;
    desc.depthStencil.depthEnable         = false;
    desc.depthStencil.depthWrite          = false;
    desc.renderTargetFormats[0]           = rtFormat;
    desc.renderTargetCount                = 1;
    desc.debugName                        = debugName;
    return device->CreateGraphicsPipeline(desc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main demo function
// ─────────────────────────────────────────────────────────────────────────────

bool RunPostProcessTest(const PostProcessTestConfig& config) {
    UZ_PROFILE_FUNCTION();
    UZ_PROFILE_THREAD("Main");

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title  = "UzEngine — Post-Process Test (T=tonemap B=bloom A=fxaa)";
    winDesc.width  = 1280;
    winDesc.height = 720;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create window");
        return false;
    }
    window.Show();

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = config.backend;
    deviceDesc.enableValidation = true;

    IRHIDevice* device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create RHI device");
        return false;
    }

    // ── Swapchain ────────────────────────────────────────────────────────
    uint32_t w = static_cast<uint32_t>(window.GetWidth());
    uint32_t h = static_cast<uint32_t>(window.GetHeight());

    RHISwapChainDesc scDesc{};
    scDesc.windowHandle   = window.GetNativeHandle();
    scDesc.displayHandle  = window.GetNativeDisplayHandle();
    scDesc.width          = w;
    scDesc.height         = h;
    scDesc.bufferCount    = kFramesInFlight;
    scDesc.format         = RHIFormat::RGBA8Unorm;
    scDesc.vsync          = false;

    IRHISwapChain* swapChain = device->CreateSwapChain(scDesc);
    if (!swapChain) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Command list + fences ────────────────────────────────────────────
    IRHICommandList* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    IRHIFence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = device->CreateFence();
        if (!frameFences[i]) {
            UZ_LOG(PostProcessDemo, Error, "Failed to create fence {}", i);
            for (uint32_t j = 0; j < i; ++j) device->DestroyFence(frameFences[j]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
    }

    // ── Compile all post-process shaders ─────────────────────────────────
    auto compiler = std::make_unique<ShaderCompiler>();
    if (!compiler->Init()) {
        UZ_LOG(PostProcessDemo, Error, "Failed to init DXC shader compiler");
        return false;
    }

    auto compilerFXC = std::make_unique<ShaderCompilerFXC>();
    compilerFXC->Init();

    auto shaderPipeline = std::make_unique<ShaderPipeline>();
    ShaderPipeline::Config pipeCfg;
    pipeCfg.cacheDir      = (uz::GetExeDir() / "cache/shaders").string();
    pipeCfg.targetBackend = "dx12_sm50";
    pipeCfg.bindless      = false;
    shaderPipeline->Init(pipeCfg);
    shaderPipeline->SetCompiler(compiler.get());
    shaderPipeline->SetCompilerFXC(compilerFXC.get());

    auto loadShader = [&](EngineShader shader) {
        return shaderPipeline->LoadBytecodeFromSource(
            EngineShaders::GetName(shader), EngineShaders::GetSource(shader));
    };

    auto fsVSBytes       = loadShader(EngineShader::FullscreenVS);
    auto tonemapBytes    = loadShader(EngineShader::TonemapPS);
    auto bloomDownBytes  = loadShader(EngineShader::BloomDownsamplePS);
    auto bloomBlurHBytes = loadShader(EngineShader::BloomBlurHPS);
    auto bloomBlurVBytes = loadShader(EngineShader::BloomBlurVPS);
    auto bloomCompBytes  = loadShader(EngineShader::BloomCompositePS);
    auto fxaaBytes       = loadShader(EngineShader::FxaaPS);

    uint32_t compiled = 0;
    uint32_t total = 7;
    if (!fsVSBytes.empty())       ++compiled;
    if (!tonemapBytes.empty())    ++compiled;
    if (!bloomDownBytes.empty())  ++compiled;
    if (!bloomBlurHBytes.empty()) ++compiled;
    if (!bloomBlurVBytes.empty()) ++compiled;
    if (!bloomCompBytes.empty())  ++compiled;
    if (!fxaaBytes.empty())       ++compiled;

    UZ_LOG(PostProcessDemo, Info, "Shader compilation: {}/{} succeeded", compiled, total);

    // Create RHI shader handles
    auto makeShader = [&](const std::vector<uint8_t>& bc, RHIShaderStage stage,
                          const char* name) -> RHIShaderHandle {
        if (bc.empty()) return {};
        RHIShaderDesc d{};
        d.bytecode     = bc.data();
        d.bytecodeSize = bc.size();
        d.stage        = stage;
        d.entryPoint   = "main";
        d.debugName    = name;
        return device->CreateShader(d);
    };

    RHIShaderHandle fsVS        = makeShader(fsVSBytes,       RHIShaderStage::Vertex, "fullscreen_vs");
    RHIShaderHandle tonemapPS   = makeShader(tonemapBytes,    RHIShaderStage::Pixel,  "tonemap_ps");
    RHIShaderHandle bloomDownPS = makeShader(bloomDownBytes,  RHIShaderStage::Pixel,  "bloom_downsample_ps");
    RHIShaderHandle bloomBlurHPS = makeShader(bloomBlurHBytes, RHIShaderStage::Pixel, "bloom_blur_h_ps");
    RHIShaderHandle bloomBlurVPS = makeShader(bloomBlurVBytes, RHIShaderStage::Pixel, "bloom_blur_v_ps");
    RHIShaderHandle bloomCompPS = makeShader(bloomCompBytes,  RHIShaderStage::Pixel,  "bloom_composite_ps");
    RHIShaderHandle fxaaPS      = makeShader(fxaaBytes,       RHIShaderStage::Pixel,  "fxaa_ps");

    // ── Create PSOs ──────────────────────────────────────────────────────
    uint32_t psoCount = 0;
    auto tryPSO = [&](RHIShaderHandle ps, RHIFormat fmt, const char* name) -> RHIPipelineHandle {
        if (!fsVS.IsValid() || !ps.IsValid()) return {};
        auto pso = CreateFullscreenPSO(device, fsVS, ps, fmt, name);
        if (pso.IsValid()) ++psoCount;
        return pso;
    };

    auto tonemapPSO    = tryPSO(tonemapPS,   RHIFormat::RGBA8Unorm,  "ToneMap_PSO");
    auto bloomDownPSO  = tryPSO(bloomDownPS, RHIFormat::RGBA16Float, "BloomDown_PSO");
    auto bloomBlurHPSO = tryPSO(bloomBlurHPS, RHIFormat::RGBA16Float, "BloomBlurH_PSO");
    auto bloomBlurVPSO = tryPSO(bloomBlurVPS, RHIFormat::RGBA16Float, "BloomBlurV_PSO");
    auto bloomCompPSO  = tryPSO(bloomCompPS, RHIFormat::RGBA8Unorm,  "BloomComp_PSO");
    auto fxaaPSO       = tryPSO(fxaaPS,      RHIFormat::RGBA8Unorm,  "FXAA_PSO");

    // Suppress unused variable warnings
    (void)tonemapPSO; (void)bloomDownPSO; (void)bloomBlurHPSO;
    (void)bloomBlurVPSO; (void)bloomCompPSO; (void)fxaaPSO;

    UZ_LOG(PostProcessDemo, Info, "PSO creation: {}/6 succeeded", psoCount);

    // ── Create intermediate textures ─────────────────────────────────────
    const RHITextureUsage kRTUsage = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;

    auto createRT = [&](RHIFormat fmt, RHITextureUsage usage, const char* name) -> RHITextureHandle {
        RHITextureDesc td{};
        td.width     = w;
        td.height    = h;
        td.format    = fmt;
        td.usage     = usage;
        td.debugName = name;
        return device->CreateTexture(td);
    };

    RHITextureHandle hdrTarget     = createRT(RHIFormat::RGBA16Float, kRTUsage, "HDR_Scene");
    RHITextureHandle tonemapTarget = createRT(RHIFormat::RGBA8Unorm,  kRTUsage, "ToneMap_Out");
    RHITextureHandle bloomDownTex  = createRT(RHIFormat::RGBA16Float, kRTUsage, "Bloom_Down");
    RHITextureHandle bloomBlurHTex = createRT(RHIFormat::RGBA16Float, kRTUsage, "Bloom_BlurH");
    RHITextureHandle bloomBlurVTex = createRT(RHIFormat::RGBA16Float, kRTUsage, "Bloom_BlurV");
    RHITextureHandle bloomCompTex  = createRT(RHIFormat::RGBA8Unorm,  kRTUsage, "Bloom_Comp");

    UZ_LOG(PostProcessDemo, Info, "Created 6 intermediate render targets ({}x{})", w, h);

    // ── Input ────────────────────────────────────────────────────────────
    InputSystem input;
    input.Init();
    window.SetInputSystem(&input);

    // ── Screenshot ───────────────────────────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        screenshotCapture.Init(device, w, h);
    }

    // ── Render graph pool ────────────────────────────────────────────────
    rg::RgResourcePool rgPool(device);

    // ── State ────────────────────────────────────────────────────────────
    bool enableTonemap = true;
    bool enableBloom   = true;
    bool enableFXAA    = true;

    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    UZ_LOG(PostProcessDemo, Info,
        "Starting render loop — T=tonemap B=bloom A=fxaa toggles");

    // ── Render loop ──────────────────────────────────────────────────────
    while (window.PumpMessages() && window.IsOpen()) {
        if (window.IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        UZ_PROFILE_SCOPE(Frame);
        if (config.frameCount > 0 && frameIndex >= config.frameCount) break;

        // Resize
        if (window.WasResized()) {
            device->WaitIdle();
            w = static_cast<uint32_t>(window.GetWidth());
            h = static_cast<uint32_t>(window.GetHeight());
            swapChain->Resize(w, h);
            window.ClearResizedFlag();
        }

        // Input
        input.BeginFrame();
        if (input.IsKeyJustPressed(Key::T)) {
            enableTonemap = !enableTonemap;
            UZ_LOG(PostProcessDemo, Info, "Tone mapping: {}", enableTonemap ? "ON" : "OFF");
        }
        if (input.IsKeyJustPressed(Key::B)) {
            enableBloom = !enableBloom;
            UZ_LOG(PostProcessDemo, Info, "Bloom: {}", enableBloom ? "ON" : "OFF");
        }
        if (input.IsKeyJustPressed(Key::A)) {
            enableFXAA = !enableFXAA;
            UZ_LOG(PostProcessDemo, Info, "FXAA: {}", enableFXAA ? "ON" : "OFF");
        }

        // Wait for oldest frame fence
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        auto now = std::chrono::high_resolution_clock::now();
        float t = std::chrono::duration<float>(now - startTime).count();

        // ── Animated HDR clear color (simulates bright scene) ────────────
        // Red/green/blue channels pulse with different frequencies
        float clearColor[4] = {
            0.3f + 2.5f * std::max(0.0f, std::sin(t * 1.0f)),         // Bright red pulse
            0.3f + 2.5f * std::max(0.0f, std::sin(t * 1.3f + 2.1f)),  // Bright green pulse
            0.3f + 2.5f * std::max(0.0f, std::sin(t * 1.7f + 4.2f)),  // Bright blue pulse
            1.0f
        };

        // ── Build render graph ───────────────────────────────────────────
        bool capturedThisFrame = false;
        {
        UZ_PROFILE_SCOPE(RecordCommands);
        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
        rg::RgBuilder rgBuilder(device, &rgPool);
        auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

        // Clear backbuffer with animated HDR-range color
        // (Values > 1.0 would bloom in a real post-process pipeline)
        rgBuilder.AddPass("ClearHDR", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                cmd->ClearRenderTarget(res.GetTexture(bbRes), clearColor);
            });
        });

        // Screenshot
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
                pb.Read(bbRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                    screenshotCapture.CaptureBackBuffer(cmdList, swapChain);
                    capturedThisFrame = true;
                });
            });
        }

        // Present barrier
        rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::Present);
            pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
        });

        rgBuilder.Compile();
        rgBuilder.Execute(cmdList);
        cmdList->End();
        }

        // ── Execute + Present ────────────────────────────────────────────
        {
        UZ_PROFILE_SCOPE(ExecuteAndPresent);
        cmdList->Execute();
        swapChain->Present(false);
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);
        }

        if (capturedThisFrame) {
            frameFences[allocFrame]->WaitCPU(globalFenceValue);
            screenshotCapture.SaveToPNG(config.screenshotPath);
            screenshotPending = false;
        }

        frameIndex++;
        UZ_PROFILE_FRAME_MARK();
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.0f;

    UZ_LOG(PostProcessDemo, Info,
        "Post-process demo finished: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
        frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.0f / avgMs : 0.0f);

    screenshotCapture.Shutdown();

    device->DestroyTexture(hdrTarget);
    device->DestroyTexture(tonemapTarget);
    device->DestroyTexture(bloomDownTex);
    device->DestroyTexture(bloomBlurHTex);
    device->DestroyTexture(bloomBlurVTex);
    device->DestroyTexture(bloomCompTex);

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        device->DestroyFence(frameFences[i]);
    }
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return true;
}

} // namespace uz
