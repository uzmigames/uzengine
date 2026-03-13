/// UzEngine OS Integration Implementation (Windows)
///
/// Dark mode detection via registry, taskbar progress via ITaskbarList3,
/// system tray icons and balloon notifications via Shell_NotifyIconW.

#include "os_integration.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <string>

UZ_DEFINE_LOG_CATEGORY(OSIntegration, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// UTF-8 helpers (local to this TU)
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a UTF-8 string to a wide string. Returns empty on failure.
static std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, result.data(), len);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dark Mode
// ─────────────────────────────────────────────────────────────────────────────

bool IsSystemDarkMode() {
    DWORD value = 1; // Default: light theme (AppsUseLightTheme = 1)
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD, nullptr,
        &value, &size);

    if (status != ERROR_SUCCESS) {
        UZ_LOG(OSIntegration, Trace, "Could not read AppsUseLightTheme registry key: status={}", status);
        return false;
    }

    // AppsUseLightTheme == 0 means dark mode is active
    return value == 0;
}

/// Global dark mode callback state.
static DarkModeCallback g_darkModeCallback = nullptr;
static void* g_darkModeUserData = nullptr;

void SetDarkModeCallback(DarkModeCallback callback, void* userData) {
    g_darkModeCallback = callback;
    g_darkModeUserData = userData;
}

void NotifyDarkModeChange() {
    if (g_darkModeCallback) {
        g_darkModeCallback(IsSystemDarkMode(), g_darkModeUserData);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Taskbar Progress
// ─────────────────────────────────────────────────────────────────────────────

/// Lazily creates and caches the ITaskbarList3 COM interface.
/// Returns nullptr if the interface is not available (e.g., pre-Win7).
static ITaskbarList3* GetTaskbarList() {
    static ITaskbarList3* s_taskbar = nullptr;
    static bool s_attempted = false;

    if (s_attempted) return s_taskbar;
    s_attempted = true;

    HRESULT hr = CoCreateInstance(
        CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&s_taskbar));
    if (FAILED(hr) || !s_taskbar) {
        UZ_LOG(OSIntegration, Warning, "ITaskbarList3 unavailable: hr=0x{:08X}", static_cast<unsigned>(hr));
        s_taskbar = nullptr;
        return nullptr;
    }

    hr = s_taskbar->HrInit();
    if (FAILED(hr)) {
        UZ_LOG(OSIntegration, Warning, "ITaskbarList3::HrInit failed: hr=0x{:08X}", static_cast<unsigned>(hr));
        s_taskbar->Release();
        s_taskbar = nullptr;
        return nullptr;
    }

    return s_taskbar;
}

void SetTaskbarProgress(void* hwnd, int value, int max) {
    if (!hwnd) return;
    ITaskbarList3* taskbar = GetTaskbarList();
    if (!taskbar) return;

    auto hWnd = static_cast<HWND>(hwnd);
    taskbar->SetProgressState(hWnd, TBPF_NORMAL);
    taskbar->SetProgressValue(hWnd,
        static_cast<ULONGLONG>(value),
        static_cast<ULONGLONG>(max));
}

void ClearTaskbarProgress(void* hwnd) {
    if (!hwnd) return;
    ITaskbarList3* taskbar = GetTaskbarList();
    if (!taskbar) return;

    taskbar->SetProgressState(static_cast<HWND>(hwnd), TBPF_NOPROGRESS);
}

// ─────────────────────────────────────────────────────────────────────────────
// System Tray Icon
// ─────────────────────────────────────────────────────────────────────────────

/// Tray icon callback message ID — arbitrary value above WM_USER.
static constexpr UINT kTrayCallbackMsg = WM_USER + 0x100;

/// Builds a NOTIFYICONDATAW with common fields populated.
static NOTIFYICONDATAW MakeTrayData(HWND hwnd, uint32_t iconId) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = iconId;
    return nid;
}

bool ShowTrayIcon(void* hwnd, const char* tooltip, uint32_t iconId) {
    if (!hwnd || !tooltip) return false;

    NOTIFYICONDATAW nid = MakeTrayData(static_cast<HWND>(hwnd), iconId);
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = kTrayCallbackMsg;
    nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

    // Copy tooltip (max 127 chars + null)
    const std::wstring wideTip = Utf8ToWide(tooltip);
    wcsncpy_s(nid.szTip, wideTip.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        UZ_LOG(OSIntegration, Error, "Shell_NotifyIconW(NIM_ADD) failed: error={}", GetLastError());
        return false;
    }

    // Set version for modern balloon behavior
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);

    return true;
}

void HideTrayIcon(void* hwnd) {
    if (!hwnd) return;

    NOTIFYICONDATAW nid = MakeTrayData(static_cast<HWND>(hwnd), 0);
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowTrayNotification(void* hwnd, const char* title, const char* body) {
    if (!hwnd || !title || !body) return;

    NOTIFYICONDATAW nid = MakeTrayData(static_cast<HWND>(hwnd), 0);
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;

    const std::wstring wideTitle = Utf8ToWide(title);
    const std::wstring wideBody = Utf8ToWide(body);

    wcsncpy_s(nid.szInfoTitle, wideTitle.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, wideBody.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        UZ_LOG(OSIntegration, Warning, "Shell_NotifyIconW(NIM_MODIFY) failed: error={}", GetLastError());
    }
}

} // namespace uz

#endif // _WIN32
