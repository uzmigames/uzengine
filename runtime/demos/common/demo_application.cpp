/// DemoApplication — Boilerplate extraction for all engine demos.
///
/// Centralizes window creation, RHI device init, swapchain, command list,
/// per-frame fence sync, resize handling, minimize sleep, frame timing,
/// AND common engine infrastructure (input, shaders, pipelines, camera,
/// CB ring, editor grid, stats overlay, screenshot capture, RG pool).
/// See demo_application.h for full documentation.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "demo_application.h"
#include "uzengine.h"
#include "compare_grid.h"

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
// GetScreenshotsDir — Platform-aware screenshots output directory
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the user-scoped directory where all engine screenshots are saved.
///
/// - Windows : `%APPDATA%\UzEngine\.screenshots\`
/// - Linux   : `~/.local/share/UzEngine/.screenshots/`
/// - macOS   : `~/Library/Application Support/UzEngine/.screenshots/`
///
/// The directory is created if it does not exist.
/// Falls back to `.screenshots/` relative to CWD on any error.
static std::filesystem::path GetScreenshotsDir() {
    std::filesystem::path base;

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        base = std::filesystem::path(appdata) / "UzEngine" / ".screenshots";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / "Library" / "Application Support"
             / "UzEngine" / ".screenshots";
    }
#else // Linux / other POSIX
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = std::filesystem::path(xdg) / "UzEngine" / ".screenshots";
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / ".local" / "share"
             / "UzEngine" / ".screenshots";
    }
#endif

    if (base.empty()) {
        base = std::filesystem::path(".screenshots");
    }

    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base;
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

DemoApplication::DemoApplication(const DemoConfig& config)
    : m_config(config)
    , m_debugViewMode(config.debugViewMode) {}

DemoApplication::DemoApplication(const char* title, uint32_t width, uint32_t height)
    : m_config{} {
    m_config.title = title;
    m_config.width = width;
    m_config.height = height;
}

