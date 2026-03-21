/// UzEngine Virtual File System (VFS)
///
/// Provides transparent file access across different storage backends,
/// abstracting the difference between development (loose files on disk)
/// and shipping (packed .uzpak archives) modes.
///
/// # Backends
/// - `LooseFileVFS`: reads files directly from a root directory on disk.
///   Used during development for instant iteration without packing.
/// - `PakVFS`: reads assets from .uzpak archives. Supports two modes:
///   - **Legacy mode**: loads a single .uzpak entirely into memory via `PakReader`.
///   - **Lazy mode**: loads a .uzpakindex master index (~small) and reads
///     individual blobs on demand via `PakStreamReader` + `LRUCache`.
/// - `LayeredVFS`: combines multiple IVFS backends into a priority-based
///   mount stack. Higher-priority mounts shadow lower-priority ones.
///
/// # Path Convention
/// All paths are logical asset paths using forward slashes, relative to
/// the VFS root (e.g., "shaders/simple_vs.uzshader", "textures/brick.uztexture").
/// No leading slash. No Windows backslashes.
///
/// # Resolution Order (LayeredVFS)
/// 1. Highest-priority mounted source that contains the path
/// 2. Next-priority source (descending order)
/// 3. Failure — empty vector returned, no exception
///
/// # Thread Safety
/// Individual backends are NOT thread-safe. If concurrent access is needed,
/// the caller must provide external synchronization.

#pragma once

#include "pak_format.h"
#include "engine/core/lru_cache.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

/// Abstract interface for virtual file system backends.
///
/// Implementations provide read-only access to assets identified by
/// logical path strings. All paths use forward slashes and are relative
/// to the backend's root (e.g., "shaders/simple_vs.uzshader").
///
/// # Contract
/// - `Read()` returns empty on failure, never throws.
/// - `Exists()` is a lightweight check — does NOT load the file.
/// - `Name()` returns a human-readable identifier for log messages.
class IVFS {
public:
    virtual ~IVFS() = default;

    /// Reads an entire file into memory as a byte buffer.
    ///
    /// # Arguments
    /// - `path`: Logical asset path relative to the VFS root.
    ///   Uses forward slashes (e.g., "shaders/pbr_vs.uzshader").
    ///
    /// # Returns
    /// - Non-empty vector containing the file bytes on success.
    /// - Empty vector if the file does not exist or an I/O error occurs.
    [[nodiscard]] virtual std::vector<uint8_t> Read(const std::string& path) = 0;

    /// Checks if a file exists at the given path without reading it.
    ///
    /// # Arguments
    /// - `path`: Logical asset path relative to the VFS root.
    ///
    /// # Returns
    /// - `true` if the file exists and is accessible.
    /// - `false` if the file does not exist or is not a regular file.
    [[nodiscard]] virtual bool Exists(const std::string& path) = 0;

    /// Returns a human-readable name identifying this VFS backend.
    ///
    /// Used in log messages for diagnostics (e.g., "LooseFileVFS(/path/to/assets/)").
    [[nodiscard]] virtual std::string Name() const = 0;

    /// Resolves a logical VFS path to an absolute filesystem path.
    ///
    /// Only meaningful for file-backed backends (LooseFileVFS).
    /// Returns the input path unchanged for archive-backed backends (PakVFS).
    ///
    /// # Arguments
    /// - `path`: Logical asset path relative to the VFS root.
    ///
    /// # Returns
    /// Absolute filesystem path string, or `path` unchanged if not resolvable.
    [[nodiscard]] virtual std::string ResolvePath(const std::string& path) const {
        return path;
    }

    /// Statistics about the VFS internal asset cache.
    struct CacheStats {
        size_t currentBytes = 0;  ///< Current memory usage of cached assets
        size_t maxBytes = 0;      ///< Maximum cache budget in bytes
        size_t entryCount = 0;    ///< Number of entries currently cached
    };

