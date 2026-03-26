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
#include <cstdio>
#include <optional>
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
    /// Cached entry metadata from the index for zero-copy lookup.
    struct LoadedEntry {
        uint64_t offset;        ///< Byte offset into data region
        uint64_t size;          ///< Asset size in bytes
        uint64_t contentHash;   ///< xxHash64 of blob data (integrity check)
    };

    std::vector<uint8_t> m_data;
    std::unordered_map<uint64_t, LoadedEntry> m_index;
    bool m_loaded = false;
};

// ─── Pak Flags ───────────────────────────────────────────────────────────────

/// Pak blob is LZ4 compressed (decompress before use).
static constexpr uint32_t kPakFlagCompressed = 0x1;

/// Pak blob is XOR scrambled (descramble before decompression).
static constexpr uint32_t kPakFlagScrambled = 0x2;

// ─── Compression / Scramble ──────────────────────────────────────────────────

/// Scrambles (and optionally compresses) a raw asset blob for pak storage.
///
/// Currently applies XOR scramble only. LZ4 compression will be added when
/// runtime/thirdparty/lz4/ is integrated via scripts/setup.ps1.
///
/// # Arguments
/// - `raw`: Uncompressed asset bytes.
/// - `pathHash`: xxHash64 of the asset path (used as part of XOR key).
/// - `secret`: Application-level secret XORed with pathHash to derive the key.
///
/// # Returns
/// Scrambled bytes. Size equals `raw.size()` (no compression yet).
[[nodiscard]] std::vector<uint8_t> PakCompressBlob(
    const std::vector<uint8_t>& raw, uint64_t pathHash, uint64_t secret);

/// Descrambles (and optionally decompresses) a pak blob back to raw bytes.
///
/// # Arguments
/// - `data`: Pointer to scrambled (and possibly compressed) bytes.
/// - `compressedSize`: Size of the input data in bytes.
/// - `rawSize`: Expected size of the decompressed output.
/// - `pathHash`: Same pathHash used during compression.
/// - `secret`: Same secret used during compression.
///
/// # Returns
/// Original raw bytes on success, empty vector on failure.
[[nodiscard]] std::vector<uint8_t> PakDecompressBlob(
    const uint8_t* data, size_t compressedSize, size_t rawSize,
    uint64_t pathHash, uint64_t secret);

// ─── Master Index Format (.uzpakindex) ───────────────────────────────────────
// Small file loaded entirely in RAM. Maps pathHash -> (pakId, offset, sizes).
// Enables lazy loading from multiple .uzpak files without loading their data.

/// Magic number for .uzpakindex files: "UZPI" in little-endian.
static constexpr uint32_t kMasterIndexMagic = 0x49505A55;

/// Current .uzpakindex format version.
static constexpr uint32_t kMasterIndexVersion = 1;

/// Maximum filename length (including null terminator) for pak table entries.
static constexpr size_t kMaxPakFilename = 256;

/// Maximum size of a single .uzpak file in bytes (500 MB).
static constexpr size_t kMaxPakSize = size_t{500} * 1024 * 1024;

/// .uzpakindex file header -- exactly 16 bytes.
#pragma pack(push, 1)
struct MasterIndexHeader {
    uint32_t magic = kMasterIndexMagic;     ///< Must be kMasterIndexMagic ("UZPI")
    uint32_t version = kMasterIndexVersion; ///< Format version (currently 1)
    uint32_t pakCount = 0;                  ///< Number of .uzpak files referenced
    uint32_t entryCount = 0;               ///< Number of assets indexed
};
#pragma pack(pop)

static_assert(sizeof(MasterIndexHeader) == 16, "MasterIndexHeader must be exactly 16 bytes");

/// Pak table entry -- exactly 264 bytes.
/// Stores the filename and checksum for one .uzpak file referenced by the index.
#pragma pack(push, 1)
struct MasterPakTableEntry {
    uint32_t pakId = 0;                 ///< Unique identifier for this pak within the index
    char filename[256] = {};            ///< Null-terminated relative filename (forward slashes)
    uint32_t checksum = 0;              ///< xxHash32 of the pak file contents (integrity check)
};
#pragma pack(pop)

