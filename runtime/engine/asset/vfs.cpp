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
#include <array>
#include <filesystem>
#include <system_error>

static auto* cv_PakCacheMB = UZ_CVAR_INT("asset.PakCacheMB", 256, "PakVFS LRU cache size in MB", 64, 2048);

namespace fs = std::filesystem;

namespace uz {

UZ_DEFINE_STAT(IO, VFSReads);

// ─────────────────────────────────────────────────────────────────────────────
// Logical path validation
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Win32 device names that resolve to a device in any directory, with or
/// without an extension. The superscript-digit variants are UTF-8 encoded
/// because Win32 treats `COM¹`..`COM³` / `LPT¹`..`LPT³` as devices too.
constexpr std::array<std::string_view, 30> kReservedDeviceNames = {
    "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    "COM\xC2\xB9", "COM\xC2\xB2", "COM\xC2\xB3",
    "LPT\xC2\xB9", "LPT\xC2\xB2", "LPT\xC2\xB3",
};

[[nodiscard]] char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool EqualsIgnoreAsciiCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y) { return ToLowerAscii(x) == ToLowerAscii(y); });
}

/// Win32 matches device names on the part before the first '.', ignoring
/// trailing spaces ("nul.txt" and "NUL .txt" both open the NUL device).
[[nodiscard]] bool IsReservedDeviceName(std::string_view segment) {
    std::string_view stem = segment.substr(0, segment.find('.'));
    while (!stem.empty() && stem.back() == ' ') {
        stem.remove_suffix(1);
    }
    return std::any_of(kReservedDeviceNames.begin(), kReservedDeviceNames.end(),
                       [stem](std::string_view device) { return EqualsIgnoreAsciiCase(stem, device); });
}

/// Rejected paths are attacker-controlled; keep control bytes (NUL, newlines)
/// out of the log so a path cannot forge or truncate log lines.
[[nodiscard]] std::string SanitizeForLog(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            c = '?';
        }
    }
    return out;
}

/// Absolute form of `p` with every symlink/junction in its existing prefix
/// resolved, without a trailing separator so element-wise comparison never
/// sees an empty final element.
[[nodiscard]] std::optional<fs::path> CanonicalForm(const fs::path& p) {
    std::error_code ec;
    const fs::path absolutePath = fs::absolute(p, ec);
    if (ec) {
        return std::nullopt;
    }
    fs::path canonical = fs::weakly_canonical(absolutePath, ec);
    if (ec) {
        return std::nullopt;
    }
    if (!canonical.has_filename() && canonical.has_relative_path()) {
        canonical = canonical.parent_path();
    }
    return canonical;
}

/// Canonical form of a VFS root directory; an empty root means the working directory.
[[nodiscard]] std::optional<fs::path> CanonicalRoot(const std::string& rootDir) {
    return CanonicalForm(rootDir.empty() ? fs::path(".") : fs::path(rootDir));
}

/// True when `fullPath`, after resolving links, lies strictly below the
/// already-canonical `root`. Any canonicalization failure counts as outside
/// (fail closed).
[[nodiscard]] bool IsInsideRootOnDisk(const std::optional<fs::path>& root, const std::string& fullPath) {
    const std::optional<fs::path> target = CanonicalForm(fs::path(fullPath));
    if (!root || !target) {
        return false;
    }
    const fs::path relative = target->lexically_relative(*root);
    if (relative.empty() || relative == fs::path(".")) {
        return false;
    }
    return *relative.begin() != fs::path("..");
}

} // namespace

std::optional<std::string> NormalizeVfsPath(std::string_view path) {
    if (path.empty() ||
        path.find('\0') != std::string_view::npos ||
        path.find(':') != std::string_view::npos ||
        path.front() == '/' || path.front() == '\\') {
        return std::nullopt;
    }

    std::vector<std::string_view> segments;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view segment = path.substr(start, end - start);
        start = end + 1;

        if (segment.empty() || segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (segments.empty()) {
                return std::nullopt;  // climbs above the root
            }
            segments.pop_back();
            continue;
        }
        if (segment.back() == '.' || segment.back() == ' ' || IsReservedDeviceName(segment)) {
            return std::nullopt;
        }
        segments.push_back(segment);
    }

    if (segments.empty()) {
        return std::nullopt;  // names the root itself, not a file below it
    }

    std::string normalized;
    normalized.reserve(path.size());
    for (const std::string_view segment : segments) {
        if (!normalized.empty()) {
            normalized += '/';
        }
        normalized.append(segment);
    }
    return normalized;
}

