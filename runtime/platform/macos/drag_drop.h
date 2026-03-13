#pragma once

/// UzEngine Drag & Drop — AppKit NSDraggingDestination implementation for file drops.
///
/// Registers an NSWindow as a drag-drop target for file URLs. Supports extraction
/// of file paths from the dragging pasteboard and reports mouse position in
/// top-left-origin window client coordinates (matching the Windows API contract).
///
/// # Lifecycle
/// 1. Call `EnableDragDrop(nsWindow, callback, userData)` after window creation.
/// 2. Receive file drops via the `FileDropCallback`.
/// 3. Optionally call `SetDragHoverCallbacks` to add hover/leave visual feedback.
/// 4. Call `DisableDragDrop(nsWindow)` before window destruction.
///
/// # Implementation Notes
/// The implementation installs an `UzDropTargetView` transparent overlay on top
/// of the window's content view. The overlay registers for `NSPasteboardTypeFileURL`
/// and implements `NSDraggingDestination`. No explicit event pump is required —
/// AppKit calls the protocol methods automatically from the main run loop.
///
/// # Thread Safety
/// All functions must be called from the main thread (AppKit requirement).

#if defined(__APPLE__)

#include <string>
#include <vector>

namespace uz {

/// Payload delivered when files are dropped onto the window.
struct DropData {
    std::vector<std::string> filePaths; ///< Dropped file paths (UTF-8 encoded, absolute POSIX paths).
    float mouseX = 0.0F;               ///< Drop X position in window client coordinates (top-left origin).
    float mouseY = 0.0F;               ///< Drop Y position in window client coordinates (top-left origin).
};

/// Callback invoked when one or more files are dropped onto the window.
///
/// # Arguments
/// * `data`     — Drop payload containing file paths and mouse position.
/// * `userData` — Opaque pointer passed to `EnableDragDrop`.
using FileDropCallback = void(*)(const DropData& data, void* userData);

/// Callback invoked while a drag hovers over the window (for visual feedback).
///
/// Called repeatedly as the drag moves, matching the rate at which AppKit fires
/// `draggingUpdated:`. Coordinates are in top-left-origin window client space.
///
/// # Arguments
/// * `mouseX`   — Current hover X in window client coordinates.
/// * `mouseY`   — Current hover Y in window client coordinates.
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragHoverCallback = void(*)(float mouseX, float mouseY, void* userData);

/// Callback invoked when a drag leaves the window without dropping.
///
/// # Arguments
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragLeaveCallback = void(*)(void* userData);

/// Registers an NSWindow as a drag-drop target for file drops.
///
/// Creates an `UzDropTargetView` overlay, installs it as the topmost subview of
/// the window's content view, and registers `NSPasteboardTypeFileURL` as the
/// accepted dragging type. Safe to call multiple times on the same window —
/// subsequent calls replace the existing registration.
///
/// # Arguments
/// * `nsWindow` — Native NSWindow pointer cast to `void*`. Must not be nullptr.
/// * `onDrop`   — Callback invoked on a completed file drop. Must not be nullptr.
/// * `userData` — Opaque pointer forwarded unchanged to `onDrop`.
///
/// # Returns
/// - `true`  if the drag-drop overlay was successfully installed.
/// - `false` on failure (logs `DragDrop::Error`). Window handle invalid or nil
///           content view are the typical failure modes.
[[nodiscard]] bool EnableDragDrop(void* nsWindow, FileDropCallback onDrop, void* userData);

/// Sets optional hover and leave callbacks for drag visual feedback.
///
/// Must be called after `EnableDragDrop`. These callbacks are optional — omitting
/// them does not affect drop delivery. Pass `nullptr` to clear an existing callback.
///
/// The `userData` supplied here is independent of the one passed to `EnableDragDrop`;
/// it is forwarded only to `onHover` and `onLeave`.
///
/// # Arguments
/// * `onHover`  — Called during `draggingUpdated:` with the current mouse position,
///               or `nullptr` to clear.
/// * `onLeave`  — Called on `draggingExited:`, or `nullptr` to clear.
/// * `userData` — Opaque pointer forwarded to hover/leave callbacks.
void SetDragHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave, void* userData);

/// Unregisters drag-drop from an NSWindow and removes the overlay view.
///
/// Removes the `UzDropTargetView` from the view hierarchy and erases the window's
/// entry from the internal callback map. Safe to call if drag-drop was never
/// enabled for the given window (no-op).
///
/// # Arguments
/// * `nsWindow` — Native NSWindow pointer cast to `void*`.
void DisableDragDrop(void* nsWindow);

} // namespace uz

#endif // __APPLE__
