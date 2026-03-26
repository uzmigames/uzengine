/// UzEngine Asset Manifest (.uzmanifest)
///
/// Binary manifest generated at pack time that catalogs all assets in a
/// .uzpak archive. Provides O(log n) lookup by path hash at runtime
/// without opening or scanning the pak file.
///
/// # Binary Layout (little-endian)
/// ```
/// [Header]     16 bytes
///   magic:       4 bytes   "UZMF" (0x464D5A55)
///   version:     4 bytes   uint32 (1)
///   entryCount:  4 bytes   uint32
///   flags:       4 bytes   uint32 (reserved)
///
/// [Entries]    entryCount * 32 bytes (sorted ascending by pathHash)
///   pathHash:    8 bytes   uint64 (xxHash64 of logical path)
///   size:        8 bytes   uint64 (uncompressed asset size in bytes)
///   typeId:      4 bytes   uint32 (asset type: 0=generic, 1=shader, 2=texture, ...)
///   depCount:    4 bytes   uint32 (number of dependencies)
///   depOffset:   4 bytes   uint32 (byte offset into dependency table, 0 if depCount==0)
///   reserved:    4 bytes   uint32
///
/// [Dependency Table]  variable
///   Flat array of uint64 path hashes. Each entry's dependencies are
///   a contiguous slice: [depOffset .. depOffset + depCount * 8).
/// ```
///
/// # Usage
/// ```cpp
/// // At pack time
/// AssetManifestWriter writer;
/// writer.AddEntry("shaders/pbr.uzshader", 4096, AssetType::Shader, {"common.hlsli"});
/// WriteManifestFile("build/game.uzmanifest", writer);
///
/// // At runtime startup
/// AssetManifest manifest;
/// manifest.LoadFile("game.uzmanifest");
/// auto* entry = manifest.Find("shaders/pbr.uzshader");
/// ```
///
/// # Design Rationale
/// The manifest is separate from the .uzpak so it can be loaded first
/// (single small read) to determine which assets exist and their types
/// before touching the pak data region.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

/// Magic number for .uzmanifest files: "UZMF" in little-endian.
static constexpr uint32_t kManifestMagic = 0x464D5A55;

/// Current .uzmanifest format version.
static constexpr uint32_t kManifestVersion = 1;

/// Known asset types for the manifest type field.
enum class AssetType : uint32_t {
    Generic = 0,   ///< Untyped binary blob
    Shader  = 1,   ///< Compiled .uzshader (DXIL/SPIR-V bytecode)
    Texture = 2,   ///< Compiled .uztexture (BC7/DDS)
    Mesh    = 3,   ///< Compiled .uzmesh (vertex/index data)
    Audio   = 4,   ///< Compiled .uzaudio (PCM/Vorbis)
    Script  = 5,   ///< Script file (Lua bytecode)
};

/// .uzmanifest file header — exactly 16 bytes.
#pragma pack(push, 1)
struct ManifestHeader {
    uint32_t magic = kManifestMagic;      ///< Must be kManifestMagic
    uint32_t version = kManifestVersion;  ///< Format version
    uint32_t entryCount = 0;              ///< Number of asset entries
    uint32_t flags = 0;                   ///< Reserved for future use
};
#pragma pack(pop)

static_assert(sizeof(ManifestHeader) == 16, "ManifestHeader must be exactly 16 bytes");

/// Single entry in the manifest — exactly 32 bytes.
#pragma pack(push, 1)
struct ManifestEntry {
    uint64_t pathHash = 0;       ///< xxHash64 of the logical asset path
    uint64_t size = 0;           ///< Uncompressed asset size in bytes
    uint32_t typeId = 0;         ///< Asset type (see AssetType enum)
    uint32_t depCount = 0;       ///< Number of dependencies
    uint32_t depOffset = 0;      ///< Byte offset into dependency table
    uint32_t reserved = 0;
};
#pragma pack(pop)

