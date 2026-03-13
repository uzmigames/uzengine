# Platform — Clipboard & Drag-Drop

> **Status**: Implemented
> **Source**: `platform/windows/clipboard.h`, `platform/windows/drag_drop.h`
> **Depends on**: Win32 clipboard API (CF_UNICODETEXT, custom formats), OLE `IDropTarget`, `WM_DROPFILES` fallback

## Purpose

The editor needs clipboard (copy/paste entities, properties, shader snippets) and drag-drop (drag assets into viewport, drag files from Explorer). Without these, every asset import requires manual path entry. UzEngine integrates both via Win32 clipboard API and OLE drag-drop, with a simple `WM_DROPFILES` fallback for non-OLE sources.

## Architecture

```
PlatformWindow
    │
    ├── Clipboard System
    │   ├── SetClipboardText()      → Win32 clipboard
    │   ├── GetClipboardText()      → read Unicode text
    │   ├── SetClipboardData()      → custom format (msgpack, JSON)
    │   └── GetClipboardData()      → retrieve bytes
    │
    ├── OLE Drag-Drop (IDropTarget)
    │   ├── DragEnter() → check format, show cursor feedback
    │   ├── DragOver()  → track mouse, update drop hint
    │   ├── DragLeave() → hide visual feedback
    │   └── Drop()      → finalize drop, invoke callback
    │
    └── Fallback WM_DROPFILES
        └── Simple file drop for Explorer drags
```

## Clipboard API

The clipboard module provides UTF-8 text and custom binary format support via Win32 clipboard APIs. All functions open/close the clipboard internally — callers do not manage clipboard state.

### Text Operations

```cpp
/// Copies UTF-8 text to the system clipboard.
///
/// Converts the input from UTF-8 to UTF-16 (CF_UNICODETEXT).
/// Replaces any existing clipboard contents.
///
/// # Arguments
/// * `text` — Null-terminated UTF-8 string. Must not be nullptr.
///
/// # Returns
/// - `true` if successfully placed on clipboard.
/// - `false` if clipboard could not be opened or allocation failed.
///   Logs `Clipboard::Error` with Win32 error code on failure.
[[nodiscard]] bool ClipboardSetText(const char* text);

/// Retrieves UTF-8 text from the system clipboard.
///
/// Reads CF_UNICODETEXT data and converts it to UTF-8 std::string.
///
/// # Returns
/// - The clipboard text as a UTF-8 string on success.
/// - Empty string if clipboard does not contain text or cannot be opened.
[[nodiscard]] std::string ClipboardGetText();
```

### Custom Format Operations

```cpp
/// Copies binary data to clipboard under a custom named format.
///
/// Registers (or retrieves) a clipboard format via RegisterClipboardFormatW
/// and stores the raw bytes. Replaces any existing clipboard contents.
///
/// # Arguments
/// * `formatName` — UTF-8 format identifier (e.g., "UzEngine/SceneNode").
///   Must not be nullptr.
/// * `data` — Raw bytes to store. Must not be nullptr if `size > 0`.
/// * `size` — Byte count. Zero is valid (no-op, returns true).
///
/// # Returns
/// - `true` on success.
/// - `false` on failure (logs `Clipboard::Error`).
[[nodiscard]] bool ClipboardSetData(const char* formatName, const void* data, size_t size);

/// Retrieves binary data from clipboard for a custom named format.
///
/// # Arguments
/// * `formatName` — UTF-8 format identifier (must match a prior SetData call).
///
/// # Returns
/// - Vector containing the raw bytes on success.
/// - Empty vector if format not present or clipboard cannot be opened.
[[nodiscard]] std::vector<uint8_t> ClipboardGetData(const char* formatName);
```

Example usage in editor:

```cpp
// Copy entity to clipboard
Entity entity = g_scene->GetSelectedEntity();
msgpack::object_handle oh = msgpack::pack(entity);
uz::ClipboardSetData("UzEngine/Entity",
    oh.get().data(), oh.get().size());

// Paste entity from clipboard
auto blob = uz::ClipboardGetData("UzEngine/Entity");
if (!blob.empty()) {
    Entity pasted = msgpack::unpack<Entity>(blob.data(), blob.size());
    g_scene->AddEntity(pasted);
}

// Simple text copy/paste
uz::ClipboardSetText("Hello, World!");
std::string text = uz::ClipboardGetText();
```

### Thread Safety

Must be called from the thread that owns the window (typically main thread). Win32 clipboard is inherently single-access.

## OLE Drag-Drop Registration

UzEngine registers windows as OLE drag-drop targets via `IDropTarget` COM interface, supporting file drops (CF_HDROP) and custom data formats. Visual feedback is provided through hover and leave callbacks.

### Drop Event Handling

```cpp
/// Payload delivered when files are dropped onto the window.
struct DropData {
    std::vector<std::string> filePaths; ///< Dropped file paths (UTF-8).
    float mouseX = 0.0f;                ///< Drop X in client coordinates.
    float mouseY = 0.0f;                ///< Drop Y in client coordinates.
};

/// Callback invoked when files are dropped onto the window.
///
/// # Arguments
/// * `data` — Drop payload with file paths and position.
/// * `userData` — Opaque pointer passed to `EnableDragDrop`.
using FileDropCallback = void(*)(const DropData& data, void* userData);

/// Callback invoked during drag hover (for visual feedback).
///
/// # Arguments
/// * `mouseX` — Hover X in client coordinates.
/// * `mouseY` — Hover Y in client coordinates.
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragHoverCallback = void(*)(float mouseX, float mouseY, void* userData);

/// Callback invoked when drag leaves the window without dropping.
///
/// # Arguments
/// * `userData` — Opaque pointer passed to `SetDragHoverCallbacks`.
using DragLeaveCallback = void(*)(void* userData);
```

