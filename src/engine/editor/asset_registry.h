#pragma once

// asset_registry.h -- Asset discovery and import tracking for the editor.
//
// The AssetRegistry scans a project directory tree, discovers asset files by
// extension, and maintains import metadata (.import.json sidecar files).
//
// Supported asset types:
//   .glb/.gltf     -> Mesh
//   .png/.jpg/.ktx2 -> Texture
//   .wav/.ogg      -> Audio
//   .uzscene       -> Scene
//   .uzmat         -> Material
//   .hlsl/.glsl    -> Shader
//
// The registry can integrate with FileWatcher for auto-rescan on changes.

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class FileWatcher;

// ---------------------------------------------------------------------------
// Asset type enumeration — classified by file extension during scan.
// ---------------------------------------------------------------------------
enum class AssetType : uint8_t {
    Mesh     = 0,
    Texture  = 1,
    Audio    = 2,
    Scene    = 3,
    Material = 4,
    Shader   = 5,
    Unknown  = 6,
};

/// Convert AssetType to a human-readable string (e.g. "Mesh", "Texture").
[[nodiscard]] const char* asset_type_to_string(AssetType type) noexcept;

/// Parse a string back to AssetType. Returns Unknown on failure.
[[nodiscard]] AssetType asset_type_from_string(const char* str) noexcept;

/// Classify a file path by its extension. Returns Unknown for unrecognized.
[[nodiscard]] AssetType classify_asset(const std::string& path) noexcept;

// ---------------------------------------------------------------------------
// Import status — tracks the state of an asset's import sidecar.
// ---------------------------------------------------------------------------
enum class ImportStatus : uint8_t {
    Fresh       = 0,   // .import.json exists and hash matches source
    Stale       = 1,   // .import.json exists but hash differs (source changed)
    Error       = 2,   // import was attempted but failed
    NotImported = 3,   // no .import.json exists yet
};

/// Convert ImportStatus to string.
[[nodiscard]] const char* import_status_to_string(ImportStatus status) noexcept;

// ---------------------------------------------------------------------------
// AssetEntry — one discovered asset in the project.
// ---------------------------------------------------------------------------
struct AssetEntry {
    std::string  path;       // Relative path from project root (e.g. "textures/grass.png")
    AssetType    type    = AssetType::Unknown;
    ImportStatus status  = ImportStatus::NotImported;
    uint64_t     hash    = 0;        // Lightweight hash (file size + mtime)
    std::string  error_msg;          // Non-empty when status == Error
};

// ---------------------------------------------------------------------------
// AssetRegistry — scans, tracks, and imports project assets.
// ---------------------------------------------------------------------------
class AssetRegistry {
public:
    AssetRegistry();
    ~AssetRegistry();

    // Non-copyable.
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    /// Scan the given project root directory recursively, discovering all
    /// supported asset files. Clears any previous entries first.
    /// Returns the number of discovered assets.
    uint32_t scan(const std::string& project_root);

    /// Returns a const reference to all discovered entries.
    [[nodiscard]] const std::vector<AssetEntry>& entries() const noexcept;

    /// Look up an entry by its relative path. Returns nullptr if not found.
    [[nodiscard]] AssetEntry* get(const std::string& path) noexcept;
    [[nodiscard]] const AssetEntry* get(const std::string& path) const noexcept;

    /// Import a single asset: compute hash, write .import.json sidecar.
    /// Returns true on success. On failure, the entry's status is set to Error.
    /// If the asset is already Fresh (hash unchanged), returns true (no-op).
    bool import_asset(const std::string& path);

    /// Read the .import.json sidecar for the given asset path.
    /// Returns the JSON content as a string, or empty string if not found.
    [[nodiscard]] std::string read_import_json(const std::string& path) const;

    /// Register with a FileWatcher for automatic rescan on file changes.
    /// The watcher must outlive the registry (or be stopped first).
    void watch(FileWatcher& watcher);

    /// Get the project root that was last scanned.
    [[nodiscard]] const std::string& project_root() const noexcept;

    /// Remove all entries.
    void clear() noexcept;

private:
    std::string              project_root_;
    std::vector<AssetEntry>  entries_;

    /// Recursively walk a directory and add discovered assets.
    void scan_directory(const std::string& dir, const std::string& relative_prefix);

    /// Compute a lightweight hash for the given absolute file path.
    /// Uses file size combined with modification time.
    [[nodiscard]] uint64_t compute_hash(const std::string& abs_path) const noexcept;

    /// Build the .import.json sidecar path for a given relative asset path.
    [[nodiscard]] std::string import_json_path(const std::string& rel_path) const;

    /// Read existing .import.json and update the entry's status accordingly.
    void refresh_entry_status(AssetEntry& entry);
};

} // namespace uz
