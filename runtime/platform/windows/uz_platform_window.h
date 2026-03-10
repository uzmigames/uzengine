#pragma once

/// UzEngine Platform Window — Win32 implementation.
/// Creates native HWND for DX12 swapchain binding.

#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace uz {

struct WindowDesc {
    const char* title = "UzEngine";
    int32_t width = 1280;
    int32_t height = 720;
    bool resizable = true;
    bool fullscreen = false;
};

class PlatformWindow {
public:
    PlatformWindow() = default;
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    bool Create(const WindowDesc& desc);
    void Destroy();

    /// Process pending OS messages. Returns false if window was closed.
    bool PumpMessages();

    /// Show/hide the window.
    void Show();
    void Hide();

    /// Getters.
    bool IsOpen() const { return m_open; }
    int32_t GetWidth() const { return m_width; }
    int32_t GetHeight() const { return m_height; }
    bool WasResized() const { return m_resized; }
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
