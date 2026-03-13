/// UzEngine Platform Window Implementation (Linux/X11)
///
/// X11 window creation and event handling.
/// Creates a native X11 window for Vulkan rendering and dispatches events
/// to InputSystem for input routing.

#include "platform_window.h"
#include "input.h"

#ifdef __linux__

#include "log.h"
#include "profiler.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

/// Log category for window lifecycle and event messages.
UZ_DEFINE_LOG_CATEGORY(Window, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// X11 Keysym → Engine Key Translation
// ─────────────────────────────────────────────────────────────────────────────

/// Translates an X11 keysym to a Windows-compatible virtual key code (uint16_t).
///
/// The engine's Key enum uses Windows VK_* codes internally. This function maps
/// X11 keysyms to the same numeric values so the platform-independent InputSystem
/// works unchanged on Linux.
///
/// # Arguments
/// * `keysym` — X11 keysym from XLookupKeysym or XLookupString.
///
/// # Returns
/// The corresponding VK_* code (0-255), or 0 for unknown/unmapped keysyms.
static uint16_t TranslateKeysym(KeySym keysym) {
    // Letters (XK_a..XK_z and XK_A..XK_Z → 'A'..'Z')
    if (keysym >= XK_a && keysym <= XK_z)
        return static_cast<uint16_t>(keysym - XK_a + 'A');
    if (keysym >= XK_A && keysym <= XK_Z)
        return static_cast<uint16_t>(keysym - XK_A + 'A');

    // Digits (XK_0..XK_9 → '0'..'9')
    if (keysym >= XK_0 && keysym <= XK_9)
        return static_cast<uint16_t>(keysym); // ASCII-compatible

    // Function keys (XK_F1..XK_F12 → 0x70..0x7B)
    if (keysym >= XK_F1 && keysym <= XK_F12)
        return static_cast<uint16_t>(0x70 + (keysym - XK_F1));

    switch (keysym) {
    case XK_Escape:       return 0x1B;
    case XK_Tab:          return 0x09;
    case XK_space:        return 0x20;
    case XK_Return:       return 0x0D;
    case XK_BackSpace:    return 0x08;

    case XK_Shift_L:      return 0xA0; // VK_LSHIFT
    case XK_Shift_R:      return 0xA1; // VK_RSHIFT
    case XK_Control_L:    return 0xA2; // VK_LCONTROL
    case XK_Control_R:    return 0xA3; // VK_RCONTROL
    case XK_Alt_L:        return 0xA4; // VK_LMENU
    case XK_Alt_R:        return 0xA5; // VK_RMENU

    case XK_Left:         return 0x25;
    case XK_Up:           return 0x26;
    case XK_Right:        return 0x27;
    case XK_Down:         return 0x28;

    case XK_Delete:       return 0x2E;
    case XK_Insert:       return 0x2D;
    case XK_Home:         return 0x24;
    case XK_End:          return 0x23;
    case XK_Page_Up:      return 0x21;
    case XK_Page_Down:    return 0x22;

    default:              return 0; // Unknown
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool PlatformWindow::Create(const WindowDesc& desc) {
    UZ_PROFILE_FUNCTION();

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        UZ_LOG(Window, Error, "XOpenDisplay failed — no X11 display available");
        return false;
    }

    int screen = DefaultScreen(m_display);
    ::Window root = RootWindow(m_display, screen);

    m_window = XCreateSimpleWindow(
        m_display, root,
        0, 0,
        static_cast<unsigned int>(desc.width),
        static_cast<unsigned int>(desc.height),
        0,                                         // border width
        BlackPixel(m_display, screen),             // border color
        BlackPixel(m_display, screen)              // background color
    );

    if (!m_window) {
        UZ_LOG(Window, Error, "XCreateSimpleWindow failed");
        XCloseDisplay(m_display);
        m_display = nullptr;
        return false;
    }

    // Set window title
    XStoreName(m_display, m_window, desc.title);

    // Register for WM_DELETE_WINDOW so we get a graceful close event
    m_wmDeleteMessage = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(m_display, m_window, &m_wmDeleteMessage, 1);

    // Select events we care about
    long eventMask = KeyPressMask | KeyReleaseMask
                   | ButtonPressMask | ButtonReleaseMask
                   | PointerMotionMask
                   | StructureNotifyMask  // ConfigureNotify, MapNotify, UnmapNotify
                   | FocusChangeMask;
    XSelectInput(m_display, m_window, eventMask);

    // Non-resizable: set min/max size hints to the requested dimensions
    if (!desc.resizable) {
        XSizeHints* hints = XAllocSizeHints();
        if (hints) {
            hints->flags = PMinSize | PMaxSize;
            hints->min_width = desc.width;
            hints->min_height = desc.height;
            hints->max_width = desc.width;
            hints->max_height = desc.height;
            XSetWMNormalHints(m_display, m_window, hints);
            XFree(hints);
        }
    }

    m_width = desc.width;
    m_height = desc.height;
    m_open = true;

    if (desc.fullscreen) {
        // Request _NET_WM_STATE_FULLSCREEN via EWMH
        Atom wmState = XInternAtom(m_display, "_NET_WM_STATE", False);
        Atom wmFullscreen = XInternAtom(m_display, "_NET_WM_STATE_FULLSCREEN", False);

        XChangeProperty(m_display, m_window, wmState, XA_ATOM, 32,
                        PropModeReplace,
                        reinterpret_cast<unsigned char*>(&wmFullscreen), 1);
    }

    UZ_LOG(Window, Info, "Window created: {}x{} ({})", m_width, m_height,
           desc.fullscreen ? "fullscreen" : "windowed");

    return true;
}

void PlatformWindow::Destroy() {
    UZ_PROFILE_FUNCTION();
    if (m_display && m_window) {
        XDestroyWindow(m_display, m_window);
        m_window = 0;
        UZ_LOG(Window, Info, "Window destroyed");
    }
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
    m_open = false;
}

PlatformWindow::~PlatformWindow() {
    Destroy();
}

bool PlatformWindow::PumpMessages() {
    UZ_PROFILE_FUNCTION();
    if (!m_display) return false;

    while (XPending(m_display)) {
        XEvent event = {};
        XNextEvent(m_display, &event);

        switch (event.type) {
        case ConfigureNotify: {
            auto& cfg = event.xconfigure;
            int32_t newWidth = static_cast<int32_t>(cfg.width);
            int32_t newHeight = static_cast<int32_t>(cfg.height);
            if (newWidth != m_width || newHeight != m_height) {
                m_width = newWidth;
                m_height = newHeight;
                m_resized = true;
            }
            break;
        }

        case ClientMessage: {
            // WM_DELETE_WINDOW protocol — user clicked close button
            if (static_cast<Atom>(event.xclient.data.l[0]) == m_wmDeleteMessage) {
                m_open = false;
                UZ_LOG(Window, Info, "Window close requested");
            }
            break;
        }

        case FocusIn:
            m_focused = true;
            UZ_LOG(Window, Info, "Application focus: gained");
            break;

        case FocusOut:
            m_focused = false;
            UZ_LOG(Window, Info, "Application focus: lost");
            break;

        case MapNotify:
            if (m_minimized) {
                m_minimized = false;
                m_resized = true; // Force swapchain resize on restore
                UZ_LOG(Window, Info, "Window restored from minimized state");
            }
            break;

        case UnmapNotify:
            m_minimized = true;
            UZ_LOG(Window, Info, "Window minimized");
            break;

        // ── Input dispatch ───────────────────────────────────────────────

        case KeyPress: {
            if (m_input) {
                KeySym keysym = XLookupKeysym(&event.xkey, 0);
                uint16_t vk = TranslateKeysym(keysym);
                if (vk != 0)
                    m_input->OnKeyDown(vk);
            }
            break;
        }

        case KeyRelease: {
            // X11 generates spurious KeyRelease/KeyPress pairs for auto-repeat.
            // Detect and skip the KeyRelease if the next event is an immediate KeyPress
            // for the same key.
            if (XPending(m_display)) {
                XEvent next = {};
                XPeekEvent(m_display, &next);
                if (next.type == KeyPress
                    && next.xkey.time == event.xkey.time
                    && next.xkey.keycode == event.xkey.keycode) {
                    // Auto-repeat pair — consume both and treat as held
                    XNextEvent(m_display, &next);
                    break;
                }
            }
            if (m_input) {
                KeySym keysym = XLookupKeysym(&event.xkey, 0);
                uint16_t vk = TranslateKeysym(keysym);
                if (vk != 0)
                    m_input->OnKeyUp(vk);
            }
            break;
        }

        case MotionNotify:
            if (m_input) {
                m_input->OnMouseMove(
                    static_cast<float>(event.xmotion.x),
                    static_cast<float>(event.xmotion.y));
            }
            break;

        case ButtonPress: {
            if (!m_input) break;
            unsigned int button = event.xbutton.button;
            if (button == Button1)
                m_input->OnMouseButton(MouseButton::Left, true);
            else if (button == Button2)
                m_input->OnMouseButton(MouseButton::Middle, true);
            else if (button == Button3)
                m_input->OnMouseButton(MouseButton::Right, true);
            else if (button == Button4) // Scroll up
                m_input->OnMouseScroll(1.0f);
            else if (button == Button5) // Scroll down
                m_input->OnMouseScroll(-1.0f);
            break;
        }

        case ButtonRelease: {
            if (!m_input) break;
            unsigned int button = event.xbutton.button;
            if (button == Button1)
                m_input->OnMouseButton(MouseButton::Left, false);
            else if (button == Button2)
                m_input->OnMouseButton(MouseButton::Middle, false);
            else if (button == Button3)
                m_input->OnMouseButton(MouseButton::Right, false);
            // Scroll buttons (4,5) don't have a "release" in engine terms
            break;
        }

        default:
            break;
        }
    }

    return m_open;
}

void PlatformWindow::Show() {
    if (m_display && m_window)
        XMapWindow(m_display, m_window);
}

void PlatformWindow::Hide() {
    if (m_display && m_window)
        XUnmapWindow(m_display, m_window);
}

} // namespace uz

#endif // __linux__
