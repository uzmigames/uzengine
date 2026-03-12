/// UzEngine Asset Importer Interface
///
/// Pure-function interface for converting source assets into runtime-ready formats.
/// Each importer handles one category of source files (shaders, textures, meshes, etc.)
/// and produces deterministic output: same inputs always yield identical bytes.
///
/// # Contract
/// - `Import()` MUST be deterministic (no RNG, no timestamps, no side effects)
/// - `Import()` MUST NOT access the cache — caching is handled externally
/// - `Version()` MUST be bumped whenever import logic changes

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Result of an asset import operation.
///
/// On success: `success == true`, `data` contains the runtime-ready bytes,
/// and `error` is empty. On failure: `success == false`, `data` is empty,
/// and `error` contains a human-readable description of what went wrong.
struct AssetImportResult {
    bool success = false;              ///< `true` if import completed without errors
    std::vector<uint8_t> data;         ///< Imported asset bytes in runtime format (empty on failure)
    std::string error;                 ///< Human-readable error message (empty on success)
};

/// Configuration options for an import operation.
///
/// Not all fields apply to every importer. Each importer documents which
/// options it reads. Unused fields are silently ignored.
struct AssetImportOptions {
    std::string entryPoint;            ///< Shader entry function name (e.g., "main")
    std::string stage;                 ///< Shader pipeline stage: "vertex", "pixel", "compute", etc.
    std::string model;                 ///< Target shader model: "sm5_1", "sm6_0" .. "sm6_7"
    std::string format;                ///< Output bytecode format: "dxil" or "spirv"
    std::vector<std::string> defines;  ///< Preprocessor defines (e.g., "USE_PBR=1", "DEBUG")
    bool debug = false;                ///< Include debug symbols and validation info in output
};

/// Pure-function interface for converting source assets to runtime-ready format.
///
/// Implementations must be stateless and deterministic: the same source bytes
/// and options must always produce identical output bytes. This guarantees
/// that the content-addressed cache produces correct results.
///
/// # Contract
/// - `Import()` MUST be deterministic — no RNG, no timestamps, no side effects
/// - `Import()` MUST NOT access the cache or filesystem — only transform the given bytes
/// - `Version()` MUST be bumped whenever the import logic changes, to invalidate
///   all cached outputs from the previous version
///
/// # Implementing a New Importer
/// ```cpp
/// class TextureImporter final : public IAssetImporter {
/// public:
///     const char* Name() const override { return "texture"; }
///     uint32_t Version() const override { return 1; }
///     const std::vector<std::string>& Extensions() const override { return m_exts; }
///     AssetImportResult Import(const std::vector<uint8_t>& src,
///                              const std::string& name,
///                              const AssetImportOptions& opts) override { ... }
/// private:
///     std::vector<std::string> m_exts = {".png", ".jpg", ".tga"};
/// };
/// ```
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    /// Returns the unique importer name (e.g., "shader", "texture", "mesh").
    ///
    /// Used as the key in `AssetImporterRegistry` and stored in .uzimport sidecars.
    /// Must be stable across versions (changing the name invalidates all sidecars).
    [[nodiscard]] virtual const char* Name() const = 0;

    /// Returns the importer version number.
    ///
    /// Bump this whenever the import logic changes to automatically invalidate
    /// all cached outputs from previous versions. Stored in .uzimport sidecars.
    [[nodiscard]] virtual uint32_t Version() const = 0;

    /// Returns the file extensions this importer handles (e.g., {".hlsl", ".glsl"}).
    ///
    /// Extensions must include the leading dot. Each extension can only be
    /// claimed by one importer in the registry.
    [[nodiscard]] virtual const std::vector<std::string>& Extensions() const = 0;

    /// Converts source asset bytes into runtime-ready format.
    ///
    /// # Arguments
    /// * `sourceData` - Raw bytes of the source file.
    /// * `sourceName` - Original filename for error messages (e.g., "pbr_vs.hlsl").
    /// * `options` - Import configuration (entry point, stage, defines, etc.).
    ///
    /// # Returns
    /// `AssetImportResult` with `success == true` and `data` containing the
    /// compiled output, or `success == false` with `error` describing the failure.
    ///
    /// # Contract
    /// - MUST be deterministic: same `sourceData` + `options` → identical `data`
    /// - MUST NOT access the filesystem or cache
    /// - MUST NOT produce side effects (logging for diagnostics is acceptable)
    [[nodiscard]] virtual AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) = 0;
};

} // namespace uz
