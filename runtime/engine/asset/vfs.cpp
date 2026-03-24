/// UzEngine Virtual File System — Implementation
///
/// Implements three IVFS backends:
/// - LooseFileVFS: disk I/O via std::ifstream + std::filesystem
/// - PakVFS: delegates to PakReader for hash-based archive lookup
/// - LayeredVFS: iterates mount stack in priority order

#include "vfs.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

#include "file_system.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <filesystem>

static auto* cv_PakCacheMB = UZ_CVAR_INT("asset.PakCacheMB", 256, "PakVFS LRU cache size in MB", 64, 2048);

namespace fs = std::filesystem;

namespace uz {

UZ_DEFINE_STAT(IO, VFSReads);

// ─────────────────────────────────────────────────────────────────────────────
// LooseFileVFS — disk-based file access for development mode
// ─────────────────────────────────────────────────────────────────────────────

LooseFileVFS::LooseFileVFS(const std::string& rootDir)
    : m_rootDir(rootDir) {
    // Ensure trailing slash so concatenation works: "root/" + "path"
    if (!m_rootDir.empty() && m_rootDir.back() != '/' && m_rootDir.back() != '\\') {
        m_rootDir += '/';
    }
    UZ_LOG(Asset, Debug, "LooseFileVFS mounted: root='{}'", m_rootDir);
}

std::string LooseFileVFS::ResolvePath(const std::string& path) const {
    return m_rootDir + path;
}

std::vector<uint8_t> LooseFileVFS::Read(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    UZ_STAT_INCREMENT(IO, VFSReads);

    // Lock protects against concurrent ifstream reads on the same LooseFileVFS
    // instance from multiple AsyncTextureDecoder worker threads.
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string fullPath = ResolvePath(path);
    auto data = FileReadAll(fullPath.c_str());
    if (data.empty() && !fs::exists(fullPath)) {
        // FileReadAll already logs the I/O error; only log here when file truly absent.
        return {};
    }

    return data;
}

bool LooseFileVFS::Exists(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    std::string fullPath = ResolvePath(path);
    return fs::exists(fullPath) && fs::is_regular_file(fullPath);
}

std::string LooseFileVFS::Name() const {
    return "LooseFileVFS(" + m_rootDir + ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// PakVFS — .uzpak archive access for shipping builds
// ─────────────────────────────────────────────────────────────────────────────

PakVFS::PakVFS(PakReader reader)
    : m_reader(std::move(reader))
    , m_name("PakVFS") {
    UZ_LOG(Asset, Debug, "PakVFS mounted (legacy): entries={}", m_reader.EntryCount());
}

std::unique_ptr<PakVFS> PakVFS::FromFile(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    PakReader reader;
    if (!reader.LoadFile(path)) {
        UZ_LOG(Asset, Error, "PakVFS: failed to load archive: path='{}'", path);
        return nullptr;
    }

    auto vfs = std::make_unique<PakVFS>(std::move(reader));
    vfs->m_name = "PakVFS(" + path + ")";
    return vfs;
}

std::unique_ptr<PakVFS> PakVFS::FromIndexFile(
    const std::string& indexPath, uint64_t secret, size_t cacheSizeBytes) {
    UZ_PROFILE_FUNCTION();

    // Use private default constructor via new + unique_ptr
    auto vfs = std::unique_ptr<PakVFS>(new PakVFS());

    // Load the master index — this is the only file read during init
    if (!vfs->m_index.LoadFile(indexPath)) {
        UZ_LOG(Asset, Error, "PakVFS: failed to load master index: path='{}'", indexPath);
        return nullptr;
    }

    // Extract base directory from the index file path.
    // Pak filenames in the index are relative to this directory.
    fs::path indexFs(indexPath);
    fs::path parentDir = indexFs.parent_path();
    if (parentDir.empty()) {
        vfs->m_baseDir = ".";
    } else {
        // Normalize to forward slashes for cross-platform consistency
        vfs->m_baseDir = parentDir.generic_string();
    }

    vfs->m_secret = secret;
    vfs->m_cache = std::make_unique<LRUCache<uint64_t, std::vector<uint8_t>>>(cacheSizeBytes);
    vfs->m_name = "PakVFS(" + indexPath + ")";

    UZ_LOG(Asset, Info, "PakVFS: loaded {} assets from {} paks (lazy mode, cache={} MB)",
           vfs->m_index.EntryCount(), vfs->m_index.PakCount(),
           cacheSizeBytes / (1024 * 1024));

    return vfs;
}

std::vector<uint8_t> PakVFS::Read(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    UZ_STAT_INCREMENT(IO, VFSReads);

    // Legacy mode: in-memory lookup, needs lock
    if (!m_index.IsLoaded()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_reader.Lookup(path);
    }

    // ── Lazy mode (thread-safe for concurrent worker reads) ──────────

    const uint64_t hash = XXH64(path.data(), path.size(), 0);

    // 1. Check LRU cache (lock: cache access)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto* cached = m_cache->Get(hash)) {
            return *cached;
        }
    }