DemoApplication::~DemoApplication() {
    // ShutdownPlatform is called in Run(), but guard against early destruction
    ShutdownInfrastructure();
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

    // Initialize engine infrastructure before subclass OnInit()
    InitInfrastructure();

    // Open command list so OnInit() can upload meshes/textures immediately
    m_cmdList->Begin();

    OnInit();

    // StatsOverlay init needs cmdList in recording state — it was left recording
    // by OnInit() (UploadMesh/TextureLoader leave it recording).
    // After init, flush all uploads.
    {
        UploadContext upload(m_device);
        if (m_statsOverlay && !m_statsOverlay->Init(m_device, m_cmdList, upload,
                m_shaderMgr->GetPipeline(), RHIFormat::RGBA8Unorm,
                m_width, m_height, m_device->IsBindless())) {
            UZ_LOG(App, Error, "Failed to initialize stats overlay");
        }
        m_cmdList->End();
        m_cmdList->Execute();
        m_device->WaitIdle();
        upload.ReleaseStaging();
    }

    // Clear spurious resize flag — WM_SIZE fires on first ShowWindow()
    m_window->ClearResizedFlag();

    UZ_LOG(App, Info, "DemoApplication '{}': entering render loop (maxFrames={}, screenshotPending={}, ready={}, readyFrame={}, screenshotFrame={})",
           m_config.title, m_config.maxFrames == 0 ? -1 : static_cast<int>(m_config.maxFrames),
           m_screenshotPending, m_ready, m_readyFrame, m_config.screenshotFrame);

    // ── All-modes sequencing state ──────────────────────────────────────
    // When allModesOutputPath is set, we iterate every DebugViewMode in a
    // single run: after SetReady(), switch mode, wait screenshotFrame frames,
    // capture, then advance — no window reopen needed.
    const bool allModesActive = !m_config.allModesOutputPath.empty();
    const uint32_t allModeCount = static_cast<uint32_t>(DebugViewMode::Count);
    uint32_t allModeCurrentIdx = 0;           // which mode we're currently capturing
    uint32_t allModeCaptureFrame = UINT32_MAX; // absolute frame to capture this mode
    std::vector<std::string> allModeTempPaths; // collected temp PNG paths
    if (allModesActive) {
        allModeTempPaths.reserve(allModeCount);
        // Start with mode 0; set capture frame after ready
        m_debugViewMode = static_cast<DebugViewMode>(0);
    }

    auto loopStart = std::chrono::high_resolution_clock::now();
    auto prevFrame = loopStart;

    while (m_window->PumpMessages() && m_window->IsOpen()) {
        // Skip rendering while minimized — swapchain surface is 0x0
        if (m_window->IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Frame cap check (skip in all-modes — we exit after all modes captured)
        if (!allModesActive && m_config.maxFrames > 0 && m_frameIndex >= m_config.maxFrames) {
            break;
        }

        // ── All-modes sequencer ─────────────────────────────────────────
        if (allModesActive && m_ready) {
            // Set capture frame for current mode once ready
            if (allModeCaptureFrame == UINT32_MAX) {
                allModeCaptureFrame = m_frameIndex + m_config.screenshotFrame;
            }

            if (m_frameIndex == allModeCaptureFrame) {
                // Time to capture this mode
                const auto mode = static_cast<DebugViewMode>(allModeCurrentIdx);
                const std::string modeName = DebugViewModeName(mode);
                const std::string tempPath = (GetScreenshotsDir()
                    / ("__allmode_" + std::to_string(allModeCurrentIdx) + "_" + modeName + ".png")).string();

                UZ_LOG(App, Info, "All-modes: capturing mode {} '{}' -> {}", allModeCurrentIdx, modeName, tempPath);

                // Trigger screenshot this frame
                m_config.screenshotPath = tempPath;
                m_screenshotPending = true;
            }

            // After screenshot saved, advance to next mode
            if (!m_screenshotPending && allModeCaptureFrame != UINT32_MAX
                && m_frameIndex > allModeCaptureFrame)
            {
                // Record the path we just captured
                const std::string savedPath = (GetScreenshotsDir()
                    / ("__allmode_" + std::to_string(allModeCurrentIdx)
                       + "_" + DebugViewModeName(static_cast<DebugViewMode>(allModeCurrentIdx)) + ".png")).string();
                allModeTempPaths.push_back(savedPath);

                ++allModeCurrentIdx;
                if (allModeCurrentIdx >= allModeCount) {
                    // All modes captured — compose and exit
                    break;
                }

                // Advance to next mode
                m_debugViewMode = static_cast<DebugViewMode>(allModeCurrentIdx);
                allModeCaptureFrame = m_frameIndex + m_config.screenshotFrame;
                m_screenshotPending = false;
            }
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

            // Update base-class-owned resize-sensitive systems
            if (m_statsOverlay) {
                m_statsOverlay->SetScreenSize(m_width, m_height);
            }
            if (m_camera) {
                m_camera->SetAspectRatio(
                    static_cast<float>(m_width) / static_cast<float>(m_height));
            }

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

        // Save deferred screenshot — GPU has finished the copy commands
        if (m_screenshotSaveDeferred) {
            m_device->WaitIdle();
            m_screenshotCapture->SaveToPNG(m_config.screenshotPath);
            m_screenshotSaveDeferred = false;
            m_screenshotPending = false;
        }

        m_swapChain->Present(m_config.vsync);

        // Signal fence for this frame
        m_globalFenceValue++;
        m_fenceValues[allocFrame] = m_globalFenceValue;
        m_frameFences[allocFrame]->Signal(m_globalFenceValue);

        m_frameIndex++;
    }

    // ── All-modes: compose grid and clean up temp files ─────────────────
    if (allModesActive && !allModeTempPaths.empty()) {
        CompareGrid grid;
        grid.SetCellSize(384, 384);
        grid.SetColumns(4);

        for (uint32_t m = 0; m < static_cast<uint32_t>(allModeTempPaths.size()); ++m) {
            const std::string modeName = DebugViewModeName(static_cast<DebugViewMode>(m));
            if (!grid.AddCellFromPNG(allModeTempPaths[m], modeName, "")) {
                UZ_LOG(App, Error, "All-modes: failed to load temp screenshot '{}'", allModeTempPaths[m]);
            }
        }

        const bool saved = grid.SaveToPNG(m_config.allModesOutputPath);
        UZ_LOG(App, Info, "All-modes grid saved to '{}': {}", m_config.allModesOutputPath, saved ? "OK" : "FAILED");

        for (const auto& path : allModeTempPaths) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
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
    ShutdownInfrastructure();
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
// Engine Infrastructure Accessors
// ─────────────────────────────────────────────────────────────────────────────

InputSystem* DemoApplication::GetInput() const { return m_input.get(); }
ShaderManager* DemoApplication::GetShaderManager() const { return m_shaderMgr.get(); }
PipelineLibrary* DemoApplication::GetPipelines() const { return m_pipelines.get(); }
CBRingAllocator* DemoApplication::GetCBRing() const { return m_cbRing.get(); }
EditorGrid* DemoApplication::GetEditorGrid() const { return m_editorGrid.get(); }
StatsOverlay* DemoApplication::GetStatsOverlay() const { return m_statsOverlay.get(); }
ScreenshotCapture* DemoApplication::GetScreenshotCapture() const { return m_screenshotCapture.get(); }
rg::RgResourcePool* DemoApplication::GetRgPool() const { return m_rgPool.get(); }

Camera& DemoApplication::GetCamera() { return *m_camera; }
FPSCameraController& DemoApplication::GetCameraController() { return *m_cameraCtrl; }

const std::string& DemoApplication::GetScreenshotPath() const { return m_config.screenshotPath; }
uint32_t DemoApplication::GetScreenshotFrame() const { return m_config.screenshotFrame; }

// ─────────────────────────────────────────────────────────────────────────────
// ApplyDebugViewKeyboard — F1-F11 debug view mode selection
// ─────────────────────────────────────────────────────────────────────────────

void DemoApplication::ApplyDebugViewKeyboard(const InputSystem* input) {
    static constexpr struct { Key key; DebugViewMode mode; } kKeys[] = {
        {Key::F1,  DebugViewMode::Lit},
        {Key::F2,  DebugViewMode::Albedo},
        {Key::F3,  DebugViewMode::WorldNormal},
        {Key::F4,  DebugViewMode::Metallic},
        {Key::F5,  DebugViewMode::Roughness},
        {Key::F6,  DebugViewMode::AO},
        {Key::F7,  DebugViewMode::Depth},
        {Key::F8,  DebugViewMode::WorldPosition},
        {Key::F9,  DebugViewMode::Overview},
        {Key::F10, DebugViewMode::Lighting},
        {Key::F11, DebugViewMode::Shadows},
    };
    for (const auto& [key, mode] : kKeys) {
        if (input->IsKeyJustPressed(key)) {
            m_debugViewMode = mode;
            UZ_LOG(App, Info, "Debug view mode: {}", DebugViewModeName(mode));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// InitInfrastructure — Creates engine systems after device is ready
// ─────────────────────────────────────────────────────────────────────────────

void DemoApplication::InitInfrastructure() {
    // ── Input ────────────────────────────────────────────────────────────
    m_input = std::make_unique<InputSystem>();
    m_input->Init();
    m_window->SetInputSystem(m_input.get());

    // ── Shader Manager ──────────────────────────────────────────────────
    m_shaderMgr = std::make_unique<ShaderManager>();
    if (!m_shaderMgr->Init(m_device)) {
        UZ_LOG(App, Error, "Failed to init shader manager");
    }

    // ── Pipeline Library ────────────────────────────────────────────────
    m_pipelines = std::make_unique<PipelineLibrary>();
    if (!m_pipelines->Init(m_device, m_shaderMgr.get())) {
        UZ_LOG(App, Error, "Failed to init pipeline library");
    }

    // ── CB Ring Allocator (3 frames, 2MB) ──────────────────────────────
    m_cbRing = std::make_unique<CBRingAllocator>();
    m_cbRing->Init(m_device, 3, 2 * 1024 * 1024);

    // ── Editor Grid ─────────────────────────────────────────────────────
    m_editorGrid = std::make_unique<EditorGrid>();
    if (!m_editorGrid->Init(m_device, m_shaderMgr->GetPipeline(),
                            RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(App, Error, "Failed to initialize editor grid");
    }

    // ── Stats Overlay (deferred — needs cmdList in recording state) ─────
    // Created here but Init() is called in Run() after OnInit() returns,
    // because StatsOverlay::Init needs the command list in recording state
    // and we want OnInit() to do its uploads first.
    m_statsOverlay = std::make_unique<StatsOverlay>();

    // ── Screenshot Capture ──────────────────────────────────────────────
    // Also needed for --all-modes even when --screenshot is not specified.
    m_screenshotPending = !m_config.screenshotPath.empty();
    if (m_screenshotPending || !m_config.allModesOutputPath.empty()) {
        m_screenshotCapture = std::make_unique<ScreenshotCapture>();
        if (!m_screenshotCapture->Init(m_device, m_width, m_height)) {
            UZ_LOG(App, Error, "Failed to initialize screenshot capture");
        }
    }

    // ── Render Graph Resource Pool ──────────────────────────────────────
    m_rgPool = std::make_unique<rg::RgResourcePool>(m_device);

    // ── Camera ──────────────────────────────────────────────────────────
    m_camera = std::make_unique<Camera>();
    m_cameraCtrl = std::make_unique<FPSCameraController>();

    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect  = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    m_camera->SetPerspective(vFovRad,
        static_cast<float>(m_width) / static_cast<float>(m_height),
        10.f, 100000.f);

    m_camera->SetPosition({m_config.cameraPositionX, m_config.cameraPositionY,
                           m_config.cameraPositionZ});
    {
        constexpr float kDegToRad = 3.14159265f / 180.f;
        float yawRad   = m_config.cameraYawDeg * kDegToRad;
        float pitchRad = m_config.cameraPitchDeg * kDegToRad;
        float cosPitch = std::cos(pitchRad);
        Vec3 forward = {
            std::cos(yawRad) * cosPitch,
            std::sin(yawRad) * cosPitch,
            std::sin(pitchRad)
        };
        Vec3 pos = m_camera->GetPosition();
        m_camera->LookAt(pos + forward, Vec3::Up());
    }

    m_cameraCtrl->InitFromCamera(*m_camera);
    m_cameraCtrl->SetMoveSpeed(m_config.cameraMoveSpeed);

    UZ_LOG(App, Info, "DemoApplication '{}': engine infrastructure initialized", m_config.title);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShutdownInfrastructure — Destroys engine systems in reverse creation order
// ─────────────────────────────────────────────────────────────────────────────

void DemoApplication::ShutdownInfrastructure() {
    // Reverse order of creation
    m_cameraCtrl.reset();
    m_camera.reset();
    m_rgPool.reset();
    if (m_screenshotCapture) { m_screenshotCapture->Shutdown(); m_screenshotCapture.reset(); }
    if (m_statsOverlay) { m_statsOverlay->Shutdown(); m_statsOverlay.reset(); }
    if (m_editorGrid) { m_editorGrid->Shutdown(); m_editorGrid.reset(); }
    if (m_cbRing) { m_cbRing->Shutdown(); m_cbRing.reset(); }
    if (m_pipelines) { m_pipelines->Shutdown(); m_pipelines.reset(); }
    if (m_shaderMgr) { m_shaderMgr->Shutdown(); m_shaderMgr.reset(); }
    m_input.reset();
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
        UZ_LOG(App, Error, "Failed to create window: title='{}', size={}x{}",
               m_config.title, m_config.width, m_config.height);
        return false;
    }
    m_window->Show();

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = m_config.backend.value_or(RHIBackend::DX12);
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = m_config.enableValidation;
#endif

    // CLI --sm takes precedence, then env var fallback
    if (m_config.shaderModel.has_value()) {
        deviceDesc.forceShaderModel = m_config.shaderModel.value();
    } else if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

    UZ_LOG(App, Info, "DemoApplication '{}': backend={}, forceShaderModel={}",
           m_config.title,
           RHIBackendToString(deviceDesc.backend),
           deviceDesc.forceShaderModel.has_value()
               ? ShaderModelToString(deviceDesc.forceShaderModel.value())
               : "auto");

    m_device = CreateRHIDevice(deviceDesc);
    if (!m_device) {
        UZ_LOG(App, Error, "Failed to create RHI device: backend={}, validation={}",
               static_cast<int>(deviceDesc.backend), deviceDesc.enableValidation);
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
        UZ_LOG(App, Error, "Failed to create swapchain: size={}x{}, buffers={}, vsync={}",
               scDesc.width, scDesc.height, scDesc.bufferCount, scDesc.vsync);
        return false;
    }

    // ── Command List ─────────────────────────────────────────────────────
    m_cmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
    if (!m_cmdList) {
        UZ_LOG(App, Error, "Failed to create graphics command list");
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

// ─────────────────────────────────────────────────────────────────────────────
// ParseCommonArgs — Parses common CLI flags into DemoConfig
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Case-insensitive match for backend name.
/// Returns true if recognized.
bool ParseBackendArg(const char* str, RHIBackend& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "dx12")   { out = RHIBackend::DX12;   return true; }
    if (s == "dx11")   { out = RHIBackend::DX11;   return true; }
    if (s == "vulkan") { out = RHIBackend::Vulkan;  return true; }
    return false;
}

/// Case-insensitive match for shader model.
/// Accepts: "5.0", "5.1", "6.0", "6.5", "6.6", etc.
bool ParseShaderModelArg(const char* str, ShaderModel& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Strip optional "sm" prefix
    if (s.rfind("sm", 0) == 0) s = s.substr(2);
    std::replace(s.begin(), s.end(), '_', '.');

    if (s == "5" || s == "5.0")  { out = ShaderModel::SM5_0; return true; }
    if (s == "5.1")              { out = ShaderModel::SM5_1; return true; }
    if (s == "6" || s == "6.0")  { out = ShaderModel::SM6_0; return true; }
    if (s == "6.1")              { out = ShaderModel::SM6_1; return true; }
    if (s == "6.2")              { out = ShaderModel::SM6_2; return true; }
    if (s == "6.3")              { out = ShaderModel::SM6_3; return true; }
    if (s == "6.4")              { out = ShaderModel::SM6_4; return true; }
    if (s == "6.5")              { out = ShaderModel::SM6_5; return true; }
    if (s == "6.6")              { out = ShaderModel::SM6_6; return true; }
    if (s == "6.7")              { out = ShaderModel::SM6_7; return true; }
    if (s == "6.8")              { out = ShaderModel::SM6_8; return true; }
    if (s == "6.9")              { out = ShaderModel::SM6_9; return true; }
    return false;
}

} // anonymous namespace

void DemoApplication::ParseCommonArgs(int argc, char* argv[], DemoConfig& config) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            RHIBackend backend{};
            if (ParseBackendArg(argv[++i], backend)) {
                config.backend = backend;
            } else {
                UZ_LOG(App, Warning, "Unknown --backend '{}', ignoring", argv[i]);
            }
        } else if (std::strcmp(argv[i], "--sm") == 0 && i + 1 < argc) {
            ShaderModel sm{};
            if (ParseShaderModelArg(argv[++i], sm)) {
                config.shaderModel = sm;
            } else {
                UZ_LOG(App, Warning, "Unknown --sm '{}', ignoring", argv[i]);
            }
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            config.maxFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--view-mode") == 0 && i + 1 < argc) {
            config.debugViewMode = DebugViewModeFromString(argv[++i]);
        } else if (std::strcmp(argv[i], "--all-modes") == 0 && i + 1 < argc) {
            config.allModesOutputPath = argv[++i];
        }
    }

    // Route bare filenames (no directory separator) into the user-scoped
    // screenshots directory so output is never mixed with exe/build files.
    auto routeToScreenshotDir = [](std::string& path) {
        if (path.empty()) return;
        const std::filesystem::path p(path);
        if (!p.has_parent_path() || p.parent_path() == ".") {
            path = (GetScreenshotsDir() / p.filename()).string();
        }
    };
    routeToScreenshotDir(config.screenshotPath);
    routeToScreenshotDir(config.allModesOutputPath);

    // Auto-exit after screenshot so CI pipelines don't block
    if (!config.screenshotPath.empty() && config.maxFrames == 0) {
        config.screenshotFrame = std::max(config.screenshotFrame, 5u);
        config.maxFrames = config.screenshotFrame + 2;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TryComposeGrid — Static utility for --compose-grid mode
// ─────────────────────────────────────────────────────────────────────────────

int DemoApplication::TryComposeGrid(int argc, char* argv[]) {
    // Find --compose-grid flag
    int gridIdx = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--compose-grid") == 0) {
            gridIdx = i;
            break;
        }
    }
    if (gridIdx < 0) return -1; // Not in grid mode

    // Next arg is output path
    if (gridIdx + 1 >= argc) {
        UZ_LOG(App, Error, "--compose-grid requires output path");
        return 1;
    }
    const std::string outputPath = argv[gridIdx + 1];
    int nextArg = gridIdx + 2;

    CompareGrid grid;

    // Optional --cell-size <W> (square cells)
    if (nextArg + 1 < argc && std::strcmp(argv[nextArg], "--cell-size") == 0) {
        const uint32_t sz = static_cast<uint32_t>(std::atoi(argv[nextArg + 1]));
        if (sz > 0) grid.SetCellSize(sz, sz);
        nextArg += 2;
    }

    // Optional --label-height <H>
    if (nextArg + 1 < argc && std::strcmp(argv[nextArg], "--label-height") == 0) {
        const uint32_t h = static_cast<uint32_t>(std::atoi(argv[nextArg + 1]));
        if (h > 0) grid.SetLabelHeight(h);
        nextArg += 2;
    }

    // Remaining args: triplets of <input.png> <label> <sublabel>
    while (nextArg + 2 < argc) {
        const std::string path = argv[nextArg];
        const std::string label = argv[nextArg + 1];
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

// ─────────────────────────────────────────────────────────────────────────────
// TryDiffImages — Static utility for --diff mode
// ─────────────────────────────────────────────────────────────────────────────

int DemoApplication::TryDiffImages(int argc, char* argv[]) {
    int diffIdx = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--diff") == 0) {
            diffIdx = i;
            break;
        }
    }
    if (diffIdx < 0) return -1;

    // --diff <output> <imgA> <labelA> <imgB> <labelB> [--cell-size N]
    if (diffIdx + 5 >= argc) {
        UZ_LOG(App, Error, "--diff requires: <output.png> <imgA.png> <labelA> <imgB.png> <labelB>");
        return 1;
    }

    const std::string output = argv[diffIdx + 1];
    const std::string pathA = argv[diffIdx + 2];
    const std::string labelA = argv[diffIdx + 3];
    const std::string pathB = argv[diffIdx + 4];
    const std::string labelB = argv[diffIdx + 5];

    uint32_t cellSize = 384;
    // Check for optional --cell-size after the 5 required args
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
