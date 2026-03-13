# Godot Engine — Windows Platform Layer Deep Dive

**Reference**: `/e/godot/platform/windows/`
**Focus**: Architecture, design patterns, and lessons for UzEngine Windows port

---

## Architecture Overview

The Windows platform layer in Godot is structured around three main components:

1. **OS Layer** (`os_windows.h/.cpp`) — OS abstraction, process, environment
2. **Display Server** (`display_server_windows.h/.cpp`) — Window management, input, rendering
3. **Rendering Drivers** (gl_manager, rendering_context_driver_vulkan_windows)

```
┌─────────────────────────────────────────────────────────────────┐
│                        Godot Engine                             │
├─────────────────────────────────────────────────────────────────┤
│                     DisplayServer API                           │
├─────────────────────────────────────────────────────────────────┤
│              DisplayServerWindows (40 .h/.cpp)                  │
│  ┌──────────┬──────────┬──────────┬──────────────────────────┐  │
│  │ Windows  │ Input    │ Graphics │ System Integration       │  │
│  │ Mgmt     │ Routing  │ Contexts │                          │  │
│  └──────────┴──────────┴──────────┴──────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│          Graphics Drivers (Vulkan, D3D12, OpenGL)               │
│  ┌──────────────┬──────────────┬──────────────────────────────┐  │
│  │ RenderingCD* │ GLManager*   │ WASAPI/XAudio2 Audio       │  │
│  │ (Vulkan)     │ (OpenGL)     │ WinMIDI MIDI               │  │
│  └──────────────┴──────────────┴──────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                      Win32 API Layer                            │
│   HWND, WM_*, CreateThread, Registry, DirectWrite, WinTab      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Window Management Deep Dive

### Window Creation & State

**Key Structure: WindowData** (`display_server_windows.h` lines 295-396)

```cpp
struct WindowData {
    HWND hWnd;                          // Native Windows handle
    DisplayServerEnums::WindowID id;    // Engine-side identifier

    // Window state
    bool maximized, minimized, fullscreen, borderless, resizable;
    bool always_on_top, no_focus, exclusive;
    bool was_maximized, was_fullscreen_pre_min;

    // Rendering
    bool rendering_context_window_created;
    bool gl_native_window_created, gl_angle_window_created;

    // Size/Position
    int width, height;
    int width_with_decorations, height_with_decorations;
    Size2 min_size, max_size;
    Point2 last_pos;

    // Fullscreen
    bool pre_fs_valid;
    RECT pre_fs_rect;  // Store previous rect before fullscreen

    // Focus state
    bool window_focused;
    bool first_activation_done;

    // HDR
    bool hdr_output_requested;
    float hdr_output_reference_luminance;
    float hdr_output_max_luminance;

    // Window relationships
    DisplayServerEnums::WindowID transient_parent;
    HashSet<DisplayServerEnums::WindowID> transient_children;
    bool is_popup;
    Rect2i parent_safe_rect;  // For popup positioning

    // IME (Input Method Editor)
    HIMC im_himc;
    Vector2 im_position;
    bool ime_active, ime_in_progress;

    // Tablet (WinTab)
    HANDLE wtctx;
    LOGCONTEXTW wtlc;
    int min_pressure, max_pressure;
    bool tilt_supported;
    float last_pressure;
    Vector2 last_tilt;
    bool pen_inverted;

    // Callbacks
    Callable rect_changed_callback;
    Callable event_callback;
    Callable input_event_callback;
    Callable input_text_callback;
    Callable drop_files_callback;

    // OLE drag-drop
    DropTargetWindows *drop_target;

