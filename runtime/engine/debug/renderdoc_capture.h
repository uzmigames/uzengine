/// RenderDoc In-Application API Integration for UzEngine.
///
/// Provides a thin, zero-overhead wrapper around the RenderDoc in-application
/// API (`RENDERDOC_API_1_6_0`). The DLL is loaded at runtime via `LoadLibrary`
/// so that UzEngine has no link-time dependency on RenderDoc — the engine runs
/// normally even when RenderDoc is not installed.
///
/// # Loading Strategy
/// RenderDoc works by intercepting GPU API calls. For its hooks to be in place
/// before the D3D12/Vulkan device is created, the DLL **must** be loaded (and
/// the API pointer acquired) before any RHI device is constructed. When the
/// application is launched from the RenderDoc GUI, `renderdoc.dll` is already
/// mapped into the process; `GetModuleHandleA` finds it without a disk load.
/// When launched standalone, `LoadLibraryA` looks for the DLL in the default
/// RenderDoc install path (`C:\Program Files\RenderDoc\`).
///
/// # Thread Safety
/// All functions must be called from the main thread. The RenderDoc API itself
/// is not thread-safe for these control functions.
///
/// # Usage
/// ```cpp
/// // Before device creation
/// RenderDocInit();
///
/// // In the render loop
/// if (RenderDocIsConnected()) {
///     RenderDocTriggerCapture();  // or StartCapture/EndCapture
/// }
///
/// // At shutdown
/// RenderDocShutdown();
/// ```

#pragma once

#include <cstdint>

// RenderDoc integration is Windows-only (LoadLibrary / GetModuleHandleA).
// On other platforms every function compiles to a trivial no-op inline.
#ifdef _WIN32
#  define UZ_RENDERDOC_SUPPORTED 1
#else
#  define UZ_RENDERDOC_SUPPORTED 0
#endif

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Attempts to load `renderdoc.dll` and acquire the `RENDERDOC_API_1_6_0`
/// function table.
///
/// Call this **before** creating the RHI device so that RenderDoc's hooks are
/// installed prior to any GPU API initialisation.
///
/// The function first checks whether RenderDoc already mapped the DLL into the
/// process (i.e., the app was launched from the RenderDoc GUI). If not found
/// there, it tries `C:\Program Files\RenderDoc\renderdoc.dll`.
///
/// Calling `RenderDocInit()` multiple times is safe — it is a no-op after the
/// first successful load.
///
/// # Returns
/// `true` if the API was successfully loaded and the function table is valid.
/// `false` if `renderdoc.dll` could not be found or the API version is
/// incompatible. All other `RenderDoc*` functions become safe no-ops.
[[nodiscard]] bool RenderDocInit() noexcept;

/// Releases the RenderDoc module handle and resets the internal API pointer.
///
/// Safe to call even if `RenderDocInit()` was never called or returned `false`.
/// After this call every `RenderDoc*` function becomes a no-op again.
void RenderDocShutdown() noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Status queries
// ─────────────────────────────────────────────────────────────────────────────

/// Returns `true` if RenderDoc is loaded and the API pointer is valid.
///
/// Use this to gate any RenderDoc-specific code paths.
[[nodiscard]] bool RenderDocIsLoaded() noexcept;

/// Returns `true` if the RenderDoc UI is actively connected to this process
/// via the Target Control channel.
///
/// # Returns
/// `false` when RenderDoc is not loaded or not connected.
[[nodiscard]] bool RenderDocIsConnected() noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Capture options
// ─────────────────────────────────────────────────────────────────────────────

/// Configures common capture options for useful debugging sessions.
///
/// Sets the following options when RenderDoc is loaded:
/// - `eRENDERDOC_Option_APIValidation = 1`     — Enable D3D12/Vulkan validation during capture.
/// - `eRENDERDOC_Option_RefAllResources = 1`   — Include all live resources (not just referenced ones).
/// - `eRENDERDOC_Option_CaptureCallstacks = 1` — Record CPU callstacks for GPU events.
/// - `eRENDERDOC_Option_DebugOutputMute = 0`   — Show API debug messages (not muted).
///
/// Call once after `RenderDocInit()` succeeds, before the render loop.
/// Has no effect if RenderDoc is not loaded.
void RenderDocSetCaptureOptions() noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Single-frame capture (TriggerCapture)
// ─────────────────────────────────────────────────────────────────────────────

/// Triggers a single-frame capture on the currently active window/device pair.
///
/// The capture is queued and will execute on the next frame boundary that
/// RenderDoc determines is suitable. The resulting `.rdc` file is saved to
/// the path template configured by RenderDoc (typically the capture directory
/// set in the RenderDoc UI).
///
/// Safe to call even when RenderDoc is not loaded (no-op).
void RenderDocTriggerCapture() noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Multi-frame capture (StartFrameCapture / EndFrameCapture)
// ─────────────────────────────────────────────────────────────────────────────

/// Begins a multi-frame capture on the specified device/window pair.
///
/// Use this when you need to capture more than one frame (e.g., to capture a
/// full shadow atlas update cycle). Pair with `RenderDocEndCapture()`.
///
/// Overlapping captures (calling Start twice without End) result in undefined
/// behaviour per the RenderDoc API contract — the caller must ensure they do
/// not overlap.
///
/// # Arguments
/// * `device` — Pointer to the API root handle (`ID3D12Device*`, `VkInstance`, etc.).
///              Pass `nullptr` to match any device.
/// * `window` — OS native window handle (`HWND` on Windows, etc.).
///              Pass `nullptr` to match any window.
///
/// Has no effect if RenderDoc is not loaded.
void RenderDocStartCapture(void* device, void* window) noexcept;

/// Ends a multi-frame capture started with `RenderDocStartCapture()`.
///
/// The capture is finalised and written to disk. If no capture was in progress,
/// this is a no-op (RenderDoc API behaviour: EndFrameCapture returns 0 but
/// does not crash).
///
/// # Arguments
/// * `device` — Same device pointer passed to `RenderDocStartCapture()`.
/// * `window` — Same window handle passed to `RenderDocStartCapture()`.
///
/// # Returns
/// `true` if the capture was successfully saved, `false` on error or if
/// RenderDoc is not loaded.
[[nodiscard]] bool RenderDocEndCapture(void* device, void* window) noexcept;

/// Returns `true` if a frame capture is currently in progress.
///
/// Safe to call at any time; returns `false` when RenderDoc is not loaded.
[[nodiscard]] bool RenderDocIsCapturing() noexcept;

} // namespace uz