static_assert(sizeof(MasterPakTableEntry) == 264, "MasterPakTableEntry must be exactly 264 bytes");

/// Asset entry — exactly 56 bytes.
/// Maps a single asset (by path hash) to its location inside a specific .uzpak file.
#pragma pack(push, 1)
struct MasterAssetEntry {
    uint64_t pathHash = 0;        ///< xxHash64 of the logical asset path
    uint32_t pakId = 0;           ///< Index into the pak table (which .uzpak file)
    uint32_t typeId = 0;          ///< Application-defined asset type identifier
    uint64_t offset = 0;          ///< Byte offset of the blob within the .uzpak file
    uint64_t compressedSize = 0;  ///< Size of the (possibly compressed+scrambled) blob
    uint64_t rawSize = 0;         ///< Original uncompressed size in bytes
    uint64_t contentHash = 0;     ///< xxHash64 of the raw (uncompressed) asset data
};
#pragma pack(pop)

static_assert(sizeof(MasterAssetEntry) == 48, "MasterAssetEntry must be exactly 48 bytes");

/// .uzpakindex file trailer -- exactly 20 bytes.
#pragma pack(push, 1)
struct MasterIndexTrailer {
    uint64_t pakTableOffset = 0;    ///< Byte offset where the pak table starts
    uint64_t entryTableOffset = 0;  ///< Byte offset where the asset entry table starts
    uint32_t magic = kMasterIndexMagic; ///< Sentinel for trailer discovery
};
#pragma pack(pop)

static_assert(sizeof(MasterIndexTrailer) == 20, "MasterIndexTrailer must be exactly 20 bytes");

/// In-memory master index for multi-pak lazy loading.
///
/// Loads a .uzpakindex file and provides O(1) lookup of any asset's location
/// (pak filename, byte offset, sizes) without loading any actual pak data.
class MasterIndex {
public:
    /// Location of an asset within the multi-pak archive set.
    struct AssetLocation {
        std::string pakFilename;   ///< Relative filename of the .uzpak containing this asset
        uint64_t offset;           ///< Byte offset of the blob within the .uzpak file
        uint64_t compressedSize;   ///< Size of the stored (compressed+scrambled) blob
        uint64_t rawSize;          ///< Original uncompressed size in bytes
        uint64_t contentHash;      ///< xxHash64 of the raw asset data (for integrity)
        uint32_t typeId;           ///< Application-defined asset type identifier
    };

    /// Loads and parses a .uzpakindex file from disk.
    ///
    /// Validates header magic/version, reads the trailer to locate the pak
    /// table and asset entry table, then builds the in-memory hash map.
    ///
    /// # Returns
    /// `true` on success. `false` if the file is missing, too small, has
    /// wrong magic/version, or contains corrupted offsets.
    [[nodiscard]] bool LoadFile(const std::string& path);

    /// Looks up an asset by its precomputed xxHash64 path hash.
    ///
    /// # Returns
    /// The asset's location (pak file, offset, sizes) or `std::nullopt` if
    /// the hash is not in the index.
    [[nodiscard]] std::optional<AssetLocation> Lookup(uint64_t pathHash) const;

    /// Checks whether an asset exists in the index without returning its data.
    [[nodiscard]] bool Contains(uint64_t pathHash) const;

    /// Returns the total number of asset entries in the loaded index.
    [[nodiscard]] size_t EntryCount() const;

    /// Returns the number of .uzpak files referenced by the index.
    [[nodiscard]] size_t PakCount() const;

    /// Returns the filenames of all .uzpak files referenced by the index.
    [[nodiscard]] const std::vector<std::string>& PakFilenames() const;

    /// Returns `true` if a .uzpakindex has been successfully loaded.
    [[nodiscard]] bool IsLoaded() const { return m_loaded; }

    /// Unloads the index, freeing all internal memory.
    void Unload();

private:
    std::unordered_map<uint64_t, AssetLocation> m_assets;
    std::vector<std::string> m_pakFilenames;
    bool m_loaded = false;
};

