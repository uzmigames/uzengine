# Platform — Multi-Monitor & DPI Awareness

> **Status**: Planned (Phase 4.3.0)
> **Source**: `platform/windows/platform_window.h/.cpp`
> **Depends on**: Win32 `EnumDisplayMonitors`, `GetMonitorInfoW`, `GetDpiForMonitor`, `WM_DPICHANGED`

## Purpose

Modern displays span multiple monitors with different DPI scales (e.g., 4K at 150%, 1080p at 100%, laptop at 96 DPI). Without per-monitor DPI awareness, the engine renders blurry on high-DPI or incorrectly sized on mixed-DPI setups. UzEngine detects monitor configurations, responds to DPI changes, and scales UI/viewport appropriately.

The editor (embedded in VSCode) frequently spans multiple monitors — this is critical for correct rendering and UI layout. Additionally, users often play games on multi-monitor setups and expect fullscreen mode to target a specific display.

## Architecture

```
PlatformWindow DPI & Monitor System
    │
    ├── Per-Monitor DPI Awareness V2 (startup)
    │   └── SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
    │
    ├── Monitor Enumeration
    │   ├── EnumerateMonitors()
    │   │   ├── EnumDisplayMonitors() Win32 callback
    │   │   ├── GetMonitorInfoW() for geometry
    │   │   └── GetDpiForMonitor() for DPI
    │   │
    │   └── MonitorInfo[] sorted (primary first)
    │       ├── name, x, y, width, height
    │       ├── workX, workY, workW, workH (excludes taskbar)
    │       ├── dpi (96 = 100%), dpiScale (1.0 = 100%)
    │       ├── refreshRate (Hz)
    │       └── isPrimary
    │
    ├── Per-Window DPI Tracking
    │   ├── GetDpiScale() → float (1.0 = 96 DPI)
    │   ├── GetCurrentMonitor() → int (index into EnumerateMonitors)
    │   └── WM_DPICHANGED → updates m_dpiScale + resize via SetWindowPos
    │
    ├── Monitor-Targeted Fullscreen
    │   └── SetFullscreenOnMonitor(index) → targets specific monitor
    │
    └── Callbacks (future)
        └── OnDpiChanged(float) for UI rescaling
```

## MonitorInfo Struct

The `MonitorInfo` struct describes a single connected display:

```cpp
struct MonitorInfo {
    char name[128];         ///< Monitor device name (e.g. "\\\\.\\DISPLAY1")
    int32_t x;              ///< Top-left X in virtual screen coordinates
    int32_t y;              ///< Top-left Y in virtual screen coordinates
    int32_t width;          ///< Full horizontal resolution (pixels)
    int32_t height;         ///< Full vertical resolution (pixels)
    int32_t workX;          ///< Work area top-left X (excludes taskbar)
    int32_t workY;          ///< Work area top-left Y (excludes taskbar)
    int32_t workW;          ///< Work area width (pixels)
    int32_t workH;          ///< Work area height (pixels)
    uint32_t dpi;           ///< Effective DPI (96 = 100% scaling)
    float dpiScale;         ///< DPI / 96.0f (1.0 = 100%, 1.5 = 150%)
    uint32_t refreshRate;   ///< Refresh rate in Hz
    bool isPrimary;         ///< True if this is the primary monitor
};
```

### Field Descriptions

| Field | Type | Notes |
|-------|------|-------|
| `name` | `char[128]` | Device string (e.g., `\\\\.\\DISPLAY1`). Read-only — do not modify. |
| `x, y` | `int32_t` | Top-left corner in **virtual screen coordinates**. Used for HW cursor positioning, multi-monitor layouts. |
| `width, height` | `int32_t` | Full monitor resolution in physical pixels (before DPI scaling). |
| `workX, workY, workW, workH` | `int32_t` | **Work area** — excludes Windows taskbar, panels, and reserved regions. Use for window positioning/maximization. |
| `dpi` | `uint32_t` | Raw DPI value from `GetDpiForMonitor()`. Baseline is 96 DPI (100% scaling). |
| `dpiScale` | `float` | Convenience field: `dpi / 96.0f`. Use for font sizing, UI scaling, render target sizing. |
| `refreshRate` | `uint32_t` | Refresh rate in Hz. Typical: 60, 120, 144, 165, 240. Query for adaptive frame timing. |
| `isPrimary` | `bool` | True only for the OS primary monitor. Window creation and fullscreen default to primary. |

