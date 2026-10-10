/// UzEngine Platform Window Implementation (Windows)
///
/// Win32 window creation and message handling.
/// Creates HWND for DX12 rendering and dispatches WM_* messages to handlers.
/// Integrates with InputSystem for input event routing.

#include "platform_window.h"
#include "input.h"

#ifdef _WIN32

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <string>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

/// Log category for window lifecycle and event messages.
UZ_DEFINE_LOG_CATEGORY(Window, Trace);

namespace uz {

static const wchar_t* kWindowClass = L"UzEngineWindow";

static uint16_t NormalizeWindowKey(WPARAM wp, LPARAM lp) {
    const auto flags = static_cast<uintptr_t>(lp);
    const bool extended = (flags & (uintptr_t{1} << 24)) != 0;
    switch (wp) {
    case VK_SHIFT: {
        const UINT scanCode = static_cast<UINT>((flags >> 16) & 0xffu);
        const UINT mapped = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
        return static_cast<uint16_t>(mapped != 0 ? mapped : wp);
    }
    case VK_CONTROL:
        return extended ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:
        return extended ? VK_RMENU : VK_LMENU;
    default:
        return static_cast<uint16_t>(wp);
    }
}

LRESULT CALLBACK PlatformWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PlatformWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    case WM_SIZE: {
        if (self) {
            if (wp == SIZE_MINIMIZED) {
                self->m_minimized = true;
                self->TransitionState(WindowState::Minimized);
            } else {
                bool wasMinimized = self->m_minimized;
                self->m_minimized = false;
                self->m_width = LOWORD(lp);
                self->m_height = HIWORD(lp);
                self->m_resized = true;

                if (wp == SIZE_MAXIMIZED) {
                    self->TransitionState(WindowState::Maximized);
                } else if (wp == SIZE_RESTORED && self->m_state != WindowState::FullscreenBorderless) {
                    self->TransitionState(WindowState::Normal);
                }

                if (wasMinimized) {
                    UZ_LOG(Window, Info, "Window restored from minimized state");
                }

                // Re-clip cursor if mouse capture is active
                if (self->m_input)
                    self->m_input->UpdateMouseCapture(hwnd);
            }
        }
        return 0;
    }

    case WM_CLOSE:
        if (self) {
            self->m_open = false;
            UZ_LOG(Window, Info, "Window close requested");
        }
        return 0;

    case WM_DESTROY:
        // In library mode (embedded inside VSCode/Electron), do NOT post
        // WM_QUIT — that would kill the entire host process.
        if (!self || !self->IsLibraryMode()) {
            PostQuitMessage(0);
        }
        return 0;

    case WM_ACTIVATEAPP:
        if (self) {
            self->m_focused = (wp != 0);
            UZ_LOG(Window, Info, "Application focus: {}", self->m_focused ? "gained" : "lost");
        }
        return 0;

    // ── Input dispatch ───────────────────────────────────────────────
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (self && self->m_input)
            self->m_input->OnKeyDown(NormalizeWindowKey(wp, lp));
        if (msg == WM_SYSKEYDOWN) return DefWindowProcW(hwnd, msg, wp, lp);
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (self && self->m_input)
            self->m_input->OnKeyUp(NormalizeWindowKey(wp, lp));
        if (msg == WM_SYSKEYUP) return DefWindowProcW(hwnd, msg, wp, lp);
        return 0;

    case WM_CHAR:
        if (self && self->m_input)
            self->m_input->OnChar(static_cast<uint16_t>(wp));
        return 0;

    case WM_MOUSEMOVE:
        if (self && self->m_input)
            self->m_input->OnMouseMove(
                static_cast<float>(GET_X_LPARAM(lp)),
                static_cast<float>(GET_Y_LPARAM(lp)));
        return 0;

    case WM_LBUTTONDOWN:
        if (self && self->m_input)
            self->m_input->OnMouseButton(MouseButton::Left, true);
        return 0;
    case WM_LBUTTONUP:
        if (self && self->m_input)
            self->m_input->OnMouseButton(MouseButton::Left, false);
        return 0;
    case WM_RBUTTONDOWN:
        if (self && self->m_input)
            self->m_input->OnMouseButton(MouseButton::Right, true);
        return 0;
    case WM_RBUTTONUP:
        if (self && self->m_input)
            self->m_input->OnMouseButton(MouseButton::Right, false);
        return 0;
    case WM_MBUTTONDOWN:
        if (self && self->m_input)
            self->m_input->OnMouseButton(MouseButton::Middle, true);
        return 0;
    case WM_MBUTTONUP:
        if (self && self->m_input)
            self->m_input->OnMouseButton(MouseButton::Middle, false);
        return 0;

