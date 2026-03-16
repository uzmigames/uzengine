/// UzEngine .uzpak Archive Format
///
/// Flat binary archive for shipping precompiled assets. Entries are indexed
/// by xxHash64 path hash for O(1) lookup, with binary search fallback.
///
/// # Binary Layout (little-endian)
/// ```
/// [Header]     16 bytes
///   magic:        4 bytes   "UZPK" (0x4B505A55)
///   version:      4 bytes   uint32 (1)
///   entryCount:   4 bytes   uint32
///   flags:        4 bytes   uint32 (bit 0 = reserved)
///
/// [Data Region] variable
///   blob[0]:      aligned to 64 bytes, raw bytes
///   blob[1]:      aligned to 64 bytes, raw bytes
///   ...
///
/// [Index]       entryCount * 40 bytes
///   per entry:
///     pathHash:     8 bytes   uint64 (xxhash64 of logical path)
///     offset:       8 bytes   uint64 (byte offset into data region)
///     size:         8 bytes   uint64 (uncompressed size)
///     contentHash:  8 bytes   uint64 (xxhash64 of blob for integrity)
///     typeId:       4 bytes   uint32 (asset type identifier)
///     reserved:     4 bytes   uint32
///
/// [Trailer]     16 bytes
///   indexOffset:   8 bytes   uint64 (byte offset of Index start)
///   indexChecksum: 4 bytes   uint32 (reserved, 0 for now)
///   magic:         4 bytes   "UZPK" (sentinel for trailer discovery)
/// ```
///
/// # Data Alignment
/// All data blobs are aligned to 64 bytes (DX12 resource alignment requirement).

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

/// Magic number for .uzpak files: "UZPK" in little-endian.
static constexpr uint32_t kPakMagic = 0x4B505A55;

/// Current .uzpak format version.
static constexpr uint32_t kPakVersion = 1;

/// Data blob alignment in bytes (DX12 requirement).
static constexpr size_t kPakAlignment = 64;

/// .uzpak file header — exactly 16 bytes.
#pragma pack(push, 1)
struct PakHeader {
    uint32_t magic = kPakMagic;          ///< Must be kPakMagic ("UZPK" = 0x4B505A55)
    uint32_t version = kPakVersion;      ///< Format version (currently 1)
    uint32_t entryCount = 0;             ///< Number of assets stored in this archive
    uint32_t flags = 0;                  ///< Reserved for future use (bit 0 reserved)
};
#pragma pack(pop)

static_assert(sizeof(PakHeader) == 16, "PakHeader must be exactly 16 bytes");

/// Index entry — exactly 40 bytes.
#pragma pack(push, 1)
struct PakIndexEntry {
    uint64_t pathHash = 0;       ///< xxHash64 of logical asset path
    uint64_t offset = 0;         ///< Byte offset into data region
    uint64_t size = 0;           ///< Asset size in bytes
    uint64_t contentHash = 0;    ///< xxHash64 of blob data (integrity check)
    uint32_t typeId = 0;         ///< Asset type identifier (0 = generic)
    uint32_t reserved = 0;
};
#pragma pack(pop)

static_assert(sizeof(PakIndexEntry) == 40, "PakIndexEntry must be exactly 40 bytes");

/// .uzpak file trailer — exactly 16 bytes.
#pragma pack(push, 1)
struct PakTrailer {
    uint64_t indexOffset = 0;    ///< Byte offset where the index starts
    uint32_t indexChecksum = 0;  ///< Reserved for future CRC32
    uint32_t magic = kPakMagic;  ///< Sentinel for trailer discovery
};
#pragma pack(pop)

static_assert(sizeof(PakTrailer) == 16, "PakTrailer must be exactly 16 bytes");

/// Entry to be packed into a .uzpak archive by `WritePak()`.
///
/// Each entry represents one asset to include in the archive.
/// The `path` is hashed with xxHash64 to form the index key.
struct PakWriteEntry {
    std::string path;                ///< Logical asset path used as lookup key (e.g., "shaders/simple_vs.uzshader")
    std::vector<uint8_t> data;       ///< Raw asset bytes to store in the data region
    uint32_t typeId = 0;             ///< Application-defined asset type identifier (0 = generic/untyped)
};

