/// UzEngine Mesh Importer
///
/// Converts glTF/GLB mesh files into .uzmesh binary format. Extracts geometry
/// from the first mesh primitive, applies meshoptimizer vertex cache and fetch
/// optimizations, and packages the result into the .uzmesh layout.
///
/// # Supported Extensions
/// `.glb`, `.gltf`
///
/// # Import Pipeline
/// 1. Parse glTF with fastgltf
/// 2. Extract first mesh, first primitive (positions, normals, UVs, indices)
/// 3. Generate flat normals if missing
/// 4. Apply meshopt_optimizeVertexCache + meshopt_optimizeVertexFetch
/// 5. Write via WriteUzMesh with a single LOD and single submesh
///
/// # Options
/// No importer-specific options. All fields in `AssetImportOptions` are ignored.
///
/// # Platform
/// Cross-platform — no platform-specific dependencies.

#pragma once

#include "asset_importer.h"

#include <string>
#include <vector>

namespace uz {

/// Imports glTF/GLB mesh data into .uzmesh binary format.
///
/// Extracts the first mesh's first primitive, optimizes vertex order for
/// GPU cache efficiency, and writes the result as a single-LOD .uzmesh file.
class MeshImporter final : public IAssetImporter {
public:
    MeshImporter();

    /// Returns `"mesh"` — the unique importer name.
    [[nodiscard]] const char* Name() const override;

    /// Returns the current importer version (currently `1`).
    ///
    /// Bump when import logic or .uzmesh output format changes.
    [[nodiscard]] uint32_t Version() const override;

    /// Returns `{".glb", ".gltf"}` — the file extensions this importer handles.
    [[nodiscard]] const std::vector<std::string>& Extensions() const override;

    /// Imports a glTF/GLB file into .uzmesh binary format.
    ///
    /// # Arguments
    /// * `sourceData` - Raw bytes of the .glb or .gltf file.
    /// * `sourceName` - Filename used for format detection (.glb vs .gltf)
    ///   and error messages.
    /// * `options` - Ignored — mesh import has no configurable options.
    ///
    /// # Returns
    /// On success: `result.data` contains the complete .uzmesh binary.
    /// On failure: `result.error` describes the issue (no meshes, missing
    /// positions, parse failure, etc.).
    ///
    /// # Errors
    /// Logs `Asset::Error` with the source name and error on failure.
    /// Logs `Asset::Info` with vertex/index counts on success.
    [[nodiscard]] AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) override;

private:
    std::vector<std::string> m_extensions;
};

} // namespace uz