    /// Returns VFS cache statistics. Default returns zeros (no cache).
    ///
    /// Only meaningful for backends that maintain an internal cache
    /// (e.g., PakVFS in lazy mode). File-backed backends return zeros.
    [[nodiscard]] virtual CacheStats GetCacheStats() const { return {}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// LooseFileVFS — reads from disk (dev mode)
// ─────────────────────────────────────────────────────────────────────────────

/// VFS backend that reads files directly from a directory on disk.
///
/// Used in development mode for fast iteration — assets are read from
/// their source location without any packing or preprocessing step.
///
/// The root directory is set at construction time. All logical paths
/// are resolved by simple concatenation: `rootDir + "/" + path`.
///
/// # Logging
/// - Logs `Asset::Debug` on construction with the root directory.
/// - Logs `Asset::Error` on I/O read failures with the resolved path.
///
/// # Example
/// ```cpp
/// LooseFileVFS vfs("E:/MyGame/assets/");
/// auto data = vfs.Read("shaders/simple_vs.uzshader");
/// // Resolves to: E:/MyGame/assets/shaders/simple_vs.uzshader
/// ```
class LooseFileVFS : public IVFS {
public:
    /// Creates a loose file VFS rooted at the given directory.
    ///
    /// # Arguments
    /// - `rootDir`: Absolute path to the root directory. Trailing slashes
    ///   are normalized (a slash is appended if missing).
    ///
    /// # Side Effects
    /// Logs `Asset::Debug` with the normalized root path.
    explicit LooseFileVFS(const std::string& rootDir);

    /// Reads a file from disk by resolving `rootDir + path`.
    ///
    /// Opens the file in binary mode, reads all bytes into a vector.
    ///
    /// # Returns
    /// - File contents as bytes on success.
    /// - Empty vector if the file cannot be opened, is empty, or read fails.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the full resolved path on I/O failure.
    [[nodiscard]] std::vector<uint8_t> Read(const std::string& path) override;

    /// Checks if a regular file exists at `rootDir + path`.
    ///
    /// # Returns
    /// `true` if the resolved path exists and is a regular file (not a directory).
    [[nodiscard]] bool Exists(const std::string& path) override;

    /// Returns `"LooseFileVFS(<rootDir>)"`.
    [[nodiscard]] std::string Name() const override;

    /// Concatenates the root directory with the logical path.
    ///
    /// # Returns
    /// Absolute filesystem path (`rootDir + path`).
    [[nodiscard]] std::string ResolvePath(const std::string& path) const override;

private:

    std::string m_rootDir;  ///< Absolute root directory, always ends with '/'
};

// ─────────────────────────────────────────────────────────────────────────────
// PakVFS — reads from .uzpak archives (ship mode)
// ─────────────────────────────────────────────────────────────────────────────

/// VFS backend that reads assets from .uzpak archives.
///
/// Supports two operating modes:
///
/// ## Legacy mode (FromFile / PakReader constructor)
/// Loads a single .uzpak entirely into memory. Lookups are O(1) via
/// xxHash64 path hash. Used for backward compatibility with single-pak
/// workflows and tests.
///
/// ## Lazy mode (FromIndexFile)
/// Loads only the .uzpakindex master index into RAM (~small). Pak file
/// handles are opened on-demand and blobs are read via seek+read through
/// `PakStreamReader`. An `LRUCache` holds recently-read assets to avoid
/// redundant I/O. This is the recommended mode for shipping builds with
/// multi-pak archives.
///
/// # Ownership
/// In legacy mode, owns a `PakReader` with the full archive in memory.
/// In lazy mode, owns the `MasterIndex`, a map of `PakStreamReader` handles
/// (opened lazily), and an `LRUCache` for decompressed asset data.
///
/// # Logging
/// - Logs `Asset::Debug` on construction with the entry count.
/// - Logs `Asset::Error` on I/O failures during Read().
///
/// # Examples
/// ```cpp
/// // Legacy: load entire single pak
/// auto vfs = PakVFS::FromFile("build/game.uzpak");
///
/// // Lazy: load only master index, read on demand
/// auto vfs = PakVFS::FromIndexFile("build/game.uzpakindex", mySecret);
///
/// auto data = vfs->Read("shaders/simple_vs.uzshader");
/// ```
class PakVFS : public IVFS {
public:
    /// Creates a pak VFS from an already-loaded PakReader (legacy mode).
    ///
    /// # Arguments
    /// - `reader`: A loaded PakReader. Ownership is transferred via move.
    ///   The reader must have been successfully loaded (`IsLoaded() == true`).
    ///
    /// # Side Effects
    /// Logs `Asset::Debug` with the number of entries in the archive.
    explicit PakVFS(PakReader reader);

    /// Creates a pak VFS by loading a single .uzpak file from disk (legacy mode).
    ///
    /// Validates the pak header, reads the index, and builds the lookup map.
    /// The entire archive is loaded into memory.
    ///
    /// # Arguments
    /// - `path`: Absolute or relative path to the .uzpak file on disk.
    ///
    /// # Returns
    /// - `unique_ptr<PakVFS>` on success.
    /// - `nullptr` if the file cannot be opened or has an invalid format.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the file path on failure.
    [[nodiscard]] static std::unique_ptr<PakVFS> FromFile(const std::string& path);

    /// Creates a pak VFS from a .uzpakindex master index file (lazy mode).
    ///
    /// Loads the master index into RAM and opens zero pak file handles
    /// (handles are opened lazily on first Read). The `baseDir` is derived
    /// from the directory containing `indexPath` and is prepended to pak
    /// filenames from the index to form absolute paths.
    ///
    /// An LRU cache with the given byte budget holds decompressed asset
    /// data to avoid redundant I/O for frequently-accessed assets.
    ///
    /// # Arguments
    /// - `indexPath`: Path to the .uzpakindex file.
    /// - `secret`: XOR descramble secret (must match the one used during packing).
    /// - `cacheSizeBytes`: LRU cache budget in bytes (default: 256 MB).
    ///
    /// # Returns
    /// - `unique_ptr<PakVFS>` on success.
    /// - `nullptr` if the index file cannot be loaded.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the file path on failure.
    [[nodiscard]] static std::unique_ptr<PakVFS> FromIndexFile(
        const std::string& indexPath, uint64_t secret = 0,
        size_t cacheSizeBytes = 256 * size_t{1024} * 1024);

    ~PakVFS() override = default;
    PakVFS(PakVFS&&) noexcept = default;
    PakVFS& operator=(PakVFS&&) noexcept = default;

    /// Reads an asset by logical path.
    ///
    /// In legacy mode: computes xxHash64 and queries the in-memory PakReader.
    /// In lazy mode: checks LRU cache first, then seeks into the appropriate
    /// .uzpak file, reads and decompresses the blob, and caches the result.
    ///
    /// # Returns
    /// - Copy of the asset bytes on success.
    /// - Empty vector if the path is not in the archive.
    [[nodiscard]] std::vector<uint8_t> Read(const std::string& path) override;

    /// Checks if an asset exists by path hash.
    ///
    /// In lazy mode this is a pure index lookup with no I/O.
    ///
    /// # Returns
    /// `true` if the archive contains an entry matching `xxHash64(path)`.
    [[nodiscard]] bool Exists(const std::string& path) override;

    /// Returns a human-readable name for logging.
    [[nodiscard]] std::string Name() const override;

    /// Returns empty string — pak assets have no filesystem path.
    [[nodiscard]] std::string ResolvePath(const std::string& path) const override;

    /// Returns a const reference to the underlying PakReader (legacy mode only).
    ///
    /// Useful for inspecting entry count, path hashes, or integrity data
    /// without going through the VFS abstraction.
    ///
    /// # Precondition
    /// Only valid in legacy mode (constructed via PakReader or FromFile).
    /// In lazy mode the reader is empty.
    [[nodiscard]] const PakReader& Reader() const { return m_reader; }

    /// Returns true if this PakVFS is operating in lazy (index-based) mode.
    [[nodiscard]] bool IsLazyMode() const { return m_index.IsLoaded(); }

    /// Returns cache statistics for lazy mode (LRU cache usage).
    /// In legacy mode returns zeros (no cache).
    [[nodiscard]] CacheStats GetCacheStats() const override;

private:
    /// Private default constructor for factory methods.
    PakVFS() = default;

    // ── Legacy mode state ──────────────────────────────────────────────
    PakReader m_reader;     ///< Loaded .uzpak archive (legacy mode only)

    // ── Lazy mode state ────────────────────────────────────────────────
    MasterIndex m_index;                                      ///< Path hash -> pak location
    std::unique_ptr<LRUCache<uint64_t, std::vector<uint8_t>>> m_cache; ///< Decompressed asset cache
    uint64_t m_secret = 0;                                    ///< XOR descramble secret
    std::string m_baseDir;                                    ///< Directory containing the .uzpak files

    /// File handle pool — max 8 simultaneous open pak files, LRU eviction.
    static constexpr size_t kMaxOpenHandles = 8;
    struct HandleEntry { std::string pakName; PakStreamReader reader; };
    std::list<HandleEntry> m_handleList;                      ///< Front = MRU, back = LRU
    std::unordered_map<std::string, std::list<HandleEntry>::iterator> m_handleMap;

    /// Gets or opens a PakStreamReader for the given pak filename.
    /// Promotes to MRU on hit, evicts LRU handle if pool is full.
    PakStreamReader* AcquireReader(const std::string& pakFilename);

    // ── Shared state ───────────────────────────────────────────────────
    std::string m_name;     ///< Human-readable name for logging
};

// ─────────────────────────────────────────────────────────────────────────────
// LayeredVFS — priority-based mount stack
// ─────────────────────────────────────────────────────────────────────────────

/// VFS that combines multiple backends into a priority-based mount stack.
///
/// When a file is requested, backends are queried in descending priority
/// order. The first backend that successfully returns data wins.
/// This enables layered content delivery: base game pak at priority 0,
/// DLC/patch pak at priority 50, loose dev files at priority 100.
///
/// # Priority Rules
/// - Higher numeric priority = queried first.
/// - Equal priority = insertion order (first mounted wins).
/// - Mount list is re-sorted after every `Mount()` call.
///
/// # Ownership
/// Mount points are held via `shared_ptr`, allowing the same backend
/// to be mounted in multiple LayeredVFS instances if needed.
///
/// # Logging
/// - Logs `Asset::Debug` on mount/unmount operations.
/// - Logs `Asset::Error` if a null VFS is passed to `Mount()`.
///
/// # Example
/// ```cpp
/// LayeredVFS layered;
/// layered.Mount(std::make_shared<LooseFileVFS>("assets/"), 100);
/// layered.Mount(PakVFS::FromFile("base.uzpak"), 0);
/// auto data = layered.Read("shaders/pbr.uzshader");
/// // Tries loose files first (priority 100), then pak (priority 0)
/// ```
class LayeredVFS : public IVFS {
public:
    /// Mounts a VFS backend at the given priority level.
    ///
    /// The mount list is re-sorted after insertion so that higher-priority
    /// backends are queried first during `Read()` and `Exists()`.
    ///
    /// # Arguments
    /// - `vfs`: Shared pointer to the VFS backend. Must not be null.
    /// - `priority`: Numeric priority. Higher values = higher precedence.
    ///   Typical values: 0 (base pak), 50 (DLC), 100 (dev overlay).
    ///
    /// # Side Effects
    /// - Re-sorts the internal mount list by descending priority.
    /// - Logs `Asset::Debug` with the backend name and priority.
    /// - Logs `Asset::Error` and returns early if `vfs` is null.
    void Mount(std::shared_ptr<IVFS> vfs, int priority);

    /// Unmounts all backends, clearing the entire mount stack.
    ///
    /// # Side Effects
    /// Logs `Asset::Debug` confirming the unmount.
    void UnmountAll();

    /// Returns the number of currently mounted backends.
    [[nodiscard]] size_t MountCount() const;

    /// Queries each mounted backend in descending priority order.
    ///
    /// Returns the data from the first backend that returns a non-empty result.
    ///
    /// # Returns
    /// - File bytes from the highest-priority backend that has the file.
    /// - Empty vector if no backend contains the file.
    [[nodiscard]] std::vector<uint8_t> Read(const std::string& path) override;

    /// Checks if any mounted backend contains the file.
    ///
    /// Queries backends in descending priority order, returns `true`
    /// on the first hit.
    [[nodiscard]] bool Exists(const std::string& path) override;

    /// Returns `"LayeredVFS(<N> mounts)"` where N is the mount count.
    [[nodiscard]] std::string Name() const override;

private:
    /// A single mount point in the priority stack.
    struct MountPoint {
        std::shared_ptr<IVFS> vfs;  ///< The mounted backend
        int priority;                ///< Numeric priority (higher = first)
    };

    /// Mount points sorted by descending priority.
    std::vector<MountPoint> m_mounts;
};

} // namespace uz
