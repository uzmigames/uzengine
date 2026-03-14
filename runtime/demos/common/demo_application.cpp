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

    // ── CB Ring Allocator (3 frames, 128KB) ─────────────────────────────
    m_cbRing = std::make_unique<CBRingAllocator>();
    m_cbRing->Init(m_device, 3, 128 * 1024);

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
    m_screenshotPending = !m_config.screenshotPath.empty();
    if (m_screenshotPending) {
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
        0.1f, 1000.f);

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

} // namespace uz
