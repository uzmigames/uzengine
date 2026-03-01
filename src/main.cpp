// main.cpp -- Win32 entry point for UzEngine PoC.
//
// Creates a window, initializes the DX12 renderer, and runs a
// non-blocking message loop that updates and renders each frame.

#include "renderer/renderer.h"
#include "common/timer.h"

// windows.h is pulled in transitively through timer.h / renderer.h,
// but we include it explicitly so the dependency is visible.
#include <windows.h>

namespace {

constexpr const wchar_t* kWindowClassName = L"UzEngineWndClass";
constexpr const wchar_t* kWindowTitle     = L"UzEngine PoC";

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // anonymous namespace

int WINAPI WinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPSTR     /*lpCmdLine*/,
    _In_     int       nCmdShow)
{
    // -- Register window class -----------------------------------------------

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    // -- Create the window ---------------------------------------------------
    // AdjustWindowRect ensures the *client area* is exactly kDefaultWidth x
    // kDefaultHeight.  Without this, the title bar and borders eat into the
    // render surface.

    RECT windowRect = {
        0, 0,
        static_cast<LONG>(uz::kDefaultWidth),
        static_cast<LONG>(uz::kDefaultHeight)
    };
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    const int windowWidth  = windowRect.right  - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight,
        nullptr,   // no parent
        nullptr,   // no menu
        hInstance,
        nullptr    // no extra data
    );

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // -- Initialize engine subsystems ----------------------------------------

    uz::DX12Renderer renderer;
    uz::Timer timer;

    renderer.init(hwnd, uz::kDefaultWidth, uz::kDefaultHeight);

    // -- Main loop -----------------------------------------------------------
    // PeekMessage is used instead of GetMessage so that the loop never blocks
    // waiting for input -- the renderer runs continuously.

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            renderer.update(timer.elapsed());
            renderer.render();
        }
    }

    // -- Cleanup -------------------------------------------------------------
    // Drain the GPU and release all resources before the window is destroyed.

    renderer.shutdown();

    return static_cast<int>(msg.wParam);
}
