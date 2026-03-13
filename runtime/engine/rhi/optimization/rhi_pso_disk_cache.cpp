/// PsoDiskCache Implementation
///
/// Uses std::filesystem for directory creation and file operations.
/// Hash is formatted as 16-char lowercase hex for filenames.

#include "rhi_pso_disk_cache.h"
#include "log.h"
#include "profiler.h"

#include "file_system.h"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

PsoDiskCache::PsoDiskCache(const std::string& cacheDirectory)
    : m_cacheDir(cacheDirectory) {
    UZ_LOG(RHI, Info, "PsoDiskCache: directory '{}'", m_cacheDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Path Utilities
// ─────────────────────────────────────────────────────────────────────────────

std::string PsoDiskCache::GetEntryPath(uint64_t keyHash) const {
    std::ostringstream ss;
    ss << m_cacheDir << "/"
       << std::hex << std::setfill('0') << std::setw(16) << keyHash << ".bin";
    return ss.str();
}

bool PsoDiskCache::EnsureDirectory() const {
    std::error_code ec;
    fs::create_directories(m_cacheDir, ec);
    if (ec) {
        UZ_LOG(RHI, Error, "PsoDiskCache: cannot create directory '{}': {}",
               m_cacheDir, ec.message());
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup / Store
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> PsoDiskCache::Lookup(uint64_t keyHash) const {
    UZ_PROFILE_FUNCTION();
    std::string path = GetEntryPath(keyHash);

    auto blob = FileReadAll(path.c_str());
    if (blob.empty()) return std::nullopt;

    UZ_LOG(RHI, Debug, "PsoDiskCache: hit {:016x} ({} bytes)", keyHash, blob.size());
    return blob;
}

bool PsoDiskCache::Store(uint64_t keyHash, const void* data, size_t sizeBytes) {
    UZ_PROFILE_FUNCTION();
    if (!data || sizeBytes == 0) {
        UZ_LOG(RHI, Warning, "PsoDiskCache::Store: empty blob for {:016x}", keyHash);
        return false;
    }

    if (!EnsureDirectory()) return false;

    std::string path = GetEntryPath(keyHash);

    const FileResult result = FileWriteAll(path.c_str(), data, sizeBytes);
    if (result != FileResult::Success) {
        UZ_LOG(RHI, Error, "PsoDiskCache: cannot write '{}': {}", path, FileResultToString(result));
        return false;
    }

    UZ_LOG(RHI, Debug, "PsoDiskCache: stored {:016x} ({} bytes)", keyHash, sizeBytes);
    return true;
}

bool PsoDiskCache::Contains(uint64_t keyHash) const {
    std::string path = GetEntryPath(keyHash);
    return fs::exists(path);
}

bool PsoDiskCache::Remove(uint64_t keyHash) {
    std::string path = GetEntryPath(keyHash);
    std::error_code ec;
    return fs::remove(path, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// Statistics
// ─────────────────────────────────────────────────────────────────────────────

size_t PsoDiskCache::GetEntryCount() const {
    size_t count = 0;
    std::error_code ec;

    if (!fs::exists(m_cacheDir, ec)) return 0;

    for (const auto& entry : fs::directory_iterator(m_cacheDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            ++count;
        }
    }
    return count;
}

size_t PsoDiskCache::GetTotalSizeBytes() const {
    size_t total = 0;
    std::error_code ec;

    if (!fs::exists(m_cacheDir, ec)) return 0;

    for (const auto& entry : fs::directory_iterator(m_cacheDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            total += static_cast<size_t>(entry.file_size(ec));
        }
    }
    return total;
}

void PsoDiskCache::Clear() {
    std::error_code ec;
    if (fs::exists(m_cacheDir, ec)) {
        fs::remove_all(m_cacheDir, ec);
        UZ_LOG(RHI, Info, "PsoDiskCache: cleared '{}'", m_cacheDir);
    }
}

} // namespace uz
