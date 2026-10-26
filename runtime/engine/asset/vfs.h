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
/// # Containment
/// Paths reaching a VFS frequently come from untrusted data (scene files,
/// material graphs, Lua scripts). `NormalizeVfsPath()` is the single gate
/// that decides whether such a path stays below the VFS root; every
/// file-backed backend applies it before touching the disk, and rejects the
/// path (empty result / `false` / `std::nullopt`) when it would escape.
///
/// # Resolution Order (LayeredVFS)
/// 1. Highest-priority mounted source that contains the path
/// 2. Next-priority source (descending order)
/// 3. Failure — empty vector returned, no exception
///
/// # Thread Safety
/// PakVFS and LooseFileVFS are internally thread-safe — Read() and Exists()
/// can be called from any thread (including AsyncTextureDecoder workers).
/// LayeredVFS delegates to its mounted backends' thread safety guarantees.
/// Mount()/UnmountAll() are NOT thread-safe and must only be called during
/// initialization or with external synchronization.

#pragma once

#include "pak_format.h"
#include "engine/core/lru_cache.h"

#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Logical path validation
// ─────────────────────────────────────────────────────────────────────────────

/// Validates an untrusted logical VFS path and returns its normalized form.
///
/// The check is purely lexical, so it gives the same answer on every
/// platform and for files that do not exist yet (save targets). Backends
/// that map onto a real directory add an on-disk check for links on top
/// (see `LooseFileVFS::ResolvePath`).
///
/// # Normalization
/// `\` separators become `/`, empty and `.` segments are dropped and a `..`
/// that stays inside the root is collapsed (`"a/../b"` -> `"b"`).
///
/// # Rejected (returns `std::nullopt`)
/// - Empty paths, and paths that normalize to the root itself (`"."`, `"a/.."`).
/// - Embedded NUL characters: C file APIs would silently truncate the path.
/// - Rooted paths: a leading `/` or `\` (POSIX root, Windows current-drive
///   root, UNC `\\server\share`, device `\\?\` and `\\.\` forms).
/// - Any `:` character: drive letters (`C:/x`), drive-relative paths (`C:x`)
///   and NTFS alternate data streams (`file:stream`).
/// - A `..` segment that climbs above the root (`"../x"`, `"a/../../x"`).
/// - Segments ending in `.` or space other than `.`/`..`: Win32 strips those
///   characters, so `".. "` would otherwise behave as `".."`.
/// - Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`,
///   `LPT1`-`LPT9`, with or without an extension, any case): they open a
///   device instead of a file below the root. Rejected on every platform so
///   content stays portable.
///
/// # Returns
/// The normalized relative path (forward slashes, no `.`/`..`/empty
/// segments), or `std::nullopt` when the path is not a contained logical path.
[[nodiscard]] std::optional<std::string> NormalizeVfsPath(std::string_view path);

