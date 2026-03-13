/// UzEngine Environment Variables Implementation (Windows)
///
/// GetEnvironmentVariableW / SetEnvironmentVariableW with UTF-8 conversion.

#include "environment.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

namespace uz {

namespace {

/// Converts a UTF-8 string to wide. Returns empty on failure.
std::wstring Utf8ToWide(const char* str) {
    if (!str || str[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, result.data(), len);
    return result;
}

/// Converts a wide string to UTF-8. Returns empty on failure.
std::string WideToUtf8(const wchar_t* wstr, int wlen) {
    if (!wstr || wlen <= 0) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, result.data(), len, nullptr, nullptr);
    return result;
}

} // anonymous namespace

std::string GetEnv(const char* name) {
    if (!name) {
        UZ_LOG(Platform, Warning, "GetEnv called with nullptr name");
        return {};
    }

    std::wstring wideName = Utf8ToWide(name);
    if (wideName.empty()) return {};

    // Query required buffer size
    DWORD needed = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (needed == 0) {
        // Variable not set (ERROR_ENVVAR_NOT_FOUND) — not an error
        return {};
    }

    std::vector<wchar_t> buf(static_cast<size_t>(needed));
    DWORD written = GetEnvironmentVariableW(wideName.c_str(), buf.data(), needed);
    if (written == 0 || written >= needed) {
        return {};
    }

    return WideToUtf8(buf.data(), static_cast<int>(written));
}

bool SetEnv(const char* name, const char* value) {
    if (!name || !value) {
        UZ_LOG(Platform, Error, "SetEnv called with nullptr argument");
        return false;
    }

    std::wstring wideName = Utf8ToWide(name);
    std::wstring wideValue = Utf8ToWide(value);

    if (wideName.empty()) {
        UZ_LOG(Platform, Error, "SetEnv: UTF-8 conversion failed for name '{}'", name);
        return false;
    }

    if (!SetEnvironmentVariableW(wideName.c_str(), wideValue.c_str())) {
        UZ_LOG(Platform, Error, "SetEnv: SetEnvironmentVariableW failed for '{}', error={}",
               name, GetLastError());
        return false;
    }

    return true;
}

} // namespace uz

#endif // _WIN32