bool IsPlainFileName(std::string_view name) {
    const std::optional<std::string> normalized = NormalizeVfsPath(name);
    return normalized.has_value() && *normalized == name &&
           normalized->find('/') == std::string::npos;
}

// ─────────────────────────────────────────────────────────────────────────────
// LooseFileVFS — disk-based file access for development mode
// ─────────────────────────────────────────────────────────────────────────────

LooseFileVFS::LooseFileVFS(const std::string& rootDir)
    : m_rootDir(rootDir) {
    // Ensure trailing slash so concatenation works: "root/" + "path"
    if (!m_rootDir.empty() && m_rootDir.back() != '/' && m_rootDir.back() != '\\') {
        m_rootDir += '/';
    }
    // Resolved once: canonicalizing the root on every read would double the
    // filesystem queries on the asset-loading hot path.
    m_canonicalRoot = CanonicalRoot(m_rootDir);
    UZ_LOG(Asset, Debug, "LooseFileVFS mounted: root='{}'", m_rootDir);
}

std::optional<std::string> LooseFileVFS::ResolvePath(const std::string& path) const {
    const std::optional<std::string> logicalPath = NormalizeVfsPath(path);
    if (!logicalPath) {
        UZ_LOG(Asset, Warning,
               "LooseFileVFS: rejected path '{}': not a relative path inside root '{}'",
               SanitizeForLog(path), m_rootDir);
        return std::nullopt;
    }

    std::string fullPath = m_rootDir + *logicalPath;
    const std::optional<fs::path> canonicalRoot = m_canonicalRoot ? m_canonicalRoot : CanonicalRoot(m_rootDir);
    if (!IsInsideRootOnDisk(canonicalRoot, fullPath)) {
        UZ_LOG(Asset, Warning,
               "LooseFileVFS: rejected path '{}': resolves outside root '{}' (link or unresolvable path)",
               SanitizeForLog(path), m_rootDir);
        return std::nullopt;
    }
    return fullPath;
}

std::vector<uint8_t> LooseFileVFS::Read(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    UZ_STAT_INCREMENT(IO, VFSReads);

    const std::optional<std::string> fullPath = ResolvePath(path);
    if (!fullPath) {
        return {};
    }

    // Lock protects against concurrent ifstream reads on the same LooseFileVFS
    // instance from multiple AsyncTextureDecoder worker threads.
    std::lock_guard<std::mutex> lock(m_mutex);

    auto data = FileReadAll(fullPath->c_str());
    if (data.empty() && !fs::exists(*fullPath)) {
        // FileReadAll already logs the I/O error; only log here when file truly absent.
        return {};
    }

    return data;
}

bool LooseFileVFS::Exists(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    const std::optional<std::string> fullPath = ResolvePath(path);
    if (!fullPath) {
        return false;
    }
    return fs::exists(*fullPath) && fs::is_regular_file(*fullPath);
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

    // Pak names come from the index file: each must name a sibling of the
    // index, never a path that reaches another directory.
    for (const std::string& pakFilename : vfs->m_index.PakFilenames()) {
        if (!vfs->PakFilePath(pakFilename)) {
            UZ_LOG(Asset, Error, "PakVFS: refusing master index with unsafe pak name: path='{}'",
                   indexPath);
            return nullptr;
        }
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
    const std::optional<std::string> pakPath = PakFilePath(loc->pakFilename);
    if (!pakPath) {
        return {};
    }
    PakStreamReader threadReader;
    if (!threadReader.Open(*pakPath)) {
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

std::optional<std::string> PakVFS::ResolvePath(const std::string& /*path*/) const {
    // Pak assets have no filesystem path.
    return std::nullopt;
}

std::optional<std::string> PakVFS::PakFilePath(const std::string& pakFilename) const {
    if (!IsPlainFileName(pakFilename)) {
        UZ_LOG(Asset, Error, "PakVFS: pak name '{}' is not a plain file name",
               SanitizeForLog(pakFilename));
        return std::nullopt;
    }
    return m_baseDir + "/" + pakFilename;
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
    const std::optional<std::string> pakPath = PakFilePath(pakFilename);
    if (!pakPath) {
        return nullptr;
    }
    m_handleList.emplace_front(HandleEntry{pakFilename, {}});
    auto& entry = m_handleList.front();
    if (!entry.reader.Open(*pakPath)) {
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