static_assert(sizeof(ManifestEntry) == 32, "ManifestEntry must be exactly 32 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifestWriter — generates manifests at pack time
// ─────────────────────────────────────────────────────────────────────────────

/// High-level entry data for the manifest writer.
///
/// Uses human-readable string paths (converted to xxHash64 during serialization).
/// Typically constructed from the output of an import operation.
struct ManifestWriteEntry {
    std::string path;                        ///< Logical asset path, forward-slash separated (e.g., "shaders/pbr.uzshader")
    uint64_t size = 0;                       ///< Uncompressed asset size in bytes (used for memory budgeting, not validation)
    AssetType type = AssetType::Generic;     ///< Semantic asset type — determines how the engine interprets the data
    std::vector<std::string> dependencies;   ///< Paths of assets this entry depends on (e.g., included shader headers)
};

/// Builds and serializes .uzmanifest binary data.
///
/// Entries are added incrementally, then serialized to binary.
/// The writer computes path hashes, sorts entries by hash, and
/// builds the dependency table automatically.
///
/// # Thread Safety
/// NOT thread-safe. Use from a single thread during the cook step.
class AssetManifestWriter {
public:
    /// Adds an asset entry to the manifest.
    ///
    /// # Arguments
    /// - `entry`: Asset metadata including path, size, type, and dependencies.
    ///   The path is hashed via xxHash64 for the binary format.
    void AddEntry(const ManifestWriteEntry& entry);

    /// Returns the number of entries added so far.
    [[nodiscard]] size_t EntryCount() const { return m_entries.size(); }

    /// Serializes all entries to the .uzmanifest binary format.
    ///
    /// Sorts entries by pathHash (ascending) for binary search at runtime.
    /// Builds the dependency table from all entries' dependency lists.
    ///
    /// # Returns
    /// Complete manifest bytes (header + sorted entries + dependency table).
    [[nodiscard]] std::vector<uint8_t> Serialize() const;

private:
    std::vector<ManifestWriteEntry> m_entries;
};

/// Writes a .uzmanifest file to disk.
///
/// # Arguments
/// - `path`: Output file path.
/// - `writer`: The manifest writer containing all entries.
///
/// # Returns
/// `true` on success, `false` on I/O error.
///
/// # Errors
/// Logs `Asset::Error` with the file path on failure.
bool WriteManifestFile(const std::string& path, const AssetManifestWriter& writer);

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifest — runtime manifest loader with binary search
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime-loaded asset manifest for O(log n) asset queries.
///
/// Loaded from a .uzmanifest file at startup. Entries are sorted by
/// pathHash, enabling binary search for any asset by path or hash.
///
/// # Lifecycle
/// 1. Call `Load()` or `LoadFile()` at engine startup.
/// 2. Query assets with `Find()`, `FindByHash()`, `Exists()`.
/// 3. Manifest stays in memory for the engine's lifetime.
class AssetManifest {
public:
    /// Result of a manifest query by path or hash.
    ///
    /// Contains the asset's metadata and resolved dependency path hashes.
    /// Returned by `Find()` and `FindByHash()` methods.
    struct QueryResult {
        uint64_t pathHash = 0;                 ///< xxHash64 of the asset path
        uint64_t size = 0;                     ///< Asset size in bytes
        AssetType type = AssetType::Generic;   ///< Asset type
        std::vector<uint64_t> dependencies;    ///< Dependency path hashes
    };

    /// Loads a manifest from raw bytes.
    ///
    /// Validates header, reads all entries, and builds the lookup structure.
    /// Entries must be sorted by pathHash (as produced by `AssetManifestWriter`).
    ///
    /// # Arguments
    /// - `data`: Complete .uzmanifest bytes.
    ///
    /// # Returns
    /// `true` on success. `false` if the data is too small, has wrong magic,
    /// wrong version, or inconsistent entry count.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the specific validation failure.
    [[nodiscard]] bool Load(const std::vector<uint8_t>& data);

    /// Loads a manifest from a file on disk.
    ///
    /// # Arguments
    /// - `path`: Absolute or relative path to the .uzmanifest file.
    ///
    /// # Returns
    /// `true` on success, `false` on I/O or validation error.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the file path on failure.
    [[nodiscard]] bool LoadFile(const std::string& path);

    /// Finds an asset entry by logical path string.
    ///
    /// Computes xxHash64 of the path and performs binary search.
    ///
    /// # Arguments
    /// - `path`: Logical asset path (e.g., "shaders/pbr.uzshader").
    ///
    /// # Returns
    /// Pointer to the query result, or `nullptr` if not found.
    /// The pointer is valid for the lifetime of this manifest.
    [[nodiscard]] const QueryResult* Find(const std::string& path) const;

    /// Finds an asset entry by precomputed path hash.
    ///
    /// Performs binary search on the sorted entry array.
    ///
    /// # Returns
    /// Pointer to the query result, or `nullptr` if not found.
    [[nodiscard]] const QueryResult* FindByHash(uint64_t pathHash) const;

    /// Checks if an asset exists in the manifest by logical path.
    ///
    /// Equivalent to `Find(path) != nullptr`. Does not return metadata —
    /// use `Find()` if you need size, type, or dependencies.
    ///
    /// # Arguments
    /// - `path`: Logical asset path (e.g., "textures/brick.uztexture").
    ///
    /// # Returns
    /// `true` if the manifest contains an entry matching `xxHash64(path)`.
    [[nodiscard]] bool Exists(const std::string& path) const;

    /// Returns the total number of entries in the loaded manifest.
    ///
    /// Returns 0 if no manifest has been loaded.
    [[nodiscard]] size_t EntryCount() const { return m_entries.size(); }

    /// Returns `true` if a manifest has been successfully loaded via
    /// `Load()` or `LoadFile()`.
    [[nodiscard]] bool IsLoaded() const { return m_loaded; }

    /// Returns all entries sorted by pathHash for iteration or debugging.
    ///
    /// The returned reference is valid for the lifetime of this manifest.
    /// Do not modify the vector — it is sorted for binary search.
    [[nodiscard]] const std::vector<QueryResult>& GetEntries() const { return m_entries; }

private:
    /// Entries sorted by pathHash for binary search.
    std::vector<QueryResult> m_entries;
    bool m_loaded = false;
};

} // namespace uz