### Virtual Screen Coordinates

Windows uses **virtual screen coordinates** for multi-monitor layouts. If you have two 1920x1080 monitors side-by-side:

```
Primary monitor:       Secondary monitor:
x:0, y:0              x:1920, y:0
width:1920            width:1920
height:1080           height:1080
```

This allows seamless cursor and window movement across monitors. When using monitor coordinates, always account for non-zero `x, y` offsets.

## EnumerateMonitors()

Query all connected displays:

```cpp
/// Enumerates all connected display monitors.
///
/// # Returns
/// Vector of MonitorInfo sorted with primary monitor first.
/// Empty vector on failure or if no monitors are detected.
///
/// # Thread Safety
/// Safe to call from any thread (Win32 monitor APIs are thread-safe).
std::vector<MonitorInfo> EnumerateMonitors();
```

### Usage Example

```cpp
auto monitors = EnumerateMonitors();

UZ_LOG(Platform, Info, "Found {} monitors", monitors.size());
for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& m = monitors[i];
    UZ_LOG(Platform, Info,
        "Monitor[{}]: {} @ ({}, {}) {}x{}, DPI={:.0f}% "
        "({}Hz) {}",
        i, m.name, m.x, m.y, m.width, m.height,
        m.dpiScale * 100.0f, m.refreshRate,
        m.isPrimary ? "PRIMARY" : "secondary");
}
```

### Notes

- **Never cache indefinitely** — monitors can be hot-plugged (docking stations, USB monitors, display disconnect).
- **Call at startup** to initialize UI layouts, then **call again on WM_DISPLAYCHANGE** to detect new monitors.
- **Primary monitor is always first** — useful for fallback fullscreen if window moves off a disconnected monitor.
- **Returned vector may be empty** if no display devices are detected (rare, but can happen in remote/headless environments).

## Per-Monitor DPI Awareness V2

Windows supports three DPI awareness modes. UzEngine uses **DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2** for correct multi-monitor support.

### Awareness Levels

| Mode | Behavior | Use Case |
|------|----------|----------|
| `DPI_AWARENESS_CONTEXT_UNAWARE` | Windows scales engine bitmap for high-DPI (blurry) | Legacy apps only — **never use** |
| `DPI_AWARENESS_CONTEXT_SYSTEM_AWARE` | All monitors appear to have the same DPI (coarse) | Single monitor or low-res layouts only |
| `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` | Each monitor reports its own DPI; `WM_DPICHANGED` fires | **Correct for multi-monitor setups** — use this |

### Initialization

DPI awareness **must be set before creating the first window**:

```cpp
#include <windows.h>

// At application startup (in main() or global init):
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

// Now create windows, enumerate monitors, etc.
PlatformWindow window;
window.Create(desc);
```

With `PER_MONITOR_AWARE_V2`:
- The engine receives **physical pixel coordinates** for the window (not scaled by OS).
- Each monitor independently reports its DPI via `GetDpiForMonitor()`.
- When the window moves between monitors, Windows sends `WM_DPICHANGED` with the new DPI.
- System UI (dialogs, menu bars) is scaled automatically by Windows; the engine is **not** responsible for that.

### Consequences

- **Rendering**: Target the window's physical size. If the monitor is 4K at 150%, the window may be 2560x1440 physical pixels (but appears as ~1707x960 logical pixels to the user).
- **Font sizing**: Query `GetDpiScale()` and pass to font rasterizer. A 12pt font at 150% DPI is rendered at 18px native.
- **UI layout**: If you hardcode button sizes, scale them by `dpiScale` when laying out the UI.

