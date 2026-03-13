#pragma once

/// UzEngine Drag & Drop — OLE IDropTarget implementation for file drops.
///
/// Registers a window as an OLE drag-drop target. Supports file path extraction
/// from shell drops (CF_HDROP) with UTF-8 path conversion.
///
/// # Lifecycle
/// 1. Call `EnableDragDrop(hwnd, callback, userData)` after window creation.
/// 2. Receive file drops via the callback.
/// 3. Call `DisableDragDrop(hwnd)` before window destruction.
///
/// # OLE Initialization
/// `EnableDragDrop` calls `OleInitialize(nullptr)` if OLE is not yet initialized
/// on the calling thread. This is safe to call multiple times (returns S_FALSE).
///
/// # Thread Safety
/// Must be called from the thread that created the window (main thread).

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Payload delivered when files are dropped onto the window.
struct DropData {
    std::vector<std::string> filePaths; ///< Dropped file paths (UTF-8 encoded).
    float mouseX = 0.0f;               ///< Drop X position in client coordinates.
    float mouseY = 0.0f;               ///< Drop Y position in client coordinates.
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
/// * `mouseX` — Current hover X in client coordinates.
/// * `mouseY` — Current hover Y in client coordinates.
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragHoverCallback = void(*)(float mouseX, float mouseY, void* userData);

/// Callback invoked when a drag leaves the window without dropping.
///
/// # Arguments
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragLeaveCallback = void(*)(void* userData);

/// Registers a window as an OLE drag-drop target for file drops.
///
/// Creates an internal `IDropTarget` COM object and calls `RegisterDragDrop`.
/// Only one drop target can be active per HWND.
///
/// # Arguments
/// * `hwnd` — Native window handle (HWND cast to `void*`).
/// * `onDrop` — Callback for file drop events. Must not be nullptr.
/// * `userData` — Opaque pointer forwarded to callbacks.
///
/// # Returns
/// - `true` if drag-drop was successfully registered.
/// - `false` on failure (logs `DragDrop::Error`).
[[nodiscard]] bool EnableDragDrop(void* hwnd, FileDropCallback onDrop, void* userData);

/// Sets optional hover and leave callbacks for drag visual feedback.
///
/// Must be called after `EnableDragDrop`. These callbacks are optional —
/// if not set, drags will still work but no hover feedback is provided.
///
/// # Arguments
/// * `onHover` — Called during DragOver with mouse position, or nullptr to clear.
/// * `onLeave` — Called on DragLeave, or nullptr to clear.
/// * `userData` — Opaque pointer forwarded to hover/leave callbacks.
void SetDragHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave, void* userData);

/// Unregisters drag-drop from a window.
///
/// Calls `RevokeDragDrop` and releases the internal COM object.
/// Safe to call if drag-drop was never enabled (no-op).
///
/// # Arguments
/// * `hwnd` — Native window handle (HWND cast to `void*`).
void DisableDragDrop(void* hwnd);

} // namespace uz
