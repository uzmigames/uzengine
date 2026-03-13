#pragma once

/// UzEngine Platform Window — Native Win32 implementation.
///
/// Creates and manages a native Windows (HWND) window for rendering.
/// Provides window lifecycle, input handling, and DX12 swapchain integration.
///
/// # Architecture
/// - **HWND**: Win32 window handle (created via CreateWindowExA)
/// - **Message pump**: PumpMessages() processes WM_* messages
/// - **Resize tracking**: Detects WM_SIZE and signals resize flag
/// - **Lifecycle**: Create() allocates HWND, Destroy() cleans up
///
/// # Rendering Integration
/// Pass GetHWND() to IDXGIFactory::CreateSwapChainForHwnd() for GPU output.
///
/// # Example
/// ```cpp
/// WindowDesc desc{.title = "MyGame", .width = 1920, .height = 1080};
/// PlatformWindow win;
/// win.Create(desc);
/// while (win.IsOpen()) {
///     win.PumpMessages();
///     // Render frame...
/// }
/// win.Destroy();
/// ```

#include <cstdint>
#include <vector>

#include "platform/platform_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#if defined(__APPLE__)
#ifdef __OBJC__
@class NSWindow;
@class NSView;
#else
typedef void NSWindow;
typedef void NSView;
#endif
#endif

namespace uz {

/// Information about a connected display monitor.
///
/// Populated by EnumerateMonitors(). Describes the geometry, DPI, and refresh
/// rate of a single display output. Virtual-screen coordinates are used so
/// multi-monitor layouts are contiguous.
///
/// # DPI Fields
/// `dpi` is the effective dots-per-inch (96 = 100% scaling).
/// `dpiScale` is the convenience ratio `dpi / 96.0f` (1.0 = no scaling).
struct MonitorInfo {
    char name[128] = {};        ///< Monitor device name (e.g. "\\\\.\\DISPLAY1").
    int32_t x = 0;              ///< Top-left X in virtual screen coordinates.
    int32_t y = 0;              ///< Top-left Y in virtual screen coordinates.
    int32_t width = 0;          ///< Full horizontal resolution (pixels).
    int32_t height = 0;         ///< Full vertical resolution (pixels).
    int32_t workX = 0;          ///< Work area top-left X (excludes taskbar).
    int32_t workY = 0;          ///< Work area top-left Y (excludes taskbar).
    int32_t workW = 0;          ///< Work area width.
    int32_t workH = 0;          ///< Work area height.
    uint32_t dpi = 96;          ///< Effective DPI (96 = 100% scaling).
    float dpiScale = 1.0f;      ///< DPI / 96.0f (1.0 = 100%, 1.5 = 150%).
    uint32_t refreshRate = 60;  ///< Refresh rate in Hz.
    bool isPrimary = false;     ///< True if this is the primary monitor.
};

/// Enumerates all connected display monitors.
///
/// Uses Win32 EnumDisplayMonitors to discover all active outputs.
/// Each monitor's geometry, DPI, and refresh rate are queried.
///
/// # Returns
/// Vector of MonitorInfo sorted with primary monitor first.
/// Empty vector on failure or if no monitors are detected.
///
/// # Thread Safety
/// Safe to call from any thread (Win32 monitor APIs are thread-safe).
std::vector<MonitorInfo> EnumerateMonitors();

class InputSystem;

/// Window display state.
enum class WindowState : uint8_t {
    Normal,                ///< Standard windowed mode.
    Minimized,             ///< Iconic / taskbar only.
    Maximized,             ///< Fills monitor work area with title bar.
    FullscreenBorderless,  ///< Covers entire monitor, no decorations.
};

/// Window creation parameters.
///
/// Passed to PlatformWindow::Create() to configure HWND behavior.
struct WindowDesc {
    const char* title = "UzEngine"; ///< Window title bar text
    int32_t width = 1280;           ///< Initial window width (pixels)
    int32_t height = 720;           ///< Initial window height (pixels)
    bool resizable = true;          ///< Allow user window resizing
    bool fullscreen = false;        ///< Request fullscreen mode
    int32_t minWidth = 320;         ///< Minimum window width (enforced by WM_GETMINMAXINFO)
    int32_t minHeight = 240;        ///< Minimum window height (enforced by WM_GETMINMAXINFO)
    bool rememberPosition = false;  ///< Save/restore window rect across sessions (future)
};

/// Platform window abstraction (Win32 HWND wrapper).
///
/// Single-threaded: all calls from main thread.
/// Thread-safe for: IsOpen(), GetWidth(), GetHeight() (read-only).
class PlatformWindow {
  public:
    /// Initializes empty window (not yet created).
    PlatformWindow() = default;