    // 2. Index lookup — MasterIndex is immutable after load, no lock needed
    auto loc = m_index.Lookup(hash);
    if (!loc.has_value()) {
        return {};
    }

    // 3. Open a private PakStreamReader for this thread's I/O.
    //    Each thread gets its own FILE* handle so concurrent seek+read
    //    on the same .uzpak file is safe without serialization.
    std::string pakPath = m_baseDir + "/" + loc->pakFilename;
    PakStreamReader threadReader;
    if (!threadReader.Open(pakPath)) {
        UZ_LOG(Asset, Error, "PakVFS: cannot open pak '{}' for asset '{}'",
               loc->pakFilename, path);
        return {};
    }

    // 4. Read blob — each thread has its own file handle, NO lock held
    auto blob = threadReader.ReadBlob(loc->offset, loc->compressedSize);
    if (blob.empty() && loc->compressedSize > 0) {
        UZ_LOG(Asset, Error, "PakVFS: ReadBlob failed for '{}' (offset={}, size={})",
               path, loc->offset, loc->compressedSize);
        return {};
    }

    // 5. Decompress — stateless, no lock needed
    auto raw = PakDecompressBlob(blob.data(), blob.size(), loc->rawSize, hash, m_secret);
    if (raw.empty() && loc->rawSize > 0) {
        UZ_LOG(Asset, Error, "PakVFS: decompress failed for '{}' (compressed={}, raw={})",
               path, loc->compressedSize, loc->rawSize);
        return {};
    }

    // 6. Cache insert (lock: cache access)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache->Put(hash, raw, raw.size());
    }

    return raw;
}

bool PakVFS::Exists(const std::string& path) {
    // Lazy mode: MasterIndex is immutable after load — no lock needed.
    if (m_index.IsLoaded()) {
        const uint64_t hash = XXH64(path.data(), path.size(), 0);
        return m_index.Contains(hash);
    }

    // Legacy mode: PakReader may not be thread-safe internally.
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_reader.Contains(path);
}

std::string PakVFS::Name() const {
    return m_name;
}

std::string PakVFS::ResolvePath(const std::string& /*path*/) const {
    // Pak assets have no filesystem path — return empty string
    return {};
}

IVFS::CacheStats PakVFS::GetCacheStats() const {
    if (!m_cache) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_cache->CurrentBytes(), m_cache->MaxBytes(), m_cache->EntryCount()};
}

PakStreamReader* PakVFS::AcquireReader(const std::string& pakFilename) {
    // Check if already open — promote to MRU
    auto it = m_handleMap.find(pakFilename);
    if (it != m_handleMap.end()) {
        m_handleList.splice(m_handleList.begin(), m_handleList, it->second);
        return &it->second->reader;
    }

    // Evict LRU handle if pool is full
    while (m_handleList.size() >= kMaxOpenHandles) {
        auto& back = m_handleList.back();
        m_handleMap.erase(back.pakName);
        m_handleList.pop_back();
    }

    // Open new handle
    std::string pakPath = m_baseDir + "/" + pakFilename;
    m_handleList.emplace_front(HandleEntry{pakFilename, {}});
    auto& entry = m_handleList.front();
    if (!entry.reader.Open(pakPath)) {
        m_handleList.pop_front();
        return nullptr;
    }

    m_handleMap[pakFilename] = m_handleList.begin();
    return &entry.reader;
}

// ─────────────────────────────────────────────────────────────────────────────
// LayeredVFS — priority-based mount stack for content layering
// ─────────────────────────────────────────────────────────────────────────────

void LayeredVFS::Mount(std::shared_ptr<IVFS> vfs, int priority) {
    UZ_PROFILE_FUNCTION();
    if (!vfs) {
        UZ_LOG(Asset, Error, "LayeredVFS: cannot mount null VFS backend");
        return;
    }

    UZ_LOG(Asset, Debug, "LayeredVFS: mounting '{}' at priority {}",
           vfs->Name(), priority);

    m_mounts.push_back({std::move(vfs), priority});

    // Re-sort so highest priority is first in iteration order
    std::sort(m_mounts.begin(), m_mounts.end(),
              [](const MountPoint& a, const MountPoint& b) {
                  return a.priority > b.priority;
              });
}

void LayeredVFS::UnmountAll() {
    m_mounts.clear();
    UZ_LOG(Asset, Debug, "LayeredVFS: all mounts cleared");
}

size_t LayeredVFS::MountCount() const {
    return m_mounts.size();
}

std::vector<uint8_t> LayeredVFS::Read(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    UZ_STAT_INCREMENT(IO, VFSReads);
    for (auto& mount : m_mounts) {
        auto data = mount.vfs->Read(path);
        if (!data.empty()) {
            return data;
        }
    }
    return {};
}

bool LayeredVFS::Exists(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    for (auto& mount : m_mounts) {
        if (mount.vfs->Exists(path)) {
            return true;
        }
    }
    return false;
}

std::string LayeredVFS::Name() const {
    return "LayeredVFS(" + std::to_string(m_mounts.size()) + " mounts)";
}

} // namespace uz
