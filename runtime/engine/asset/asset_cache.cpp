/// UzEngine Asset Cache — Implementation
///
/// Content-addressed storage using xxHash64 keys and flat binary files.
/// All file operations use std::filesystem for portability.

#include "asset_cache.h"
#include "log.h"
#include "profiler.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

AssetCache::AssetCache(const std::string& cacheDirectory)
    : m_cacheDir(cacheDirectory) {
    UZ_LOG(Asset, Info, "Cache directory: '{}'", m_cacheDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hashing
// ─────────────────────────────────────────────────────────────────────────────

uint64_t AssetCache::HashBytes(const void* data, size_t size) {
    return XXH64(data, size, 0);
}

uint64_t AssetCache::HashString(const std::string& str) {
    return XXH64(str.data(), str.size(), 0);
}

uint64_t AssetCache::ComputeKey(
    const std::string& importerName,
    uint32_t importerVersion,
    uint64_t sourceHash,
    uint64_t optionsHash) {
    UZ_PROFILE_FUNCTION();

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    XXH64_update(state, importerName.data(), importerName.size());

    uint32_t versionLE = importerVersion;
    XXH64_update(state, &versionLE, sizeof(versionLE));

    XXH64_update(state, &sourceHash, sizeof(sourceHash));
    XXH64_update(state, &optionsHash, sizeof(optionsHash));

    uint64_t key = XXH64_digest(state);
    XXH64_freeState(state);

    return key;
}

// ─────────────────────────────────────────────────────────────────────────────
// Path Utilities
// ─────────────────────────────────────────────────────────────────────────────

std::string AssetCache::EntryPath(uint64_t key) const {
    std::ostringstream ss;
    ss << m_cacheDir << "/imported/"
       << std::hex << std::setfill('0') << std::setw(16) << key << ".bin";
    return ss.str();
}

bool AssetCache::EnsureDirectory() const {
    std::string dirPath = m_cacheDir + "/imported";
    std::error_code ec;
    fs::create_directories(dirPath, ec);
    if (ec) {
        UZ_LOG(Asset, Error, "Cannot create cache directory '{}': {}",
               dirPath, ec.message());
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup / Store
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> AssetCache::Lookup(uint64_t key) const {
    UZ_PROFILE_FUNCTION();
    std::string path = EntryPath(key);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;

    auto size = file.tellg();
    if (size <= 0) return std::nullopt;

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);

    UZ_LOG(Asset, Debug, "Cache hit: {:016x} ({} bytes)", key, data.size());
    return data;
}

bool AssetCache::Store(uint64_t key, const std::vector<uint8_t>& data) {
    UZ_PROFILE_FUNCTION();
    if (!EnsureDirectory()) return false;

    std::string path = EntryPath(key);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(Asset, Error, "Cannot write cache entry '{}'", path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    file.close();

    UZ_LOG(Asset, Debug, "Cache store: {:016x} ({} bytes)", key, data.size());
    return true;
}

bool AssetCache::Contains(uint64_t key) const {
    return fs::exists(EntryPath(key));
}

bool AssetCache::Remove(uint64_t key) {
    std::error_code ec;
    return fs::remove(EntryPath(key), ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// Statistics
// ─────────────────────────────────────────────────────────────────────────────

size_t AssetCache::EntryCount() const {
    size_t count = 0;
    std::error_code ec;
    std::string importedDir = m_cacheDir + "/imported";

    if (!fs::exists(importedDir, ec)) return 0;

    for (const auto& entry : fs::directory_iterator(importedDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            ++count;
        }
    }
    return count;
}

size_t AssetCache::TotalSizeBytes() const {
    size_t total = 0;
    std::error_code ec;
    std::string importedDir = m_cacheDir + "/imported";

    if (!fs::exists(importedDir, ec)) return 0;

    for (const auto& entry : fs::directory_iterator(importedDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            total += static_cast<size_t>(entry.file_size(ec));
        }
    }
    return total;
}

void AssetCache::Clear() {
    UZ_PROFILE_FUNCTION();
    std::error_code ec;
    if (fs::exists(m_cacheDir, ec)) {
        fs::remove_all(m_cacheDir, ec);
        UZ_LOG(Asset, Info, "Cache cleared: '{}'", m_cacheDir);
    }
}

} // namespace uz