    // HDR
    bool no_redirection_bitmap;
};
```

**Window Creation Flow** (`_create_window()`):

1. **Determine window style** via `_get_window_style()`:
   - WS_OVERLAPPEDWINDOW (standard)
   - WS_POPUP (borderless)
   - WS_CHILD (embedded)
   - WS_EX_* extended styles (topmost, transparent, etc.)

2. **Register window class** (WNDCLASSEXW) if needed:
   - One global class per instance, reused for all windows
   - Setup: lpszClassName, hIcon, hCursor, lpfnWndProc

3. **CreateWindowEx()** with calculated styles/size

4. **Show/Position** via SetWindowPos():
   - SWP_NOMOVE | SWP_NOSIZE for just showing
   - SWP_NOZORDER to preserve Z-order
   - SWP_FRAMECHANGED for style changes

5. **Setup rendering**:
   - Call `_create_rendering_context_window()` for RD (Vulkan/D3D12)
   - Call `_create_gl_window()` for GLES3

6. **Setup input**:
   - Initialize WinTab context if tablet available
   - Attach DropTargetWindows for OLE drag-drop
   - Register for raw input if `use_raw_input`

### Fullscreen Handling

**Key Insight**: Godot preserves pre-fullscreen state in WindowData::pre_fs_rect

```cpp
// When going fullscreen:
if (!data.fullscreen) {
    data.pre_fs_valid = true;
    GetWindowRect(hwnd, &data.pre_fs_rect);  // Save current
}

// Switch to fullscreen: remove decorations, maximize, move to monitor
// When exiting: restore pre_fs_rect via SetWindowPos()
```

**Multi-window fullscreen** (multiwindow_fs flag):
- Individual windows can be fullscreen simultaneously on different monitors
- Alternative: single window fullscreen, others continue normal

### Minimize/Maximize Handling

**Problem**: Windows sends events rapidly during window moves/resizes
**Solution**: Timer-based redraw throttling

```cpp
enum TimerID {
    TIMER_ID_MOVE_REDRAW = 1,      // Rate-limit redraws during move
    TIMER_ID_WINDOW_ACTIVATION = 2, // Defer activation handling
};
```

When WM_MOVING/WM_SIZING received:
- Set timer, don't redraw immediately
- Timer fires after delay → single redraw
- Reduces CPU spike during interactive resize

**Maximize before fullscreen**:
- Windows often go maximized first
- Track was_maximized, was_maximized_pre_fs separately
- Restore correct state on exit

---

## Input Handling Architecture

### Event Flow

```
WM_* Message (Windows)
    ↓
DisplayServerWindows::wndproc() [static callback]
    ↓
_window_proc() [instance method, per-window]
    ↓
Route to handler: _input_index_press, _mouse_update, _tablet_parse, etc.
    ↓
Construct InputEvent (InputEventKey, InputEventMouseButton, etc.)
    ↓
