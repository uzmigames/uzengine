/// UzEngine Clipboard Implementation (Windows)
///
/// Win32 clipboard access via OpenClipboard/CloseClipboard.
/// Text is stored as CF_UNICODETEXT; binary uses RegisterClipboardFormatW.

#include "clipboard.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

UZ_DEFINE_LOG_CATEGORY(Clipboard, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Opens the clipboard with retry. Another process may briefly hold the lock.
static bool OpenClipboardWithRetry(HWND owner, int maxRetries = 5, int delayMs = 10) {
    for (int i = 0; i < maxRetries; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(static_cast<DWORD>(delayMs));
    }
    UZ_LOG(Clipboard, Error, "OpenClipboard failed after {} retries: error={}", maxRetries, GetLastError());
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Text Clipboard
// ─────────────────────────────────────────────────────────────────────────────

bool ClipboardSetText(const char* text) {
    if (!text) return false;

    // Determine required wide-char buffer size (includes null terminator)
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (wideLen <= 0) {
        UZ_LOG(Clipboard, Error, "UTF-8 to UTF-16 size query failed: error={}", GetLastError());
        return false;
    }

    if (!OpenClipboardWithRetry(nullptr)) {
        return false;
    }

    EmptyClipboard();

    const size_t byteSize = static_cast<size_t>(wideLen) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize);
    if (!hMem) {
        UZ_LOG(Clipboard, Error, "GlobalAlloc failed: size={}", byteSize);
        CloseClipboard();
        return false;
    }

    auto* dst = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!dst) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    MultiByteToWideChar(CP_UTF8, 0, text, -1, dst, wideLen);
    GlobalUnlock(hMem);

    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        UZ_LOG(Clipboard, Error, "SetClipboardData failed: error={}", GetLastError());
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    // OS now owns hMem — do not free
    CloseClipboard();
    return true;
}

std::string ClipboardGetText() {
    if (!OpenClipboardWithRetry(nullptr)) return {};

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return {};
    }

    const auto* wide = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!wide) {
        CloseClipboard();
        return {};
    }

    // Convert UTF-16 → UTF-8
    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string result;
    if (utf8Len > 1) { // utf8Len includes null terminator; 1 = empty string
        result.resize(static_cast<size_t>(utf8Len - 1));
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), utf8Len, nullptr, nullptr);
    }

    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom Binary Format
// ─────────────────────────────────────────────────────────────────────────────

/// Registers or retrieves a clipboard format by UTF-8 name.
static UINT RegisterFormat(const char* formatName) {
    wchar_t wideName[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, formatName, -1, wideName, 256);
    return RegisterClipboardFormatW(wideName);
}

bool ClipboardSetData(const char* formatName, const void* data, size_t size) {
    if (!formatName) return false;
    if (size == 0) return true;
    if (!data) return false;

    const UINT fmt = RegisterFormat(formatName);
    if (fmt == 0) {
        UZ_LOG(Clipboard, Error, "RegisterClipboardFormat failed: name='{}'", formatName);
        return false;
    }

    if (!OpenClipboardWithRetry(nullptr)) {
        return false;
    }

    EmptyClipboard();

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) {
        CloseClipboard();
        return false;
    }

    void* dst = GlobalLock(hMem);
    if (!dst) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    memcpy(dst, data, size);
    GlobalUnlock(hMem);

    if (!SetClipboardData(fmt, hMem)) {
        UZ_LOG(Clipboard, Error, "SetClipboardData failed: format='{}', error={}", formatName, GetLastError());
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

std::vector<uint8_t> ClipboardGetData(const char* formatName) {
    if (!formatName) return {};

    const UINT fmt = RegisterFormat(formatName);
    if (fmt == 0) return {};

    if (!OpenClipboardWithRetry(nullptr)) return {};

    HANDLE hData = GetClipboardData(fmt);
    if (!hData) {
        CloseClipboard();
        return {};
    }

    const size_t dataSize = GlobalSize(hData);
    const void* src = GlobalLock(hData);
    if (!src) {
        CloseClipboard();
        return {};
    }

    std::vector<uint8_t> result(dataSize);
    memcpy(result.data(), src, dataSize);

    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

} // namespace uz

#endif // _WIN32
