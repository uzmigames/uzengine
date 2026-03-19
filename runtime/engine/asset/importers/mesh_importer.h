/// UzEngine Mesh Importer
///
/// Converts glTF/GLB mesh files into .uzmesh binary format. Extracts geometry,
/// materials, and submesh information from glTF meshes.
///
/// # Supported Extensions
/// `.glb`, `.gltf`
///
/// # Import Pipeline
/// 1. Parse glTF with fastgltf
/// 2. Extract ALL primitives from the first mesh (multi-submesh support)
/// 3. For each primitive: extract positions, normals, UVs, tangents, indices
/// 4. Generate flat normals if missing
/// 5. Generate tangents via UV-space edge algorithm if missing
/// 6. Apply meshopt_optimizeVertexCache + meshopt_optimizeVertexFetch
/// 7. Extract PBR metallic-roughness material data per primitive
/// 8. Write via WriteUzMesh with a single LOD and N submeshes
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
/// Extracts all primitives from the first mesh as separate submeshes,
/// generates tangents when missing, extracts PBR materials, optimizes
/// vertex order for GPU cache efficiency, and writes a single-LOD .uzmesh file.
/// Extracted materials are returned in `AssetImportResult::materials`.
class MeshImporter final : public IAssetImporter {
public:
    MeshImporter();

    /// Returns `"mesh"` — the unique importer name.
    [[nodiscard]] const char* Name() const override;

    /// Returns the current importer version (currently `2`).
    ///
    /// Bump when import logic or .uzmesh output format changes.
    /// v2: multi-submesh, tangent generation, material extraction.
    [[nodiscard]] uint32_t Version() const override;

    /// Returns `{".glb", ".gltf"}` — the file extensions this importer handles.
    [[nodiscard]] const std::vector<std::string>& Extensions() const override;

    /// Imports a glTF/GLB file into .uzmesh binary format.
    ///
    /// Extracts all primitives from the first mesh. Each primitive becomes a
    /// separate submesh with its own index range and material index. Tangents
    /// are generated from UV-space edge vectors when not present in the glTF.
    /// PBR metallic-roughness materials are extracted into `result.materials`.
    ///
    /// # Arguments
    /// * `sourceData` - Raw bytes of the .glb or .gltf file.
    /// * `sourceName` - Filename used for format detection (.glb vs .gltf)
    ///   and error messages.
    /// * `options` - Ignored — mesh import has no configurable options.
    ///
    /// # Returns
    /// On success: `result.data` contains the complete .uzmesh binary,
    /// `result.materials` contains extracted PBR materials.
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
