#pragma once

// gltf_importer.h -- Import .glb/.gltf files into engine mesh data.
//
// Uses cgltf (header-only C library) for parsing.
// Extracts vertices (position, normal, texcoord, tangent), indices,
// submeshes, and referenced texture paths.
//
// RULE CS-7: No exceptions — returns empty model on failure

#include <cstdint>
#include <string>
#include <vector>

#include "engine/renderer/resources/mesh.h"

namespace uz {

// ---------------------------------------------------------------------------
// ImportedMaterial — PBR material extracted from a glTF file.
// ---------------------------------------------------------------------------
struct ImportedMaterial {
    std::string name;

    // PBR metallic-roughness parameters.
    float albedo[3]       = {0.8f, 0.8f, 0.8f};
    float metallic        = 0.0f;
    float roughness       = 0.5f;
    float emission[3]     = {0.0f, 0.0f, 0.0f};
    float emission_energy = 1.0f;

    // Texture paths (relative to base_dir, empty = no texture).
    std::string albedo_texture;
    std::string normal_texture;
    std::string metallic_roughness_texture;  // glTF packs M+R in one image
    std::string ao_texture;
    std::string emission_texture;
};

// ---------------------------------------------------------------------------
// ImportedModel — result of a glTF import operation.
// ---------------------------------------------------------------------------
struct ImportedModel {
    std::vector<MeshData>           meshes;
    std::vector<ImportedMaterial>    materials;         // Per-glTF-material data
    std::vector<std::string>        texture_paths;     // Referenced texture URIs
    std::string                     base_dir;          // Directory of the source file
};

/// Import a .glb or .gltf file.
/// Returns an ImportedModel with meshes and texture paths.
/// On failure, returns a model with empty meshes vector.
[[nodiscard]] ImportedModel import_gltf(const char* path);

} // namespace uz
