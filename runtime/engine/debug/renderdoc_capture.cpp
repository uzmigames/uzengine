/// RenderDoc In-Application API — Implementation.
///
/// Loads `renderdoc.dll` at runtime (no link-time dependency) and exposes a
/// minimal, safe surface for triggering GPU captures from within the engine.
///
/// All functions degrade gracefully to no-ops when RenderDoc is absent.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "renderdoc_capture.h"

// renderdoc_app.h lives in thirdparty/renderdoc/.
// build.zig registers thirdparty/renderdoc/ as an include path so the
// header is found by its base name, consistent with other thirdparty libs
// (e.g. stb_image.h, tracy/Tracy.hpp, etc.).
#include "renderdoc_app.h"

#include "../logging/log.h"

UZ_DEFINE_LOG_CATEGORY(RenderDoc, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Module-level state — init once, never re-entrant
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32

namespace {

/// The loaded RenderDoc function table. Null when RenderDoc is not available.
RENDERDOC_API_1_6_0* s_rdocApi = nullptr;

/// The module handle returned by LoadLibraryA or GetModuleHandleA.
/// Kept so we can FreeLibrary on shutdown (only when WE loaded it;
/// when RenderDoc injected itself GetModuleHandle doesn't increment the refcount,
/// so we track ownership separately).
HMODULE s_rdocModule = nullptr;

/// True when we called LoadLibraryA and own the refcount on s_rdocModule.
bool s_ownsModule = false;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// RenderDocInit
// ─────────────────────────────────────────────────────────────────────────────

bool RenderDocInit() noexcept {
    // Already loaded — idempotent.
    if (s_rdocApi) {
        return true;
    }

    // Step 1: Check if RenderDoc already mapped the DLL into our process.
    //
    // When the application is launched from the RenderDoc GUI, renderdoc.dll
    // is injected before our code runs. GetModuleHandleA finds it without
    // touching disk or incrementing the refcount — we must NOT FreeLibrary
    // this handle.
    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (mod) {
        UZ_LOG(RenderDoc, Debug,
               "renderdoc.dll already loaded in process (launched from RenderDoc GUI)");
        s_rdocModule = mod;
        s_ownsModule = false;
    }

    // Step 2: Do NOT auto-load renderdoc.dll from disk.
    // Loading the DLL when not launched from the GUI injects hooks that cause
    // visual artifacts (black triangles, diagonal stripes). Only use the DLL
    // if it's already injected (Step 1) or loaded via renderdoccmd capture.
    // To capture standalone, use: renderdoccmd capture -w demo.exe

    if (!s_rdocModule) {
        // RenderDoc is simply not installed or not active — completely normal.
        UZ_LOG(RenderDoc, Trace,
               "renderdoc.dll not found — RenderDoc capture unavailable");
        return false;
    }

    // Step 4: Acquire the API function table.
    //
    // RENDERDOC_GetAPI is the single exported entry point. It fills a pointer
    // to the version-matched function table struct.
    using pGetAPI = pRENDERDOC_GetAPI;
    const auto getAPI = reinterpret_cast<pGetAPI>(
        GetProcAddress(s_rdocModule, "RENDERDOC_GetAPI"));

    if (!getAPI) {
        UZ_LOG(RenderDoc, Error,
               "renderdoc.dll found but RENDERDOC_GetAPI export is missing — "
               "module may be corrupted or is an incompatible version");
        if (s_ownsModule) {
            FreeLibrary(s_rdocModule);
            s_ownsModule = false;
        }
        s_rdocModule = nullptr;
        return false;
    }

    // Request version 1.6.0. RenderDoc returns a higher-or-equal version
    // that is ABI-compatible, so this succeeds even on newer RenderDoc builds.
    const int result = getAPI(
        eRENDERDOC_API_Version_1_6_0,
        reinterpret_cast<void**>(&s_rdocApi));

    if (result != 1 || !s_rdocApi) {
        UZ_LOG(RenderDoc, Error,
               "RENDERDOC_GetAPI returned {} — API version 1.6.0 not satisfied",
               result);
        if (s_ownsModule) {
            FreeLibrary(s_rdocModule);
            s_ownsModule = false;
        }
        s_rdocModule = nullptr;
        s_rdocApi    = nullptr;
        return false;
    }

    // Retrieve the actual version for diagnostic logging.
    int major = 0, minor = 0, patch = 0;
    s_rdocApi->GetAPIVersion(&major, &minor, &patch);

    UZ_LOG(RenderDoc, Info,
           "RenderDoc API {}.{}.{} ready — captures available (F12 or TriggerCapture())",
           major, minor, patch);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RenderDocShutdown
// ─────────────────────────────────────────────────────────────────────────────

void RenderDocShutdown() noexcept {
    s_rdocApi = nullptr;

    if (s_ownsModule && s_rdocModule) {
        FreeLibrary(s_rdocModule);
        UZ_LOG(RenderDoc, Debug, "renderdoc.dll unloaded");
    }

    s_rdocModule = nullptr;
    s_ownsModule = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Status queries
// ─────────────────────────────────────────────────────────────────────────────

bool RenderDocIsLoaded() noexcept {
    return s_rdocApi != nullptr;
}

bool RenderDocIsConnected() noexcept {
    if (!s_rdocApi) return false;
    return s_rdocApi->IsTargetControlConnected() != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture options
// ─────────────────────────────────────────────────────────────────────────────

void RenderDocSetCaptureOptions() noexcept {
    if (!s_rdocApi) return;

    // Enable D3D12 / Vulkan validation messages inside the capture.
    // These appear as events in the RenderDoc event browser.
    s_rdocApi->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 1);

    // Include all live GPU resources in every capture, not just the ones
    // referenced by the captured frame. This is essential when debugging
    // resource state issues or persistent descriptor heaps.
    s_rdocApi->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources, 1);

    // Record CPU callstacks for every GPU event. This lets you click any
    // draw call in RenderDoc and immediately see the C++ call stack that
    // issued it — critical for tracing render pass submissions.
    s_rdocApi->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 1);

    // Keep API debug messages visible (not muted) so they appear alongside
    // GPU events. Without this, D3D12 debug layer messages are suppressed.
    s_rdocApi->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);

    UZ_LOG(RenderDoc, Debug,
           "Capture options set: APIValidation=1, RefAllResources=1, "
           "CaptureCallstacks=1, DebugOutputMute=0");
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-frame capture
// ─────────────────────────────────────────────────────────────────────────────

void RenderDocTriggerCapture() noexcept {
    if (!s_rdocApi) return;
    UZ_LOG(RenderDoc, Info, "Triggering single-frame RenderDoc capture");
    s_rdocApi->TriggerCapture();
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-frame capture
// ─────────────────────────────────────────────────────────────────────────────

void RenderDocStartCapture(void* device, void* window) noexcept {
    if (!s_rdocApi) return;
    UZ_LOG(RenderDoc, Info,
           "Starting RenderDoc frame capture: device={}, window={}",
           device, window);
    s_rdocApi->StartFrameCapture(device, window);
}

bool RenderDocEndCapture(void* device, void* window) noexcept {
    if (!s_rdocApi) return false;
    const uint32_t ok = s_rdocApi->EndFrameCapture(device, window);
    if (ok) {
        // Query the path of the most recent capture for the log message.
        const uint32_t numCaptures = s_rdocApi->GetNumCaptures();
        if (numCaptures > 0) {
            char capturePath[1024] = {};
            uint32_t pathLen       = static_cast<uint32_t>(sizeof(capturePath));
            uint64_t timestamp     = 0;
            s_rdocApi->GetCapture(numCaptures - 1, capturePath, &pathLen, &timestamp);
            UZ_LOG(RenderDoc, Info,
                   "RenderDoc capture saved: path='{}'", capturePath);
        } else {
            UZ_LOG(RenderDoc, Info, "RenderDoc capture complete");
        }
    } else {
        UZ_LOG(RenderDoc, Warning,
               "RenderDoc EndFrameCapture returned 0 — capture may have failed or "
               "was not in progress (device={}, window={})", device, window);
    }
    return ok != 0;
}

bool RenderDocIsCapturing() noexcept {
    if (!s_rdocApi) return false;
    return s_rdocApi->IsFrameCapturing() != 0;
}

#else // !_WIN32

// ─────────────────────────────────────────────────────────────────────────────
// Non-Windows stubs — RenderDoc is Windows-only at this time.
// Every function is a trivial no-op so callers need no #ifdef guards.
// ─────────────────────────────────────────────────────────────────────────────

bool RenderDocInit()                               noexcept { return false; }
void RenderDocShutdown()                           noexcept {}
bool RenderDocIsLoaded()                           noexcept { return false; }
bool RenderDocIsConnected()                        noexcept { return false; }
void RenderDocSetCaptureOptions()                  noexcept {}
void RenderDocTriggerCapture()                     noexcept {}
void RenderDocStartCapture(void*, void*)           noexcept {}
bool RenderDocEndCapture(void*, void*)             noexcept { return false; }
bool RenderDocIsCapturing()                        noexcept { return false; }

#endif // _WIN32

} // namespace uz
