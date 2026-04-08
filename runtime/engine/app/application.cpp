/// uz::Application — Top-level engine lifecycle implementation.
///
/// Drives the full sequence: window -> device -> renderer -> OnStart -> frame loop
/// -> OnStop -> shutdown. See application.h for the public interface.
///
/// Platform init/shutdown lives in application_platform.cpp.
/// CLI argument parsing lives in application_args.cpp.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "application.h"
#include "pixel_debug.h"
#include "crash_handler.h"
#include "units.h"
#include "uzengine.h"
#include "scene_renderer.h"
#include "compare_grid.h"
#include "world.h"
#include "cvar.h"
#include "flip_pass.h"
#include "texture_loader.h"
#include "shader_manager.h"
#include "png_codec.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <vector>

UZ_DEFINE_LOG_CATEGORY(App, Trace);

// ─── System CVars ────────────────────────────────────────────────────────────

static auto* cv_MaxDeltaTime   = UZ_CVAR_FLOAT("sys.MaxDeltaTime", 0.25f, "Max delta time clamp — prevents spiral-of-death after hitches", 0.016f, 1.0f);
static auto* cv_FixedDeltaTime = UZ_CVAR_FLOAT("sys.FixedDeltaTime", 1.0f / 60.0f, "Fixed timestep for physics/deterministic logic", 0.002f, 0.1f);
static auto* cv_FramesInFlight = UZ_CVAR_INT("rhi.FramesInFlight", 3, "GPU frames in flight (init-time only)", 1, 5);
static auto* cv_VSync          = UZ_CVAR_BOOL("rhi.VSync", false, "Vertical sync");
static auto* cv_CameraMoveSpeed = UZ_CVAR_FLOAT("camera.MoveSpeed", 1000.0f, "Fly camera movement speed (cm/s)", 100.0f, 10000.0f);

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

    // ── 2. Platform (window, device, swapchain, fences, cmdlist) ─────────
    if (!InitPlatform()) {
        UZ_LOG(App, Error, "Application: platform initialization failed");
        ShutdownPlatform();
        return 1;
    }

    m_width  = static_cast<uint32_t>(m_window->GetWidth());
    m_height = static_cast<uint32_t>(m_window->GetHeight());

    // ── 2b. FLIP comparison mode (early exit — no scene/renderer needed) ──
    if (m_flipMode) {
        int exitCode = RunFlipComparison();
        ShutdownPlatform();
        return exitCode;
    }

    // ── 3. Asset system ───────────────────────────────────────────────────
    InitVFS();
    InitWorld();

    // ── 4. SceneRenderer ─────────────────────────────────────────────────
    m_renderer = std::make_unique<SceneRenderer>();
    if (!m_renderer->Init(m_device.get(), m_window.get(), m_width, m_height)) {
        UZ_LOG(App, Error, "Application: SceneRenderer initialization failed");
        m_renderer.reset();
        m_world.reset();
        m_vfs.reset();
        ShutdownPlatform();
        return 1;
    }

    m_renderer->SetRenderPath(m_config.renderPath);
    m_renderer->SetDebugViewMode(m_config.debugViewMode);
    m_renderer->SetGpuProfileEnabled(m_config.gpuProfile);
    if (m_shadowDebugMode > 0) {
        m_renderer->SetShadowDebugMode(m_shadowDebugMode);
        // Also set the CVar so scene_lifecycle doesn't override it
        CVarRegistry::Get().Set("r.ShadowDebug", static_cast<float>(m_shadowDebugMode));
        UZ_LOG(App, Info, "Shadow debug mode set to {}", m_shadowDebugMode);
    }

    // ── Apply CVar CLI overrides (after RegisterCVars in SceneRenderer::Init) ──
    if (!m_cvarOverrides.empty()) {
        auto& cvars = CVarRegistry::Get();
        for (const auto& [name, value] : m_cvarOverrides) {
            cvars.Set(name, value);
            UZ_LOG(App, Info, "CVar override: {} = {}", name, value);
        }
    }

    // Apply pass inspector settings from CLI
    if (!m_visTarget.empty()) {
        m_renderer->GetPassInspector().SetVisTarget(m_visTarget.c_str());
        UZ_LOG(App, Info, "Pass inspector vis target set to '{}'", m_visTarget);
    }
    if (m_passValidate) {
        m_renderer->GetPassInspector().SetValidationEnabled(true);
        UZ_LOG(App, Info, "Pass inspector validation enabled");
    }
    if (m_passDumpRequested) {
        UZ_LOG(App, Info, "Pass inspector dump requested — will execute when scene is fully loaded");
    }
    if (!m_passToggle.empty()) {
        m_renderer->GetPassInspector().TogglePass(m_passToggle);
        UZ_LOG(App, Info, "Pass inspector toggle: '{}'", m_passToggle);
    }
    if (!m_passCompare.empty()) {
        m_renderer->GetPassInspector().SetComparePass(m_passCompare);
        UZ_LOG(App, Info, "Pass inspector compare: '{}'", m_passCompare);
    }

    // Apply camera defaults from config
    {
        Camera& cam  = m_renderer->GetCamera();
        FlyCameraController& ctrl = m_renderer->GetCameraController();

        const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        cam.SetHorizontalFOV(90.f, aspect, uz::Units::kDefaultNearPlaneCm, uz::Units::kDefaultFarPlaneCm);
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

    // ── 4b. Pixel debug initialization ───────────────────────────────────
    if (m_pixelDebugEnabled) {
        m_pixelDebug = std::make_unique<PixelDebugSystem>();
        PixelDebugConfig pdConfig;
        pdConfig.pixelX     = m_pixelDebugX;
        pdConfig.pixelY     = m_pixelDebugY;
        pdConfig.maxRecords = 256;
        pdConfig.enabled    = true;
        if (!m_pixelDebug->Init(m_device.get(), pdConfig)) {
            UZ_LOG(App, Error, "PixelDebug initialization failed");
            m_pixelDebug.reset();
        }
        // Wire pixel debug system into SceneRenderer so the deferred lighting
        // pass can push UAV descriptor indices as root constants for shader access.
        if (m_pixelDebug && m_renderer) {
            m_renderer->SetPixelDebugSystem(m_pixelDebug.get());
        }
    }

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
        const float maxDt = CVarRegistry::Get().GetFloat("sys.MaxDeltaTime", 0.25f);
        if (dt > maxDt) dt = maxDt;

        // ── Fence wait ────────────────────────────────────────────────────
        const uint32_t allocFrame = m_frameIndex % kFramesInFlight;
        if (m_fenceValues[allocFrame] > 0) {
            m_frameFences[allocFrame]->WaitCPU(m_fenceValues[allocFrame]);
        }

        // (Screenshot save is handled inside SceneRenderer::RenderFrame after GPU idle)

        // ── Game update ───────────────────────────────────────────────────
        OnUpdate(dt);

        // ── Request screenshot only after scene is fully loaded ──────────
        // Never capture before all meshes, textures, and async decodes complete.
        // This prevents black/missing textures in automated screenshots.
        if (m_screenshotPending && m_frameIndex % 50 == 0) {
            (void)m_renderer->IsSceneFullyLoadedDebug();
        }
        if (m_screenshotPending && m_renderer->IsSceneFullyLoaded() && m_frameIndex >= 2) {
            if (m_screenshotBase64) {
                m_renderer->RequestScreenshotBase64();
            } else {
                m_renderer->RequestScreenshot(m_config.screenshotPath);
            }
            m_screenshotPending = false;
            // Exit 2 frames after capture (allow present + readback)
            m_config.maxFrames = m_frameIndex + 3;
        }

        // ── Render frame (SceneRenderer drives Begin/End/Execute) ─────────
        // Update crash context before rendering so the crash handler knows
        // which frame we're on if a SEGFAULT occurs.
        CrashHandler_SetFrameNumber(m_frameIndex);
        CrashHandler_SetLastRhiOp("RenderFrame entry");

        m_renderer->RenderFrame(m_cmdList, m_swapChain,
                                m_world.get(), m_vfs.get(),
                                dt, m_frameIndex);

        CrashHandler_SetLastRhiOp("RenderFrame completed");

        // ── Present ───────────────────────────────────────────────────────
        m_swapChain->Present(m_config.vsync);

        // ── Signal fence ──────────────────────────────────────────────────
        m_globalFenceValue++;
        m_fenceValues[allocFrame] = m_globalFenceValue;
        m_frameFences[allocFrame]->Signal(m_globalFenceValue);

        // ── Pass dump (after present, stalls GPU to read back all targets) ──
        // Wait for scene to be fully loaded (all textures decoded + uploaded)
        // ── Pass dump — waits for scene fully loaded, then dumps + exits ──
        if (m_passDumpRequested && m_renderer && m_frameIndex >= 2
            && m_renderer->IsSceneFullyLoadedDebug()) {
            m_device->WaitIdle();
            m_renderer->GetPassInspector().ExecuteDump(
                m_device.get(), m_cmdList, m_frameIndex, ".screenshots");
            m_passDumpRequested = false;
            m_config.maxFrames = m_frameIndex + 3;
        }

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

    // ── Pixel debug output (JSON to stdout — for LLM agent consumption) ──
    if (m_pixelDebug && m_pixelDebug->IsEnabled()) {
        auto records = m_pixelDebug->ReadResults(m_device.get());
        std::string json = PixelDebugSystem::ToJSON(
            records, m_pixelDebugX, m_pixelDebugY, m_frameIndex);
        std::printf("PIXEL_DEBUG_JSON:%s\n", json.c_str());
        std::fflush(stdout);
    }

    // ── Diagnostic report (JSON to stdout — for LLM agent consumption) ──
    if (m_diagMode && m_renderer) {
        auto report = m_renderer->GetPassInspector().GenerateReport(
            m_device.get(), m_cmdList, m_frameIndex);
        // Output JSON to stdout with a parseable prefix.
        // Mirrors the SCREENSHOT_BASE64: pattern used by --screenshot-stdout.
        std::printf("DIAG_JSON:%s\n", report.ToJSON().c_str());
        std::fflush(stdout);
    }

    // ── Pass validation (after GPU idle, before shutdown) ─────────────
    if (m_passValidate && !m_diagMode && m_renderer) {
        UZ_LOG(App, Info, "Running pass validation...");
        auto results = m_renderer->GetPassInspector().Validate(m_device.get(), m_cmdList);
        bool anyFail = false;
        for (const auto& r : results) {
            if (r.hasNaN || r.hasInf) anyFail = true;
        }
        if (anyFail) {
            UZ_LOG(App, Error, "Pass validation: FAILURES detected");
        } else {
            UZ_LOG(App, Info, "Pass validation: all passes OK");
        }
    }

    const auto loopEnd  = std::chrono::high_resolution_clock::now();
    const float totalSec = std::chrono::duration<float>(loopEnd - loopStart).count();
    const float avgFps   = m_frameIndex > 0 ? static_cast<float>(m_frameIndex) / totalSec : 0.f;
    UZ_LOG(App, Info, "Application '{}': {} frames in {:.1f}s ({:.1f} avg FPS)",
           m_config.title, m_frameIndex, totalSec, avgFps);

    OnStop();

    // ── Pixel debug cleanup ──────────────────────────────────────────────
    if (m_pixelDebug) {
        m_pixelDebug->Shutdown(m_device.get());
        m_pixelDebug.reset();
    }

    m_renderer->Shutdown();
    m_renderer.reset();
    m_world.reset();
    m_vfs.reset();
    ShutdownPlatform();

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

VfxEditorOverlay* Application::GetVfxEditorOverlay() {
    return m_renderer ? m_renderer->GetVfxEditorOverlay() : nullptr;
}

ParticleEmitter* Application::GetRuntimeEmitter(int32_t runtimeIdx) {
    return m_renderer ? m_renderer->GetRuntimeEmitter(runtimeIdx) : nullptr;
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

// ─────────────────────────────────────────────────────────────────────────────
// RunFlipComparison — Headless FLIP perceptual image comparison
// ─────────────────────────────────────────────────────────────────────────────

int Application::RunFlipComparison() {
    // 1. Create a standalone ShaderManager for the compute shader
    ShaderManager shaderMgr;
    if (!shaderMgr.Init(m_device.get())) {
        UZ_LOG(App, Error, "FLIP: failed to initialize ShaderManager");
        return 1;
    }

    // 2. Load reference and test images as sRGB GPU textures
    m_cmdList->Begin();
    auto refResult = TextureLoader::Load(m_device.get(), m_cmdList, m_flipRefPath, true);
    if (!refResult.handle.IsValid()) {
        UZ_LOG(App, Error, "FLIP: failed to load reference image '{}'", m_flipRefPath);
        shaderMgr.Shutdown();
        return 1;
    }

    m_cmdList->Begin();
    auto testResult = TextureLoader::Load(m_device.get(), m_cmdList, m_flipTestPath, true);
    if (!testResult.handle.IsValid()) {
        UZ_LOG(App, Error, "FLIP: failed to load test image '{}'", m_flipTestPath);
        m_device->DestroyTexture(refResult.handle);
        shaderMgr.Shutdown();
        return 1;
    }

    // Validate dimensions match
    if (refResult.width != testResult.width || refResult.height != testResult.height) {
        UZ_LOG(App, Error, "FLIP: image dimensions mismatch — ref {}x{} vs test {}x{}",
               refResult.width, refResult.height, testResult.width, testResult.height);
        m_device->DestroyTexture(refResult.handle);
        m_device->DestroyTexture(testResult.handle);
        shaderMgr.Shutdown();
        return 1;
    }

    const uint32_t w = refResult.width;
    const uint32_t h = refResult.height;

    // 3. Init and run FlipPass
    FlipPass flip;
    if (!flip.Init(m_device.get(), &shaderMgr)) {
        UZ_LOG(App, Error, "FLIP: failed to initialize FlipPass");
        m_device->DestroyTexture(refResult.handle);
        m_device->DestroyTexture(testResult.handle);
        shaderMgr.Shutdown();
        return 1;
    }

    m_cmdList->Begin();
    FlipResult result = flip.Evaluate(m_cmdList, refResult.srvIndex, testResult.srvIndex,
                                       w, h, m_flipThreshold);

    // 4. Save heatmap PNG if --flip-output was specified
    if (!m_flipOutputPath.empty() && result.heatmap.IsValid()) {
        // Readback RGBA16F heatmap → CPU → convert to RGBA8 → save PNG
        const uint32_t rowPitch = w * 8; // 4 channels * 2 bytes (float16)
        const uint32_t bufSize  = rowPitch * h;

        RHIBufferDesc rbDesc;
        rbDesc.sizeBytes = bufSize;
        rbDesc.usage     = RHIBufferUsage::CopyDst;
        rbDesc.memory    = RHIMemoryType::Readback;
        rbDesc.debugName = "FLIP_HeatmapReadback";
        auto readbackBuf = m_device->CreateBuffer(rbDesc);

        m_cmdList->Begin();

        RHIBarrierDesc toCopy;
        toCopy.texture     = result.heatmap;
        toCopy.stateBefore = RHIResourceState::ShaderResource;
        toCopy.stateAfter  = RHIResourceState::CopySrc;
        m_cmdList->ResourceBarrier(&toCopy, 1);

        m_cmdList->CopyTextureToBuffer(result.heatmap, 0, readbackBuf, 0, rowPitch);

        m_cmdList->End();
        m_cmdList->Execute();
        m_device->WaitIdle();

        // Map and convert float16 to uint8
        const auto* src = static_cast<const uint16_t*>(m_device->MapBuffer(readbackBuf));
        std::vector<uint8_t> rgba8(w * h * 4);
        for (uint32_t i = 0; i < w * h * 4; ++i) {
            // Convert float16 to float32 then to uint8
            uint16_t fp16 = src[i];
            uint32_t sign = (fp16 >> 15) & 0x1;
            uint32_t exp  = (fp16 >> 10) & 0x1F;
            uint32_t man  = fp16 & 0x3FF;
            float f;
            if (exp == 0) {
                f = (man == 0) ? 0.0f : (std::ldexp(static_cast<float>(man), -24));
            } else if (exp == 31) {
                f = (man == 0) ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
            } else {
                f = std::ldexp(static_cast<float>(man + 1024), static_cast<int>(exp) - 25);
            }
            if (sign) f = -f;
            f = std::clamp(f, 0.0f, 1.0f);
            rgba8[i] = static_cast<uint8_t>(f * 255.0f + 0.5f);
        }
        m_device->UnmapBuffer(readbackBuf);
        m_device->DestroyBuffer(readbackBuf);

        if (PngCodec::EncodeFile(m_flipOutputPath, rgba8.data(), w, h, 4)) {
            UZ_LOG(App, Info, "FLIP: heatmap saved to '{}'", m_flipOutputPath);
        } else {
            UZ_LOG(App, Error, "FLIP: failed to save heatmap to '{}'", m_flipOutputPath);
        }
    }

    // 5. Output JSON to stdout
    std::printf("FLIP_JSON:{\"mean_flip\":%.6f,\"max_flip\":%.6f,\"threshold\":%.4f,"
                "\"pass\":%s,\"ref\":\"%s\",\"test\":\"%s\",\"width\":%u,\"height\":%u}\n",
                result.meanFlip, result.maxFlip, m_flipThreshold,
                result.pass ? "true" : "false",
                m_flipRefPath.c_str(), m_flipTestPath.c_str(), w, h);
    std::fflush(stdout);

    // 6. Cleanup
    flip.Shutdown();
    m_device->DestroyTexture(refResult.handle);
    m_device->DestroyTexture(testResult.handle);
    shaderMgr.Shutdown();

    return result.pass ? 0 : 1;
}

} // namespace uz