## WM_DPICHANGED Handling

When the window moves to a monitor with different DPI, Windows sends the `WM_DPICHANGED` message. `PlatformWindow` handles this automatically:

```cpp
// In PlatformWindow::WndProc (internal to platform_window.cpp)
case WM_DPICHANGED: {
    // Extract new DPI from wParam
    uint32_t dpiX = GET_X_LPARAM(wParam);
    uint32_t dpiY = GET_Y_LPARAM(wParam);

    // Update internal scale
    m_dpiScale = dpiX / 96.0f;

    // Windows suggests a resized rect in lParam
    // Apply it to reposition/resize the window
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    SetWindowPos(m_hwnd, nullptr,
        suggested->left, suggested->top,
        suggested->right - suggested->left,
        suggested->bottom - suggested->top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Log the change
    UZ_LOG(Window, Info, "DPI changed: scale={:.2f}x", m_dpiScale);
    break;
}
```

### What Happens Automatically

- `GetDpiScale()` returns the new scale factor immediately.
- The window is resized and positioned per Windows' suggestion (maintains relative size on the new monitor's DPI).
- If your UI uses `dpiScale`, it will update on the next frame.

### What You Must Do

If your engine has **font rendering**, **UI layout**, or **texture scaling** that depends on DPI, you should respond to DPI changes:

```cpp
// Pseudo-code: bind to DPI changes and rescale your systems
void MyGameEngine::OnDpiChanged(float newScale) {
    // Fonts: request re-rasterization at new pixel size
    font_system->Rebuild(12.0f * newScale);

    // UI: reflow layout
    ui_root->RecalculateLayout(window.GetDpiScale());

    // Textures: consider reloading at higher mip level for clarity
    // (usually not necessary if GPU handles it)
}
```

Monitor `GetDpiScale()` in your game loop and call your rescaling callback when it changes, or set up a callback mechanism.

## GetDpiScale()

Query the current DPI scale of the window:

```cpp
/// Returns the current DPI scale factor for this window.
///
/// # Returns
/// Scale factor where 1.0 = 96 DPI (100%), 1.5 = 144 DPI (150%), etc.
/// Defaults to 1.0 if the window has not been created yet.
float GetDpiScale() const { return m_dpiScale; }
```

### Usage

```cpp
float scale = window.GetDpiScale();

// Size a button to 100 logical pixels on all DPI settings
int buttonWidth = 100 * scale;

// Load/render a font
int fontPixelSize = (int)(12.0f * scale);
font->Load("Arial", fontPixelSize);
```

### Notes

- Returns **1.0 by default** if the window hasn't been created yet (safe sentinel value).
- Updated automatically when `WM_DPICHANGED` fires.
- **Main thread only** — call from the render loop, not background threads.

## GetCurrentMonitor()

Determine which monitor contains the window:

```cpp
/// Returns the index of the monitor containing this window's center point.
///
/// The index corresponds to the vector returned by EnumerateMonitors().
///
/// # Returns
/// Monitor index (0-based). Returns 0 if the window is not created or
/// if the monitor cannot be determined.
int GetCurrentMonitor() const;
```

### Usage

```cpp
auto monitors = EnumerateMonitors();
int idx = window.GetCurrentMonitor();

if (idx >= 0 && idx < (int)monitors.size()) {
    const auto& mon = monitors[idx];
    UZ_LOG(Window, Info, "Window on monitor[{}]: {} ({} Hz)",
        idx, mon.name, mon.refreshRate);
}
```

### Notes

- Based on the **window's center point** at the time of the call.
- Returns **0 (primary monitor) if window is not created** or if the monitor cannot be determined.
- If the window is partially on two monitors, the monitor containing its center wins.
- Safe to call from main thread during the game loop.

## SetFullscreenOnMonitor()

Transition to borderless fullscreen on a specific monitor:

```cpp
/// Switches to borderless fullscreen on a specific monitor.
///
/// # Arguments
/// * `monitorIndex` — Index into the vector returned by EnumerateMonitors().
///   If out of range, falls back to the primary monitor.
///
/// # Behavior
/// Saves the current windowed position/style for later restoration via
/// SetWindowed(). If already fullscreen, transitions to the target monitor.
void SetFullscreenOnMonitor(int monitorIndex);
```

### Usage

```cpp
// List monitors and let user pick
auto monitors = EnumerateMonitors();
for (size_t i = 0; i < monitors.size(); ++i) {
    std::cout << "[" << i << "] " << monitors[i].name
              << " (" << monitors[i].width << "x" << monitors[i].height
              << " @ " << monitors[i].refreshRate << " Hz)\n";
}

// User selects monitor 1
int choice = 1;
if (choice >= 0 && choice < (int)monitors.size()) {
    window.SetFullscreenOnMonitor(choice);
}
```

### Behavior

1. **Saves current state** — if in windowed mode, stores the rect and style for later `SetWindowed()` restoration.
2. **Targets the monitor** — uses the monitor's full dimensions (`width` and `height` from `MonitorInfo`).
3. **Applies borderless fullscreen** — sets `WS_POPUP` style and fills the entire monitor (no decorations).
4. **If already fullscreen** — seamlessly transitions to the new monitor without leaving fullscreen.

### Fallback Behavior

If `monitorIndex` is out of range (e.g., user unplugs a monitor), the function falls back to the **primary monitor** (index 0). This prevents fullscreen on a phantom monitor.

### Related APIs

```cpp
// For comparison:
void SetFullscreen();        // Fullscreen on current monitor
void SetWindowed();          // Restore windowed mode
bool IsFullscreen() const;   // Check if in fullscreen
```

## Thread Safety

Multi-threaded access to `PlatformWindow`:

```cpp
// SAFE (read-only, atomic)
window.IsOpen();
window.GetWidth();
window.GetHeight();
window.GetDpiScale();           // Only reads m_dpiScale
window.IsMinimized();
window.IsFocused();

// SAFE (thread-safe query)
EnumerateMonitors();            // Thread-safe Win32 calls

// NOT SAFE (main thread only)
window.Create(desc);            // Creates HWND
window.Destroy();               // Destroys HWND
window.PumpMessages();          // Processes WM_* messages
window.SetFullscreenOnMonitor(idx);  // Changes window style/rect
window.GetCurrentMonitor();     // Main thread only
```

### Guideline

- **Monitor enumeration** (`EnumerateMonitors()`) is thread-safe and can be called from background threads.
- **Window methods** are **main-thread only** — call from the render loop or event dispatcher, never from background threads.
- **Unsafe methods** that touch the HWND will cause asserts if called from the wrong thread (in debug builds).

## Usage Examples

### Example 1: Enumerate and Log Monitors

```cpp
void LogMonitorSetup() {
    auto monitors = EnumerateMonitors();

    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];

        UZ_LOG(Platform, Info,
            "Monitor[{}]: '{}'\n"
            "  Geometry: ({}, {}) to ({}, {})\n"
            "  Work area: ({}, {}) to ({}, {})\n"
            "  DPI: {} (scale={:.2f}x)\n"
            "  Refresh: {} Hz\n"
            "  Primary: {}",
            i, m.name,
            m.x, m.y, m.x + m.width, m.y + m.height,
            m.workX, m.workY, m.workX + m.workW, m.workY + m.workH,
            m.dpi, m.dpiScale,
            m.refreshRate,
            m.isPrimary ? "yes" : "no");
    }
}
```

### Example 2: Create Window and Detect DPI

