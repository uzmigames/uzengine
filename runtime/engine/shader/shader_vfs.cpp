/// ShaderVFS Implementation
///
/// Path resolution algorithm:
/// 1. Iterate mounts in reverse order (last mount has highest priority)
/// 2. Check if virtualPath starts with mount's virtualPrefix
/// 3. If match: replace prefix with realDirectory, return resolved path
/// 4. If no mount matches: return nullopt

#include "shader_vfs.h"
#include "log.h"

#include <algorithm>
#include <fstream>
#include <sstream>
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

std::optional<std::string> ShaderVFS::Resolve(const std::string& virtualPath) const {
    std::string normalized = NormalizePath(virtualPath);

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
    auto resolved = Resolve(virtualPath);
    if (!resolved) {
        UZ_LOG(IO, Warning, "ShaderVFS: cannot resolve '{}'", virtualPath);
        return std::nullopt;
    }

    std::ifstream file(*resolved, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(IO, Warning, "ShaderVFS: cannot open '{}'", *resolved);
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace uz
