/// UzEngine Runtime C API Implementation
///
/// Implements the stable C interface defined in runtime.h. All engine state is
/// held in a file-static `RuntimeState` struct. The initialization sequence in
/// `uz_create_viewport()` mirrors `RunWindowed()` in uzengine.cpp to ensure
/// identical GPU setup.
///
/// # Architecture
/// The C API is a thin facade over the same objects that the standalone
/// executable creates: PlatformWindow, IRHIDevice, IRHISwapChain,
/// IRHICommandList, IRHIFence, World, LooseFileVFS, SceneRenderer, LuaVM.
/// One global RuntimeState holds all of these — only one viewport at a time.

#include "runtime.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "rhi_backend.h"
#include "rhi_config.h"
#include "log.h"
#include "world.h"
#include "scene_loader.h"
#include "scene_renderer.h"
#include "screenshot_capture.h"
#include "editor_input_handler.h"
#include "selection_manager.h"
#include "lua_editor_api.h"
#include "vfs.h"
#include "lua_vm.h"
#include "lua_scene_api.h"
#include "lua_scene_io_api.h"
#include "lua_renderer_api.h"
#include "lua_input_api.h"
#include "script_system.h"
#include "name_component.h"
#include "entity.h"
#include "components.h"
#include "component_serializer.h"
#include "script_component.h"
#include <json.hpp>
#include "input.h"
#include "paths.h"
#include "h264_encoder.h"
#include "frame_readback.h"
#include "file_system.h"

#ifdef _WIN32
#include "platform_window.h"
#include "crash_handler.h"
#include <objbase.h>  // CoInitializeEx
#endif

#include <json.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

UZ_DEFINE_LOG_CATEGORY(RuntimeAPI, Info);

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of swapchain back buffers for triple-buffered frame pacing.
/// Must match the value in uzengine.cpp for consistent behavior.
static constexpr uint32_t kFramesInFlight = 3;

// ─────────────────────────────────────────────────────────────────────────────
// Log callback sink
// ─────────────────────────────────────────────────────────────────────────────

/// File-static log callback. Protected by s_logCallbackMutex for thread-safe
/// registration. The callback itself is invoked without the mutex held to
/// avoid deadlocks in spdlog's internal locking.
static uz_log_callback s_logCallback = nullptr;
static std::mutex s_logCallbackMutex;

/// Custom spdlog sink that forwards log messages to the registered C callback.
///
/// Installed alongside the existing console + file sinks when a callback is
/// registered. Removed when the callback is unregistered.
class CallbackSink final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        uz_log_callback cb = nullptr;
        {
            std::lock_guard<std::mutex> lock(s_logCallbackMutex);
            cb = s_logCallback;
        }
        if (!cb) return;

        // Map spdlog level to uz_log_callback level (0-5).
        int level = 2; // Info default
        switch (msg.level) {
            case spdlog::level::trace:    level = 0; break;
            case spdlog::level::debug:    level = 1; break;
            case spdlog::level::info:     level = 2; break;
            case spdlog::level::warn:     level = 3; break;
            case spdlog::level::err:      level = 4; break;
            case spdlog::level::critical: level = 5; break;
            default: break;
        }

        // Extract category from logger name (spdlog logger name = category).
        const std::string category(msg.logger_name.data(), msg.logger_name.size());
        const std::string message(msg.payload.data(), msg.payload.size());

        cb(level, category.c_str(), message.c_str());
    }

    void flush_() override {
        // No buffering — messages are forwarded immediately.
    }
};

/// Shared pointer to the callback sink. Kept alive for the lifetime of the
/// registration so spdlog can reference it safely.
static std::shared_ptr<CallbackSink> s_callbackSink;

// ─────────────────────────────────────────────────────────────────────────────
// RuntimeState — all engine state for the C API viewport
// ─────────────────────────────────────────────────────────────────────────────

/// Holds all objects that comprise the rendering viewport. Mirrors the local
/// variables in `RunWindowed()` (uzengine.cpp lines 197-439).
struct RuntimeState {
    // GPU infrastructure
    std::unique_ptr<uz::IRHIDevice> device;
    uz::IRHISwapChain* swapChain = nullptr;
    uz::IRHICommandList* cmdList = nullptr;
    // encodeCmdList removed — readback now uses the main cmdList via preEndCb.
    uz::IRHIFence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};
    uint64_t globalFenceValue = 0;

    // Window
#ifdef _WIN32
    uz::PlatformWindow window;
#endif

    // Scene
    uz::World world;
    std::unique_ptr<uz::LooseFileVFS> vfs;
    std::string contentRoot;

    // Renderer
    uz::SceneRenderer sceneRenderer;
    bool sceneRendererReady = false;
    bool editorModeRequested = false;
    bool playMode = false;

    // Editor systems (selection, gizmos, input routing)
    uz::SelectionManager selectionManager;
    uz::EditorInputHandler editorInput;
    bool editorInputReady = false;

    // Lua scripting
    bool luaReady = false;

    // Timing
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point prevTime;
    uint32_t frameIndex = 0;

    // Screenshot
    uz::ScreenshotCapture screenshotCapture;

    // H.264 encoding for viewport streaming
    uz::H264Encoder h264Encoder;

    // Triple-buffered GPU readback for stall-free H.264 encoding
    uz::FrameReadback frameReadback;

    // Static buffers for returned strings (valid until next call)
    std::string sceneTreeJson;
    std::string componentJson;

    // Lifecycle flag
    bool initialized = false;
};

/// The single global runtime state. File-static — not visible outside this TU.
static RuntimeState* s_state = nullptr;

/// Content root path set before viewport creation. Consumed by uz_create_viewport().
static std::string s_pendingContentRoot;

/// Returns true if the runtime state exists and a viewport has been created.
[[nodiscard]] static bool HasViewport() {
    return s_state != nullptr && s_state->initialized;
}

/// Creates per-frame fences. Returns false if any creation fails, cleaning
/// up previously created fences on failure.
static bool CreateFrameFences(uz::IRHIDevice* device, uz::IRHIFence* fences[], uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        auto* f = device->CreateFence();
        if (!f) {
            UZ_LOG(RuntimeAPI, Error, "Failed to create frame fence {}", i);
            for (uint32_t j = 0; j < i; ++j) {
                device->DestroyFence(fences[j]);
                fences[j] = nullptr;
            }
            return false;
        }
        fences[i] = f;
    }
    return true;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// C API Implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

const char* uz_runtime_version(void) {
    return "0.7.0";
}

const char* uz_runtime_backend(void) {
    switch (uz::GetActiveRHIBackend()) {
        case uz::RHIBackend::DX12:   return "dx12";
        case uz::RHIBackend::DX11:   return "dx11";
        case uz::RHIBackend::Vulkan: return "vulkan";
        case uz::RHIBackend::Metal:  return "metal";
        default:                     return "none";
    }
}

