/// UzEngine Virtual File System — Implementation
///
/// Implements three IVFS backends:
/// - LooseFileVFS: disk I/O via std::ifstream + std::filesystem
/// - PakVFS: delegates to PakReader for hash-based archive lookup
/// - LayeredVFS: iterates mount stack in priority order

#include "vfs.h"
#include "log.h"
#include "profiler.h"

#include "file_system.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <filesystem>

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

    // Legacy mode: delegate entirely to the in-memory PakReader
    if (!m_index.IsLoaded()) {
        return m_reader.Lookup(path);
    }

    // ── Lazy mode ──────────────────────────────────────────────────────

    const uint64_t hash = XXH64(path.data(), path.size(), 0);

    // 1. Check LRU cache — fast path, no I/O
    if (auto* cached = m_cache->Get(hash)) {
        return *cached;
    }

    // 2. Lookup in master index to find which pak file and at what offset
    auto loc = m_index.Lookup(hash);
    if (!loc.has_value()) {
        return {};
    }

    // 3. Get or lazily open the PakStreamReader for this pak file
    auto& reader = m_readers[loc->pakFilename];
    if (!reader.IsOpen()) {
        std::string pakPath = m_baseDir + "/" + loc->pakFilename;
        if (!reader.Open(pakPath)) {
            UZ_LOG(Asset, Error, "PakVFS: cannot open pak '{}' for asset '{}'",
                   pakPath, path);
            return {};
        }
    }

    // 4. Read the compressed+scrambled blob from disk
    auto blob = reader.ReadBlob(loc->offset, loc->compressedSize);
    if (blob.empty() && loc->compressedSize > 0) {
        UZ_LOG(Asset, Error, "PakVFS: ReadBlob failed for '{}' (offset={}, size={})",
               path, loc->offset, loc->compressedSize);
        return {};
    }

    // 5. Decompress (XOR descramble + optional LZ4 decompression)
    auto raw = PakDecompressBlob(blob.data(), blob.size(), loc->rawSize, hash, m_secret);
    if (raw.empty() && loc->rawSize > 0) {
        UZ_LOG(Asset, Error, "PakVFS: decompress failed for '{}' (compressed={}, raw={})",
               path, loc->compressedSize, loc->rawSize);
        return {};
    }

    // 6. Insert into LRU cache for future reads
    const size_t rawSize = raw.size();
    m_cache->Put(hash, raw, rawSize);

    return raw;
}

bool PakVFS::Exists(const std::string& path) {
    // Legacy mode
    if (!m_index.IsLoaded()) {
        return m_reader.Contains(path);
    }

    // Lazy mode: pure index lookup, no I/O
    const uint64_t hash = XXH64(path.data(), path.size(), 0);
    return m_index.Contains(hash);
}

std::string PakVFS::Name() const {
    return m_name;
}

std::string PakVFS::ResolvePath(const std::string& /*path*/) const {
    // Pak assets have no filesystem path — return empty string
    return {};
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
