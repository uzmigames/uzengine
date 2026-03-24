/// uz::Application — Platform initialization and shutdown.
///
/// Creates/destroys window, RHI device, swapchain, command list, fences,
/// VFS, and ECS World. Also handles resize and deferred scene loading.
/// See application.h for the full Application interface.

#include "application.h"
#include "uzengine.h"
#include "scene_renderer.h"
#include "scene_loader.h"
#include "world.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// InitPlatform
// ─────────────────────────────────────────────────────────────────────────────

bool Application::InitPlatform() {
    m_window = std::make_unique<PlatformWindow>();

    WindowDesc winDesc{};
    winDesc.title  = m_config.title;
    winDesc.width  = static_cast<int32_t>(m_config.width);
    winDesc.height = static_cast<int32_t>(m_config.height);

    if (!m_window->Create(winDesc)) {
        UZ_LOG(App, Error, "Application: failed to create window: '{}' {}x{}",
               m_config.title, m_config.width, m_config.height);
        return false;
    }
    if (!m_headless) {
        m_window->Show();
    } else {
        UZ_LOG(App, Info, "Headless mode: window created but hidden (HWND exists for swapchain)");
    }

    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = m_config.backend.value_or(RHIBackend::DX12);
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = m_config.enableValidation;
#endif
    if (m_config.shaderModel.has_value()) {
        deviceDesc.forceShaderModel = m_config.shaderModel.value();
    } else if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

    UZ_LOG(App, Info, "Application '{}': backend={}, shaderModel={}",
           m_config.title, RHIBackendToString(deviceDesc.backend),
           deviceDesc.forceShaderModel.has_value()
               ? ShaderModelToString(deviceDesc.forceShaderModel.value()) : "auto");

    m_device = CreateRHIDevice(deviceDesc);
    if (!m_device) {
        UZ_LOG(App, Error, "Application: failed to create RHI device");
        return false;
    }
    UZ_LOG(App, Info, "GPU: {} ({})", m_device->GetAdapterName(), m_device->GetFeatureLevel());

    RHISwapChainDesc scDesc{};
    scDesc.windowHandle  = m_window->GetNativeHandle();
    scDesc.displayHandle = m_window->GetNativeDisplayHandle();
    scDesc.width         = static_cast<uint32_t>(m_window->GetWidth());
    scDesc.height        = static_cast<uint32_t>(m_window->GetHeight());
    scDesc.bufferCount   = kFramesInFlight;
    scDesc.format        = RHIFormat::RGBA8Unorm;
    scDesc.vsync         = m_config.vsync;

    m_swapChain = m_device->CreateSwapChain(scDesc);
    if (!m_swapChain) {
        UZ_LOG(App, Error, "Application: failed to create swapchain");
        return false;
    }

    m_cmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
    if (!m_cmdList) {
        UZ_LOG(App, Error, "Application: failed to create command list");
        return false;
    }

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        m_frameFences[i] = m_device->CreateFence();
        if (!m_frameFences[i]) {
            UZ_LOG(App, Error, "Application: failed to create frame fence {}", i);
            return false;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// InitVFS
// ─────────────────────────────────────────────────────────────────────────────

void Application::InitVFS() {
    const auto exeDir = uz::GetExeDir();
    // Pak files are in build/data/ (exe is in build/bin/), so go up one level.
    const auto indexPath = (exeDir / ".." / "data" / "game.uzpakindex").string();

    // Pak mode: auto-detect data/game.uzpakindex (unless forced loose)
    if (!m_config.forceLooseVFS && std::filesystem::exists(indexPath)) {
        const size_t cacheSizeBytes = m_config.pakCacheMB * size_t{1024} * 1024;
        constexpr uint64_t kPakSecret = 0; // No scramble by default; match the packing secret
        m_vfs = PakVFS::FromIndexFile(indexPath, kPakSecret, cacheSizeBytes);
        if (m_vfs) {
            UZ_LOG(App, Info, "VFS: pak mode ({})", indexPath);
            return;
        }
        UZ_LOG(App, Warning, "VFS: pak index load failed, falling back to loose files");
    }

    // Loose-file mode: use configured root or default <exeDir>/assets/ (dev mode).
    const std::string root = m_config.vfsRoot.empty()
        ? (exeDir / "assets").string()
        : m_config.vfsRoot;

    m_vfs = std::make_unique<LooseFileVFS>(root);
    UZ_LOG(App, Info, "VFS: loose file mode ({})", root);
}

// ─────────────────────────────────────────────────────────────────────────────
// InitWorld
// ─────────────────────────────────────────────────────────────────────────────

void Application::InitWorld() {
    m_world = std::make_unique<World>();
}

// ─────────────────────────────────────────────────────────────────────────────
// HandleResize
// ─────────────────────────────────────────────────────────────────────────────

void Application::HandleResize() {
    m_device->WaitIdle();

    m_width  = static_cast<uint32_t>(m_window->GetWidth());
    m_height = static_cast<uint32_t>(m_window->GetHeight());

    m_swapChain->Resize(m_width, m_height);
    m_window->ClearResizedFlag();

    if (m_renderer) {
        m_renderer->Resize(m_width, m_height);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FlushPendingSceneLoad
// ─────────────────────────────────────────────────────────────────────────────

void Application::FlushPendingSceneLoad() {
    if (m_pendingScenePath.empty()) return;

    const std::string path = std::move(m_pendingScenePath);
    m_pendingScenePath.clear();

    UZ_LOG(App, Info, "Application: loading scene '{}'", path);

    if (!m_vfs) {
        UZ_LOG(App, Warning, "Application: cannot load scene — VFS not initialized");
        return;
    }

    const auto bytes = m_vfs->Read(path);
    if (bytes.empty()) {
        UZ_LOG(App, Error, "Application: scene file not found: '{}'", path);
        return;
    }

    m_world = std::make_unique<World>();
    const size_t count = uz::LoadScene(*m_world, bytes, path);
    UZ_LOG(App, Info, "Application: scene '{}' loaded ({} entities)", path, count);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShutdownPlatform
// ─────────────────────────────────────────────────────────────────────────────

void Application::ShutdownPlatform() {
    if (m_device) {
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            if (m_frameFences[i]) {
                m_device->DestroyFence(m_frameFences[i]);
                m_frameFences[i] = nullptr;
            }
        }
        if (m_cmdList) {
            m_device->DestroyCommandList(m_cmdList);
            m_cmdList = nullptr;
        }
        if (m_swapChain) {
            m_device->DestroySwapChain(m_swapChain);
            m_swapChain = nullptr;
        }
        m_device.reset();
    }
    m_window.reset();
}

} // namespace uz
