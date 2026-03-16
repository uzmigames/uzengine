/// uz::Application — Top-level engine lifecycle implementation.
///
/// Drives the full sequence: window → device → renderer → OnStart → frame loop
/// → OnStop → shutdown. See application.h for the public interface.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "application.h"
#include "uzengine.h"
#include "scene_renderer.h"
#include "compare_grid.h"
#include "scene_loader.h"
#include "world.h"
#include "renderdoc_capture.h"


#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

UZ_DEFINE_LOG_CATEGORY(App, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

Application::Application() = default;

Application::~Application() {
    ShutdownPlatform();
}

// ─────────────────────────────────────────────────────────────────────────────
// Run — Main lifecycle
// ─────────────────────────────────────────────────────────────────────────────

int Application::Run(int argc, char** argv) {
    // ── 0. Logging ────────────────────────────────────────────────────────
    LogInit();

    // ── 1. Configuration ──────────────────────────────────────────────────
    m_config = Configure();
    ParseArgs(argc, argv);

    // ── 2a. RenderDoc — must be initialised BEFORE device creation so that
    //        its API hooks are in place when D3D12/Vulkan is initialised.
    if (RenderDocInit()) {
        RenderDocSetCaptureOptions();
        UZ_LOG(App, Info, "RenderDoc: connected={}", RenderDocIsConnected());
    } else {
        UZ_LOG(App, Info, "RenderDoc: not available (launch from RenderDoc GUI to enable)");
    }

    // ── 2. Platform (window, device, swapchain, fences, cmdlist) ─────────
    if (!InitPlatform()) {
        UZ_LOG(App, Error, "Application: platform initialization failed");
        ShutdownPlatform();
        return 1;
    }

    m_width  = static_cast<uint32_t>(m_window->GetWidth());
    m_height = static_cast<uint32_t>(m_window->GetHeight());

    // ── 3. Asset system ───────────────────────────────────────────────────
    InitVFS();
    InitWorld();

    // ── 4. SceneRenderer ─────────────────────────────────────────────────
    m_renderer = std::make_unique<SceneRenderer>();
    if (!m_renderer->Init(m_device, m_window, m_width, m_height)) {
        UZ_LOG(App, Error, "Application: SceneRenderer initialization failed");
        m_renderer.reset();
        m_world.reset();
        m_vfs.reset();
        ShutdownPlatform();
        return 1;
    }

    m_renderer->SetRenderPath(m_config.renderPath);
    m_renderer->SetDebugViewMode(m_config.debugViewMode);
    if (m_shadowDebugMode > 0) {
        m_renderer->SetShadowDebugMode(m_shadowDebugMode);
        UZ_LOG(App, Info, "Shadow debug mode set to {}", m_shadowDebugMode);
    }

    // Apply camera defaults from config
    {
        Camera& cam  = m_renderer->GetCamera();
        FPSCameraController& ctrl = m_renderer->GetCameraController();

        const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        cam.SetHorizontalFOV(90.f, aspect, 10.f, 100000.f);
        cam.SetPosition({m_config.cameraPositionX, m_config.cameraPositionY,
                         m_config.cameraPositionZ});

        constexpr float kDegToRad = 3.14159265f / 180.f;
        const float yawRad   = m_config.cameraYawDeg * kDegToRad;
        const float pitchRad = m_config.cameraPitchDeg * kDegToRad;
        const float cosPitch = std::cos(pitchRad);
        const Vec3 forward   = {
            std::cos(yawRad) * cosPitch,
            std::sin(yawRad) * cosPitch,
            std::sin(pitchRad)
        };
        cam.LookAt(cam.GetPosition() + forward, Vec3::Up());
        ctrl.InitFromCamera(cam);
        ctrl.SetMoveSpeed(m_config.cameraMoveSpeed);
    }

    // Clear spurious WM_SIZE fired on first ShowWindow
    m_window->ClearResizedFlag();

    // ── 5. Game OnStart ───────────────────────────────────────────────────
    OnStart();

    UZ_LOG(App, Info, "Application '{}': entering render loop (maxFrames={})",
           m_config.title, m_config.maxFrames == 0 ? -1 : static_cast<int>(m_config.maxFrames));

    // ── 6. All-modes capture setup ────────────────────────────────────────
    m_allModesActive    = !m_config.allModesOutputPath.empty();
    m_allModeCurrentIdx = 0;
    m_allModeCaptureFrame = UINT32_MAX;
    const uint32_t allModeCount = static_cast<uint32_t>(DebugViewMode::Count);
    std::vector<std::string> allModeTempPaths;
    if (m_allModesActive) {
        allModeTempPaths.reserve(allModeCount);
        m_renderer->SetDebugViewMode(static_cast<DebugViewMode>(0));
    }

    // ── 7. Screenshot capture ──────────────────────────────────────────────
    // Screenshots are captured inside SceneRenderer::RenderFrame() via
    // RequestScreenshot() — no separate ScreenshotCapture needed here.

    auto loopStart = std::chrono::high_resolution_clock::now();
    auto prevFrame = loopStart;

    // ─────────────────────────────────────────────────────────────────────
    // FRAME LOOP
    // ─────────────────────────────────────────────────────────────────────
    while (!m_quitRequested && m_window->PumpMessages() && m_window->IsOpen()) {
        if (m_window->IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!m_allModesActive && m_config.maxFrames > 0 && m_frameIndex >= m_config.maxFrames) {
            break;
        }

        // ── All-modes sequencer ─────────────────────────────────────────
        if (m_allModesActive) {
            if (m_allModeCaptureFrame == UINT32_MAX) {
                m_allModeCaptureFrame = m_frameIndex + m_config.screenshotFrame;
            }

            if (m_frameIndex == m_allModeCaptureFrame) {
                const auto mode = static_cast<DebugViewMode>(m_allModeCurrentIdx);
                const std::string tempPath = "__allmode_" + std::to_string(m_allModeCurrentIdx)
                                           + "_" + DebugViewModeName(mode) + ".png";
                m_config.screenshotPath = tempPath;
                m_screenshotPending = true;
                UZ_LOG(App, Info, "All-modes: capturing mode {} '{}' -> {}",
                       m_allModeCurrentIdx, DebugViewModeName(mode), tempPath);
            }

            if (!m_screenshotPending && m_allModeCaptureFrame != UINT32_MAX
                && m_frameIndex > m_allModeCaptureFrame) {
                const std::string savedPath = "__allmode_" + std::to_string(m_allModeCurrentIdx)
                    + "_" + DebugViewModeName(static_cast<DebugViewMode>(m_allModeCurrentIdx)) + ".png";
                allModeTempPaths.push_back(savedPath);

                m_allModeCurrentIdx++;
                if (m_allModeCurrentIdx >= allModeCount) break;

                m_renderer->SetDebugViewMode(static_cast<DebugViewMode>(m_allModeCurrentIdx));
                m_allModeCaptureFrame = m_frameIndex + m_config.screenshotFrame;
                m_screenshotPending = false;
            }
        }

        // ── Resize ────────────────────────────────────────────────────────
        if (m_window->WasResized()) {
            HandleResize();
        }

        // ── Deferred scene load ───────────────────────────────────────────
        FlushPendingSceneLoad();

        // ── Delta time ────────────────────────────────────────────────────
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prevFrame).count();
        prevFrame = now;
        if (dt > kMaxDeltaTime) dt = kMaxDeltaTime;

        // ── Fence wait ────────────────────────────────────────────────────
        const uint32_t allocFrame = m_frameIndex % kFramesInFlight;
        if (m_fenceValues[allocFrame] > 0) {
            m_frameFences[allocFrame]->WaitCPU(m_fenceValues[allocFrame]);
        }

        // (Screenshot save is handled inside SceneRenderer::RenderFrame after GPU idle)

        // ── Game update ───────────────────────────────────────────────────
        OnUpdate(dt);

        // ── RenderDoc: F12 single-frame capture ───────────────────────────
        // Checked via raw Win32 GetAsyncKeyState so we have no dependency on
        // the higher-level InputSystem (which may not be initialised yet in
        // some code paths, and F12 is a debug-only binding).
#ifdef _WIN32
        if (RenderDocIsLoaded()) {
            // VK_F12 = 0x7B. GetAsyncKeyState returns negative when pressed.
            static bool s_f12WasDown = false;
            const bool  f12Down      = (GetAsyncKeyState(0x7B) & 0x8000) != 0;
            // Edge-detect: only trigger on the press edge, not while held.
            if (f12Down && !s_f12WasDown) {
                UZ_LOG(App, Info,
                       "F12 pressed — triggering RenderDoc capture on frame {}",
                       m_frameIndex);
                RenderDocTriggerCapture();
            }
            s_f12WasDown = f12Down;
        }
#endif

        // ── RenderDoc: auto-capture on the requested frame ────────────────
        if (m_renderdocCaptureFrame != UINT32_MAX
            && m_frameIndex == m_renderdocCaptureFrame
            && RenderDocIsLoaded()) {
            UZ_LOG(App, Info,
                   "Auto-triggering RenderDoc capture on frame {} (--renderdoc-capture)",
                   m_frameIndex);
            RenderDocTriggerCapture();
        }

        // ── Request screenshot BEFORE RenderFrame so it captures THIS frame ──
        if (m_screenshotPending && m_frameIndex >= m_config.screenshotFrame) {
            if (m_screenshotBase64) {
                m_renderer->RequestScreenshotBase64();
            } else {
                m_renderer->RequestScreenshot(m_config.screenshotPath);
            }
            m_screenshotPending = false;
        }

        // ── Render frame (SceneRenderer drives Begin/End/Execute) ─────────
        m_renderer->RenderFrame(m_cmdList, m_swapChain,
                                m_world.get(), m_vfs.get(),
                                dt, m_frameIndex);

        // ── Present ───────────────────────────────────────────────────────
        m_swapChain->Present(m_config.vsync);

        // ── Signal fence ──────────────────────────────────────────────────
        m_globalFenceValue++;
        m_fenceValues[allocFrame] = m_globalFenceValue;
        m_frameFences[allocFrame]->Signal(m_globalFenceValue);

        m_frameIndex++;
    }

    // ── All-modes: compose grid ───────────────────────────────────────────
    if (m_allModesActive && !allModeTempPaths.empty()) {
        CompareGrid grid;
        grid.SetCellSize(384, 384);
        grid.SetColumns(4);
        for (uint32_t m = 0; m < static_cast<uint32_t>(allModeTempPaths.size()); ++m) {
            grid.AddCellFromPNG(allModeTempPaths[m],
                                DebugViewModeName(static_cast<DebugViewMode>(m)), "");
        }
        (void)grid.SaveToPNG(m_config.allModesOutputPath);
        UZ_LOG(App, Info, "All-modes grid saved to '{}'", m_config.allModesOutputPath);
        for (const auto& path : allModeTempPaths) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }

    // ── Shutdown ──────────────────────────────────────────────────────────
    m_device->WaitIdle();

    const auto loopEnd  = std::chrono::high_resolution_clock::now();
    const float totalSec = std::chrono::duration<float>(loopEnd - loopStart).count();
    const float avgFps   = m_frameIndex > 0 ? static_cast<float>(m_frameIndex) / totalSec : 0.f;
    UZ_LOG(App, Info, "Application '{}': {} frames in {:.1f}s ({:.1f} avg FPS)",
           m_config.title, m_frameIndex, totalSec, avgFps);

    OnStop();

    m_renderer->Shutdown();
    m_renderer.reset();
    m_world.reset();
    m_vfs.reset();
    ShutdownPlatform();

    RenderDocShutdown();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

World& Application::GetWorld() {
    UZ_CHECK(m_world != nullptr);
    return *m_world;
}

IVFS& Application::GetVFS() {
    UZ_CHECK(m_vfs != nullptr);
    return *m_vfs;
}

Camera& Application::GetCamera() {
    UZ_CHECK(m_renderer != nullptr);
    return m_renderer->GetCamera();
}

InputSystem& Application::GetInput() {
    UZ_CHECK(m_renderer != nullptr);
    InputSystem* input = m_renderer->GetInput();
    UZ_CHECK(input != nullptr);
    return *input;
}

SceneRenderer& Application::GetRenderer() {
    UZ_CHECK(m_renderer != nullptr);
    return *m_renderer;
}

// ─────────────────────────────────────────────────────────────────────────────
// Control
// ─────────────────────────────────────────────────────────────────────────────

void Application::Quit() {
    m_quitRequested = true;
}

void Application::LoadScene(std::string_view path) {
    m_pendingScenePath = std::string(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// ParseArgs — Parses common CLI flags into m_config
// ─────────────────────────────────────────────────────────────────────────────

namespace {

bool ParseBackendArg(const char* str, RHIBackend& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "dx12")   { out = RHIBackend::DX12;   return true; }
    if (s == "dx11")   { out = RHIBackend::DX11;   return true; }
    if (s == "vulkan") { out = RHIBackend::Vulkan;  return true; }
    return false;
}

bool ParseShaderModelArg(const char* str, ShaderModel& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s.rfind("sm", 0) == 0) s = s.substr(2);
    std::replace(s.begin(), s.end(), '_', '.');
    if (s == "5" || s == "5.0")  { out = ShaderModel::SM5_0; return true; }
    if (s == "5.1")              { out = ShaderModel::SM5_1; return true; }
    if (s == "6" || s == "6.0")  { out = ShaderModel::SM6_0; return true; }
    if (s == "6.5")              { out = ShaderModel::SM6_5; return true; }
    if (s == "6.6")              { out = ShaderModel::SM6_6; return true; }
    return false;
}

} // anonymous namespace

void Application::ParseArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            RHIBackend backend{};
            if (ParseBackendArg(argv[++i], backend)) m_config.backend = backend;
        } else if (std::strcmp(argv[i], "--sm") == 0 && i + 1 < argc) {
            ShaderModel sm{};
            if (ParseShaderModelArg(argv[++i], sm)) m_config.shaderModel = sm;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            m_config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            m_config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            m_config.maxFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--view-mode") == 0 && i + 1 < argc) {
            m_config.debugViewMode = DebugViewModeFromString(argv[++i]);
        } else if (std::strcmp(argv[i], "--all-modes") == 0 && i + 1 < argc) {
            m_config.allModesOutputPath = argv[++i];
        } else if (std::strcmp(argv[i], "--shadow-debug") == 0 && i + 1 < argc) {
            m_shadowDebugMode = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--renderdoc-capture") == 0 && i + 1 < argc) {
            // --renderdoc-capture <frame_number>
            // Automatically triggers a RenderDoc capture on the specified frame.
            // Useful for headless CI pipelines or reproducible captures.
            const int frameNum = std::atoi(argv[++i]);
            if (frameNum >= 0) {
                m_renderdocCaptureFrame = static_cast<uint32_t>(frameNum);
            } else {
                UZ_LOG(App, Warning,
                       "--renderdoc-capture requires a non-negative frame number, got '{}'",
                       argv[i]);
            }
        } else if (std::strcmp(argv[i], "--screenshot-stdout") == 0) {
            m_screenshotBase64 = true;
        }
    }

    if (!m_config.screenshotPath.empty() && m_config.maxFrames == 0) {
        m_config.screenshotFrame = std::max(m_config.screenshotFrame, 400u);
        m_config.maxFrames = m_config.screenshotFrame + 2;
    }

    // --screenshot-stdout uses the same frame/maxFrames logic as --screenshot
    if (m_screenshotBase64 && m_config.maxFrames == 0) {
        m_config.screenshotFrame = std::max(m_config.screenshotFrame, 5u);
        m_config.maxFrames = m_config.screenshotFrame + 2;
    }

    // Suppress console logging when stdout is reserved for base64 output.
    // Logs still go to the file sink (uzengine.log).
    if (m_screenshotBase64) {
        LogSuppressConsole();
    }

    m_screenshotPending = !m_config.screenshotPath.empty() || m_screenshotBase64;
}

