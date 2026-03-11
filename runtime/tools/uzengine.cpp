/// UzEngine Standalone Runtime Executable
///
/// Entry point for the engine runtime. Parses CLI arguments, resolves RHI
/// configuration, creates a window (unless headless), and runs a render loop.
/// Currently renders an animated clear screen as a foundation for future
/// scene rendering.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "config.h"
#include "rhi_config.h"
#include "rhi_backend.h"
#include "log.h"

#ifdef _WIN32
#include "platform_window.h"
#include "dx12_device.h"
#include "dx12_swapchain.h"
#include "dx12_command_list.h"
#include "dx12_fence.h"
#include <d3d12.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Exit codes
// ─────────────────────────────────────────────────────────────────────────────

/// Process exit code: normal termination.
static constexpr int kExitSuccess = 0;

/// Process exit code: RHI device/swapchain/command list creation failure.
static constexpr int kExitRenderError = 1;

/// Process exit code: shader compilation failure.
static constexpr int kExitShaderError = 2;

/// Process exit code: GPU device lost (TDR, driver crash).
static constexpr int kExitDeviceLost = 3;

/// Number of swapchain back buffers for triple-buffered frame pacing.
static constexpr uint32_t kFramesInFlight = 3;

/// Default window width in pixels when no `--width` CLI override is given.
static constexpr int kDefaultWidth = 1280;

/// Default window height in pixels when no `--height` CLI override is given.
static constexpr int kDefaultHeight = 720;

/// Sentinel value for `--frames` meaning "run until window close".
static constexpr int kInfiniteFrames = -1;

// ─────────────────────────────────────────────────────────────────────────────
// Config helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Loads the project file and applies CLI overrides.
static Config BuildConfig(int argc, char** argv) {
    Config config;
    ConfigSetEngineDefaults(config);

    const char* const* constArgv = argv;
    config.ApplyCLIOverrides(argc, constArgv);

    const std::string projectPath = config.GetString("project");
    if (!projectPath.empty()) {
        UZ_LOG(Engine, Info, "Loading project: {}", projectPath);
        if (!config.LoadFromFile(projectPath)) {
            UZ_LOG(Engine, Warning, "Failed to load project file: {}", projectPath);
        }
        // Re-apply CLI so they take precedence over project file
        config.ApplyCLIOverrides(argc, constArgv);
    }

    return config;
}

/// Logs the resolved RHI configuration for diagnostics.
static void LogResolvedConfig(const RHIConfig& rhiCfg, bool headless, int frames, int width, int height) {
    UZ_LOG(Engine, Info, "RHI backend:       {}", RHIBackendToString(rhiCfg.backend));
    UZ_LOG(Engine, Info, "Shader model:      {}", ShaderModelToString(rhiCfg.shaderModel));
    UZ_LOG(Engine, Info, "Validation:        {}", rhiCfg.enableValidation ? "on" : "off");
    UZ_LOG(Engine, Info, "GPU validation:    {}", rhiCfg.enableGpuValidation ? "on" : "off");
    UZ_LOG(Engine, Info, "Breadcrumbs:       {}", rhiCfg.enableBreadcrumbs ? "on" : "off");
    UZ_LOG(Engine, Info, "Adapter index:     {}", rhiCfg.adapterIndex);
    UZ_LOG(Engine, Info, "Headless:          {}", headless ? "yes" : "no");
    UZ_LOG(Engine, Info, "Frame limit:       {}", frames == kInfiniteFrames ? "infinite" : std::to_string(frames));
    UZ_LOG(Engine, Info, "Window size:       {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Headless mode
// ─────────────────────────────────────────────────────────────────────────────

/// Runs headless mode: creates device, logs success, exits.
static int RunHeadless(const RHIDeviceDesc& deviceDesc) {
    UZ_LOG(Engine, Info, "Headless mode — creating device only");

    auto* device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(Engine, Error, "Failed to create RHI device in headless mode");
        return kExitRenderError;
    }

    UZ_LOG(Engine, Info, "Headless device created successfully");
    DestroyRHIDevice(device);
    return kExitSuccess;
}

// ─────────────────────────────────────────────────────────────────────────────
// Windowed render loop (Windows/DX12)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32

/// Creates per-frame fences for triple-buffered synchronization.
///
/// Returns false if any fence creation fails. On failure, all previously
/// created fences are cleaned up.
static bool CreateFrameFences(DX12Device* device, DX12Fence* fences[], uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        auto* f = static_cast<DX12Fence*>(device->CreateFence());
        if (!f) {
            UZ_LOG(RHI, Error, "Failed to create frame fence {}", i);
            for (uint32_t j = 0; j < i; ++j) {
                device->DestroyFence(fences[j]);
            }
            return false;
        }
        fences[i] = f;
    }
    return true;
}

/// Computes an animated sine-wave clear color.
static void ComputeClearColor(float t, float outColor[4]) {
    outColor[0] = 0.5f + 0.5f * std::sin(t * 1.0f);
    outColor[1] = 0.5f + 0.5f * std::sin(t * 1.3f + 2.094f);
    outColor[2] = 0.5f + 0.5f * std::sin(t * 1.7f + 4.189f);
    outColor[3] = 1.0f;
}