Dispatch to Godot scene tree via Input singleton
```

### Keyboard Input

**File**: `key_mapping_windows.h` (~150 lines)

Maps WM_KEYDOWN/WM_KEYUP messages to Godot KeyCode enum:

```cpp
// Windows WPARAM (VK_*) → Godot::Key
case VK_ESCAPE:     return Key::ESCAPE;
case VK_TAB:        return Key::TAB;
case VK_BACK:       return Key::BACKSPACE;
case VK_RETURN:     return Key::ENTER;
// ... 100+ entries
case 0x41 ... 0x5A: return Key::A + (key - 0x41);  // A-Z
```

**Shift/Ctrl/Alt handling**:
- GetAsyncKeyState(VK_SHIFT/CONTROL/MENU) for modifier detection
- More reliable than WM_KEYDOWN modifier bits

**Language/layout handling**:
- `GetKeyboardLayout(GetWindowThreadProcessId(hwnd))` for current layout
- MapVirtualKeyEx(vk, MAPVK_VK_TO_CHAR, hkl) for character translation
- Handles AZERTY, Dvorak, etc.

**IME (Input Method Editor) integration**:
- WM_IME_COMPOSITION: composing text (East Asian input)
- WM_IME_ENDCOMPOSITION: final text received
- GetCompositionString() retrieves composed string
- Per-window HIMC context, position updated on caret move

### Mouse Input

**Button events** (WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN):
- Left, Right, Middle buttons supported
- Double-click via WM_LBUTTONDBLCLK, etc.
- Distinguish via button count: `GET_X_LPARAM(lParam)`

**Movement** (WM_MOUSEMOVE):
- Track last_x, last_y for delta calculation
- Mouse trails (restore_mouse_trails flag) disabled for games
- Outside window: WM_MOUSELEAVE → mouse_exit event

**Wheel** (WM_MOUSEWHEEL, WM_MOUSEHWHEEL):
- GET_WHEEL_DELTA_WPARAM(wParam) for scroll amount
- Normalize to wheel up/down

**Cursor confinement** (mouse mode):
- MOUSE_MODE_CONFINED: SetClipCursor(rect) to limit movement
- MOUSE_MODE_HIDDEN: ShowCursor(FALSE)
- MOUSE_MODE_CAPTURED: SetCapture(hwnd) for drag outside bounds

### Pen/Tablet Input

**File**: `display_server_windows.h` (WinTab support, lines 71-153)

**WinTab API** (industry standard for pen tablets):

```cpp
typedef HANDLE(WINAPI *WTOpenPtr)(HWND p_window, LOGCONTEXTW *p_ctx, BOOL p_enable);
typedef BOOL(WINAPI *WTPacketPtr)(HANDLE p_ctx, UINT p_param, LPVOID p_packets);
```

**Per-window WinTab context**:
- HANDLE wtctx: WinTab context for this window
- LOGCONTEXTW wtlc: context configuration (pressure ranges, button masks)
- min_pressure, max_pressure: normalized pressure range
- tilt_supported: check device capabilities upfront
- last_pressure, last_tilt: current state

**Packet structure** (PACKET struct, lines 142-147):
```cpp
struct PACKET {
    int pkStatus;           // Button/proximity state
    int pkNormalPressure;   // Tip pressure (0-255 or 0-4096 typically)
    int pkTangentPressure;  // Side/barrel pressure
    ORIENTATION {
        int orAzimuth;      // Rotation around vertical axis
        int orAltitude;     // Tilt from horizontal (0-90°)
        int orTwist;        // Pen rotation in plane
    } pkOrientation;
};
```

**Handling**:
- WM_*_PACKET (custom message 0x7FF0): WTPacket() retrieves data
- WM_PROXIMITY (custom message 0x7FF5): Pen enters/leaves digitizer range
- Normalize pressure to 0.0-1.0 float for InputEventMouseButton
- Convert tilt to Vector2 (x=altitude, y=azimuth)

**Inverted pen**: Track pen_inverted flag per window, swap pressure interpretation

### Multi-Touch Input

**Architecture**:
```cpp
RBMap<int, Vector2> touch_state;  // Touch ID → current position
```

**Flow**:
- WM_TOUCH message (Windows 7+) or gesture events (earlier)
- GetTouchInputInfo() to retrieve touch points
- Normalize to 0.0-1.0 screen coords
- Dispatch InputEventScreenTouch per contact

**Gesture events**:
- WM_GESTURE for pinch, rotate, pan (two-finger gestures)
- Map to InputEventGesture for scene consumption

### Gamepad Input

**Integrated via SDL** (JoypadSDL referenced in display_server_windows.h):
- Not Windows-specific code (shared driver)
- SDL handles XInput, DirectInput devices
- Polls periodically in engine main loop

---

## Rendering Context Management

### Graphics Driver Selection

**Problem**: Windows supports multiple rendering APIs
**Solution**: Runtime driver selection + parallel context creation

```cpp
enum DriverID {
    DRIVER_ID_COMPAT_OPENGL3 = 1 << 0,
    DRIVER_ID_COMPAT_ANGLE_D3D11 = 1 << 1,
    DRIVER_ID_RD_VULKAN = 1 << 2,
    DRIVER_ID_RD_D3D12 = 1 << 3,  // Primary modern driver
};
static BitField<DriverID> tested_drivers;  // Track which tried
```

**Selection order** (at engine startup):
1. Command-line flag (--gpu=vulkan, etc.)
2. Project settings
3. Try in order: D3D12 → Vulkan → OpenGL/ANGLE

**Window setup** (parallel contexts):
- Main window: used for rendering
- Rendering context window: offscreen, created per RHI backend for setup
- GL windows: separate for OpenGL render surface

### OpenGL Management

**Two implementations**:
- `gl_manager_windows_native.h/.cpp`: Native WGL
  - Direct Windows OpenGL via wgl*() functions
  - Faster but older

- `gl_manager_windows_angle.h/.cpp`: ANGLE (D3D11/D3D12 backend)
  - EGL over DirectX
  - Better compatibility, especially on weak Intel drivers

**Creation flow**:
```
1. CreateDC(hwnd)          // Device context
2. ChoosePixelFormat(dc)   // 32-bit RGBA, depth, etc.
3. SetPixelFormat(dc, fmt)
4. wglCreateContext(dc)    // Native
   OR
   eglCreateContext()       // ANGLE
