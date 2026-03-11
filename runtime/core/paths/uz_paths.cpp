/// UzEngine Path Resolution — Implementation
///
/// Platform-specific executable directory detection with caching.

#include "uz_paths.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <climits>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#endif

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// GetExeDir — cached exe directory lookup
// ─────────────────────────────────────────────────────────────────────────────

std::filesystem::path GetExeDir() {
    static std::filesystem::path cachedDir = []() -> std::filesystem::path {
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return std::filesystem::current_path();
        return std::filesystem::path(buf).parent_path();

#elif defined(__linux__)
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0) return std::filesystem::current_path();
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path();

#elif defined(__APPLE__)
        char buf[PATH_MAX];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) != 0) return std::filesystem::current_path();
        return std::filesystem::canonical(buf).parent_path();

#else
        return std::filesystem::current_path();
#endif
    }();
    return cachedDir;
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveEngineResource
// ─────────────────────────────────────────────────────────────────────────────

std::filesystem::path ResolveEngineResource(const std::string& relativePath) {
    std::filesystem::path p(relativePath);
    if (p.is_absolute()) return p;
    return GetExeDir() / p;
}

} // namespace uz