    /// Destroys the HWND if still open.
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    /// Creates a native Win32 window (HWND).
    ///
    /// # Arguments
    /// * `desc` - Window creation parameters (title, size, flags)
    ///
    /// # Returns
    /// - true if window created successfully
    /// - false if window creation failed (check logs)
    ///
    /// # Thread Safety
    /// Must call from main thread. Sets up WndProc and message pump.
    bool Create(const WindowDesc& desc);

    /// Destroys the HWND and releases window resources.
    ///
    /// # Behavior
    /// - Closes HWND if open
    /// - Resets all state (IsOpen = false)
    /// - Safe to call if window wasn't created
    void Destroy();

    /// Processes pending OS messages (WM_* events).
    ///
    /// # Returns
    /// - true if window is still open (no WM_QUIT)
    /// - false if user closed window or WM_QUIT sent
    ///
    /// # Behavior
    /// - Drains the message queue (non-blocking)
    /// - Updates IsOpen, WasResized flags based on WM_SIZE, WM_CLOSE
    /// - Dispatches input events to InputSystem if available
    ///
    /// # Must Call Every Frame
    /// Failure to call PumpMessages() means window hangs (unresponsive to OS).
    bool PumpMessages();

    /// Shows the window (makes visible).
    ///
    /// # Behavior
    /// - Equivalent to ShowWindow(m_hwnd, SW_SHOW)
    /// - Sets window z-order, brings to foreground
    void Show();

    /// Hides the window (makes invisible).
    ///
    /// # Behavior
    /// - Equivalent to ShowWindow(m_hwnd, SW_HIDE)
    /// - Does not destroy HWND or stop rendering
    void Hide();

    /// Returns true if window is open (not WM_CLOSE).
    bool IsOpen() const { return m_open; }

    /// Returns current window width (pixels).
    int32_t GetWidth() const { return m_width; }

    /// Returns current window height (pixels).
    int32_t GetHeight() const { return m_height; }

    /// Returns true if window was resized since last check.
    ///
    /// # Use Case
    /// Check before presenting to detect swapchain resize needs.
    /// ```cpp
    /// if (window.WasResized()) {
    ///     ResizeSwapchain(window.GetWidth(), window.GetHeight());
    ///     window.ClearResizedFlag();
    /// }
    /// ```
    bool WasResized() const { return m_resized; }

    /// Clears the resized flag (after handling WM_SIZE).
    void ClearResizedFlag() { m_resized = false; }

    /// Returns true if the window is currently minimized (iconic).
    ///
    /// When minimized, the swapchain surface is 0x0 — rendering MUST be skipped
    /// to avoid DXGI_ERROR_DEVICE_REMOVED. The message pump must still run.
    bool IsMinimized() const { return m_minimized; }

    /// Returns true if the application window has input focus.
    ///
    /// Unfocused windows may throttle rendering to reduce GPU/CPU usage.
    /// Focus state is updated via WM_ACTIVATEAPP messages.
    bool IsFocused() const { return m_focused; }

    /// Returns the current window display state.
    [[nodiscard]] WindowState GetState() const { return m_state; }

