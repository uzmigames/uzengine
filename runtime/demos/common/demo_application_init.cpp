/// DemoApplication — Platform and engine infrastructure initialization.
///
/// Contains: InitPlatform(), ShutdownPlatform(), InitInfrastructure(),
/// ShutdownInfrastructure().

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "demo_application.h"
#include "units.h"
#include "uzengine.h"

#include <cmath>
#include <cstdlib>
#include <string>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

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
    if (!m_shaderMgr->Init(m_device.get())) {
        UZ_LOG(App, Error, "Failed to init shader manager");
    }

    // ── Pipeline Library ────────────────────────────────────────────────
    m_pipelines = std::make_unique<PipelineLibrary>();
    if (!m_pipelines->Init(m_device.get(), m_shaderMgr.get())) {
        UZ_LOG(App, Error, "Failed to init pipeline library");
    }

    // ── CB Ring Allocator (3 frames, 2MB) ──────────────────────────────
    m_cbRing = std::make_unique<CBRingAllocator>();
    m_cbRing->Init(m_device.get(), 3, 2 * 1024 * 1024);

    // ── Editor Grid ─────────────────────────────────────────────────────
    m_editorGrid = std::make_unique<EditorGrid>();
    if (!m_editorGrid->Init(m_device.get(), m_shaderMgr->GetPipeline(),
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
        if (!m_screenshotCapture->Init(m_device.get(), m_width, m_height)) {
            UZ_LOG(App, Error, "Failed to initialize screenshot capture");
        }
    }

    // ── Render Graph Resource Pool ──────────────────────────────────────
    m_rgPool = std::make_unique<rg::RgResourcePool>(m_device.get());

    // ── Camera ──────────────────────────────────────────────────────────
    m_camera = std::make_unique<Camera>();
    m_cameraCtrl = std::make_unique<FPSCameraController>();

    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect  = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    m_camera->SetPerspective(vFovRad,
        static_cast<float>(m_width) / static_cast<float>(m_height),
        uz::Units::kDefaultNearPlaneCm, uz::Units::kDefaultFarPlaneCm);

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
    m_device.reset();

    // Window
    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }
}

} // namespace uz
