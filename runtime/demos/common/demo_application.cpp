/// DemoApplication — Run loop, lifecycle, accessors, and debug view keyboard.
///
/// Platform init and engine infrastructure are in demo_application_init.cpp.
/// CLI argument parsing is in demo_application_args.cpp.
/// See demo_application.h for full documentation.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "demo_application.h"
#include "uzengine.h"
#include "compare_grid.h"
#include "cvar.h"
#include "pixel_debug.h"
#include "inspector_pass.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// LogApp is defined in engine/app/application.cpp — just declare extern here.
UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

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
        UploadContext upload(m_device.get());
        if (m_statsOverlay && !m_statsOverlay->Init(m_device.get(), m_cmdList, upload,
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

    // ── Apply CLI CVar overrides ─────────────────────────────────────────
    // CVars are registered during SceneRenderer::Init() (called from OnInit()).
    // Now that registration is complete, apply any --cvar overrides from the CLI
    // so they take effect on the first ApplyCVars() poll in the render loop.
    if (!m_config.cvarOverrides.empty()) {
        auto& cvars = CVarRegistry::Get();
        for (const auto& [name, value] : m_config.cvarOverrides) {
            cvars.Set(name, value);
            UZ_LOG(App, Info, "CVar override: {} = {}", name, value);
        }
    }

    // ── Diagnostic systems ────────────────────────────────────────────────
    if (m_config.diagMode) {
        m_passInspector = std::make_unique<PassInspector>();
    }

    if (m_config.pixelDebugEnabled) {
        m_pixelDebug = std::make_unique<PixelDebugSystem>();
        PixelDebugConfig pdConfig;
        pdConfig.pixelX     = m_config.pixelDebugX;
        pdConfig.pixelY     = m_config.pixelDebugY;
        pdConfig.maxRecords = 256;
        pdConfig.enabled    = true;
        if (!m_pixelDebug->Init(m_device.get(), pdConfig)) {
            UZ_LOG(App, Error, "PixelDebug initialization failed");
            m_pixelDebug.reset();
        }
    }

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

    // ── Pixel debug output (JSON to stdout) ─────────────────────────────
    if (m_pixelDebug && m_pixelDebug->IsEnabled()) {
        auto records = m_pixelDebug->ReadResults(m_device.get());
        std::string json = PixelDebugSystem::ToJSON(
            records, m_config.pixelDebugX, m_config.pixelDebugY, m_frameIndex);
        std::printf("PIXEL_DEBUG_JSON:%s\n", json.c_str());
        std::fflush(stdout);
    }

    // ── Diagnostic report (JSON to stdout) ──────────────────────────────
    if (m_config.diagMode && m_passInspector) {
        auto report = m_passInspector->GenerateReport(
            m_device.get(), m_cmdList, m_frameIndex);
        std::printf("DIAG_JSON:%s\n", report.ToJSON().c_str());
        std::fflush(stdout);
    }

    // ── Diagnostic cleanup ──────────────────────────────────────────────
    if (m_pixelDebug) {
        m_pixelDebug->Shutdown(m_device.get());
        m_pixelDebug.reset();
    }
    m_passInspector.reset();

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
FlyCameraController& DemoApplication::GetCameraController() { return *m_cameraCtrl; }

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

} // namespace uz
