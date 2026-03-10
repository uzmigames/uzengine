/// UzEngine Platform Window Implementation (Windows)
///
/// Win32 window creation and message handling.
/// Creates HWND for DX12 rendering and dispatches WM_* messages to handlers.
/// Integrates with InputSystem for input event routing.

#include "uz_platform_window.h"

#ifdef _WIN32

#include "../../core/logging/uz_log.h"

/// Log category for window lifecycle and event messages.
UZ_DEFINE_LOG_CATEGORY(Window, Trace);

namespace uz {

static const wchar_t* kWindowClass = L"UzEngineWindow";

LRESULT CALLBACK PlatformWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PlatformWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    case WM_SIZE: {
        if (self && wp != SIZE_MINIMIZED) {
            self->m_width = LOWORD(lp);
            self->m_height = HIWORD(lp);
            self->m_resized = true;
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
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool PlatformWindow::Create(const WindowDesc& desc) {
    m_hinstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hinstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
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

    m_width = desc.width;
    m_height = desc.height;
    m_open = true;

    if (desc.fullscreen) {
        // Borderless fullscreen
        HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(monitor, &mi);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
    }

    UZ_LOG(Window, Info, "Window created: {}x{} ({})", m_width, m_height,
           desc.fullscreen ? "fullscreen" : "windowed");

    return true;
}

void PlatformWindow::Destroy() {
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
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
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

} // namespace uz

#endif // _WIN32