    case WM_MOUSEWHEEL:
        if (self && self->m_input)
            self->m_input->OnMouseScroll(
                static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / 120.f);
        return 0;

    // ── Raw input dispatch ────────────────────────────────────────────
    case WM_INPUT:
        if (self && self->m_input)
            self->m_input->OnRawMouseInput(lp);
        return 0;

    // ── Cursor management ─────────────────────────────────────────────
    case WM_SETCURSOR:
        if (self && self->m_input && LOWORD(lp) == HTCLIENT) {
            if (self->m_input->ApplyCursorState())
                return TRUE;
        }
        break;

    // ── Re-clip cursor on window move ─────────────────────────────────
    case WM_MOVE:
        if (self && self->m_input)
            self->m_input->UpdateMouseCapture(hwnd);
        return 0;

    case WM_DPICHANGED: {
        if (self) {
            const uint32_t newDpi = LOWORD(wp);
            const float oldScale = self->m_dpiScale;
            self->m_dpiScale = static_cast<float>(newDpi) / 96.0f;

            // OS provides the ideal window rect for the new DPI
            const auto* suggested = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(self->m_hwnd, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            UZ_LOG(Window, Info, "DPI changed: {} -> {} (scale {:.2f} -> {:.2f})",
                   static_cast<uint32_t>(oldScale * 96.0f), newDpi, oldScale, self->m_dpiScale);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        if (self) {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = self->m_minWidth;
            mmi->ptMinTrackSize.y = self->m_minHeight;
        }
        return 0;
    }

    // ── Legacy file drop (WM_DROPFILES fallback) ─────────────────────
    case WM_DROPFILES: {
        if (self && self->m_fileDropCb) {
            auto hDrop = reinterpret_cast<HDROP>(wp);
            const UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

            std::vector<std::string> paths;
            paths.reserve(fileCount);

            for (UINT i = 0; i < fileCount; ++i) {
                const UINT charCount = DragQueryFileW(hDrop, i, nullptr, 0);
                if (charCount == 0) continue;

                std::vector<wchar_t> widePath(static_cast<size_t>(charCount) + 1);
                DragQueryFileW(hDrop, i, widePath.data(), charCount + 1);

                const int utf8Len = WideCharToMultiByte(
                    CP_UTF8, 0, widePath.data(), -1, nullptr, 0, nullptr, nullptr);
                if (utf8Len > 1) {
                    std::string path(static_cast<size_t>(utf8Len - 1), '\0');
                    WideCharToMultiByte(
                        CP_UTF8, 0, widePath.data(), -1,
                        path.data(), utf8Len, nullptr, nullptr);
                    paths.push_back(std::move(path));
                }
            }

            DragFinish(hDrop);

            if (!paths.empty()) {
                std::vector<const char*> cPaths;
                cPaths.reserve(paths.size());
                for (const auto& p : paths) cPaths.push_back(p.c_str());

                UZ_LOG(Window, Info, "WM_DROPFILES: {} file(s)", paths.size());
                self->m_fileDropCb(cPaths.data(), static_cast<int>(cPaths.size()),
                                   self->m_fileDropUserData);
            }
        }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool PlatformWindow::Create(const WindowDesc& desc) {
    UZ_PROFILE_FUNCTION();
    m_hinstance = GetModuleHandleW(nullptr);

    // Enable per-monitor DPI awareness before creating any windows.
    // Harmless if already set (returns E_ACCESSDENIED, which we ignore).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hinstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(m_hinstance, MAKEINTRESOURCEW(1));   // IDI_ICON1 from app.rc
    wc.hIconSm = LoadIconW(m_hinstance, MAKEINTRESOURCEW(1)); // Small icon for title bar
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            UZ_LOG(Window, Error, "RegisterClassEx failed: {}", err);
            return false;
        }
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    RECT rect = {0, 0, desc.width, desc.height};
    AdjustWindowRect(&rect, style, FALSE);

    int adjustedWidth = rect.right - rect.left;
    int adjustedHeight = rect.bottom - rect.top;

    // Convert title to wide string
    wchar_t wTitle[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, desc.title, -1, wTitle, 256);

    m_hwnd = CreateWindowExW(0, kWindowClass, wTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             adjustedWidth, adjustedHeight, nullptr, nullptr, m_hinstance,
                             this // passed to WM_CREATE → GWLP_USERDATA
    );

    if (!m_hwnd) {
        UZ_LOG(Window, Error, "CreateWindowEx failed: {}", GetLastError());
        return false;
    }

    // Accept legacy WM_DROPFILES messages (fallback for non-OLE drops)
    DragAcceptFiles(m_hwnd, TRUE);

    // Query per-monitor DPI for the newly created window
    const uint32_t dpi = GetDpiForWindow(m_hwnd);
    m_dpiScale = static_cast<float>(dpi) / 96.0f;

    // Re-adjust window size for the actual DPI if scaling is active
    if (m_dpiScale != 1.0f) {
        const int scaledW = static_cast<int>(desc.width * m_dpiScale);
        const int scaledH = static_cast<int>(desc.height * m_dpiScale);
        RECT dpiRect = {0, 0, scaledW, scaledH};
        AdjustWindowRect(&dpiRect, style, FALSE);
        SetWindowPos(m_hwnd, nullptr, 0, 0,
                     dpiRect.right - dpiRect.left, dpiRect.bottom - dpiRect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        UZ_LOG(Window, Info, "DPI-scaled window size: {}x{} (DPI={}, scale={:.2f})",
               scaledW, scaledH, dpi, m_dpiScale);
    }

    m_width = desc.width;
    m_height = desc.height;
    m_open = true;
    m_minWidth = desc.minWidth;
    m_minHeight = desc.minHeight;

    if (desc.fullscreen) {
        // Borderless fullscreen
        HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(monitor, &mi);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);

        // Save windowed state so SetWindowed() can restore it
        m_windowedStyle = WS_OVERLAPPEDWINDOW;
        m_windowedRect = {CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT + desc.width, CW_USEDEFAULT + desc.height};
        TransitionState(WindowState::FullscreenBorderless);
    }

    UZ_LOG(Window, Info, "Window created: {}x{} ({})", m_width, m_height,
           desc.fullscreen ? "fullscreen" : "windowed");

    return true;
}

void PlatformWindow::Destroy() {
    UZ_PROFILE_FUNCTION();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        m_open = false;
        UZ_LOG(Window, Info, "Window destroyed");
    }
}

PlatformWindow::~PlatformWindow() {
    Destroy();
}

bool PlatformWindow::PumpMessages() {
    UZ_PROFILE_FUNCTION();
    MSG msg = {};
    // In library mode, scope PeekMessage to our HWND only so we do not
    // steal messages from the host process (VSCode/Electron).
    HWND filterHwnd = m_libraryMode ? m_hwnd : nullptr;
    while (PeekMessageW(&msg, filterHwnd, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_open = false;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return m_open;
}

void PlatformWindow::Show() {
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_SHOW);
}

void PlatformWindow::Hide() {
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
}

NativeWindowHandle PlatformWindow::GetNativeHandle() const {
    return static_cast<NativeWindowHandle>(m_hwnd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Window State Machine
// ─────────────────────────────────────────────────────────────────────────────

void PlatformWindow::TransitionState(WindowState newState) {
    if (m_state == newState) return;
    WindowState old = m_state;
    m_state = newState;
    if (m_stateChangeCb) {
        m_stateChangeCb(old, newState, m_stateChangeUserData);
    }
    UZ_LOG(Window, Info, "Window state: {} -> {}",
           static_cast<int>(old), static_cast<int>(newState));
}

void PlatformWindow::Minimize() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);
}

void PlatformWindow::Maximize() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_MAXIMIZE);
}

void PlatformWindow::Restore() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_RESTORE);
}

