// ---------------------------------------------------------------------------
// win32_app.cpp -- Win32 window creation and stdio setup for the engine.
// ---------------------------------------------------------------------------
#include "win32_app.h"

#include "common/types.h"

#include <io.h>
#include <fcntl.h>
#include <cstdio>

namespace {

constexpr const wchar_t* kWindowClassName = L"UzEngineWndClass";
constexpr const wchar_t* kWindowTitle     = L"UzEngine PoC";

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        // If the renderer needs notification, handle via SceneParams::requestResize.
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // anonymous namespace

void setupStdio() {
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStdOut && hStdOut != INVALID_HANDLE_VALUE) {
        int fdOut = _open_osfhandle(reinterpret_cast<intptr_t>(hStdOut), 0);
        if (fdOut >= 0) {
            FILE* fOut = _fdopen(fdOut, "w");
            if (fOut) { *stdout = *fOut; setvbuf(stdout, nullptr, _IONBF, 0); }
        }
    }
    if (hStdErr && hStdErr != INVALID_HANDLE_VALUE) {
        int fdErr = _open_osfhandle(reinterpret_cast<intptr_t>(hStdErr), 0);
        if (fdErr >= 0) {
            FILE* fErr = _fdopen(fdErr, "w");
            if (fErr) { *stderr = *fErr; setvbuf(stderr, nullptr, _IONBF, 0); }
        }
    }
}

HWND createEngineWindow(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        return nullptr;
    }

    // Detect headless / windowed-debug mode.
    wchar_t headlessEnv[4]{};
    wchar_t windowedEnv[4]{};
    const bool headless  = GetEnvironmentVariableW(L"UZENGINE_HEADLESS", headlessEnv, 4) > 0;
    const bool forceWin  = GetEnvironmentVariableW(L"UZENGINE_WINDOWED", windowedEnv, 4) > 0;
    const bool showWindow = !headless || forceWin;

    DWORD exStyle = (!showWindow)
        ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
        : 0;
    DWORD style = (!showWindow)
        ? WS_POPUP
        : WS_OVERLAPPEDWINDOW;

    RECT windowRect = { 0, 0,
        static_cast<LONG>(uz::kDefaultWidth),
        static_cast<LONG>(uz::kDefaultHeight) };
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

    const int winX = showWindow ? CW_USEDEFAULT : -32000;
    const int winY = showWindow ? CW_USEDEFAULT : -32000;

    HWND hwnd = CreateWindowExW(
        exStyle,
        kWindowClassName,
        kWindowTitle,
        style,
        winX, winY,
        windowRect.right  - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (hwnd && showWindow) {
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
    }

    return hwnd;
}
