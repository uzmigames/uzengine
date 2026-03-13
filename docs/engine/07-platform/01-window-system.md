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
    │   ├── WM_SIZE            → state machine + m_resized, width/height
    │   ├── WM_CLOSE           → sets m_open = false
    │   ├── WM_ACTIVATEAPP     → tracks focus state
    │   ├── WM_GETMINMAXINFO   → enforces minimum size
    │   ├── WM_KEYDOWN/UP      → forwards to InputSystem::OnKeyDown/Up
    │   ├── WM_MOUSEMOVE       → forwards to InputSystem::OnMouseMove
    │   ├── WM_LBUTTONDOWN/UP  → forwards to InputSystem::OnMouseButton
    │   └── WM_MOUSEWHEEL      → forwards to InputSystem::OnMouseScroll
    │
    ├── Window State Control
    │   ├── Minimize()         → SW_MINIMIZE (WM_SIZE dispatches state)
    │   ├── Maximize()         → SW_MAXIMIZE (WM_SIZE dispatches state)
    │   ├── Restore()          → SW_RESTORE (WM_SIZE dispatches state)
    │   ├── SetFullscreen()    → Saves windowed state, applies WS_POPUP borderless
    │   └── SetWindowed()      → Restores saved state
    │
    ├── Show() / Hide()        → ShowWindow(SW_SHOW / SW_HIDE)
    ├── Destroy()              → DestroyWindow + cleanup
    │
    └── Queries
        ├── IsOpen()           → window not closed
        ├── GetState()         → current WindowState
        ├── IsFullscreen()     → state == FullscreenBorderless
        ├── IsFocused()        → has input focus
        ├── IsMinimized()      → cannot render (0x0)
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

### DPI-Aware Creation

UzEngine window creation is DPI-aware by default. At platform startup, `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` is called, which enables per-monitor DPI detection. Initial window dimensions specified in `WindowDesc` are automatically scaled by the DPI factor of the monitor where the window appears.

For example, requesting 1280x720 on a 150% DPI monitor results in a 960x540 logical viewport with 150% scaling — sharp rendering without manual DPI calculations in the engine.

See [Multi-Monitor & DPI Awareness](03-multi-monitor-dpi.md) for full details on monitor enumeration, DPI change handling, and UI scaling patterns.

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

## Window State Machine (4.1.0)

UzEngine tracks the window's display state via the `WindowState` enum, which transitions based on user actions and OS messages:

```cpp
enum class WindowState : uint8_t {
    Normal,                ///< Standard windowed mode
    Minimized,             ///< Iconic / taskbar only
    Maximized,             ///< Fills monitor work area with title bar
    FullscreenBorderless,  ///< Covers entire monitor, no decorations
};
```

### State Transitions

The window internally calls `TransitionState(WindowState newState)`, which:
1. Detects state changes (ignores duplicate transitions)
2. Updates `m_state`
3. Invokes the optional `StateChangeCallback` if set (fires with old state, new state, and user context)
4. Logs the transition with `UZ_LOG(Window, Info, ...)`

Transitions happen via:

| Action | WM_* Handler | New State |
|--------|--------------|-----------|
| User minimizes or `Minimize()` called | WM_SIZE(SIZE_MINIMIZED) | Minimized |
| User maximizes or `Maximize()` called | WM_SIZE(SIZE_MAXIMIZED) | Maximized |
| User restores or `Restore()` called | WM_SIZE(SIZE_RESTORED) | Normal (unless FullscreenBorderless) |
| `SetFullscreen()` called | (no WM_SIZE) | FullscreenBorderless |
| `SetWindowed()` called | (no WM_SIZE) | Normal |

### Window Control Methods

```cpp
/// Minimize to taskbar. Actual state change fires in WM_SIZE(SIZE_MINIMIZED).
void Minimize();

/// Maximize to monitor work area. Actual state change fires in WM_SIZE(SIZE_MAXIMIZED).
void Maximize();

/// Restore from minimized or maximized. Actual state change fires in WM_SIZE(SIZE_RESTORED).
void Restore();

/// Transition to borderless fullscreen on the window's current monitor.
/// Saves the current windowed position and style for restoration via SetWindowed().
void SetFullscreen();

/// Restore from borderless fullscreen to the previously saved windowed position.
void SetWindowed();

/// Return true if in FullscreenBorderless state.
bool IsFullscreen() const { return m_state == WindowState::FullscreenBorderless; }

/// Return true if in Minimized state (rendering is impossible — client area is 0x0).
bool IsMinimized() const { return m_minimized; }

/// Return true if the application window has input focus.
bool IsFocused() const { return m_focused; }

/// Return the current window display state.
WindowState GetState() const { return m_state; }

/// Dynamically change the window title bar text.
void SetTitle(const char* title);
```

### State Change Callbacks

Clients can register a callback to be notified when the window state changes:

```cpp
/// Callback type: void(WindowState oldState, WindowState newState, void* userData)
using StateChangeCallback = void(*)(WindowState oldState, WindowState newState, void* userData);

/// Set a callback invoked on every state transition.
void SetStateChangeCallback(StateChangeCallback cb, void* userData = nullptr);
```

Example:

```cpp
void OnWindowStateChanged(uz::WindowState oldState, uz::WindowState newState, void* userData) {
    auto* renderer = reinterpret_cast<MyRenderer*>(userData);

    if (newState == uz::WindowState::Minimized) {
        renderer->PauseRendering();  // Skip GPU work while minimized
    } else if (oldState == uz::WindowState::Minimized && newState == uz::WindowState::Normal) {
        renderer->ResumeRendering();
    }
}

// In initialization:
window.SetStateChangeCallback(OnWindowStateChanged, &renderer);
```

### Focus Tracking

The `WM_ACTIVATEAPP` message handler updates the `m_focused` flag:

```cpp
bool IsFocused() const { return m_focused; }
```

This allows the engine to throttle rendering when the window is unfocused, reducing power consumption.

### Minimum Size Enforcement

The `WindowDesc` struct accepts `minWidth` and `minHeight` fields that are enforced by the `WM_GETMINMAXINFO` handler:

```cpp
struct WindowDesc {
    // ...
    int32_t minWidth = 320;   ///< Minimum window width (pixels)
    int32_t minHeight = 240;  ///< Minimum window height (pixels)
};
```

When the user attempts to resize the window below these minimums, Windows prevents it.

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