void uz_set_engine_dir(const char* path) {
    if (!path || path[0] == '\0') {
        uz::SetEngineDir({});
        return;
    }
    uz::SetEngineDir(std::filesystem::path(path));

    // Redirect crash reports to the engine directory instead of the host
    // exe dir (which would be VSCode's install path for .node addons).
    uz::CrashHandler_SetCrashDir(path);

    UZ_LOG(RuntimeAPI, Info, "Engine resource dir set to: {}", path);
}

int uz_runtime_init(void) {
#ifdef _WIN32
    // Initialize COM for this thread. DX12 device creation, DXGI enumeration,
    // and Media Foundation (H.264 encoder) all require COM. When running as a
    // child process (runtime-worker.js), COM is not initialized by default.
    // STA is required for DXGI (swapchain Present() pumps messages internally).
    // S_FALSE means COM was already initialized — that's fine (Electron host).
    ::CoInitializeEx(nullptr, 0x2 /*COINIT_APARTMENTTHREADED*/ | 0x4 /*COINIT_DISABLE_OLE1DDE*/);
#endif

    uz::LogInit();
    uz::LogSetLevel(uz::LogLevel::Info);  // IPC mode: full logging (goes to file, not stdout)
    UZ_LOG(RuntimeAPI, Info, "Log file: {}", uz::LogGetFilePath());

    // In isolated worker mode, the crash handler's Vectored Exception Handler
    // can interfere with GPU driver exceptions (NVIDIA thread naming, etc.) and
    // cause false-positive stack buffer overrun (/GS) terminations. Since the
    // worker is a separate process, the OS will handle crashes natively — no need
    // for our VEH. Only install the crash handler when NOT running as a library.
    // uz::CrashHandler_SetLibraryMode(true);
    // uz::InstallCrashHandler();

    UZ_LOG(RuntimeAPI, Info, "UzEngine Runtime C API initializing (v{})", uz_runtime_version());
    return 1;
}

void uz_runtime_shutdown(void) {
    // Destroy viewport if still active.
    uz_destroy_viewport();

    UZ_LOG(RuntimeAPI, Info, "UzEngine Runtime C API shutting down");

    uz::UninstallCrashHandler();

    // Remove callback sink before log shutdown.
    {
        std::lock_guard<std::mutex> lock(s_logCallbackMutex);
        s_logCallback = nullptr;
        s_callbackSink.reset();
    }

    uz::LogShutdown();
}

// ── Viewport ────────────────────────────────────────────────────────────────