```cpp
PlatformWindow window;
WindowDesc desc{};
desc.title = "UzEngine";
desc.width = 1280;
desc.height = 720;
desc.resizable = true;

if (!window.Create(desc)) {
    UZ_LOG(Window, Error, "Failed to create window");
    return false;
}

// Check which monitor we're on
int monIdx = window.GetCurrentMonitor();
auto monitors = EnumerateMonitors();
if (monIdx >= 0 && monIdx < (int)monitors.size()) {
    float dpiScale = monitors[monIdx].dpiScale;
    UZ_LOG(Window, Info, "Created on monitor {} with DPI scale {:.2f}x",
        monIdx, dpiScale);
}
```

### Example 3: Respond to DPI Changes

```cpp
// In your game loop:
static float lastDpiScale = 0.0f;
float currentDpiScale = window.GetDpiScale();

if (currentDpiScale != lastDpiScale) {
    UZ_LOG(Window, Info, "DPI changed: {:.2f}x → {:.2f}x",
        lastDpiScale, currentDpiScale);

    // Rescale UI
    ui_system->SetDpiScale(currentDpiScale);

    // Rasterize fonts at new size
    font_system->RebuildFonts(currentDpiScale);

    // Invalidate layout caches
    layout_cache->Clear();

    lastDpiScale = currentDpiScale;
}
```

### Example 4: Fullscreen on Specific Monitor (Multi-Monitor Game)

```cpp
void ToggleFullscreen() {
    if (window.IsFullscreen()) {
        window.SetWindowed();
    } else {
        // Get the monitor the window is currently on
        int monIdx = window.GetCurrentMonitor();
        window.SetFullscreenOnMonitor(monIdx);
    }
}

void MoveFullscreenToMonitor(int idx) {
    // User pressed "move to monitor 2"
    window.SetFullscreenOnMonitor(idx);
}
```

### Example 5: High-DPI Aware Font Rendering

```cpp
void RenderText(const std::string& text, float baseSize) {
    float dpiScale = window.GetDpiScale();
    float pixelSize = baseSize * dpiScale;

    // Request font at the correct pixel size
    Font* font = font_system->Get("Arial", (int)pixelSize);

    // Render at this size
    font->DrawString(text, x, y);
}

// Caller passes logical size (e.g., 12pt)
RenderText("Press SPACE to start", 12.0f);  // Auto-scales to 18px at 150% DPI
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Monitor enum | `FMonitorInfo`, `GetMonitorInfo()` | `DisplayServer::screen_get_*()` | `MonitorInfo`, `EnumerateMonitors()` |
| DPI query | `FHighDPI::GetDisplayDensity()` | `DisplayServer::screen_get_dpi()` | `GetDpiScale()` on window |
| DPI change event | `ViewportClient::NotifyMobileInputDensityChanged()` | `Window.dpi_changed` signal | `WM_DPICHANGED` → auto-handled |
| Multi-monitor fullscreen | Complex (`FGenericWindow::SetDisplayMode()`) | `Window.current_screen = idx` | `SetFullscreenOnMonitor(idx)` |
| Virtual coordinates | Yes, but abstracted | Yes, via `screen_*` getters | Yes, exposed in `MonitorInfo.x/y` |

## Rules

1. **ALWAYS call `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` before window creation** — must be in application startup, before any window ops.
2. **NEVER assume 96 DPI = 1.0 scale** — query actual values via `GetDpiScale()` or `MonitorInfo.dpiScale`.
3. **ALWAYS respond to DPI changes** — call `GetDpiScale()` in the game loop and rescale UI/fonts when it changes.
4. **NEVER cache the monitor list indefinitely** — monitors hot-plug; call `EnumerateMonitors()` again on WM_DISPLAYCHANGE.
5. **ALWAYS test on mixed-DPI setups** — a 4K monitor at 150% + 1080p at 100% is a common configuration in developer studios.
6. **NEVER apply DPI scale to coordinates passed to Win32** — virtual screen coordinates are already physical; Windows will not rescale them.
7. **ALWAYS use `GetCurrentMonitor()` to verify monitors exist** — out-of-range monitor indices fall back gracefully, but logging helps detect issues.
8. **NEVER block the render loop waiting for monitor enumeration** — it's fast but should happen off-frame if querying at runtime.
