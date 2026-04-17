/// UzEngine Default Monospace System Font Resolver — Implementation.

#include "system_font.h"

#include <filesystem>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace uz {

#if defined(_WIN32)
static std::string ResolveWindowsMonoFont() {
    // Derive `<Windows>/Fonts/consola.ttf` at runtime — the Windows install
    // directory is NOT guaranteed to be on C: so hardcoding a drive letter
    // breaks portability.
    wchar_t winDir[MAX_PATH] = {};
    const UINT n = ::GetWindowsDirectoryW(winDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }

    std::filesystem::path p(winDir);
    p /= L"Fonts";
    p /= L"consola.ttf";
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return {};
    }
    return p.string();
}
#endif

#if defined(__APPLE__)
static std::string ResolveMacMonoFont() {
    // Apple ships Monaco at a stable system path. Check it exists so callers
    // get an empty string rather than a broken load on reduced-footprint
    // installs where the font may be absent.
    const char* kPath = "/System/Library/Fonts/Monaco.ttf";
    std::error_code ec;
    if (std::filesystem::exists(kPath, ec)) {
        return kPath;
    }
    return {};
}
#endif

#if defined(__linux__)
static std::string ResolveLinuxMonoFont() {
    // Probe the most common distro paths. No single path is guaranteed —
    // distributions vary, so fall through until one exists.
    static const char* const kCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    };
    std::error_code ec;
    for (const char* candidate : kCandidates) {
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}
#endif

std::string ResolveDefaultMonoFont() {
#if defined(_WIN32)
    return ResolveWindowsMonoFont();
#elif defined(__APPLE__)
    return ResolveMacMonoFont();
#elif defined(__linux__)
    return ResolveLinuxMonoFont();
#else
    return {};
#endif
}

} // namespace uz