int uz_create_viewport(void* parent_hwnd, int width, int height) {
#ifndef _WIN32
    UZ_LOG(RuntimeAPI, Error, "uz_create_viewport: only supported on Windows");
    return 0;
#else
    if (HasViewport()) {
        UZ_LOG(RuntimeAPI, Error, "uz_create_viewport: viewport already exists — call uz_destroy_viewport() first");
        return 0;
    }

    if (width <= 0 || height <= 0) {
        UZ_LOG(RuntimeAPI, Error, "uz_create_viewport: invalid dimensions {}x{}", width, height);
        return 0;
    }

    // Allocate state on the heap so we never overflow the stack with
    // World (contains entt::registry) or SceneRenderer.
    auto state = std::make_unique<RuntimeState>();

    // ── Window ──────────────────────────────────────────────────────────
    uz::WindowDesc winDesc{};
    winDesc.title = "UzEngine Viewport";
    winDesc.width = width;
    winDesc.height = height;

    if (!state->window.Create(winDesc)) {
        UZ_LOG(RuntimeAPI, Error, "uz_create_viewport: window creation failed");
        return 0;
    }

    // When running inside a host process (VSCode/Electron), enable library
    // mode to prevent PostQuitMessage and message-queue interference.
    state->window.SetLibraryMode(true);

    if (parent_hwnd) {
        // Reparent as a child window inside the provided parent HWND.
        HWND hwnd = state->window.GetHWND();
        HWND parent = static_cast<HWND>(parent_hwnd);

        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style = (style & ~WS_OVERLAPPEDWINDOW) | WS_CHILD;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);

        SetParent(hwnd, parent);
        SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        state->window.Show();
    }
    // When no parent is provided (embedded PNG streaming mode), keep the
    // window hidden. DX12 still needs an HWND for swapchain creation, but
    // there is no reason to display it — frames are captured via
    // uz_capture_screenshot() and streamed to the webview.
    // DO NOT call Show() here.

    // ── Device ──────────────────────────────────────────────────────────
    uz::RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = uz::RHIBackend::DX12;
    // Debug layer disabled in DLL mode — the SM5/non-bindless path has
    // known validation errors (barrier state mismatches, CPU-only heap copies)
    // that the debug layer promotes to fatal CommandList::Close failures.
    // Enable manually for debugging specific issues.
    deviceDesc.enableValidation = false;
    deviceDesc.enableGpuValidation = false;
    deviceDesc.adapterIndex = 0;

    state->device = uz::CreateRHIDevice(deviceDesc);
    if (!state->device) {
        UZ_LOG(RuntimeAPI, Error, "uz_create_viewport: RHI device creation failed");
        return 0;
    }
    auto* device = state->device.get();

    // ── Swapchain ───────────────────────────────────────────────────────
    uz::RHISwapChainDesc scDesc{};
    scDesc.windowHandle = state->window.GetNativeHandle();
    scDesc.displayHandle = state->window.GetNativeDisplayHandle();
    scDesc.width = static_cast<uint32_t>(state->window.GetWidth());
    scDesc.height = static_cast<uint32_t>(state->window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = uz::RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    state->swapChain = device->CreateSwapChain(scDesc);
    if (!state->swapChain) {
        UZ_LOG(RuntimeAPI, Error, "uz_create_viewport: swapchain creation failed");
        return 0;
    }

    // ── Command list ────────────────────────────────────────────────────
    state->cmdList = device->CreateCommandList(uz::RHIQueueType::Graphics);
    if (!state->cmdList) {
        UZ_LOG(RuntimeAPI, Error, "uz_create_viewport: command list creation failed");
        device->DestroySwapChain(state->swapChain);
        return 0;
    }

    // ── Per-frame fences ────────────────────────────────────────────────
    if (!CreateFrameFences(device, state->frameFences, kFramesInFlight)) {
        device->DestroyCommandList(state->cmdList);
        device->DestroySwapChain(state->swapChain);
        return 0;
    }

    // ── World + VFS ─────────────────────────────────────────────────────
    // Use content root set via uz_set_content_root(), or fall back to cwd.
    if (!s_pendingContentRoot.empty()) {
        state->contentRoot = std::move(s_pendingContentRoot);
        s_pendingContentRoot.clear();
    }
    if (state->contentRoot.empty()) {
        state->contentRoot = std::filesystem::current_path().string();
    }
    state->vfs = std::make_unique<uz::LooseFileVFS>(state->contentRoot);

    // ── Lua scripting ───────────────────────────────────────────────────
    if (uz::LuaVM::Init()) {
        uz::LuaSetWorld(uz::LuaVM::GetState(), &state->world);
        uz::LuaSetVFS(uz::LuaVM::GetState(), state->vfs.get());
        uz::ScriptSystem::Initialize(&state->world, state->vfs.get());
        // Standalone runtime starts in Play Mode (scripts active). Editor
        // clients override via uz_set_editor_mode(1) → ScriptSystem suspends.
        uz::ScriptSystem::SetPlayMode(!state->editorModeRequested);
        state->playMode = !state->editorModeRequested;
        state->luaReady = true;
        UZ_LOG(RuntimeAPI, Info, "Lua scripting initialized");
    } else {
        UZ_LOG(RuntimeAPI, Warning, "LuaVM failed to initialize — scripting disabled");
    }

    // ── Scene Renderer ──────────────────────────────────────────────────
    state->sceneRendererReady = state->sceneRenderer.Init(
        device, &state->window,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    if (!state->sceneRendererReady) {
        UZ_LOG(RuntimeAPI, Warning, "SceneRenderer init failed — only clear-screen available");
    } else if (state->editorModeRequested) {
        // Apply editor mode that was requested before the renderer was ready.
        state->sceneRenderer.SetEditorMode(true);
    }

    // ── Editor input (selection, gizmos) ────────────────────────────────
    if (state->sceneRendererReady && state->editorModeRequested) {
        state->editorInput.Init(
            state->sceneRenderer.GetInput(), &state->selectionManager,
            &state->sceneRenderer.GetCamera(), &state->world);
        state->editorInput.SetSceneRenderer(&state->sceneRenderer);
        state->sceneRenderer.SetEditorInputHandler(&state->editorInput);
        state->editorInputReady = true;
        UZ_LOG(RuntimeAPI, Info, "Editor input handler initialized (selection + gizmos)");
    }

    // Connect Lua to the scene renderer for uz.renderer / uz.mesh / uz.texture APIs.
    if (state->sceneRendererReady && state->luaReady) {
        uz::LuaSetSceneRenderer(uz::LuaVM::GetState(), &state->sceneRenderer);
        uz::LuaSetInputSystem(uz::LuaVM::GetState(), state->sceneRenderer.GetInput());
        UZ_LOG(RuntimeAPI, Info, "Lua renderer + input API connected");
    }

    // Wire editor state into Lua for uz.editor.* APIs.
    if (state->editorInputReady && state->luaReady) {
        uz::LuaSetEditorState(uz::LuaVM::GetState(), &state->selectionManager, &state->editorInput);
    }

    // ── Screenshot capture ──────────────────────────────────────────────
    state->screenshotCapture.Init(device,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    // ── Triple-buffered readback for H.264 encoding ─────────────────────
    if (!state->frameReadback.Init(device,
            static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        UZ_LOG(RuntimeAPI, Warning,
               "Frame readback init failed — H.264 encoding will use fallback path");
    }

    // ── H.264 encoder for viewport streaming ────────────────────────────
    {
        uz::H264Encoder::Config encConfig{};
        encConfig.width  = static_cast<uint32_t>(width);
        encConfig.height = static_cast<uint32_t>(height);
        encConfig.fps    = 120;
        encConfig.bitrateKbps    = 20000;
        encConfig.keyframeInterval = 15;
        if (!state->h264Encoder.Initialize(encConfig)) {
            UZ_LOG(RuntimeAPI, Warning,
                   "H.264 encoder not available — frame streaming disabled");
        } else {
            UZ_LOG(RuntimeAPI, Info, "H.264 encoder ready: {}",
                   state->h264Encoder.GetEncoderName());
        }
    }

    // ── Timing ──────────────────────────────────────────────────────────
    state->startTime = std::chrono::high_resolution_clock::now();
    state->prevTime = state->startTime;
    state->frameIndex = 0;
    state->globalFenceValue = 0;

    state->initialized = true;
    s_state = state.release();

    UZ_LOG(RuntimeAPI, Info, "Viewport created: {}x{} (parent={})",
           width, height, parent_hwnd ? "child" : "standalone");
    return 1;
#endif // _WIN32
}

void uz_destroy_viewport(void) {
    if (!HasViewport()) return;

    auto* state = s_state;
    s_state = nullptr;

    UZ_LOG(RuntimeAPI, Info, "Destroying viewport...");

    // Shutdown Lua scripting first (may reference World/VFS).
    if (state->luaReady) {
        uz::ScriptSystem::Shutdown();
        uz::LuaVM::Shutdown();
        state->luaReady = false;
    }

    // Flush all GPU work before destroying resources.
    if (state->device) {
        state->device->WaitIdle();
    }

    // Shutdown SceneRenderer (releases shaders, pipelines, render targets, etc.).
    if (state->sceneRendererReady) {
        state->sceneRenderer.Shutdown();
        state->sceneRendererReady = false;
    }

    // Shutdown H.264 encoder.
    state->h264Encoder.Shutdown();

    // Shutdown triple-buffered readback (fences + buffers).
    state->frameReadback.Shutdown();

    // Shutdown screenshot capture.
    state->screenshotCapture.Shutdown();

    // Release GPU resources in reverse creation order.
    auto* device = state->device.get();
    if (device) {
        for (uint32_t i = kFramesInFlight; i > 0; --i) {
            if (state->frameFences[i - 1]) {
                device->DestroyFence(state->frameFences[i - 1]);
                state->frameFences[i - 1] = nullptr;
            }
        }
        if (state->cmdList) {
            device->DestroyCommandList(state->cmdList);
            state->cmdList = nullptr;
        }
        if (state->swapChain) {
            device->DestroySwapChain(state->swapChain);
            state->swapChain = nullptr;
        }
    }

    // VFS and World are destroyed by the RuntimeState destructor.
    // Device unique_ptr is destroyed last.
    delete state;

    UZ_LOG(RuntimeAPI, Info, "Viewport destroyed");
}

void uz_resize_viewport(int width, int height) {
    if (!HasViewport()) return;
    if (width <= 0 || height <= 0) return;

    auto* state = s_state;
    const uint32_t newW = static_cast<uint32_t>(width);
    const uint32_t newH = static_cast<uint32_t>(height);

    // No-op if dimensions haven't changed.
    if (newW == static_cast<uint32_t>(state->window.GetWidth()) &&
        newH == static_cast<uint32_t>(state->window.GetHeight())) {
        return;
    }

    state->device->WaitIdle();
    state->swapChain->Resize(newW, newH);

    if (state->sceneRendererReady) {
        state->sceneRenderer.Resize(newW, newH);
    }

    // Reinitialize screenshot capture for the new dimensions.
    state->screenshotCapture.Shutdown();
    state->screenshotCapture.Init(state->device.get(), newW, newH);

    // Reinitialize triple-buffered readback for the new dimensions.
    state->frameReadback.Shutdown();
    if (!state->frameReadback.Init(state->device.get(), newW, newH)) {
        UZ_LOG(RuntimeAPI, Warning,
               "Frame readback re-init failed after resize — encoding will use fallback");
    }

    // Reinitialize H.264 encoder for the new dimensions.
    if (state->h264Encoder.IsInitialized()) {
        state->h264Encoder.Shutdown();
        uz::H264Encoder::Config encConfig{};
        encConfig.width  = newW;
        encConfig.height = newH;
        encConfig.fps    = 120;
        encConfig.bitrateKbps    = 20000;
        encConfig.keyframeInterval = 15;
        if (!state->h264Encoder.Initialize(encConfig)) {
            UZ_LOG(RuntimeAPI, Warning,
                   "H.264 encoder re-init failed after resize — streaming disabled");
        }
    }

    UZ_LOG(RuntimeAPI, Info, "Viewport resized to {}x{}", newW, newH);
}

void uz_set_viewport_position(int x, int y, int width, int height) {
#ifdef _WIN32
    if (!HasViewport()) return;
    if (width <= 0 || height <= 0) return;

    HWND hwnd = s_state->window.GetHWND();
    if (!hwnd) return;

    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER);

    // If the size changed, resize the swapchain.
    const uint32_t newW = static_cast<uint32_t>(width);
    const uint32_t newH = static_cast<uint32_t>(height);
    if (newW != static_cast<uint32_t>(s_state->window.GetWidth()) ||
        newH != static_cast<uint32_t>(s_state->window.GetHeight())) {
        uz_resize_viewport(width, height);
    }
#endif
}

// ── Frame Rendering ─────────────────────────────────────────────────────────

uz_frame_stats uz_render_frame(void) {
    uz_frame_stats stats = {};

    if (!HasViewport()) return stats;

    auto* state = s_state;

    // Fatal: GPU device was removed. Stop rendering immediately — all GPU
    // operations will fail. The caller must break the render loop and exit.
    if (state->device->IsDeviceRemoved()) {
        stats.device_removed = 1;
        return stats;
    }

#ifdef _WIN32
    // Pump OS messages (input, resize, close).
    state->window.PumpMessages();

    // Handle window resize.
    if (state->window.WasResized()) {
        state->device->WaitIdle();
        const uint32_t newW = static_cast<uint32_t>(state->window.GetWidth());
        const uint32_t newH = static_cast<uint32_t>(state->window.GetHeight());
        state->swapChain->Resize(newW, newH);
        if (state->sceneRendererReady) {
            state->sceneRenderer.Resize(newW, newH);
        }
        state->window.ClearResizedFlag();
    }

    // Skip rendering while minimized (swapchain surface is 0x0).
    if (state->window.IsMinimized()) {
        return stats;
    }
#endif

    // Wait for frame N-2 before reusing its command allocator.
    const uint32_t allocFrame = state->frameIndex % kFramesInFlight;
    if (state->fenceValues[allocFrame] > 0) {
        state->frameFences[allocFrame]->WaitCPU(state->fenceValues[allocFrame]);
    }

    // Update timing.
    auto now = std::chrono::high_resolution_clock::now();
    const float totalTime = std::chrono::duration<float>(now - state->startTime).count();
    float dt = std::chrono::duration<float>(now - state->prevTime).count();
    state->prevTime = now;
    // Clamp dt to avoid huge jumps on first frame or after hitches (shader compile, etc.)
    if (dt > 0.1f) dt = 0.016f;

    // Update Lua scripts.
    if (state->luaReady) {
        uz::LuaVM::UpdateFrameGlobals(dt, totalTime, static_cast<uint64_t>(state->frameIndex));
        uz::ScriptSystem::Update(dt);
    }

    // Update editor input (selection picking, gizmo interaction).
    if (state->editorInputReady) {
        state->editorInput.Update(dt);
        // Sync selection + gizmo state to renderer for outline/gizmo passes.
        state->sceneRenderer.SetSelectedEntity(state->selectionManager.GetSelected());
        state->sceneRenderer.SetGizmoMode(static_cast<uint8_t>(state->editorInput.GetGizmoMode()));
        state->sceneRenderer.SetGizmoSpace(static_cast<uint8_t>(state->editorInput.GetGizmoSpace()));
        state->sceneRenderer.SetGizmoHoveredAxis(state->editorInput.GetGizmoHoveredAxis());
        state->sceneRenderer.SetGizmoActiveAxis(state->editorInput.GetGizmoActiveAxis());
    }

    // Render the frame.
    if (state->sceneRendererReady) {
        // Build a pre-End callback that copies the LDR intermediate to the
        // readback buffer inside the same command list submission. This avoids
        // the post-Present backbuffer ambiguity and the double-WaitIdle stall
        // that caused DXGI_ERROR_DEVICE_REMOVED in the old encode path.
        uz::SceneRenderer::PreEndCallback readbackCb = nullptr;

        if (state->frameReadback.IsInitialized()) {
            readbackCb = [state](uz::IRHICommandList* cmd) {
                // Capture the backbuffer (which includes editor overlays: grid,
                // stats, gizmos) instead of the LDR intermediate (which doesn't).
                // This ensures the H.264 stream sent to the webview shows the
                // complete editor UI, not just the scene color.
                uz::RHITextureHandle bb = state->swapChain->GetCurrentBackBuffer();
                if (!bb.IsValid()) return;

                // The render graph leaves the backbuffer in Present state after
                // the final overlay pass. Transition to CopySrc for readback.
                const uz::RHIBarrierDesc toCopySrc{
                    .texture     = bb,
                    .stateBefore = uz::RHIResourceState::Present,
                    .stateAfter  = uz::RHIResourceState::CopySrc,
                };
                cmd->ResourceBarrier(&toCopySrc, 1);

                state->frameReadback.CaptureFromTexture(cmd, bb);

                // Transition back to Present for SwapChain::Present().
                const uz::RHIBarrierDesc toPresent{
                    .texture     = bb,
                    .stateBefore = uz::RHIResourceState::CopySrc,
                    .stateAfter  = uz::RHIResourceState::Present,
                };
                cmd->ResourceBarrier(&toPresent, 1);
            };
        }

        state->sceneRenderer.RenderFrame(
            state->cmdList, state->swapChain,
            &state->world, state->vfs.get(),
            dt, state->frameIndex, readbackCb);

        // Log first 3 frames to diagnose draw call issues.
        if (state->frameIndex < 3) {
            UZ_LOG(RuntimeAPI, Info,
                   "Frame {}: sceneRendererReady=true, entityCount={}, contentRoot='{}'",
                   state->frameIndex,
                   static_cast<uint32_t>(state->world.GetRegistry().storage<entt::entity>().size()),
                   state->contentRoot);
        }
    } else {
        // Fallback: animated clear screen (same as uzengine.cpp).
        float color[4];
        color[0] = 0.5f + 0.5f * std::sin(totalTime * 1.0f);
        color[1] = 0.5f + 0.5f * std::sin(totalTime * 1.3f + 2.094f);
        color[2] = 0.5f + 0.5f * std::sin(totalTime * 1.7f + 4.189f);
        color[3] = 1.0f;

        state->cmdList->Begin();
        const uz::RHITextureHandle backBuffer = state->swapChain->GetCurrentBackBuffer();

        const uz::RHIBarrierDesc toRT{
            .texture = backBuffer,
            .stateBefore = uz::RHIResourceState::Present,
            .stateAfter = uz::RHIResourceState::RenderTarget,
        };
        state->cmdList->ResourceBarrier(&toRT, 1);
        state->cmdList->ClearRenderTarget(backBuffer, color);

        const uz::RHIBarrierDesc toPresent{
            .texture = backBuffer,
            .stateBefore = uz::RHIResourceState::RenderTarget,
            .stateAfter = uz::RHIResourceState::Present,
        };
        state->cmdList->ResourceBarrier(&toPresent, 1);
        state->cmdList->End();
        state->cmdList->Execute();
    }

    // ── Signal readback fence (after Execute, before Present) ──────────
    // The readback copy was already recorded inside the RenderFrame command
    // list via the pre-End callback. Signal the fence so the CPU can map
    // the buffer after the GPU finishes the copy.
    if (state->frameReadback.IsInitialized()) {
        state->frameReadback.SignalFence();
    }

    // Present + signal fence.
    state->swapChain->Present(false);
    state->globalFenceValue++;
    state->fenceValues[allocFrame] = state->globalFenceValue;
    state->frameFences[allocFrame]->Signal(state->globalFenceValue);

    state->frameIndex++;

    // Fill stats.
    const float frameMs = dt * 1000.0f;
    stats.fps = (frameMs > 0.0f) ? (1000.0f / frameMs) : 0.0f;
    stats.frame_ms = frameMs;
    stats.frame_index = state->frameIndex;
    // Real stats from the renderer (entity count + mesh count as placeholder
    // until the renderer exposes actual draw call / triangle counters).
    extern std::atomic<uint32_t> g_lastSyncMeshEntityCount;
    stats.draw_calls = static_cast<uint32_t>(state->world.GetRegistry().storage<entt::entity>().size());
    stats.triangles = g_lastSyncMeshEntityCount.load(std::memory_order_relaxed);

    return stats;
}

// ── Scene Management ────────────────────────────────────────────────────────

int uz_load_scene(const char* path) {
    if (!path) {
        UZ_LOG(RuntimeAPI, Error, "uz_load_scene: path is null");
        return 0;
    }
    if (!s_state) {
        UZ_LOG(RuntimeAPI, Error, "uz_load_scene: runtime not initialized (s_state=null)");
        return 0;
    }
    if (!s_state->initialized) {
        UZ_LOG(RuntimeAPI, Error, "uz_load_scene: viewport not created (initialized=false)");
        return 0;
    }

    auto* state = s_state;
    UZ_LOG(RuntimeAPI, Info, "uz_load_scene: path='{}' contentRoot='{}'", path, state->contentRoot);

    // Support both absolute and relative paths.
    // If absolute, read directly from disk. If relative, go through VFS.
    std::vector<uint8_t> sceneBytes;
    const std::filesystem::path fsPath(path);
    if (fsPath.is_absolute()) {
        UZ_LOG(RuntimeAPI, Info, "uz_load_scene: reading absolute path");
        sceneBytes = uz::FileReadAll(path);
    } else {
        UZ_LOG(RuntimeAPI, Info, "uz_load_scene: reading via VFS");
        sceneBytes = state->vfs->Read(path);
    }
    if (sceneBytes.empty()) {
        UZ_LOG(RuntimeAPI, Error, "uz_load_scene: file read returned 0 bytes for '{}'", path);
        return 0;
    }
    UZ_LOG(RuntimeAPI, Info, "uz_load_scene: read {} bytes", sceneBytes.size());

    // Pass the VFS so "prefab" entities resolve their referenced assets —
    // the VFS-less overload silently skips prefab instantiation.
    const size_t count = uz::LoadScene(state->world, sceneBytes, path, state->vfs.get());
    UZ_LOG(RuntimeAPI, Info, "uz_load_scene: loaded {} entities from '{}'", count, path);

    // If Lua is ready, re-scan scripts in the newly loaded scene.
    if (state->luaReady) {
        uz::LuaSetWorld(uz::LuaVM::GetState(), &state->world);
    }

    return 1;
}

int uz_save_scene(const char* path) {
    if (!HasViewport() || !path) return 0;

    auto* state = s_state;
    auto sceneBytes = uz::SaveScene(state->world);
    if (sceneBytes.empty()) {
        UZ_LOG(RuntimeAPI, Error, "uz_save_scene: serialization produced empty output");
        return 0;
    }

    // Write bytes to disk.
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "wb");
#else
    f = fopen(path, "wb");
#endif
    if (!f) {
        UZ_LOG(RuntimeAPI, Error, "uz_save_scene: failed to open '{}' for writing", path);
        return 0;
    }
    const size_t written = fwrite(sceneBytes.data(), 1, sceneBytes.size(), f);
    fclose(f);

    if (written != sceneBytes.size()) {
        UZ_LOG(RuntimeAPI, Error, "uz_save_scene: write incomplete ({}/{} bytes)", written, sceneBytes.size());
        return 0;
    }

    UZ_LOG(RuntimeAPI, Info, "uz_save_scene: saved {} bytes to '{}'", sceneBytes.size(), path);
    return 1;
}

const char* uz_get_scene_tree(void) {
    static const char* kEmpty = "{}";
    if (!HasViewport()) return kEmpty;

    auto* state = s_state;
    auto& registry = state->world.GetRegistry();

    nlohmann::json root;
    nlohmann::json entities = nlohmann::json::array();

    auto view = registry.view<uz::CName>();
    for (auto entity : view) {
        const auto& name = view.get<uz::CName>(entity);
        nlohmann::json entityObj;
        entityObj["id"] = static_cast<uint32_t>(entity);
        entityObj["name"] = name.name;
        entityObj["children"] = nlohmann::json::array();
        entities.push_back(std::move(entityObj));
    }

    root["entities"] = std::move(entities);
    state->sceneTreeJson = root.dump();
    return state->sceneTreeJson.c_str();
}

uint32_t uz_create_entity(const char* name) {
    if (!HasViewport() || !name) return 0;

    auto* state = s_state;
    auto entity = state->world.CreateEntity();
    if (!entity.IsValid()) return 0;

    state->world.AddComponent<uz::CName>(entity, uz::CName{name});

    UZ_LOG(RuntimeAPI, Debug, "Created entity '{}' (id={})", name, entity.id);
    return entity.id;
}

void uz_destroy_entity(uint32_t entity_id) {
    if (!HasViewport()) return;

    auto* state = s_state;

    // Reconstruct a generational Entity handle from the raw slot index.
    // EntityFromEntt looks up the current generation for the slot, so the
    // resulting handle will pass IsValid() if the entity is still alive.
    const auto enttEntity = static_cast<entt::entity>(entity_id);
    if (!state->world.GetRegistry().valid(enttEntity)) return;

    uz::Entity entity = state->world.EntityFromEntt(enttEntity);
    if (state->world.IsValid(entity)) {
        state->world.DestroyEntity(entity);
        UZ_LOG(RuntimeAPI, Debug, "Destroyed entity id={}", entity_id);
    }
}

const char* uz_get_component_json(uint32_t entity_id, const char* component_type) {
    static std::string s_buffer;  // valid until next call
    s_buffer = "{}";

    if (!HasViewport() || !component_type) return s_buffer.c_str();

    auto* state = s_state;
    const auto& reg = state->world.GetRegistry();
    const auto enttEntity = static_cast<entt::entity>(entity_id);
    if (!reg.valid(enttEntity)) return s_buffer.c_str();

    const uz::Entity entity = state->world.EntityFromEntt(enttEntity);
    const std::string type(component_type);
    nlohmann::json j;

    // Built-in components (not in the auto-registry)
    if (type == "transform") {
        if (const auto* sc = reg.try_get<uz::SceneComponent>(enttEntity)) {
            j["position"] = {sc->GetPosition().x, sc->GetPosition().y, sc->GetPosition().z};
            const auto euler = sc->GetRotation().get_euler();
            j["rotation"] = {euler.x * 57.2957795f, euler.y * 57.2957795f, euler.z * 57.2957795f};
            j["scale"] = {sc->GetScale().x, sc->GetScale().y, sc->GetScale().z};
        }
    } else if (type == "camera") {
        if (const auto* cam = reg.try_get<uz::CameraComponent>(enttEntity)) {
            j["fov"] = cam->fov;
            j["near"] = cam->nearPlane;
            j["far"] = cam->farPlane;
            j["active"] = cam->isActive;
        }
    } else if (type == "mesh") {
        if (const auto* mesh = reg.try_get<uz::StaticMeshComponent>(enttEntity)) {
            j["asset"] = mesh->meshPath;
        }
    } else if (type == "light") {
        if (const auto* dir = reg.try_get<uz::DirectionalLightComponent>(enttEntity)) {
            j["type"] = "directional";
            j["color"] = {dir->color.x, dir->color.y, dir->color.z};
            j["intensity"] = dir->intensity;
        } else if (const auto* spot = reg.try_get<uz::SpotLightComponent>(enttEntity)) {
            j["type"] = "spot";
            j["color"] = {spot->color.x, spot->color.y, spot->color.z};
            j["intensity"] = spot->intensity;
            j["range"] = spot->range;
            j["innerAngle"] = spot->innerAngleDeg;
            j["outerAngle"] = spot->outerAngleDeg;
        } else if (const auto* point = reg.try_get<uz::PointLightComponent>(enttEntity)) {
            j["type"] = "point";
            j["color"] = {point->color.x, point->color.y, point->color.z};
            j["intensity"] = point->intensity;
            j["range"] = point->range;
        }
    } else if (type == "script") {
        if (const auto* script = reg.try_get<uz::CScript>(enttEntity)) {
            j["path"] = script->scriptPath;
        }
    } else if (type == "name") {
        if (const auto* name = reg.try_get<uz::CName>(enttEntity)) {
            j["name"] = name->name;
        }
    } else {
        // Registry-based components (sky_atmosphere, height_fog, etc.)
        (void)uz::ComponentSerializerRegistry::Get().Serialize(type, state->world, entity, &j);
    }

    s_buffer = j.dump();
    return s_buffer.c_str();
}

int uz_set_component_property(uint32_t entity_id, const char* component_type,
                              const char* property_name, const char* value_json) {
    if (!HasViewport() || !component_type || !property_name || !value_json)
        return 0;

    auto* state = s_state;
    auto& reg = state->world.GetRegistry();
    const auto enttEntity = static_cast<entt::entity>(entity_id);
    if (!reg.valid(enttEntity)) return 0;

    const std::string type(component_type);
    const std::string prop(property_name);

    nlohmann::json val;
    try { val = nlohmann::json::parse(value_json); }
    catch (...) { return 0; }

    // Transform properties
    if (type == "transform") {
        auto* sc = reg.try_get<uz::SceneComponent>(enttEntity);
        if (!sc) return 0;
        if (prop == "position" && val.is_array() && val.size() == 3) {
            sc->SetPosition({val[0].get<float>(), val[1].get<float>(), val[2].get<float>()});
            return 1;
        }
        if (prop == "scale" && val.is_array() && val.size() == 3) {
            sc->SetScale({val[0].get<float>(), val[1].get<float>(), val[2].get<float>()});
            return 1;
        }
    }

    // Camera properties
    if (type == "camera") {
        auto* cam = reg.try_get<uz::CameraComponent>(enttEntity);
        if (!cam) return 0;
        if (prop == "fov")    { cam->fov = val.get<float>(); return 1; }
        if (prop == "near")   { cam->nearPlane = val.get<float>(); return 1; }
        if (prop == "far")    { cam->farPlane = val.get<float>(); return 1; }
        if (prop == "active") { cam->isActive = val.get<bool>(); return 1; }
    }

    // Light properties
    if (type == "light") {
        if (auto* dir = reg.try_get<uz::DirectionalLightComponent>(enttEntity)) {
            if (prop == "intensity") { dir->intensity = val.get<float>(); return 1; }
            if (prop == "color" && val.is_array() && val.size() == 3) {
                dir->color = {val[0].get<float>(), val[1].get<float>(), val[2].get<float>()};
                return 1;
            }
        }
        if (auto* point = reg.try_get<uz::PointLightComponent>(enttEntity)) {
            if (prop == "intensity") { point->intensity = val.get<float>(); return 1; }
            if (prop == "range") { point->range = val.get<float>(); return 1; }
            if (prop == "color" && val.is_array() && val.size() == 3) {
                point->color = {val[0].get<float>(), val[1].get<float>(), val[2].get<float>()};
                return 1;
            }
        }
        if (auto* spot = reg.try_get<uz::SpotLightComponent>(enttEntity)) {
            if (prop == "innerAngle") { spot->innerAngleDeg = val.get<float>(); return 1; }
            if (prop == "outerAngle") { spot->outerAngleDeg = val.get<float>(); return 1; }
        }
    }

    return 0;
}

const char* uz_get_entity_components(uint32_t entity_id) {
    static std::string s_buf;
    s_buf = "[]";

    if (!HasViewport()) return s_buf.c_str();

    const auto& reg = s_state->world.GetRegistry();
    const auto enttEntity = static_cast<entt::entity>(entity_id);
    if (!reg.valid(enttEntity)) return s_buf.c_str();

    nlohmann::json arr = nlohmann::json::array();

    // Built-in component checks
    if (reg.all_of<uz::SceneComponent>(enttEntity))      arr.push_back("transform");
    if (reg.all_of<uz::CameraComponent>(enttEntity))     arr.push_back("camera");
    if (reg.all_of<uz::StaticMeshComponent>(enttEntity))  arr.push_back("mesh");
    if (reg.all_of<uz::DirectionalLightComponent>(enttEntity)) arr.push_back("light");
    else if (reg.all_of<uz::SpotLightComponent>(enttEntity))   arr.push_back("light");
    else if (reg.all_of<uz::PointLightComponent>(enttEntity))  arr.push_back("light");
    if (reg.all_of<uz::CScript>(enttEntity))             arr.push_back("script");
    if (reg.all_of<uz::CName>(enttEntity))               arr.push_back("name");

    // Registry-based components (environment components, etc.)
    const uz::Entity entity = s_state->world.EntityFromEntt(enttEntity);
    for (const auto& [key, entry] : uz::ComponentSerializerRegistry::Get().GetEntries()) {
        nlohmann::json tmp;
        if (entry.serialize(s_state->world, entity, &tmp)) {
            arr.push_back(key);
        }
    }

    s_buf = arr.dump();
    return s_buf.c_str();
}

void uz_select_entity(uint32_t entity_id) {
    if (!HasViewport()) return;
    auto* state = s_state;

    if (entity_id == UINT32_MAX) {
        state->selectionManager.Deselect();
        if (state->sceneRendererReady) {
            state->sceneRenderer.SetSelectedEntity(uz::Entity{});
        }
        return;
    }

    const auto enttEntity = static_cast<entt::entity>(entity_id);
    if (!state->world.GetRegistry().valid(enttEntity)) return;

    uz::Entity entity = state->world.EntityFromEntt(enttEntity);
    state->selectionManager.Select(entity);
    if (state->sceneRendererReady) {
        state->sceneRenderer.SetSelectedEntity(entity);
    }
}

uint32_t uz_get_selected_entity(void) {
    if (!HasViewport()) return UINT32_MAX;
    const uz::Entity sel = s_state->selectionManager.GetSelected();
    return sel.IsValid() ? sel.id : UINT32_MAX;
}

// ── Play Mode ───────────────────────────────────────────────────────────────

int uz_play(void) {
    if (!HasViewport() || !s_state->sceneRendererReady) return 0;
    if (s_state->playMode) return 0; // already playing

    s_state->playMode = true;
    s_state->sceneRenderer.SetEditorMode(false);
    uz::ScriptSystem::SetPlayMode(true);

    UZ_LOG(RuntimeAPI, Info, "Play mode started");
    return 1;
}

int uz_stop(void) {
    if (!HasViewport() || !s_state->sceneRendererReady) return 0;
    if (!s_state->playMode) return 0; // not playing

    s_state->playMode = false;
    s_state->sceneRenderer.SetEditorMode(true);
    uz::ScriptSystem::SetPlayMode(false);

    UZ_LOG(RuntimeAPI, Info, "Play mode stopped — editor restored");
    return 1;
}

int uz_is_playing(void) {
    return HasViewport() && s_state->playMode ? 1 : 0;
}

// ── Input Forwarding ────────────────────────────────────────────────────────

void uz_on_mouse_move(float x, float y) {
    if (!HasViewport() || !s_state->sceneRendererReady) return;

    auto* input = s_state->sceneRenderer.GetInput();
    if (input) {
        input->OnMouseMove(x, y);
    }
}

void uz_on_mouse_button(int button, int pressed) {
    if (!HasViewport() || !s_state->sceneRendererReady) return;

    auto* input = s_state->sceneRenderer.GetInput();
    if (!input) return;

    // Web API MouseEvent.button: 0=Left, 1=Middle, 2=Right
    uz::MouseButton btn = uz::MouseButton::Left;
    switch (button) {
        case 0: btn = uz::MouseButton::Left; break;
        case 1: btn = uz::MouseButton::Middle; break;
        case 2: btn = uz::MouseButton::Right; break;
        default: return;
    }
    input->OnMouseButton(btn, pressed != 0);
}

void uz_on_key(int keycode, int pressed) {
    if (!HasViewport() || !s_state->sceneRendererReady) return;

    auto* input = s_state->sceneRenderer.GetInput();
    if (!input) return;

    if (pressed) {
        input->OnKeyDown(static_cast<uint16_t>(keycode));
    } else {
        input->OnKeyUp(static_cast<uint16_t>(keycode));
    }
}

void uz_on_key_ex(int keycode, int pressed, int modifiers) {
    if (!HasViewport() || !s_state->sceneRendererReady) return;

    auto* input = s_state->sceneRenderer.GetInput();
    if (!input) return;

    // Modifier keys are handled via their own keydown/keyup events
    // (ShiftLeft → VK_SHIFT, ControlLeft → VK_CONTROL, etc.) which
    // flow through the normal OnKeyDown/OnKeyUp path. We do NOT call
    // SetModifierState here because the DOM modifier flags on non-modifier
    // key events can be stale (e.g., keyup of 'W' may report shiftKey=false
    // even if Shift is still held, causing premature modifier clearing).

    if (pressed) {
        input->OnKeyDown(static_cast<uint16_t>(keycode));
    } else {
        input->OnKeyUp(static_cast<uint16_t>(keycode));
    }
}

void uz_on_scroll(float delta) {
    if (!HasViewport() || !s_state->sceneRendererReady) return;

    auto* input = s_state->sceneRenderer.GetInput();
    if (input) {
        input->OnMouseScroll(delta);
    }
}

// ── Logging ─────────────────────────────────────────────────────────────────

void uz_set_log_callback(uz_log_callback callback) {
    std::lock_guard<std::mutex> lock(s_logCallbackMutex);

    if (callback) {
        s_logCallback = callback;
        if (!s_callbackSink) {
            s_callbackSink = std::make_shared<CallbackSink>();
            // Add the sink to ALL existing loggers (fixes BUG-8).
            // LogAddSink rebuilds every cached logger to include this sink.
            uz::LogAddSink(s_callbackSink);
        }
    } else {
        if (s_callbackSink) {
            uz::LogRemoveSink(s_callbackSink);
        }
        s_logCallback = nullptr;
        s_callbackSink.reset();
    }
}

// ── Screenshot ──────────────────────────────────────────────────────────────

int uz_capture_screenshot(uint8_t** out_data, uint32_t* out_size) {
    if (out_data) *out_data = nullptr;
    if (out_size) *out_size = 0;

    if (!HasViewport() || !out_data || !out_size) return 0;

    auto* state = s_state;

    if (!state->screenshotCapture.IsInitialized()) {
        UZ_LOG(RuntimeAPI, Error, "uz_capture_screenshot: screenshot capture not initialized");
        return 0;
    }

    // Capture the current back buffer. The GPU copy was already recorded
    // during RenderFrame, but we need to do an explicit capture here since
    // the C API doesn't control the render graph timing.
    //
    // Record a standalone capture: transition back buffer from Present to
    // CopySrc, copy to readback, transition back to Present.
    state->cmdList->Begin();
    state->screenshotCapture.CaptureFromPresent(state->cmdList, state->swapChain);
    state->cmdList->End();
    state->cmdList->Execute();

    // Wait for the copy to complete.
    state->device->WaitIdle();

    // Encode to PNG via the base64 path, then decode the base64 into raw PNG.
    // The ToBase64PNG() method returns base64-encoded PNG data.
    std::string base64Png = state->screenshotCapture.ToBase64PNG();
    if (base64Png.empty()) {
        UZ_LOG(RuntimeAPI, Error, "uz_capture_screenshot: PNG encoding failed");
        return 0;
    }

    // Decode base64 to raw PNG bytes.
    // For efficiency, we use SaveToPNG to a temp file and read it back.
    // A better path would be to have ToRawPNG() but we use what's available.
    //
    // Actually, the base64 string IS the PNG data encoded in base64. The caller
    // likely wants raw PNG bytes. Let's decode the base64.
    //
    // Simple base64 decode:
    static constexpr unsigned char kDecodeTable[256] = {
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
         52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
        255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
         15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
        255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
         41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    };

    // Estimate output size (base64 encodes 3 bytes as 4 chars).
    const size_t b64Len = base64Png.size();
    const size_t maxOutputLen = (b64Len / 4) * 3 + 3;

    auto* pngData = static_cast<uint8_t*>(std::malloc(maxOutputLen));
    if (!pngData) {
        UZ_LOG(RuntimeAPI, Error, "uz_capture_screenshot: allocation failed");
        return 0;
    }

    size_t outIdx = 0;
    uint32_t accum = 0;
    int bits = 0;

    for (size_t i = 0; i < b64Len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(base64Png[i]);
        if (ch == '=' || ch == '\n' || ch == '\r') continue;
        const unsigned char val = kDecodeTable[ch];
        if (val == 255) continue;

        accum = (accum << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            pngData[outIdx++] = static_cast<uint8_t>((accum >> bits) & 0xFF);
        }
    }

    *out_data = pngData;
    *out_size = static_cast<uint32_t>(outIdx);

    UZ_LOG(RuntimeAPI, Debug, "uz_capture_screenshot: captured {} bytes PNG", outIdx);
    return 1;
}

void uz_free_buffer(uint8_t* data) {
    std::free(data);
}

// ── H.264 Frame Encoding ────────────────────────────────────────────────────

int uz_encode_frame(const uint8_t** out_data, uint32_t* out_size) {
    if (out_data) *out_data = nullptr;
    if (out_size) *out_size = 0;

    if (!HasViewport() || !out_data || !out_size) return 0;

    auto* state = s_state;
    if (!state->h264Encoder.IsInitialized()) return 0;
    if (!state->frameReadback.IsInitialized()) return 0;

    // The GPU readback copy was recorded inside uz_render_frame() via the
    // pre-End callback (same command list, same submission — no separate
    // command list, no WaitIdle, no post-Present backbuffer ambiguity).
    // Here we only read the oldest triple-buffered readback and encode it.
    if (state->frameReadback.IsReadReady()) {
        const uint8_t* rgbaData = nullptr;
        uint32_t rgbaStride = 0;
        if (state->frameReadback.MapRead(&rgbaData, &rgbaStride)) {
            state->h264Encoder.SubmitFrameAsync(rgbaData, rgbaStride);
            state->frameReadback.UnmapRead();
            state->frameReadback.AdvanceRead();
        }
    }
    return state->h264Encoder.GetEncodedFrame(out_data, out_size) ? 1 : 0;
}

void uz_request_keyframe(void) {
    if (!HasViewport()) return;
    s_state->h264Encoder.RequestKeyframe();
}

void uz_set_encode_bitrate(uint32_t kbps) {
    if (!HasViewport()) return;
    s_state->h264Encoder.SetBitrate(kbps);
}

int uz_get_codec_extradata(const uint8_t** out_data, uint32_t* out_size) {
    if (out_data) *out_data = nullptr;
    if (out_size) *out_size = 0;

    if (!HasViewport() || !out_data || !out_size) return 0;

    auto* state = s_state;
    if (!state->h264Encoder.IsInitialized()) return 0;

    return state->h264Encoder.GetExtradata(out_data, out_size) ? 1 : 0;
}

// ── Editor Configuration ────────────────────────────────────────────────────

void uz_set_editor_mode(int enabled) {
    if (!HasViewport()) return;
    s_state->editorModeRequested = (enabled != 0);
    // Editor mode and play mode are mutually exclusive. Enabling editor mode
    // suspends all gameplay scripts — the built-in FlyCameraController is the
    // only active controller, as in Unreal / Unity / Godot editors.
    uz::ScriptSystem::SetPlayMode(enabled == 0);
    s_state->playMode = (enabled == 0);
    if (s_state->sceneRendererReady) {
        s_state->sceneRenderer.SetEditorMode(enabled != 0);

        // Initialize editor input handler (selection, gizmos) on first enable.
        if (enabled && !s_state->editorInputReady) {
            s_state->editorInput.Init(
                s_state->sceneRenderer.GetInput(), &s_state->selectionManager,
                &s_state->sceneRenderer.GetCamera(), &s_state->world);
            s_state->editorInput.SetSceneRenderer(&s_state->sceneRenderer);
            s_state->sceneRenderer.SetEditorInputHandler(&s_state->editorInput);
            s_state->editorInputReady = true;
            UZ_LOG(RuntimeAPI, Info, "Editor input handler initialized (selection + gizmos)");

            if (s_state->luaReady) {
                uz::LuaSetEditorState(uz::LuaVM::GetState(),
                    &s_state->selectionManager, &s_state->editorInput);
            }
        }
    }
}

void uz_set_content_root(const char* path) {
    if (!path) return;

    if (HasViewport()) {
        // Viewport exists — recreate VFS with the new root.
        s_state->contentRoot = path;
        s_state->vfs = std::make_unique<uz::LooseFileVFS>(path);

        // Update Lua's VFS reference.
        if (s_state->luaReady) {
            uz::LuaSetVFS(uz::LuaVM::GetState(), s_state->vfs.get());
        }

        UZ_LOG(RuntimeAPI, Info, "Content root set to: {}", path);
    } else {
        // Before viewport creation — store for later consumption.
        s_pendingContentRoot = path;
    }
}

} // extern "C"