    /// Minimizes the window to the taskbar.
    ///
    /// Issues SW_MINIMIZE via ShowWindow. The actual state transition
    /// happens in WndProc when WM_SIZE(SIZE_MINIMIZED) arrives.
    void Minimize();

    /// Maximizes the window to fill the monitor work area.
    ///
    /// Issues SW_MAXIMIZE via ShowWindow. The actual state transition
    /// happens in WndProc when WM_SIZE(SIZE_MAXIMIZED) arrives.
    void Maximize();

    /// Restores the window from minimized or maximized to its previous size/position.
    ///
    /// Issues SW_RESTORE via ShowWindow. The actual state transition
    /// happens in WndProc when WM_SIZE(SIZE_RESTORED) arrives.
    void Restore();

    /// Switches to borderless fullscreen on the window's current monitor.
    ///
    /// Saves the current windowed position and style for later restoration
    /// via SetWindowed(). If already fullscreen, this is a no-op.
    void SetFullscreen();

    /// Restores from borderless fullscreen to the previously saved windowed position.
    ///
    /// If the window is not in fullscreen mode, this is a no-op.
    void SetWindowed();

    /// Returns true if the window is in borderless fullscreen mode.
    [[nodiscard]] bool IsFullscreen() const { return m_state == WindowState::FullscreenBorderless; }

    /// Dynamically changes the window title bar text.
    ///
    /// # Arguments
    /// * `title` — UTF-8 title string. Must not be nullptr.
    void SetTitle(const char* title);

    /// Callback type for window state changes.
    ///
    /// Receives the old state, new state, and user-provided context pointer.
    using StateChangeCallback = void(*)(WindowState oldState, WindowState newState, void* userData);

    /// Sets a callback invoked when the window state changes.
    ///
    /// # Arguments
    /// * `cb` — Callback function, or nullptr to clear.
    /// * `userData` — Opaque pointer forwarded to the callback.
    void SetStateChangeCallback(StateChangeCallback cb, void* userData = nullptr) {
        m_stateChangeCb = cb;
        m_stateChangeUserData = userData;
    }

#ifdef _WIN32
    /// Returns the native Win32 window handle.
    ///
    /// Pass to `IDXGIFactory::CreateSwapChainForHwnd()` for GPU rendering.
    /// Returns `nullptr` if the window was not created or was destroyed.
    [[nodiscard]] HWND GetHWND() const { return m_hwnd; }

    /// Returns the HINSTANCE of the application module.
    [[nodiscard]] HINSTANCE GetHInstance() const { return m_hinstance; }
#endif

    /// Returns the native window handle as an opaque pointer.
    ///
    /// Use this for platform-agnostic code (e.g., passing to `RHISwapChainDesc::windowHandle`).
    /// On Windows, this is the HWND cast to `void*`.
    /// On macOS, this is the NSWindow* cast to `void*`.
    ///
    /// # Returns
    /// - Non-null handle if the window was created and not yet destroyed.
    /// - `nullptr` if the window was not created or was destroyed.
#if defined(__APPLE__)
    [[nodiscard]] NativeWindowHandle GetNativeHandle() const { return m_nsWindow; }
#else
    [[nodiscard]] NativeWindowHandle GetNativeHandle() const;
#endif

    /// Returns the native display handle (always nullptr on Windows/macOS).
    ///
    /// Windows and macOS do not use a separate display connection.
    /// Provided for API parity with Linux/X11.
    ///
    /// # Returns
    /// Always `nullptr` on Windows and macOS.
    [[nodiscard]] NativeDisplayHandle GetNativeDisplayHandle() const { return nullptr; }

    /// Returns the current DPI scale factor for this window.
    ///
    /// # Returns
    /// Scale factor where 1.0 = 96 DPI (100%), 1.5 = 144 DPI (150%), etc.
    /// Defaults to 1.0 if the window has not been created yet.
    [[nodiscard]] float GetDpiScale() const { return m_dpiScale; }