/// Returns `true` if `name` is a single plain file name that is safe to join
/// onto a directory: it passes `NormalizeVfsPath()` unchanged and contains no
/// separator, so no `/`, `\`, `:`, NUL, `.`, `..` or device names.
///
/// Used for `.uzpak` file names read from a master index, which must name a
/// sibling of the index file and nothing else.
[[nodiscard]] bool IsPlainFileName(std::string_view name);

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

    /// Resolves a logical VFS path to a filesystem path inside the VFS root.
    ///
    /// Only file-backed backends (LooseFileVFS) have a filesystem mapping.
    /// Archive-backed and composite backends (PakVFS, LayeredVFS) return
    /// `std::nullopt`; callers then read the asset through `Read()`.
    ///
    /// # Arguments
    /// - `path`: Logical asset path relative to the VFS root. May come from
    ///   untrusted data; it is validated with `NormalizeVfsPath()`.
    ///
    /// # Returns
    /// - Filesystem path below the VFS root.
    /// - `std::nullopt` if the backend has no filesystem mapping, or if the
    ///   path is rejected because it would resolve outside the root.
    [[nodiscard]] virtual std::optional<std::string> ResolvePath(const std::string& /*path*/) const {
        return std::nullopt;
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

/// Filesystem path for an asset reference taken from scene/content data.
///
/// With a VFS bound the reference is resolved through `IVFS::ResolvePath`,
/// so it must stay inside the VFS root (absolute paths and `..` escapes are
/// rejected) and backends without a filesystem mapping yield `std::nullopt`.
/// Without a VFS there is no content root to contain against: the path was
/// supplied by the application itself and is returned unchanged.
///
/// # Returns
/// The filesystem path to open, or `std::nullopt` if there is none.
[[nodiscard]] inline std::optional<std::string> ResolveAssetFsPath(const IVFS* vfs,
                                                                   const std::string& path) {
    if (!vfs) {
        return path;
    }
    return vfs->ResolvePath(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// LooseFileVFS — reads from disk (dev mode)
// ─────────────────────────────────────────────────────────────────────────────

/// VFS backend that reads files directly from a directory on disk.
///
/// Used in development mode for fast iteration — assets are read from
/// their source location without any packing or preprocessing step.
///
/// The root directory is set at construction time. Logical paths are
/// validated with `NormalizeVfsPath()` and joined onto the root; paths that
/// would leave the root are rejected (see `ResolvePath()`).
///
/// # Logging
/// - Logs `Asset::Debug` on construction with the root directory.
/// - Logs `Asset::Warning` when a path is rejected for leaving the root.
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

    /// Reads a file from disk at `ResolvePath(path)`.
    ///
    /// Opens the file in binary mode, reads all bytes into a vector.
    ///
    /// # Returns
    /// - File contents as bytes on success.
    /// - Empty vector if the path is rejected by `ResolvePath()`, or the file
    ///   cannot be opened, is empty, or read fails.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the full resolved path on I/O failure.
    [[nodiscard]] std::vector<uint8_t> Read(const std::string& path) override;

    /// Checks if a regular file exists at `ResolvePath(path)`.
    ///
    /// # Returns
    /// `true` if the path is accepted by `ResolvePath()` and names an existing
    /// regular file (not a directory).
    [[nodiscard]] bool Exists(const std::string& path) override;

    /// Returns `"LooseFileVFS(<rootDir>)"`.
    [[nodiscard]] std::string Name() const override;

    /// Resolves a logical path to a filesystem path contained in the root.
    ///
    /// Two checks, both of which must pass:
    /// 1. Lexical: `NormalizeVfsPath()` rejects absolute paths, drive
    ///    letters, NUL bytes and `..` escapes before anything touches the
    ///    disk. This alone guarantees `rootDir + normalized` is textually
    ///    below the root, including for files that do not exist yet.
    /// 2. On disk: the root (once, at construction) and the joined path are
    ///    both passed through `std::filesystem::weakly_canonical`, which follows every symlink or
    ///    junction in the part of the path that exists, and the result must
    ///    still lie below the canonical root. This catches a link inside the
    ///    content tree that points elsewhere. A canonicalization error fails
    ///    closed (the path is rejected).
    ///
    /// The on-disk check is best effort against a concurrent attacker who can
    /// swap a directory for a link between this call and the later open
    /// (time-of-check/time-of-use); content trees are not expected to be
    /// modified concurrently by an adversary while the engine runs.
    ///
    /// # Returns
    /// - `rootDir + normalized path` (forward slashes) on success.
    /// - `std::nullopt` if the path is rejected; logs `Asset::Warning`.
    [[nodiscard]] std::optional<std::string> ResolvePath(const std::string& path) const override;

private:
    mutable std::mutex m_mutex;  ///< Protects Read() for thread-safe VFS access from worker threads.
    std::string m_rootDir;       ///< Absolute root directory, always ends with '/'
    /// Root with links resolved (weakly_canonical) at construction, used by
    /// the on-disk containment check. `std::nullopt` if it could not be
    /// resolved then; `ResolvePath()` then resolves it per call. Immutable
    /// after construction, so concurrent readers need no lock.
    std::optional<std::filesystem::path> m_canonicalRoot;
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
    /// - `nullptr` if the index file cannot be loaded, or if any pak file
    ///   name it lists is not a plain file name (`IsPlainFileName()`): a
    ///   name such as `"../x"` or `"C:/x"` would otherwise make the engine
    ///   open an arbitrary file as a pak.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the file path on failure.
    [[nodiscard]] static std::unique_ptr<PakVFS> FromIndexFile(
        const std::string& indexPath, uint64_t secret = 0,
        size_t cacheSizeBytes = 256 * size_t{1024} * 1024);

    ~PakVFS() override = default;

    /// Non-movable — mutex is not movable. PakVFS is always heap-allocated
    /// via unique_ptr from the factory methods, so moves are unnecessary.
    PakVFS(PakVFS&&) = delete;
    PakVFS& operator=(PakVFS&&) = delete;

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

    /// Returns `std::nullopt` — pak assets have no filesystem path.
    [[nodiscard]] std::optional<std::string> ResolvePath(const std::string& path) const override;

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
    /// Cached file handle for one open .uzpak, evicted LRU when pool is full.
    struct HandleEntry { std::string pakName; PakStreamReader reader; };
    std::list<HandleEntry> m_handleList;                      ///< Front = MRU, back = LRU
    std::unordered_map<std::string, std::list<HandleEntry>::iterator> m_handleMap;

    /// Gets or opens a PakStreamReader for the given pak filename.
    /// Promotes to MRU on hit, evicts LRU handle if pool is full.
    /// Returns `nullptr` if the file cannot be opened or the name is rejected
    /// by `PakFilePath()`.
    PakStreamReader* AcquireReader(const std::string& pakFilename);

    /// Joins a pak file name from the master index onto `m_baseDir`.
    ///
    /// # Returns
    /// `m_baseDir + "/" + pakFilename`, or `std::nullopt` (with an
    /// `Asset::Error` log) if `pakFilename` is not a plain file name.
    [[nodiscard]] std::optional<std::string> PakFilePath(const std::string& pakFilename) const;

    // ── Shared state ───────────────────────────────────────────────────
    mutable std::mutex m_mutex;  ///< Protects all mutable state (cache, handle pool, legacy reader) for thread-safe Read().
    std::string m_name;          ///< Human-readable name for logging
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
    ///
    /// `ResolvePath()` is not overridden: a layered stack has no single
    /// filesystem mapping, so it returns `std::nullopt` and callers read
    /// through `Read()`, where each mounted backend applies its own containment.
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
