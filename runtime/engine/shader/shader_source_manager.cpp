/// ShaderSourceManager Implementation
///
/// Include parsing uses a simple text scanner for `#include "path"` directives.
/// We deliberately do NOT support `#include <path>` (angle brackets) — all shader
/// includes use virtual paths with quotes.

#include "shader_source_manager.h"
#include "shader_vfs.h"
#include "log.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <sstream>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ShaderSourceManager::ShaderSourceManager(const ShaderVFS& vfs)
    : m_vfs(vfs) {}

// ─────────────────────────────────────────────────────────────────────────────
// Path Utilities
// ─────────────────────────────────────────────────────────────────────────────

std::string ShaderSourceManager::GetDirectory(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return "";
    return path.substr(0, pos);
}

std::string ShaderSourceManager::ResolveIncludePath(const std::string& baseDir,
                                                      const std::string& includePath) {
    // Absolute virtual path — use as-is
    if (!includePath.empty() && includePath[0] == '/') {
        return includePath;
    }

    // Relative path — resolve against base directory
    if (baseDir.empty()) return "/" + includePath;
    return baseDir + "/" + includePath;
}

// ─────────────────────────────────────────────────────────────────────────────
// Include Parsing
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a single line for `#include "path"` and extracts the path.
static bool ParseIncludeDirective(const std::string& line, std::string& outPath) {
    // Skip leading whitespace
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

    // Must start with #
    if (i >= line.size() || line[i] != '#') return false;
    ++i;

    // Skip whitespace after #
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

    // Must be "include"
    const char* kInclude = "include";
    for (int j = 0; kInclude[j]; ++j) {
        if (i >= line.size() || line[i] != kInclude[j]) return false;
        ++i;
    }

    // Skip whitespace after "include"
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

    // Must be double-quote (we don't support angle brackets for shaders)
    if (i >= line.size() || line[i] != '"') return false;
    ++i;

    // Extract path until closing quote
    size_t start = i;
    while (i < line.size() && line[i] != '"') ++i;
    if (i >= line.size()) return false;

    outPath = line.substr(start, i - start);
    return !outPath.empty();
}

bool ShaderSourceManager::ResolveIncludes(
    const std::string& virtualPath,
    const std::string& source,
    std::vector<std::string>& outIncludes,
    std::unordered_set<std::string>& visited,
    uint32_t depth)
{
    if (depth > kMaxIncludeDepth) {
        UZ_LOG(IO, Error, "ShaderSourceManager: include depth exceeded ({}) at '{}'",
               kMaxIncludeDepth, virtualPath);
        return false;
    }

    std::string baseDir = GetDirectory(virtualPath);
    std::istringstream stream(source);
    std::string line;

    while (std::getline(stream, line)) {
        std::string includePath;
        if (!ParseIncludeDirective(line, includePath)) continue;

        std::string resolvedInclude = ResolveIncludePath(baseDir, includePath);

        // Skip already-visited includes (deduplication)
        if (visited.count(resolvedInclude)) continue;
        visited.insert(resolvedInclude);

        // Read the included file
        auto content = m_vfs.ReadFile(resolvedInclude);
        if (!content) {
            UZ_LOG(IO, Error, "ShaderSourceManager: cannot read include '{}' from '{}'",
                   resolvedInclude, virtualPath);
            return false;
        }

        outIncludes.push_back(resolvedInclude);

        // Recurse into the included file
        if (!ResolveIncludes(resolvedInclude, *content, outIncludes, visited, depth + 1)) {
            return false;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Content Hashing
// ─────────────────────────────────────────────────────────────────────────────

uint64_t ShaderSourceManager::ComputeContentHash(
    const std::string& source,
    const std::vector<std::string>& includes)
{
    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    // Hash the root source
    XXH64_update(state, source.data(), source.size());

    // Hash each include's content (sorted by path for determinism)
    std::vector<std::string> sortedIncludes = includes;
    std::sort(sortedIncludes.begin(), sortedIncludes.end());

    for (const auto& inc : sortedIncludes) {
        auto content = m_vfs.ReadFile(inc);
        if (content) {
            XXH64_update(state, content->data(), content->size());
        }
    }

    uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

std::optional<ShaderSourceInfo> ShaderSourceManager::LoadSource(const std::string& virtualPath) {
    // Check cache
    auto cacheIt = m_cache.find(virtualPath);
    if (cacheIt != m_cache.end()) {
        return cacheIt->second;
    }

    // Read source
    auto source = m_vfs.ReadFile(virtualPath);
    if (!source) {
        UZ_LOG(IO, Error, "ShaderSourceManager: cannot load '{}'", virtualPath);
        return std::nullopt;
    }

    auto resolved = m_vfs.Resolve(virtualPath);
    if (!resolved) return std::nullopt;

    // Resolve includes
    ShaderSourceInfo info;
    info.virtualPath = virtualPath;
    info.resolvedPath = *resolved;
    info.source = *source;

    std::unordered_set<std::string> visited;
    visited.insert(virtualPath);

    if (!ResolveIncludes(virtualPath, *source, info.includes, visited, 0)) {
        return std::nullopt;
    }

    // Compute content hash
    info.contentHash = ComputeContentHash(info.source, info.includes);

    UZ_LOG(IO, Debug, "ShaderSourceManager: loaded '{}' ({} includes, hash={:016x})",
           virtualPath, info.includes.size(), info.contentHash);

    // Cache and return
    m_cache[virtualPath] = info;
    return info;
}

uint64_t ShaderSourceManager::ComputeIdentityHash(
    const std::string& virtualPath,
    const std::vector<std::string>& defines,
    ShaderTarget target,
    uint32_t compilerVersion)
{
    auto info = LoadSource(virtualPath);
    if (!info) return 0;

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    // Content hash (source + includes)
    XXH64_update(state, &info->contentHash, sizeof(info->contentHash));

    // Defines (sorted for determinism)
    std::vector<std::string> sortedDefines = defines;
    std::sort(sortedDefines.begin(), sortedDefines.end());
    for (const auto& def : sortedDefines) {
        XXH64_update(state, def.data(), def.size());
    }

    // Target
    XXH64_update(state, &target, sizeof(target));

    // Compiler version
    XXH64_update(state, &compilerVersion, sizeof(compilerVersion));

    uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

void ShaderSourceManager::Invalidate(const std::string& virtualPath) {
    auto it = m_cache.find(virtualPath);
    if (it != m_cache.end()) {
        UZ_LOG(IO, Debug, "ShaderSourceManager: invalidated '{}'", virtualPath);
        m_cache.erase(it);
    }
}

void ShaderSourceManager::InvalidateAll() {
    UZ_LOG(IO, Debug, "ShaderSourceManager: invalidated all ({} entries)", m_cache.size());
    m_cache.clear();
}

} // namespace uz
