#pragma once

/// UzEngine Platform Window — Native X11 implementation.
///
/// Creates and manages a native X11 window for rendering.
/// Provides window lifecycle, input handling, and Vulkan swapchain integration.
///
/// # Architecture
/// - **Display/Window**: X11 connection and window (created via XCreateSimpleWindow)
/// - **Message pump**: PumpMessages() processes X11 events via XPending/XNextEvent
/// - **Resize tracking**: Detects ConfigureNotify and signals resize flag
/// - **Lifecycle**: Create() opens display and creates window, Destroy() cleans up
///
/// # Rendering Integration
/// Pass GetDisplay() and GetWindow() to VkXlibSurfaceCreateInfoKHR for Vulkan output.
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

#include "platform/platform_types.h"

#ifdef __linux__
#include <X11/Xlib.h>
#endif

namespace uz {

class InputSystem;

/// Window creation parameters.
///
/// Passed to PlatformWindow::Create() to configure window behavior.
struct WindowDesc {
    const char* title = "UzEngine"; ///< Window title bar text
    int32_t width = 1280;           ///< Initial window width (pixels)
    int32_t height = 720;           ///< Initial window height (pixels)
    bool resizable = true;          ///< Allow user window resizing
    bool fullscreen = false;        ///< Request fullscreen mode
};

/// Platform window abstraction (X11 wrapper).
///
/// Single-threaded: all calls from main thread.
/// Thread-safe for: IsOpen(), GetWidth(), GetHeight() (read-only).
class PlatformWindow {
  public:
    /// Initializes empty window (not yet created).
    PlatformWindow() = default;

    /// Destroys the X11 window if still open.
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    /// Creates a native X11 window.
    ///
    /// # Arguments
    /// * `desc` - Window creation parameters (title, size, flags)
    ///
    /// # Returns
    /// - true if window created successfully
    /// - false if window creation failed (check logs)
    ///
    /// # Thread Safety
    /// Must call from main thread. Opens X11 display and creates window.
    bool Create(const WindowDesc& desc);

    /// Destroys the X11 window and closes the display connection.
    ///
    /// # Behavior
    /// - Destroys window and closes display if open
    /// - Resets all state (IsOpen = false)
    /// - Safe to call if window wasn't created
    void Destroy();

    /// Processes pending X11 events (key, mouse, configure, client messages).
    ///
    /// # Returns
    /// - true if window is still open (no close request)
    /// - false if user closed window via WM_DELETE_WINDOW
    ///
    /// # Behavior
    /// - Drains the event queue (non-blocking via XPending)
    /// - Updates IsOpen, WasResized flags based on ConfigureNotify, ClientMessage
    /// - Dispatches input events to InputSystem if available
    ///
    /// # Must Call Every Frame
    /// Failure to call PumpMessages() means window hangs (unresponsive to WM).
    bool PumpMessages();

    /// Shows the window (maps to screen).
    ///
    /// # Behavior
    /// - Equivalent to XMapWindow
    void Show();

    /// Hides the window (unmaps from screen).
    ///
    /// # Behavior
    /// - Equivalent to XUnmapWindow
    /// - Does not destroy window or stop rendering
    void Hide();

    /// Returns true if window is open (not closed).
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

    /// Clears the resized flag (after handling resize).
    void ClearResizedFlag() { m_resized = false; }

    /// Returns true if the window is currently minimized (unmapped/iconic).
    ///
    /// When minimized, the swapchain surface may be 0x0 — rendering MUST be skipped.
    /// The message pump must still run.
    bool IsMinimized() const { return m_minimized; }

    /// Returns true if the application window has input focus.
    ///
    /// Unfocused windows may throttle rendering to reduce GPU/CPU usage.
    /// Focus state is updated via FocusIn/FocusOut events.
    bool IsFocused() const { return m_focused; }

#ifdef __linux__
    /// Returns the X11 display connection.
    ///
    /// Pass to `VkXlibSurfaceCreateInfoKHR::dpy` for Vulkan surface creation.
    /// Returns `nullptr` if the window was not created or was destroyed.
    [[nodiscard]] Display* GetDisplay() const { return m_display; }

    /// Returns the X11 window handle.
    ///
    /// Pass to `VkXlibSurfaceCreateInfoKHR::window` for Vulkan surface creation.
    /// Returns `0` if the window was not created or was destroyed.
    [[nodiscard]] ::Window GetWindow() const { return m_window; }
#endif

    /// Returns the native window handle as an opaque pointer.
    ///
    /// Use this for platform-agnostic code (e.g., passing to `RHISwapChainDesc::windowHandle`).
    /// On Linux/X11, this is the X11 `Window` (unsigned long) cast to `void*` via `reinterpret_cast`.
    ///
    /// # Returns
    /// - Non-null handle if the window was created and not yet destroyed.
    /// - `nullptr` if the window was not created or was destroyed.
    [[nodiscard]] NativeWindowHandle GetNativeHandle() const {
        #ifdef __linux__
        return reinterpret_cast<NativeWindowHandle>(m_window);
        #else
        return nullptr;
        #endif
    }

    /// Returns the native display handle (X11 Display* on Linux).
    ///
    /// Pass to `VkXlibSurfaceCreateInfoKHR::dpy` for Vulkan surface creation.
    ///
    /// # Returns
    /// - Non-null Display* if the window was created and not yet destroyed.
    /// - `nullptr` if not created, destroyed, or on non-Linux platforms.
    [[nodiscard]] NativeDisplayHandle GetNativeDisplayHandle() const {
        #ifdef __linux__
        return static_cast<NativeDisplayHandle>(m_display);
        #else
        return nullptr;
        #endif
    }

    /// Binds an InputSystem to receive keyboard and mouse events from this window.
    ///
    /// # Arguments
    /// * `input` — Input system to route X11 key/mouse events to.
    ///   Pass `nullptr` to unbind. Must outlive this window while bound.
    void SetInputSystem(InputSystem* input) { m_input = input; }

  private:
#ifdef __linux__
    Display* m_display = nullptr;
    ::Window m_window = 0;
    Atom m_wmDeleteMessage = 0;
#endif

    InputSystem* m_input = nullptr;
    int32_t m_width = 0;
    int32_t m_height = 0;
    bool m_open = false;
    bool m_resized = false;
    bool m_minimized = false;
    bool m_focused = true;
};

} // namespace uz