// ─────────────────────────────────────────────────────────────────────────────
// InitPlatform
// ─────────────────────────────────────────────────────────────────────────────

bool Application::InitPlatform() {
    m_window = new PlatformWindow();

    WindowDesc winDesc{};
    winDesc.title  = m_config.title;
    winDesc.width  = static_cast<int32_t>(m_config.width);
    winDesc.height = static_cast<int32_t>(m_config.height);

    if (!m_window->Create(winDesc)) {
        UZ_LOG(App, Error, "Application: failed to create window: '{}' {}x{}",
               m_config.title, m_config.width, m_config.height);
        return false;
    }
    m_window->Show();

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
    const std::string root = m_config.vfsRoot.empty()
        ? (uz::GetExeDir() / "assets").string()
        : m_config.vfsRoot;

    m_vfs = std::make_unique<LooseFileVFS>(root);
    UZ_LOG(App, Info, "Application: VFS root='{}'", root);
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
        DestroyRHIDevice(m_device);
        m_device = nullptr;
    }
    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Static CLI Utilities — compose grid and diff image modes
// ─────────────────────────────────────────────────────────────────────────────

int Application::TryComposeGrid(int argc, char* argv[]) {
    int gridIdx = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--compose-grid") == 0) {
            gridIdx = i;
            break;
        }
    }
    if (gridIdx < 0) return -1;

    if (gridIdx + 1 >= argc) {
        UZ_LOG(App, Error, "--compose-grid requires output path");
        return 1;
    }
    const std::string outputPath = argv[gridIdx + 1];
    int nextArg = gridIdx + 2;

    CompareGrid grid;

    if (nextArg + 1 < argc && std::strcmp(argv[nextArg], "--cell-size") == 0) {
        const uint32_t sz = static_cast<uint32_t>(std::atoi(argv[nextArg + 1]));
        if (sz > 0) grid.SetCellSize(sz, sz);
        nextArg += 2;
    }

    if (nextArg + 1 < argc && std::strcmp(argv[nextArg], "--label-height") == 0) {
        const uint32_t h = static_cast<uint32_t>(std::atoi(argv[nextArg + 1]));
        if (h > 0) grid.SetLabelHeight(h);
        nextArg += 2;
    }

    while (nextArg + 2 < argc) {
        const std::string path     = argv[nextArg];
        const std::string label    = argv[nextArg + 1];
        const std::string sublabel = argv[nextArg + 2];
        if (!grid.AddCellFromPNG(path, label, sublabel)) {
            UZ_LOG(App, Error, "Failed to load grid cell: path='{}'", path);
            return 1;
        }
        nextArg += 3;
    }

    if (grid.GetCellCount() == 0) {
        UZ_LOG(App, Error, "--compose-grid: no input images specified");
        return 1;
    }

    if (!grid.SaveToPNG(outputPath)) {
        UZ_LOG(App, Error, "Failed to save grid: path='{}'", outputPath);
        return 1;
    }

    UZ_LOG(App, Info, "Compare grid saved: path='{}' ({} cells)", outputPath, grid.GetCellCount());
    return 0;
}

int Application::TryDiffImages(int argc, char* argv[]) {
    int diffIdx = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--diff") == 0) {
            diffIdx = i;
            break;
        }
    }
    if (diffIdx < 0) return -1;

    if (diffIdx + 5 >= argc) {
        UZ_LOG(App, Error, "--diff requires: <output.png> <imgA.png> <labelA> <imgB.png> <labelB>");
        return 1;
    }

    const std::string output = argv[diffIdx + 1];
    const std::string pathA  = argv[diffIdx + 2];
    const std::string labelA = argv[diffIdx + 3];
    const std::string pathB  = argv[diffIdx + 4];
    const std::string labelB = argv[diffIdx + 5];

    uint32_t cellSize = 384;
    const int optIdx = diffIdx + 6;
    if (optIdx + 1 < argc && std::strcmp(argv[optIdx], "--cell-size") == 0) {
        const int sz = std::atoi(argv[optIdx + 1]);
        if (sz > 0) cellSize = static_cast<uint32_t>(sz);
    }

    if (!ImageDiff::DiffToPNG(pathA, labelA, pathB, labelB, output, cellSize)) {
        UZ_LOG(App, Error, "Failed to generate diff: output='{}'", output);
        return 1;
    }

    return 0;
}

} // namespace uz
