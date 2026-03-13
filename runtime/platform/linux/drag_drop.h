#pragma once

/// UzEngine Drag & Drop — XDnd (XDND protocol v5) implementation for file drops.
///
/// Registers a window as an XDnd target. Supports file path extraction from
/// `text/uri-list` selections with URL decoding and `file://` URI stripping.
///
/// # Lifecycle
/// 1. Call `EnableDragDrop(nativeWindow, callback, userData)` after window creation.
///    `nativeWindow` is the X11 `Window` (unsigned long) cast to `void*` via `reinterpret_cast`,
///    as returned by `PlatformWindow::GetNativeHandle()`.
/// 2. In the X11 event loop (inside `PlatformWindow::PumpMessages`), call
///    `ProcessDragDropEvent(&event)` for every `XEvent` received.
/// 3. Receive file drops via the registered `FileDropCallback`.
/// 4. Call `DisableDragDrop(nativeWindow)` before window destruction.
///
/// # XDnd Event Loop Integration
/// `ProcessDragDropEvent` MUST be called for every XEvent in the application's
/// X11 event loop. It handles ClientMessage and SelectionNotify events that
/// carry XDnd protocol messages. Events unrelated to drag-and-drop are ignored.
///
/// # Example
/// ```cpp
/// // Setup
/// EnableDragDrop(window.GetNativeHandle(), onFileDrop, nullptr);
///
/// // Inside event loop
/// while (XPending(display)) {
///     XEvent ev{};
///     XNextEvent(display, &ev);
///     ProcessDragDropEvent(&ev);   // forward every event
///     // ... other event handling
/// }
///
/// // Teardown
/// DisableDragDrop(window.GetNativeHandle());
/// ```
///
/// # Thread Safety
/// Must be called from the thread that owns the X11 display connection (main thread).

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Payload delivered when files are dropped onto the window.
struct DropData {
    std::vector<std::string> filePaths; ///< Dropped file paths (UTF-8 encoded, absolute).
    float mouseX = 0.0F;               ///< Drop X position in window-local coordinates.
    float mouseY = 0.0F;               ///< Drop Y position in window-local coordinates.
};

/// Callback invoked when one or more files are dropped onto the window.
///
/// # Arguments
/// * `data` — Drop payload containing file paths and mouse position.
/// * `userData` — Opaque pointer passed to `EnableDragDrop`.
using FileDropCallback = void(*)(const DropData& data, void* userData);

/// Callback invoked while a drag hovers over the window (for visual feedback).
///
/// # Arguments
/// * `mouseX` — Current hover X in window-local coordinates.
/// * `mouseY` — Current hover Y in window-local coordinates.
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragHoverCallback = void(*)(float mouseX, float mouseY, void* userData);

/// Callback invoked when a drag leaves the window without dropping.
///
/// # Arguments
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragLeaveCallback = void(*)(void* userData);

/// Registers a window as an XDnd drag-drop target for file drops.
///
/// Sets the `XdndAware` property (version 5) on the window and records the
/// callbacks. The Display is retrieved from X11 by opening a separate connection
/// to `$DISPLAY` using `XOpenDisplay(nullptr)` — the same display the window
/// was created on.
///
/// Only one window is supported at a time. Calling `EnableDragDrop` a second
/// time replaces the previous registration.
///
/// # Arguments
/// * `nativeWindow` — X11 `Window` cast to `void*` (from `PlatformWindow::GetNativeHandle()`).
/// * `onDrop` — Callback for file drop events. Must not be nullptr.
/// * `userData` — Opaque pointer forwarded to the drop callback.
///
/// # Returns
/// - `true` if registration succeeded.
/// - `false` on failure (logs `DragDrop::Error`).
[[nodiscard]] bool EnableDragDrop(void* nativeWindow, FileDropCallback onDrop, void* userData);

/// Sets optional hover and leave callbacks for drag visual feedback.
///
/// Must be called after `EnableDragDrop`. These callbacks are optional —
/// if not set, drags will still work but no hover feedback is provided.
///
/// # Arguments
/// * `onHover` — Called during XdndPosition with current mouse position, or nullptr to clear.
/// * `onLeave` — Called on XdndLeave (drag cancelled), or nullptr to clear.
/// * `userData` — Opaque pointer forwarded to both callbacks.
void SetDragHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave, void* userData);

/// Unregisters drag-drop from a window.
///
/// Removes the `XdndAware` property from the window and clears all callbacks.
/// Safe to call if drag-drop was never enabled (no-op).
///
/// # Arguments
/// * `nativeWindow` — X11 `Window` cast to `void*` (from `PlatformWindow::GetNativeHandle()`).
void DisableDragDrop(void* nativeWindow);

/// Processes a single X11 event for XDnd protocol messages.
///
/// Must be called for every `XEvent` received in the application's X11 event loop.
/// Internally handles the following XDnd sequence:
/// - `XdndEnter`    — Checks if `text/uri-list` is offered.
/// - `XdndPosition` — Replies with `XdndStatus` (accept/reject), fires hover callback.
/// - `XdndLeave`    — Fires leave callback, resets state.
/// - `XdndDrop`     — Calls `XConvertSelection` to request `text/uri-list` data.
/// - `SelectionNotify` — Reads selection data, parses URIs, fires drop callback,
///                       sends `XdndFinished` to the source.
///
/// Events that are not XDnd messages are ignored (safe to pass all events).
///
/// # Arguments
/// * `xevent` — Pointer to the `XEvent` to process. Must not be nullptr.
///
/// # Example — integration inside PlatformWindow::PumpMessages
/// ```cpp
/// while (XPending(m_display)) {
///     XEvent ev{};
///     XNextEvent(m_display, &ev);
///     ProcessDragDropEvent(&ev);
///     switch (ev.type) { /* ... */ }
/// }
/// ```
void ProcessDragDropEvent(void* xevent);

} // namespace uz
