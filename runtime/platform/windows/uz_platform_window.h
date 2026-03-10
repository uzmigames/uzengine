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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace uz {

/// Window creation parameters.
///
/// Passed to PlatformWindow::Create() to configure HWND behavior.
struct WindowDesc {
    const char* title = "UzEngine"; ///< Window title bar text
    int32_t width = 1280;           ///< Initial window width (pixels)
    int32_t height = 720;           ///< Initial window height (pixels)
    bool resizable = true;          ///< Allow user window resizing
    bool fullscreen = false;        ///< Request fullscreen mode
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

#ifdef _WIN32
    HWND GetHWND() const { return m_hwnd; }
    HINSTANCE GetHInstance() const { return m_hinstance; }
#endif

  private:
#ifdef _WIN32
    HWND m_hwnd = nullptr;
    HINSTANCE m_hinstance = nullptr;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
#endif

    int32_t m_width = 0;
    int32_t m_height = 0;
    bool m_open = false;
    bool m_resized = false;
};

} // namespace uz
