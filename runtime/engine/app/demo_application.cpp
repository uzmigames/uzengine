/// DemoApplication — Boilerplate extraction for all engine demos.
///
/// Centralizes window creation, RHI device init, swapchain, command list,
/// per-frame fence sync, resize handling, minimize sleep, and frame timing.
/// See demo_application.h for full documentation.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "demo_application.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"
#include "platform_window.h"
#include "log.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace uz {

UZ_DEFINE_LOG_CATEGORY(App, Trace);

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

DemoApplication::DemoApplication(const DemoConfig& config)
    : m_config(config) {}

DemoApplication::DemoApplication(const char* title, uint32_t width, uint32_t height)
    : m_config{} {
    m_config.title = title;
    m_config.width = width;
    m_config.height = height;
}

DemoApplication::~DemoApplication() {
    // ShutdownPlatform is called in Run(), but guard against early destruction
    ShutdownPlatform();
}

// ─────────────────────────────────────────────────────────────────────────────
// Run — Main application lifecycle
// ─────────────────────────────────────────────────────────────────────────────

int DemoApplication::Run() {
    if (!InitPlatform()) {
        UZ_LOG(App, Error, "DemoApplication: platform initialization failed");
        ShutdownPlatform();
        return 1;
    }

    m_width = static_cast<uint32_t>(m_window->GetWidth());
    m_height = static_cast<uint32_t>(m_window->GetHeight());

    OnInit();

    UZ_LOG(App, Info, "DemoApplication '{}': entering render loop (maxFrames={})",
           m_config.title, m_config.maxFrames == 0 ? -1 : static_cast<int>(m_config.maxFrames));

    auto loopStart = std::chrono::high_resolution_clock::now();
    auto prevFrame = loopStart;

    while (m_window->PumpMessages() && m_window->IsOpen()) {
        // Skip rendering while minimized — swapchain surface is 0x0
        if (m_window->IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Frame cap check
        if (m_config.maxFrames > 0 && m_frameIndex >= m_config.maxFrames) {
            break;
        }

        // Resize detection
        if (m_window->WasResized()) {
            m_device->WaitIdle();

            const auto newWidth = static_cast<uint32_t>(m_window->GetWidth());
            const auto newHeight = static_cast<uint32_t>(m_window->GetHeight());
            m_swapChain->Resize(newWidth, newHeight);
            m_window->ClearResizedFlag();

            m_width = newWidth;
            m_height = newHeight;

            OnResize(m_width, m_height);
        }

        // Delta time calculation
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prevFrame).count();
        prevFrame = now;

        // Clamp to prevent explosion after breakpoints/stalls
        if (dt > kMaxDeltaTime) {
            dt = kMaxDeltaTime;
        }

        // Wait for the oldest in-flight frame to finish on GPU
        const uint32_t allocFrame = m_frameIndex % kFramesInFlight;
        if (m_fenceValues[allocFrame] > 0) {
            m_frameFences[allocFrame]->WaitCPU(m_fenceValues[allocFrame]);
        }

        // Begin recording
        m_cmdList->Begin();

        // Subclass renders here
        OnFrame(dt);

        // Submit and present
        m_cmdList->End();
        m_cmdList->Execute();

        m_swapChain->Present(m_config.vsync);

        // Signal fence for this frame
        m_globalFenceValue++;
        m_fenceValues[allocFrame] = m_globalFenceValue;
        m_frameFences[allocFrame]->Signal(m_globalFenceValue);

        m_frameIndex++;
    }

    // Shutdown
    m_device->WaitIdle();

    // Log stats
    auto loopEnd = std::chrono::high_resolution_clock::now();
    const float totalSec = std::chrono::duration<float>(loopEnd - loopStart).count();
    const float avgFps = m_frameIndex > 0 ? static_cast<float>(m_frameIndex) / totalSec : 0.0f;

    UZ_LOG(App, Info, "DemoApplication '{}': {} frames in {:.1f}s ({:.1f} avg FPS)",
           m_config.title, m_frameIndex, totalSec, avgFps);

    OnShutdown();
    ShutdownPlatform();

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// IsBindless
// ─────────────────────────────────────────────────────────────────────────────

bool DemoApplication::IsBindless() const {
    return m_device && m_device->IsBindless();
}

// ─────────────────────────────────────────────────────────────────────────────
// InitPlatform — Creates window, device, swapchain, command list, fences
// ─────────────────────────────────────────────────────────────────────────────

bool DemoApplication::InitPlatform() {
    // ── Window ───────────────────────────────────────────────────────────
    m_window = new PlatformWindow();

    WindowDesc winDesc{};
    winDesc.title = m_config.title;
    winDesc.width = static_cast<int32_t>(m_config.width);
    winDesc.height = static_cast<int32_t>(m_config.height);

    if (!m_window->Create(winDesc)) {
        UZ_LOG(App, Error, "Failed to create window");
        return false;
    }
    m_window->Show();

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = RHIBackend::DX12;
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = m_config.enableValidation;
#endif

    // Allow SM5 force via environment variable (for testing fallback paths)
    if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

    m_device = CreateRHIDevice(deviceDesc);
    if (!m_device) {
        UZ_LOG(App, Error, "Failed to create RHI device");
        return false;
    }

    UZ_LOG(App, Info, "GPU: {} ({})", m_device->GetAdapterName(), m_device->GetFeatureLevel());

    // ── Swapchain ────────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = m_window->GetNativeHandle();
    scDesc.displayHandle = m_window->GetNativeDisplayHandle();
    scDesc.width = static_cast<uint32_t>(m_window->GetWidth());
    scDesc.height = static_cast<uint32_t>(m_window->GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = m_config.vsync;

    m_swapChain = m_device->CreateSwapChain(scDesc);
    if (!m_swapChain) {
        UZ_LOG(App, Error, "Failed to create swapchain");
        return false;
    }

    // ── Command List ─────────────────────────────────────────────────────
    m_cmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
    if (!m_cmdList) {
        UZ_LOG(App, Error, "Failed to create command list");
        return false;
    }

    // ── Fences ───────────────────────────────────────────────────────────
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        m_frameFences[i] = m_device->CreateFence();
        if (!m_frameFences[i]) {
            UZ_LOG(App, Error, "Failed to create frame fence {}", i);
            return false;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShutdownPlatform — Destroys all objects in reverse creation order
// ─────────────────────────────────────────────────────────────────────────────

void DemoApplication::ShutdownPlatform() {
    // Fences
    if (m_device) {
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            if (m_frameFences[i]) {
                m_device->DestroyFence(m_frameFences[i]);
                m_frameFences[i] = nullptr;
            }
        }
    }

    // Command list
    if (m_device && m_cmdList) {
        m_device->DestroyCommandList(m_cmdList);
        m_cmdList = nullptr;
    }

    // Swapchain
    if (m_device && m_swapChain) {
        m_device->DestroySwapChain(m_swapChain);
        m_swapChain = nullptr;
    }

    // Device
    if (m_device) {
        DestroyRHIDevice(m_device);
        m_device = nullptr;
    }

    // Window
    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }
}

} // namespace uz
