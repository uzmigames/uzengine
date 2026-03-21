/// UzEngine FBX Importer
///
/// Converts FBX mesh files into .uzmesh binary format. Extracts geometry,
/// materials, and submesh information from FBX meshes using the ufbx library.
///
/// # Supported Extensions
/// `.fbx`
///
/// # Import Pipeline
/// 1. Parse FBX with ufbx via `ufbx_load_memory()`
/// 2. Extract the first mesh found in `scene->meshes`
/// 3. For each face: triangulate via `ufbx_triangulate_face()`
/// 4. Extract positions from `mesh->vertex_position`
/// 5. Extract normals from `mesh->vertex_normal` (generate flat normals if missing)
/// 6. Extract UVs from `mesh->vertex_uv` (default to 0,0 if missing)
/// 7. Generate tangents via UV-space edge algorithm
/// 8. Apply meshopt_optimizeVertexCache + meshopt_optimizeVertexFetch
/// 9. Extract PBR material data from `mesh->materials[]`
/// 10. Write via WriteUzMesh with a single LOD and one submesh per material
/// 11. Generate SDF sidecar (.uzsdf) for distance field shadows
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

/// Imports FBX mesh data into .uzmesh binary format.
///
/// Extracts geometry from the first mesh in the FBX scene, generates
/// tangents when missing, extracts PBR materials, optimizes vertex order
/// for GPU cache efficiency, and writes a single-LOD .uzmesh file.
/// Extracted materials are returned in `AssetImportResult::materials`.
class FbxImporter final : public IAssetImporter {
public:
    FbxImporter();

    /// Returns `"fbx"` — the unique importer name.
    [[nodiscard]] const char* Name() const override;

    /// Returns the current importer version (currently `1`).
    ///
    /// Bump when import logic or .uzmesh output format changes.
    /// v1: single-mesh extraction, tangent generation, material extraction.
    [[nodiscard]] uint32_t Version() const override;

    /// Returns `{".fbx"}` — the file extensions this importer handles.
    [[nodiscard]] const std::vector<std::string>& Extensions() const override;

    /// Imports an FBX file into .uzmesh binary format.
    ///
    /// Extracts geometry from the first mesh in the scene. Faces are
    /// triangulated via `ufbx_triangulate_face()`. Normals are generated
    /// as flat (faceted) normals if not present in the FBX. UVs default
    /// to (0,0) when absent. Tangents are always generated from UV-space
    /// edge vectors. PBR materials are extracted from the mesh's material
    /// assignments into `result.materials`.
    ///
    /// # Arguments
    /// * `sourceData` - Raw bytes of the .fbx file.
    /// * `sourceName` - Filename used for error messages.
    /// * `options` - Ignored — FBX import has no configurable options.
    ///
    /// # Returns
    /// On success: `result.data` contains the complete .uzmesh binary,
    /// `result.materials` contains extracted PBR materials.
    /// On failure: `result.error` describes the issue (no meshes, parse
    /// failure, etc.).
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
