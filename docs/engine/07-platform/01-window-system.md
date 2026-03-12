# Platform — Window System

> **Status**: Done (Milestone 1.2)
> **Source**: `platform/windows/platform_window.h/.cpp`
> **Depends on**: Win32 API

## Purpose

Every rendering engine needs a window — a native OS surface to present GPU output. UzEngine uses `PlatformWindow`, a thin wrapper over Win32's HWND that handles creation, message pumping, resize tracking, and input dispatch.

The window is intentionally minimal: it creates a surface, pumps OS messages, and reports state. It does **not** manage rendering, swap chains, or input mapping — those are separate systems that consume the window's output.

## Architecture

```
PlatformWindow
    │
    ├── Create(WindowDesc)     → RegisterClass + CreateWindowExA → HWND
    ├── PumpMessages()         → PeekMessage loop (non-blocking)
    │   ├── WM_SIZE            → sets m_resized = true, updates m_width/m_height
    │   ├── WM_CLOSE           → sets m_open = false
    │   ├── WM_KEYDOWN/UP      → forwards to InputSystem::OnKeyDown/Up
    │   ├── WM_MOUSEMOVE       → forwards to InputSystem::OnMouseMove
    │   ├── WM_LBUTTONDOWN/UP  → forwards to InputSystem::OnMouseButton
    │   └── WM_MOUSEWHEEL      → forwards to InputSystem::OnMouseScroll
    ├── Show() / Hide()        → ShowWindow(SW_SHOW / SW_HIDE)
    ├── Destroy()              → DestroyWindow + cleanup
    └── Queries
        ├── IsOpen()           → window not closed
        ├── WasResized()       → WM_SIZE received since last clear
        ├── GetWidth/Height()  → current client area size
        └── GetHWND()          → raw handle for swap chain creation
```

## Window Creation

```cpp
WindowDesc desc{};
desc.title      = "UzEngine";
desc.width      = 1280;
desc.height     = 720;
desc.resizable  = true;
desc.fullscreen = false;

PlatformWindow window;
window.Create(desc);
```

The `WindowDesc` struct provides sensible defaults — 1280x720 resizable windowed mode. Fullscreen transitions the window to borderless fullscreen on the primary monitor.

### Win32 Details

Internally, `Create()`:
1. Calls `RegisterClassExA` with a static WndProc
2. Calls `CreateWindowExA` with `WS_OVERLAPPEDWINDOW` style (or `WS_POPUP` for fullscreen)
3. Stores `this` pointer via `SetWindowLongPtr(GWLP_USERDATA)` so the static WndProc can dispatch to the instance
4. Calls `ShowWindow(SW_SHOW)` and `UpdateWindow()`

## Message Pump

```cpp
while (window.IsOpen()) {
    window.PumpMessages();    // Non-blocking — drains all pending messages

    if (window.WasResized()) {
        device->WaitIdle();
        swapChain->Resize(window.GetWidth(), window.GetHeight());
        window.ClearResizedFlag();
    }

    // ... render frame ...
}
```

`PumpMessages()` uses `PeekMessage` (not `GetMessage`) for non-blocking behavior — the render loop never stalls waiting for input. This is critical for maintaining high frame rates.

### Resize Handling

When the user resizes the window:
1. Win32 sends `WM_SIZE` with new width/height
2. WndProc sets `m_resized = true` and stores new dimensions
3. Next frame, the render loop detects `WasResized()` and recreates the swap chain
4. The loop calls `ClearResizedFlag()` to reset

`Resize(0, 0)` (minimized window) is a no-op at the swap chain level — the engine skips rendering while minimized.

## Input Integration

The window optionally routes OS messages to an `InputSystem`:

```cpp
InputSystem input;
input.Init();
window.SetInputSystem(&input);

// Now WM_KEY*/WM_MOUSE* messages are forwarded automatically
// via PumpMessages() → WndProc → InputSystem::OnKey*/OnMouse*
```

This decouples the window from input processing — the window only knows that "something wants input events." The InputSystem handles state tracking, action mapping, and frame deltas independently.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Window API | `FGenericWindow` + per-platform (`FWindowsWindow`, `FCocoaWindow`) | `DisplayServer` singleton + `Window` node | `PlatformWindow` (Win32 only, others planned) |
| Message pump | `FWindowsApplication::PumpMessages()` | `OS_Windows::process_events()` | `PlatformWindow::PumpMessages()` |
| Resize model | `OnWindowSizedChanged` delegate chain | `MainLoop::_main_loop_resize()` | `WasResized()` flag + manual clear |
| Fullscreen | `SetWindowMode()` (Fullscreen, WindowedFullscreen, Windowed) | `Window.mode` property | `WindowDesc::fullscreen` |
| Multi-window | Yes (editor has many windows) | Yes (`Window` nodes) | **No** (single window, multi planned) |

### Why Not SDL2?

SDL2 is the standard cross-platform window/input library. UzEngine doesn't use it because:

1. **VSCode embedding** — the final target is a child HWND inside VSCode, which requires direct Win32 control. SDL2 assumes it owns the window.
2. **Minimal surface** — the engine needs ~200 lines of window code. SDL2 brings 50,000+ lines of platform abstraction for features we don't need.
3. **Input model mismatch** — UzEngine's action-based input system would wrap SDL events into its own types anyway, adding a translation layer with no benefit.

When Linux/macOS support is needed, platform-specific window implementations will be added under `platform/linux/` and `platform/macos/`.

## Rules

1. **ALWAYS call `PumpMessages()` every frame** — failure causes "not responding" in the OS taskbar
2. **ALWAYS check `WasResized()` before presenting** — stale swap chain dimensions cause GPU errors
3. **ALWAYS call `device->WaitIdle()` before swap chain resize** — GPU must not be using the old back buffers
4. **NEVER block in the message pump** — use `PeekMessage`, not `GetMessage`
5. **NEVER access the HWND from threads other than the main thread** — Win32 windows are thread-affine