5. wglMakeCurrent()
6. Load GL functions (GLAD, wgl*GetProcAddress)
7. Enable vsync via wglSwapIntervalEXT()
```

### Vulkan Management

**File**: `rendering_context_driver_vulkan_windows.h/.cpp`

**Windows-specific**:
```cpp
// VK_KHR_win32_surface extension
VkWin32SurfaceCreateInfoKHR surface_info{
    .hwnd = hwnd,
    .hinstance = GetModuleHandle(nullptr),
};
vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface);
```

**Typical flow**:
1. CreateInstance(extension: VK_KHR_WIN32_SURFACE)
2. EnumeratePhysicalDevices()
3. CreateDevice(with graphics queue)
4. CreateWin32Surface(hwnd)
5. Create swapchain (FIFO, 3 images typical)

---

## Audio System

### WASAPI (Windows Audio Session API)

**File**: `drivers/wasapi/audio_driver_wasapi.h/.cpp`

**Why WASAPI over DirectSound/XAudio2**:
- Modern, low-latency
- Volume/mute per app (app volume control in Windows mixer)
- Spatial audio capable (3D positional)
- Exclusive mode for lowest latency

**Initialization**:
```cpp
1. CoInitializeEx(COINIT_MULTITHREADED)  // COM setup
2. CoCreateInstance(CLSID_MMDeviceEnumerator, ...)
3. GetDefaultAudioEndpoint(eRender, eConsole, ppDevice)
4. Activate(IAudioClient, CLSCTX_ALL, ...)
5. GetMixFormat(pwfx) → discover device format
6. Initialize(exclusive/shared mode, latency, format)
7. GetService(IID_IAudioRenderClient, ...)
8. Start() → audio engine running
```

**Exclusive vs Shared**:
- **Exclusive**: App gets sole access, lowest latency, no mixing
  - Use for games wanting precise control
- **Shared**: Audio mixes with other apps
  - Use for background/UI audio

**Thread model**:
- Dedicated audio thread
- GetBuffer() → write PCM data → ReleaseBuffer()
- Signaled via HANDLE or polling

### XAudio2 (Legacy Fallback)

**File**: `drivers/xaudio2/audio_driver_xaudio2.h/.cpp`

Built on DirectX audio pipeline, simpler than WASAPI but older.
Only used if WASAPI unavailable (rare modern Windows).

---

## System Integration

### Crash Handler

**Files**:
- `crash_handler_windows.h` — Interface
- `crash_handler_windows_seh.cpp` — Structured Exception Handling
- `crash_handler_windows_signal.cpp` — Signal handler registration

**SEH (Structured Exception Handling)**:
```cpp
__try {
    // Engine main loop
} __except (CrashHandler::handle_exception(exc_info)) {
    // Crash occurred, dialog/log output
}
```

**Captures**:
- Exception code (EXCEPTION_ACCESS_VIOLATION, etc.)
- Stack backtrace (StackWalk64 or RtlCaptureStackBackTrace)
- Register state
- Module/DLL info

### Environment Variables

**Simple key-value access**:
```cpp
GetEnvironmentVariable(L"GODOT_TEST_VAR", buffer, size);
SetEnvironmentVariable(L"GODOT_TEST_VAR", L"value");
```

**Special vars**:
- GODOT_CPU_AFFINITY: Thread pool CPU affinity
- GODOT_NO_OPTIMIZE: Debug build hints
- WINE_CPU_TOPOLOGY: Wine-specific

### Process Management

**execute()**: Synchronous subprocess
```cpp
CreateProcessW(
    cmd,                    // Command line
    startup_info,
    process_info,
    CREATE_NO_WINDOW,       // No console
    ...
);
WaitForSingleObject(process_info.hProcess, INFINITE);
GetExitCodeProcess(process_info.hProcess, &exit_code);
CloseHandle(process_info.hProcess);
```

**create_process()**: Asynchronous
- Don't wait, return PID
- Caller can poll is_process_running() or wait elsewhere

**kill()**: Terminate by PID
```cpp
TerminateProcess(handle, 1);  // Force exit code 1
```

### Font System (DirectWrite)

**Modern font enumeration** via DirectWrite (not GDI):

```cpp
// Setup
CoCreateInstance(CLSID_DirectWriteFactory, ...);
factory->CreateFontCollection(...);

