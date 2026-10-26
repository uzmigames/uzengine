/// ShaderVFS Implementation
///
/// Path resolution algorithm:
/// 1. Iterate mounts in reverse order (last mount has highest priority)
/// 2. Check if virtualPath starts with mount's virtualPrefix
/// 3. If match: replace prefix with realDirectory, return resolved path
/// 4. If no mount matches: return nullopt

#include "shader_vfs.h"
#include "log.h"
#include "profiler.h"

#include "file_system.h"

#include <algorithm>
#include <filesystem>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Path Normalization
// ─────────────────────────────────────────────────────────────────────────────

/// Normalizes a path: forward slashes, no trailing slash, no double slashes.
static std::string NormalizePath(const std::string& path) {
    std::string result;
    result.reserve(path.size());

    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        if (c == '\\') c = '/';

        // Skip consecutive slashes
        if (c == '/' && !result.empty() && result.back() == '/') continue;

        result.push_back(c);
    }

    // Remove trailing slash (unless it's the root "/")
    if (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mount / Unmount
// ─────────────────────────────────────────────────────────────────────────────

void ShaderVFS::Mount(const std::string& virtualPrefix, const std::string& realDirectory) {
    UZ_PROFILE_FUNCTION();
    ShaderMount mount;
    mount.virtualPrefix = NormalizePath(virtualPrefix);
    mount.realDirectory = NormalizePath(realDirectory);

    UZ_LOG(IO, Info, "ShaderVFS: mount '{}' → '{}'", mount.virtualPrefix, mount.realDirectory);
    m_mounts.push_back(std::move(mount));
}

void ShaderVFS::Unmount(const std::string& virtualPrefix) {
    std::string normalized = NormalizePath(virtualPrefix);
    auto it = std::remove_if(m_mounts.begin(), m_mounts.end(),
        [&normalized](const ShaderMount& m) { return m.virtualPrefix == normalized; });

    if (it != m_mounts.end()) {
        UZ_LOG(IO, Info, "ShaderVFS: unmount '{}'", normalized);
        m_mounts.erase(it, m_mounts.end());
    }
}

void ShaderVFS::Clear() {
    m_mounts.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Resolution
// ─────────────────────────────────────────────────────────────────────────────

/// True if `path` (already '/'-normalized) has a ".." segment. Callers
/// collapse legitimate relative includes before resolving
/// (ShaderSourceManager::ResolveIncludePath), so a surviving ".." can only
/// climb out of a mount's real directory.
static bool HasParentSegment(const std::string& path) {
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        if (path.compare(start, end - start, "..") == 0) return true;
        start = end + 1;
    }
    return false;
}

std::optional<std::string> ShaderVFS::Resolve(const std::string& virtualPath) const {
    std::string normalized = NormalizePath(virtualPath);

    // Shader sources choose their own #include paths, so the virtual path is
    // data: refuse anything that could leave the mounted directory or be
    // truncated at the C file API boundary.
    if (normalized.find('\0') != std::string::npos || HasParentSegment(normalized)) {
        UZ_LOG(IO, Warning, "ShaderVFS: rejected virtual path escaping its mount: '{}'",
               normalized.substr(0, normalized.find('\0')));
        return std::nullopt;
    }

    // Search in reverse order — last mount wins
    for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it) {
        const auto& mount = *it;

        if (normalized.compare(0, mount.virtualPrefix.size(), mount.virtualPrefix) != 0) {
            continue;
        }

        // Ensure we match at a path boundary (not partial prefix)
        if (normalized.size() > mount.virtualPrefix.size() &&
            normalized[mount.virtualPrefix.size()] != '/') {
            continue;
        }

        // Replace virtual prefix with real directory
        std::string suffix = normalized.substr(mount.virtualPrefix.size());
        std::string realPath = mount.realDirectory + suffix;
        return realPath;
    }

    return std::nullopt;
}

bool ShaderVFS::Exists(const std::string& virtualPath) const {
    auto resolved = Resolve(virtualPath);
    if (!resolved) return false;

    return std::filesystem::exists(*resolved);
}

std::optional<std::string> ShaderVFS::ReadFile(const std::string& virtualPath) const {
    UZ_PROFILE_FUNCTION();
    auto resolved = Resolve(virtualPath);
    if (!resolved) {
        UZ_LOG(IO, Warning, "ShaderVFS: cannot resolve '{}'", virtualPath);
        return std::nullopt;
    }

    std::string content = FileReadAllText(resolved->c_str());
    if (content.empty()) {
        // FileReadAll already logs the error; emit a warning here for VFS context.
        UZ_LOG(IO, Warning, "ShaderVFS: cannot read '{}'", *resolved);
        return std::nullopt;
    }

    return content;
}

} // namespace uz