void PlatformWindow::SetFullscreen() {
    if (!m_hwnd || m_state == WindowState::FullscreenBorderless) return;

    // Save current windowed state for restoration
    m_windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    GetWindowRect(m_hwnd, &m_windowedRect);

    // Switch to borderless fullscreen on the window's current monitor
    HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);

    SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(m_hwnd, HWND_TOP,
                 mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

    TransitionState(WindowState::FullscreenBorderless);
}

void PlatformWindow::SetWindowed() {
    if (!m_hwnd || m_state != WindowState::FullscreenBorderless) return;

    // Restore saved windowed style and position
    SetWindowLongPtrW(m_hwnd, GWL_STYLE, m_windowedStyle);
    SetWindowPos(m_hwnd, HWND_NOTOPMOST,
                 m_windowedRect.left, m_windowedRect.top,
                 m_windowedRect.right - m_windowedRect.left,
                 m_windowedRect.bottom - m_windowedRect.top,
                 SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

    TransitionState(WindowState::Normal);
}

void PlatformWindow::SetTitle(const char* title) {
    if (!m_hwnd || !title) return;
    wchar_t wTitle[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wTitle, 256);
    SetWindowTextW(m_hwnd, wTitle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Monitor & DPI
// ─────────────────────────────────────────────────────────────────────────────

std::vector<MonitorInfo> EnumerateMonitors() {
    struct EnumContext {
        std::vector<MonitorInfo> monitors;
    };
    EnumContext ctx;

    auto callback = [](HMONITOR hmon, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* ctx = reinterpret_cast<EnumContext*>(data);

        MONITORINFOEXW miex = {};
        miex.cbSize = sizeof(miex);
        if (!GetMonitorInfoW(hmon, &miex))
            return TRUE; // Skip this monitor, continue enumeration

        MonitorInfo info = {};

        // Convert device name to narrow string
        WideCharToMultiByte(CP_UTF8, 0, miex.szDevice, -1,
                            info.name, sizeof(info.name), nullptr, nullptr);

        info.x = miex.rcMonitor.left;
        info.y = miex.rcMonitor.top;
        info.width = miex.rcMonitor.right - miex.rcMonitor.left;
        info.height = miex.rcMonitor.bottom - miex.rcMonitor.top;

        info.workX = miex.rcWork.left;
        info.workY = miex.rcWork.top;
        info.workW = miex.rcWork.right - miex.rcWork.left;
        info.workH = miex.rcWork.bottom - miex.rcWork.top;

        info.isPrimary = (miex.dwFlags & MONITORINFOF_PRIMARY) != 0;

        // Query per-monitor DPI via Shell Scaling API
        UINT dpiX = 96, dpiY = 96;
        if (SUCCEEDED(GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
            info.dpi = dpiX;
            info.dpiScale = static_cast<float>(dpiX) / 96.0f;
        }

        // Query refresh rate via EnumDisplaySettingsW
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(miex.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
            info.refreshRate = dm.dmDisplayFrequency;
        }

        ctx->monitors.push_back(info);
        return TRUE;
    };

    EnumDisplayMonitors(nullptr, nullptr, callback, reinterpret_cast<LPARAM>(&ctx));

    // Sort: primary monitor first, then by position (left-to-right, top-to-bottom)
    std::sort(ctx.monitors.begin(), ctx.monitors.end(),
              [](const MonitorInfo& a, const MonitorInfo& b) {
                  if (a.isPrimary != b.isPrimary) return a.isPrimary;
                  if (a.x != b.x) return a.x < b.x;
                  return a.y < b.y;
              });

    return ctx.monitors;
}

int PlatformWindow::GetCurrentMonitor() const {
    if (!m_hwnd) return 0;

    HMONITOR hmon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hmon) return 0;

    MONITORINFOEXW miex = {};
    miex.cbSize = sizeof(miex);
    if (!GetMonitorInfoW(hmon, &miex)) return 0;

    auto monitors = EnumerateMonitors();
    for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
        if (monitors[i].x == miex.rcMonitor.left &&
            monitors[i].y == miex.rcMonitor.top &&
            monitors[i].width == (miex.rcMonitor.right - miex.rcMonitor.left) &&
            monitors[i].height == (miex.rcMonitor.bottom - miex.rcMonitor.top)) {
            return i;
        }
    }
    return 0;
}

void PlatformWindow::SetFullscreenOnMonitor(int monitorIndex) {
    if (!m_hwnd) return;

    auto monitors = EnumerateMonitors();
    if (monitors.empty()) return;

    // Clamp to valid range
    if (monitorIndex < 0 || monitorIndex >= static_cast<int>(monitors.size())) {
        UZ_LOG(Window, Warning, "Monitor index {} out of range (0-{}), using primary",
               monitorIndex, static_cast<int>(monitors.size()) - 1);
        monitorIndex = 0;
    }

    const auto& mon = monitors[monitorIndex];

    // Save windowed state if not already fullscreen
    if (m_state != WindowState::FullscreenBorderless) {
        m_windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
        GetWindowRect(m_hwnd, &m_windowedRect);
    }

    SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(m_hwnd, HWND_TOP,
                 mon.x, mon.y, mon.width, mon.height,
                 SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

    TransitionState(WindowState::FullscreenBorderless);
    UZ_LOG(Window, Info, "Fullscreen on monitor {}: '{}' ({}x{} @ {}Hz, DPI={})",
           monitorIndex, mon.name, mon.width, mon.height, mon.refreshRate, mon.dpi);
}

} // namespace uz

#endif // _WIN32