// Enumerate
for (int i = 0; i < collection->GetFontFamilyCount(); i++) {
    IDirectWriteFontFamily* family;
    collection->GetFontFamily(i, &family);
    family->GetFamilyNames()->GetString(0, name);  // "Arial"
}

// Resolve specific font
collection->FindFamilyName(L"Arial", &index, &exists);
family->GetFont(weight, stretch, style, &font);
```

**Weight/Stretch conversion**:
```cpp
DWRITE_FONT_WEIGHT _weight_to_dw(int p_weight) {
    // 100, 200, ..., 900
    return (DWRITE_FONT_WEIGHT)p_weight;
}
```

**Fallback chains**:
```cpp
factory2->CreateFallbackBuilder();
builder->AddMapping(range, family);  // "Add Arial for Latin"
builder->CreateFallback(&fallback);
fallback->MapCharacters(...);  // Resolve missing glyphs
```

### Taskbar Integration

**ITaskbarList3**:
```cpp
CoCreateInstance(CLSID_TaskbarList, ...);
taskbar->HrInit();
taskbar->SetProgressValue(hwnd, 50, 100);  // 50% done
taskbar->SetProgressState(hwnd, TBPF_NORMAL);  // Green
```

**States**:
- TBPF_NOPROGRESS: No indicator
- TBPF_INDETERMINATE: Spinning/pulse
- TBPF_NORMAL: Green bar
- TBPF_ERROR: Red bar
- TBPF_PAUSED: Yellow bar

---

## DPI & Multi-Monitor Support

### DPI Awareness

**Levels** (Windows 10+):
1. **Per-monitor DPI-aware**: Each monitor has its own DPI
   - Scalable apps adjust UI/rendering per monitor
   - Godot uses this for best quality

2. **System DPI-aware**: One DPI for all monitors
   - Blurry on high-DPI monitors

3. **Unaware**: OS scales app uniformly
   - Can look fuzzy

**Setup in manifest or code**:
```cpp
// At startup, tell Windows we handle DPI
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
```

### Monitor Enumeration

**Use Xrandr equivalent: Monitor enumerator**:
```cpp
EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, lParam);

// Callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hmon, HDC hdc, LPRECT lprc, LPARAM dwData) {
    MONITORINFOEX info{};
    GetMonitorInfo(hmon, &info);
    // info.rcMonitor: screen coords
    // info.rcWork: usable area (excludes taskbar)
    // info.dwFlags: MONITORINFOF_PRIMARY
}
```

**DPI per monitor**:
```cpp
UINT dpi = GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI);
float scale = (float)dpi / 96.0f;  // 96 DPI = 100% scale
```

---

## HDR Support

**Modern feature** (Windows 10 2004+):

**Enable**:
```cpp
data.hdr_output_requested = true;
```

**What it means**:
- Extended color gamut (Rec. 2020 vs sRGB)
- Higher bit depth (16-bit vs 8-bit per channel)
- Luminance values > 1.0 (up to ~1000 nits on HDR displays)

**Implementation**:
- Set DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 color space
- Render to HDR-capable format (R10G10B10A2, RGBA16F)
- Reference luminance tells compositor peak brightness

---

## Platform Config & Compilation

**File**: `platform_config.h`

```cpp
#define PLATFORM_WINDOWS 1

// Feature flags
#define WINDOWS_SUBSYSTEM_WINDOWS 0  // vs CONSOLE
#define WINDOWS_SUBSYSTEM_CONSOLE 1

#define WASAPI_ENABLED 1
#define XAUDIO2_ENABLED 1
#define WINMIDI_ENABLED 1
```

**Conditional compilation**:
- `#ifdef GLES3_ENABLED`: Include GL manager headers
- `#ifdef RD_ENABLED`: Include rendering device (Vulkan/D3D12)
- `#ifdef TOOLS_ENABLED`: Editor-only code

---

## Key Lessons for UzEngine Windows Port

### 1. Window Management
✓ WindowData struct pattern is solid — separate concerns (state, rendering, input)
✓ Pre-fullscreen RECT saving prevents state loss
✓ Timer-based throttling for move/resize events reduces CPU spike
✓ Transient window parent/child relationship for modals

### 2. Input Routing
✓ Single static wndproc, dispatch to instance per HWND
✓ KeyCode mapping table is maintainable
✓ Modifier detection via GetAsyncKeyState, not WM_ flags (more reliable)
✓ IME per-window HIMC context, position tracking for candidate window

### 3. Pen/Tablet
✓ WinTab is mature and industry-standard
✓ Pressure: normalize to 0.0-1.0, store min/max per device
✓ Tilt: ORIENTATION struct provides azimuth/altitude/twist
✓ Inverted: track per-window, affects pressure/button interpretation

### 4. Rendering
✓ Parallel context creation (one main, one per RHI backend for setup)
✓ Driver selection via priority list, track tested_drivers to avoid retry
✓ ANGLE as fallback for OpenGL (more stable than native WGL on Intel)
✓ Vulkan via VK_KHR_win32_surface, standard flow

### 5. Audio
✓ WASAPI as primary (modern, low-latency, spatial-capable)
✓ XAudio2 as legacy fallback
✓ Exclusive vs Shared mode tradeoff: exclusive = lower latency, shared = volume control

### 6. System Integration
✓ DirectWrite for modern font management (vs GDI)
✓ ITaskbarList3 for progress bar — easy win for UX
✓ Crash handler via SEH, stack capture via StackWalk64
✓ Proper DPI awareness setup at startup

### 7. Architecture Patterns
✓ DisplayServer abstraction cleanly separates platform from renderer
✓ Callbacks (rect_changed, event, input_event, drop_files) for decoupling
✓ RBMap<WindowID, WindowData> for efficient multi-window lookup
✓ Static callback → instance dispatch pattern for Win32 WNDPROC

---

## Related Files

- Full inventory: `docs/research/GODOT_PLATFORM_LAYER_INVENTORY.md`
- Windows file count: 40 .h/.cpp files
- Largest files: display_server_windows.h (1000+ lines), os_windows.cpp (2000+ lines)
- Key dependencies: Win32 API, COM (DirectWrite, WASAPI, OLE), DirectX headers

---

**End of Deep Dive**