### Registration Functions

```cpp
/// Registers a window as an OLE drag-drop target for file drops.
///
/// Creates an internal IDropTarget COM object and calls RegisterDragDrop().
/// Only one drop target can be active per HWND.
///
/// # OLE Initialization
/// EnableDragDrop calls OleInitialize(nullptr) if OLE is not yet initialized
/// on the calling thread. This is safe to call multiple times (returns S_FALSE).
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

/// Sets optional hover and leave callbacks for visual feedback.
///
/// Must be called after `EnableDragDrop`. These callbacks are optional —
/// if not set, drags still work but no hover feedback is provided.
///
/// # Arguments
/// * `onHover` — Called during DragOver with mouse position, or nullptr to clear.
/// * `onLeave` — Called on DragLeave, or nullptr to clear.
/// * `userData` — Opaque pointer forwarded to hover/leave callbacks.
void SetDragHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave, void* userData);

/// Unregisters drag-drop from a window.
///
/// Calls RevokeDragDrop and releases the internal COM object.
/// Safe to call if drag-drop was never enabled (no-op).
///
/// # Arguments
/// * `hwnd` — Native window handle (HWND cast to `void*`).
void DisableDragDrop(void* hwnd);
```

### Thread Safety

Must be called from the thread that created the window (main thread). OLE and Win32 drag-drop APIs are thread-local.

## File Drop Handling

The drag-drop system extracts file paths from CF_HDROP (Explorer drags) and converts them to UTF-8 for cross-platform consistency. Callbacks provide drop positions for viewport integration.

### Usage Example

```cpp
// Enable drag-drop on window after creation
void MyEditor::OnWindowCreated(void* hwnd) {
    uz::EnableDragDrop(hwnd, OnFilesDropped, this);
    uz::SetDragHoverCallbacks(OnDragOver, OnDragLeave, this);
}

// Handle file drops with position
void MyEditor::OnFilesDropped(const uz::DropData& data, void* userData) {
    MyEditor* pThis = reinterpret_cast<MyEditor*>(userData);

    for (const auto& path : data.filePaths) {
        if (path.ends_with(".gltf") || path.ends_with(".glb")) {
            pThis->ImportMesh(path, {data.mouseX, data.mouseY});
        } else if (path.ends_with(".png") || path.ends_with(".jpg")) {
            pThis->ImportTexture(path);
        }
    }
}

// Provide visual feedback during hover
void MyEditor::OnDragHover(float mouseX, float mouseY, void* userData) {
    MyEditor* pThis = reinterpret_cast<MyEditor*>(userData);
    pThis->m_viewport->SetDropHint({mouseX, mouseY});
    pThis->m_viewport->SetDropZoneActive(true);
}

// Hide visual feedback on leave
void MyEditor::OnDragLeave(void* userData) {
    MyEditor* pThis = reinterpret_cast<MyEditor*>(userData);
    pThis->m_viewport->SetDropZoneActive(false);
}

// Cleanup on window destruction
void MyEditor::OnWindowDestroyed(void* hwnd) {
    uz::DisableDragDrop(hwnd);
}
```

## Implementation Details

### Win32 Integration

The implementation uses two mechanisms for file drops:

1. **OLE IDropTarget** (primary) — Full COM-based drag-drop with format negotiation
   - Registers via `RegisterDragDrop()` after window creation
   - Supports custom formats via `RegisterClipboardFormat()`
   - Provides DragEnter/Over/Leave events for visual feedback
   - OleInitialize called internally (safe to call multiple times)

2. **WM_DROPFILES** (fallback) — Simple Explorer drag fallback
   - Enabled via `DragAcceptFiles(hwnd, TRUE)` for compatibility
   - Fires when non-OLE sources drag files
   - Extracts paths via `DragQueryFile()` and converts to UTF-8

### Code Flow

```
1. EnableDragDrop(hwnd, onDrop, userData)
   ├─ OleInitialize(nullptr)  [if not already done]
   ├─ Create IDropTarget COM object
   └─ RegisterDragDrop(hwnd, pDropTarget)

2. User drags files over window
   └─ DragEnter → DragOver* → (Drop OR DragLeave)

3. Drop event or WM_DROPFILES
   ├─ Extract CF_HDROP to filePaths
   ├─ Convert WCHAR paths to UTF-8
   └─ Invoke FileDropCallback with DropData

4. DisableDragDrop(hwnd)
   ├─ RevokeDragDrop(hwnd)
   └─ Release COM reference
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Clipboard | `FGenericPlatformMisc::ClipboardCopy/Paste` | `DisplayServer::clipboard_set/get_primary` | `SetClipboardText()`, `GetClipboardText()` |
| Custom format | `ClipboardCopy(FString, FString format)` | Custom via system | `SetClipboardData(format_name, bytes)` |
| Drag-drop | `FDropTarget` (IDropTarget COM) | `get_drop_files` signal + Godot-internal | `IDropTarget` + `WM_DROPFILES` |
| Visual feedback | Drop zone highlight | Automatic | `OnDragEnter/Over/Leave()` callbacks |
| Editor use | Copy/paste actors, materials, keyframes | Copy/paste scenes, objects | Copy/paste entities, components, shaders |

## Rules

1. **ALWAYS call EnableDragDrop after window creation** — dropped files before registration are silently ignored
2. **ALWAYS call DisableDragDrop before window destruction** — OLE resources must be cleaned up (RevokeDragDrop called internally)
3. **ALWAYS handle UTF-8 paths** — file paths are already converted, do not double-convert
4. **NEVER share FileDropCallback across multiple windows** — each HWND gets its own registration
5. **ALWAYS check if clipboard/drag operations succeed** — return values indicate Win32 API failures (e.g., clipboard locked by another app)
