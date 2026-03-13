/// ShaderCache Implementation
///
/// Uses std::filesystem for directory creation and file operations.
/// Hash is formatted as 16-char lowercase hex for filenames.

#include "shader_cache.h"
#include "log.h"
#include "profiler.h"

#include "file_system.h"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ShaderCache::ShaderCache(const std::string& cacheDirectory)
    : m_cacheDir(cacheDirectory) {
    UZ_LOG(IO, Info, "ShaderCache: directory '{}'", m_cacheDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Path Utilities
// ─────────────────────────────────────────────────────────────────────────────

std::string ShaderCache::GetEntryPath(const std::string& subdirectory,
                                        uint64_t identityHash) const {
    std::ostringstream ss;
    ss << m_cacheDir << "/" << subdirectory << "/"
       << std::hex << std::setfill('0') << std::setw(16) << identityHash << ".bin";
    return ss.str();
}

bool ShaderCache::EnsureDirectory(const std::string& subdirectory) const {
    std::string dirPath = m_cacheDir + "/" + subdirectory;
    std::error_code ec;
    fs::create_directories(dirPath, ec);
    if (ec) {
        UZ_LOG(IO, Error, "ShaderCache: cannot create directory '{}': {}",
               dirPath, ec.message());
        return false;
    }
    return true;
}

std::string ShaderCache::GetSubdirectory(const std::string& backend,
                                           const std::string& shaderModel) {
    return backend + "_" + shaderModel;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup / Store
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> ShaderCache::Lookup(
    const std::string& subdirectory,
    uint64_t identityHash) const
{
    UZ_PROFILE_FUNCTION();
    std::string path = GetEntryPath(subdirectory, identityHash);

    auto bytecode = FileReadAll(path.c_str());
    if (bytecode.empty()) return std::nullopt;

    UZ_LOG(IO, Debug, "ShaderCache: hit {:016x} ({} bytes)", identityHash, bytecode.size());
    return bytecode;
}

bool ShaderCache::Store(const std::string& subdirectory,
                          uint64_t identityHash,
                          const std::vector<uint8_t>& bytecode,
                          const std::string& sourcePath) {
    UZ_PROFILE_FUNCTION();
    if (!EnsureDirectory(subdirectory)) return false;

    std::string path = GetEntryPath(subdirectory, identityHash);

    const FileResult result = FileWriteAll(path.c_str(), bytecode.data(), bytecode.size());
    if (result != FileResult::Success) {
        UZ_LOG(IO, Error, "ShaderCache: cannot write '{}': {}", path, FileResultToString(result));
        return false;
    }

    UZ_LOG(IO, Debug, "ShaderCache: stored {:016x} ({} bytes, source='{}')",
           identityHash, bytecode.size(), sourcePath);
    return true;
}

bool ShaderCache::Contains(const std::string& subdirectory,
                             uint64_t identityHash) const {
    std::string path = GetEntryPath(subdirectory, identityHash);
    return fs::exists(path);
}

bool ShaderCache::Remove(const std::string& subdirectory, uint64_t identityHash) {
    std::string path = GetEntryPath(subdirectory, identityHash);
    std::error_code ec;
    return fs::remove(path, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// Statistics
// ─────────────────────────────────────────────────────────────────────────────

size_t ShaderCache::GetEntryCount() const {
    size_t count = 0;
    std::error_code ec;

    if (!fs::exists(m_cacheDir, ec)) return 0;

    for (const auto& entry : fs::recursive_directory_iterator(m_cacheDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            ++count;
        }
    }
    return count;
}

size_t ShaderCache::GetTotalSizeBytes() const {
    size_t total = 0;
    std::error_code ec;

    if (!fs::exists(m_cacheDir, ec)) return 0;

    for (const auto& entry : fs::recursive_directory_iterator(m_cacheDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            total += static_cast<size_t>(entry.file_size(ec));
        }
    }
    return total;
}

void ShaderCache::Clear() {
    std::error_code ec;
    if (fs::exists(m_cacheDir, ec)) {
        fs::remove_all(m_cacheDir, ec);
        UZ_LOG(IO, Info, "ShaderCache: cleared '{}'", m_cacheDir);
    }
}

} // namespace uz