/// Records and submits a single clear-screen frame.
static void RenderClearFrame(DX12CommandList* cmdList, DX12SwapChain* swapChain, float t) {
    float color[4];
    ComputeClearColor(t, color);

    cmdList->Begin();

    ID3D12Resource* backBuffer = swapChain->GetCurrentBackBufferResource();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain->GetCurrentRTV();

    cmdList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ClearRTV(rtv, color);
    cmdList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    cmdList->End();
    cmdList->Execute();
}

/// Logs final render statistics after the loop ends.
static void LogRenderStats(uint32_t frameIndex, float totalMs) {
    const float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.0f;
    const float fps = avgMs > 0.0f ? 1000.0f / avgMs : 0.0f;
    UZ_LOG(Engine, Info, "Render loop finished: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, fps);
}

/// Runs the windowed render loop with DX12 clear screen.
static int RunWindowed(const RHIDeviceDesc& deviceDesc, int width, int height, int maxFrames,
                       const std::string& scenePath) {
    // ── Window ──────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine Runtime";
    winDesc.width = width;
    winDesc.height = height;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(Engine, Error, "Failed to create window");
        return kExitRenderError;
    }
    window.Show();

    // ── Device ──────────────────────────────────────────────────────────
    auto* rhiDevice = CreateRHIDevice(deviceDesc);
    if (!rhiDevice) {
        UZ_LOG(Engine, Error, "Failed to create RHI device");
        return kExitRenderError;
    }
    auto* device = static_cast<DX12Device*>(rhiDevice);

    // ── Swapchain ───────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = window.GetHWND();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    auto* swapChain = static_cast<DX12SwapChain*>(device->CreateSwapChain(scDesc));
    if (!swapChain) {
        UZ_LOG(Engine, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return kExitRenderError;
    }

    // ── Command List ────────────────────────────────────────────────────
    auto* cmdList = static_cast<DX12CommandList*>(device->CreateCommandList(RHIQueueType::Graphics));
    if (!cmdList) {
        UZ_LOG(Engine, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return kExitRenderError;
    }

    // ── Per-frame fences ────────────────────────────────────────────────
    DX12Fence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};

    if (!CreateFrameFences(device, frameFences, kFramesInFlight)) {
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return kExitRenderError;
    }

    // ── Render Loop ─────────────────────────────────────────────────────
    if (!scenePath.empty()) {
        UZ_LOG(Engine, Info, "Scene path stored (not loaded yet): {}", scenePath);
    }

    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    const uint32_t frameLimit = maxFrames == kInfiniteFrames ? 0 : static_cast<uint32_t>(maxFrames);
    UZ_LOG(Engine, Info, "Starting render loop (frames={})", maxFrames);

    while (window.PumpMessages() && window.IsOpen()) {
        if (frameLimit > 0 && frameIndex >= frameLimit) break;

        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            window.ClearResizedFlag();
        }

        // Wait for frame N-2 before reusing its allocator
        const uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // Render
        auto now = std::chrono::high_resolution_clock::now();
        const float t = std::chrono::duration<float>(now - startTime).count();
        RenderClearFrame(cmdList, swapChain, t);

        // Present + signal fence
        swapChain->Present(false);
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        device->GetDirectQueue()->Signal(
            frameFences[allocFrame]->GetNativeFence(), globalFenceValue);

        frameIndex++;
    }

    // ── Cleanup (reverse order of creation) ─────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    const float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    LogRenderStats(frameIndex, totalMs);

    for (uint32_t i = kFramesInFlight; i > 0; --i) {
        device->DestroyFence(frameFences[i - 1]);
    }
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return kExitSuccess;
}

#endif // _WIN32

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    uz::LogInit();

    UZ_LOG(Engine, Info, "UzEngine Runtime starting");

    // Config: project file + CLI overrides
    uz::Config config = uz::BuildConfig(argc, argv);

    // Resolve RHI settings from config
    const uz::RHIConfig rhiCfg = uz::ResolveRHIConfig(config);
    const uz::RHIDeviceDesc deviceDesc = uz::ToDeviceDesc(rhiCfg);

    const bool headless = config.GetBool("headless", false);
    const int maxFrames = config.GetInt("frames", uz::kInfiniteFrames);
    const int width = config.GetInt("width", uz::kDefaultWidth);
    const int height = config.GetInt("height", uz::kDefaultHeight);
    const std::string scenePath = config.GetString("scene");

    uz::LogResolvedConfig(rhiCfg, headless, maxFrames, width, height);

    int exitCode = uz::kExitSuccess;

    if (headless) {
        exitCode = uz::RunHeadless(deviceDesc);
    } else {
#ifdef _WIN32
        exitCode = uz::RunWindowed(deviceDesc, width, height, maxFrames, scenePath);
#else
        UZ_LOG(Engine, Error, "Windowed mode not supported on this platform yet");
        exitCode = uz::kExitRenderError;
#endif
    }

    UZ_LOG(Engine, Info, "UzEngine Runtime exiting with code {}", exitCode);
    uz::LogShutdown();
    return exitCode;
}