/// Writes a .uzpakindex file to disk.
///
/// The output file contains: header (16 bytes) + asset entries (sorted by
/// pathHash, 48 bytes each) + pak table (264 bytes each) + trailer (20 bytes).
///
/// # Arguments
/// - `path`: Output file path for the .uzpakindex.
/// - `pakFilenames`: Ordered list of .uzpak filenames. Index = pakId.
/// - `entries`: All asset entries to include (pakId must be valid index into pakFilenames).
///
/// # Returns
/// `true` on success, `false` on I/O error or validation failure.
bool WriteMasterIndex(const std::string& path,
                      const std::vector<std::string>& pakFilenames,
                      const std::vector<MasterAssetEntry>& entries);

// ─── Multi-Pak Split Writer ─────────────────────────────────────────────────

/// Result of a WritePakSplit operation.
struct PakSplitResult {
    bool success = false;           ///< true if all paks + index were written
    uint32_t pakCount = 0;          ///< Number of .uzpak files created
    uint64_t totalRawBytes = 0;     ///< Sum of uncompressed asset sizes
    uint64_t totalPackedBytes = 0;  ///< Sum of compressed+scrambled sizes (across all paks)
    std::string indexPath;          ///< Path to the written .uzpakindex file
};

/// Splits assets into multiple .uzpak files and writes a .uzpakindex master index.
///
/// Each .uzpak file is capped at `maxPakSizeBytes`. Assets are compressed with
/// XOR scramble using the provided `secret`. The output files are:
///   - `{outputDir}/{baseName}.uzpakindex` — master index
///   - `{outputDir}/{baseName}_001.uzpak`, `{baseName}_002.uzpak`, ...
///
/// # Arguments
/// - `entries`: Assets to pack (path + raw data + typeId).
/// - `outputDir`: Directory for output files (must exist).
/// - `baseName`: Base name for output files (e.g., "game").
/// - `maxPakSizeBytes`: Maximum size per .uzpak file (default: kMaxPakSize = 500MB).
/// - `secret`: XOR scramble secret (0 = no scramble).
///
/// # Returns
/// A `PakSplitResult` with success status and statistics.
[[nodiscard]] PakSplitResult WritePakSplit(
    const std::vector<PakWriteEntry>& entries,
    const std::string& outputDir,
    const std::string& baseName,
    size_t maxPakSizeBytes = kMaxPakSize,
    uint64_t secret = 0);

// ─── Seek-Based Lazy Reader ─────────────────────────────────────────────────

/// Seek-based reader for .uzpak files.
///
/// Opens a file handle and reads individual blobs on demand via seek+read
/// without loading the entire archive into memory. This is the I/O primitive
/// used by the lazy-load pipeline.
///
/// Owns a single FILE* handle. Not thread-safe -- callers must synchronize.
class PakStreamReader {
public:
    PakStreamReader() = default;
    ~PakStreamReader();

    PakStreamReader(const PakStreamReader&) = delete;
    PakStreamReader& operator=(const PakStreamReader&) = delete;

    PakStreamReader(PakStreamReader&& other) noexcept;
    PakStreamReader& operator=(PakStreamReader&& other) noexcept;

    /// Opens a .uzpak file for streaming reads.
    ///
    /// Only opens the file handle -- does NOT read any data from the file.
    ///
    /// # Returns
    /// `true` if the file was opened successfully.
    [[nodiscard]] bool Open(const std::string& pakPath);

    /// Reads a blob at the given byte offset and size from the open file.
    ///
    /// Seeks to `offset` and reads `size` bytes. Returns empty vector on
    /// any I/O error (seek failure, partial read, file not open).
    [[nodiscard]] std::vector<uint8_t> ReadBlob(uint64_t offset, uint64_t size);

    /// Closes the file handle. Safe to call if already closed or never opened.
    void Close();

    /// Returns `true` if a file is currently open.
    [[nodiscard]] bool IsOpen() const;

    /// Returns the path of the currently open file (empty if not open).
    [[nodiscard]] const std::string& Path() const { return m_path; }

private:
    std::string m_path;
    FILE* m_file = nullptr;
};

} // namespace uz