    /// Returns the index of the monitor containing this window's center point.
    ///
    /// The index corresponds to the vector returned by EnumerateMonitors().
    ///
    /// # Returns
    /// Monitor index (0-based). Returns 0 if the window is not created or
    /// if the monitor cannot be determined.
    [[nodiscard]] int GetCurrentMonitor() const;

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

    /// Callback type for legacy WM_DROPFILES file drops.
    ///
    /// Receives an array of UTF-8 file paths and user-provided context.
    /// Prefer OLE drag-drop (see drag_drop.h) for richer feedback.
    using FileDropCallback = void(*)(const char* const* paths, int count, void* userData);

    /// Sets a callback for WM_DROPFILES file drops (legacy fallback).
    ///
    /// # Arguments
    /// * `cb` — Callback function, or nullptr to clear.
    /// * `userData` — Opaque pointer forwarded to the callback.
    ///
    /// # Note
    /// This is the simple WM_DROPFILES path. For hover/leave feedback,
    /// use the OLE IDropTarget API in `drag_drop.h` instead.
    void SetFileDropCallback(FileDropCallback cb, void* userData = nullptr) {
        m_fileDropCb = cb;
        m_fileDropUserData = userData;
    }

    /// Binds an InputSystem to receive keyboard and mouse events from this window.
    ///
    /// # Arguments
    /// * `input` — Input system to route WM_KEY*/WM_MOUSE* events to.
    ///   Pass `nullptr` to unbind. Must outlive this window while bound.
    ///
    /// # Platform Notes
    /// On macOS, also forwards the pointer to the UZMetalView so that
    /// NSEvent handlers can dispatch directly to the InputSystem.
#if defined(__APPLE__)
    void SetInputSystem(InputSystem* input);
#else
    void SetInputSystem(InputSystem* input) { m_input = input; }
#endif

  private:
#ifdef _WIN32
    HWND m_hwnd = nullptr;
    HINSTANCE m_hinstance = nullptr;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
#endif

#if defined(__APPLE__)
    /// Called by UZWindowDelegate when the window is resized.
    friend void MacOS_NotifyResize(PlatformWindow*, int32_t, int32_t);
    /// Called by UZWindowDelegate when the window is closed.
    friend void MacOS_NotifyClose(PlatformWindow*);

    void* m_nsWindow = nullptr;    ///< NSWindow* (stored as void* to avoid ObjC in header)
    void* m_nsView = nullptr;      ///< NSView* with CAMetalLayer backing
    void* m_appDelegate = nullptr; ///< UZWindowDelegate* (NSWindowDelegate impl)
#endif

    InputSystem* m_input = nullptr;
    int32_t m_width = 0;
    int32_t m_height = 0;
    bool m_open = false;
    bool m_resized = false;
    bool m_minimized = false;
    bool m_focused = true;

    WindowState m_state = WindowState::Normal;

    // ── Fullscreen state restoration ──────────────────────────────────
#ifdef _WIN32
    RECT m_windowedRect = {};       ///< Saved windowed position/size before fullscreen.
    DWORD m_windowedStyle = 0;      ///< Saved window style before fullscreen.
#endif

    // ── DPI state ─────────────────────────────────────────────────────
    float m_dpiScale = 1.0f;

    // ── Minimum size ─────────────────────────────────────────────────
    int32_t m_minWidth = 320;
    int32_t m_minHeight = 240;

    // ── State change callback ────────────────────────────────────────
    StateChangeCallback m_stateChangeCb = nullptr;
    void* m_stateChangeUserData = nullptr;

    // ── File drop callback ─────────────────────────────────────────
    FileDropCallback m_fileDropCb = nullptr;
    void* m_fileDropUserData = nullptr;

    /// Transitions to a new state and fires the state change callback.
    void TransitionState(WindowState newState);
};

} // namespace uz