/// Writes a .uzpak archive from a list of entries into an in-memory buffer.
///
/// # Layout
/// Produces a complete .uzpak binary: header (16 bytes), data blobs
/// (each 64-byte aligned), index (40 bytes per entry, sorted by pathHash),
/// and trailer (16 bytes).
///
/// # Arguments
/// - `entries`: Assets to pack. Each entry provides a logical path,
///   raw byte data, and an optional type identifier.
///
/// # Returns
/// Complete archive bytes ready to write to disk or embed.
/// Returns empty vector if entries is empty.
///
/// # Side Effects
/// Logs `Asset::Debug` with the number of entries packed.
[[nodiscard]] std::vector<uint8_t> WritePak(const std::vector<PakWriteEntry>& entries);

/// Writes a .uzpak archive to a file on disk.
///
/// Calls `WritePak()` internally, then writes the result to `path`.
///
/// # Arguments
/// - `path`: Absolute or relative file path for the output .uzpak.
/// - `entries`: Assets to pack (see `WritePak()` for details).
///
/// # Returns
/// `true` if the archive was written successfully, `false` on I/O error.
///
/// # Errors
/// Logs `Asset::Error` with the file path on write failure.
bool WritePakFile(const std::string& path, const std::vector<PakWriteEntry>& entries);

/// Loaded .uzpak archive for runtime asset lookup.
///
/// Loads the entire archive into memory, builds a hash-based index,
/// and provides O(1) lookup by path hash or path string.
class PakReader {
public:
    /// Loads a .uzpak archive from raw bytes already in memory.
    ///
    /// Validates the header magic and version, reads the trailer to find
    /// the index offset, parses all index entries, and builds the hash
    /// lookup map. The input data is copied into internal storage.
    ///
    /// # Arguments
    /// - `data`: Complete .uzpak archive bytes (header + data + index + trailer).
    ///
    /// # Returns
    /// `true` if the archive was parsed successfully. `false` if the data
    /// is too small, has wrong magic, wrong version, or corrupted index.
    ///
    /// # Errors
    /// Logs `Asset::Error` with specific reason on failure.
    [[nodiscard]] bool Load(const std::vector<uint8_t>& data);

    /// Loads a .uzpak archive from a file on disk.
    ///
    /// Reads the entire file into memory, then calls `Load()`.
    ///
    /// # Arguments
    /// - `path`: Absolute or relative path to the .uzpak file.
    ///
    /// # Returns
    /// `true` on success, `false` if the file cannot be opened or is invalid.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the file path on failure.
    [[nodiscard]] bool LoadFile(const std::string& path);

    /// Looks up an asset by its logical path string.
    ///
    /// Computes `xxHash64(path)` and queries the in-memory index.
    ///
    /// # Arguments
    /// - `path`: Logical asset path (e.g., "shaders/simple_vs.uzshader").
    ///
    /// # Returns
    /// - Copy of the asset bytes on success.
    /// - Empty vector if the path is not in the archive.
    [[nodiscard]] std::vector<uint8_t> Lookup(const std::string& path) const;

    /// Looks up an asset by a precomputed xxHash64 path hash.
    ///
    /// Useful when the caller has already hashed the path (avoids rehashing).
    ///
    /// # Returns
    /// - Copy of the asset bytes on success.
    /// - Empty vector if no entry matches the hash.
    [[nodiscard]] std::vector<uint8_t> LookupByHash(uint64_t pathHash) const;

    /// Checks if an asset exists at the given logical path.
    ///
    /// Does NOT load the data — only checks the index.
    [[nodiscard]] bool Contains(const std::string& path) const;

    /// Returns the number of entries in the loaded archive.
    ///
    /// Returns 0 if no archive is loaded.
    [[nodiscard]] size_t EntryCount() const;

    /// Returns all path hashes present in the archive index.
    ///
    /// Useful for iteration or debugging. The hashes are xxHash64 values
    /// of the original logical paths used during packing.
    [[nodiscard]] std::vector<uint64_t> GetPathHashes() const;

    /// Returns `true` if an archive has been successfully loaded.
    [[nodiscard]] bool IsLoaded() const { return m_loaded; }

    /// Unloads the archive, freeing all internal memory.
    ///
    /// After calling this, `IsLoaded()` returns `false` and all lookups
    /// return empty results.
    void Unload();

private:
    struct LoadedEntry {
        uint64_t offset;
        uint64_t size;
        uint64_t contentHash;
    };

    std::vector<uint8_t> m_data;
    std::unordered_map<uint64_t, LoadedEntry> m_index;
    bool m_loaded = false;
};

} // namespace uz
