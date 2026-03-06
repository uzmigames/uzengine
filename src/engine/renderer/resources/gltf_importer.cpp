// ---------------------------------------------------------------------------
// gltf_importer.cpp -- Import glTF/glb files via cgltf
//
// Extracts mesh geometry (position, normal, texcoord, tangent) and
// material texture paths from glTF 2.0 files.
//
// RULE CS-7:  No exceptions
// RULE CS-21: Under 1500 lines
// ---------------------------------------------------------------------------

#include "gltf_importer.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstring>
#include <algorithm>
#include <cmath>

#include "engine/core/diag.h"

namespace uz {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool read_accessor_float(const cgltf_accessor* acc, size_t index,
                                 float* out, size_t component_count) {
    if (!acc || index >= acc->count) return false;
    return cgltf_accessor_read_float(acc, index, out, component_count);
}

static uint32_t read_accessor_uint(const cgltf_accessor* acc, size_t index) {
    if (!acc || index >= acc->count) return 0;
    cgltf_uint val = 0;
    cgltf_accessor_read_uint(acc, index, &val, 1);
    return static_cast<uint32_t>(val);
}

// Find an accessor for a given attribute type in a primitive.
static const cgltf_accessor* find_attribute(const cgltf_primitive& prim,
                                             cgltf_attribute_type type,
                                             int index = 0) {
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type == type &&
            prim.attributes[i].index == index) {
            return prim.attributes[i].data;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Import a single primitive into a MeshData
// ---------------------------------------------------------------------------

static bool import_primitive(const cgltf_primitive& prim,
                              MeshData& out_mesh,
                              uint32_t material_index) {
    const cgltf_accessor* pos_acc  = find_attribute(prim, cgltf_attribute_type_position);
    const cgltf_accessor* norm_acc = find_attribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uv_acc   = find_attribute(prim, cgltf_attribute_type_texcoord);
    const cgltf_accessor* tan_acc  = find_attribute(prim, cgltf_attribute_type_tangent);

    if (!pos_acc) {
        DIAG("gltf: primitive has no POSITION attribute, skipping");
        return false;
    }

    const uint32_t base_vertex = static_cast<uint32_t>(out_mesh.vertices.size());
    const uint32_t base_index  = static_cast<uint32_t>(out_mesh.indices.size());
    const size_t vertex_count  = pos_acc->count;

    // Read vertices.
    out_mesh.vertices.reserve(out_mesh.vertices.size() + vertex_count);
    for (size_t v = 0; v < vertex_count; ++v) {
        MeshVertex vert{};

        // Position (required).
        float pos[3]{};
        read_accessor_float(pos_acc, v, pos, 3);
        vert.position = Float3{pos[0], pos[1], pos[2]};

        // Update bounds.
        out_mesh.bounds.merge(Vec3{pos[0], pos[1], pos[2]});

        // Normal (optional).
        if (norm_acc) {
            float n[3]{};
            read_accessor_float(norm_acc, v, n, 3);
            vert.normal = Float3{n[0], n[1], n[2]};
        } else {
            vert.normal = Float3{0.0f, 1.0f, 0.0f};
        }

        // Texcoord (optional).
        if (uv_acc) {
            read_accessor_float(uv_acc, v, vert.texcoord, 2);
        }

        // Tangent (optional).
        if (tan_acc) {
            read_accessor_float(tan_acc, v, vert.tangent, 4);
        } else {
            vert.tangent[0] = 1.0f;
            vert.tangent[3] = 1.0f;
        }

        out_mesh.vertices.push_back(vert);
    }

    // Read indices.
    if (prim.indices) {
        const size_t idx_count = prim.indices->count;
        out_mesh.indices.reserve(out_mesh.indices.size() + idx_count);
        for (size_t i = 0; i < idx_count; ++i) {
            out_mesh.indices.push_back(
                base_vertex + read_accessor_uint(prim.indices, i));
        }

        // Add submesh entry.
        SubMesh sub;
        sub.index_offset   = base_index;
        sub.index_count    = static_cast<uint32_t>(idx_count);
        sub.material_index = material_index;
        out_mesh.submeshes.push_back(sub);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Main import function
// ---------------------------------------------------------------------------

ImportedModel import_gltf(const char* path) {
    ImportedModel model;

    if (!path) {
        DIAG("import_gltf: null path");
        return model;
    }

    // Extract base directory for resolving relative texture paths.
    std::string path_str(path);
    size_t last_sep = path_str.find_last_of("/\\");
    model.base_dir = (last_sep != std::string::npos)
                         ? path_str.substr(0, last_sep + 1)
                         : "./";

    // Parse the glTF file.
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        DIAG_FMT("import_gltf: parse failed for '%s' (result=%d)", path, result);
        return model;
    }

    // Load external buffers (for .gltf files with separate .bin).
    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        DIAG_FMT("import_gltf: load_buffers failed for '%s'", path);
        cgltf_free(data);
        return model;
    }

    DIAG_FMT("import_gltf: '%s' — %zu meshes, %zu materials, %zu textures",
             path, data->meshes_count, data->materials_count,
             data->textures_count);

    // Extract texture URI paths.
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        if (data->images[i].uri) {
            model.texture_paths.emplace_back(data->images[i].uri);
        }
    }

    // Import meshes.
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& gltf_mesh = data->meshes[m];
        MeshData mesh_data;

        for (cgltf_size p = 0; p < gltf_mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = gltf_mesh.primitives[p];

            // Only import triangle primitives.
            if (prim.type != cgltf_primitive_type_triangles) {
                DIAG_FMT("gltf: skipping non-triangle primitive (type=%d)",
                         prim.type);
                continue;
            }

            // Determine material index.
            uint32_t mat_idx = 0;
            if (prim.material) {
                mat_idx = static_cast<uint32_t>(prim.material - data->materials);
            }

            import_primitive(prim, mesh_data, mat_idx);
        }

        if (!mesh_data.vertices.empty()) {
            DIAG_FMT("  mesh[%zu]: %zu verts, %zu indices, %zu submeshes",
                     m, mesh_data.vertices.size(), mesh_data.indices.size(),
                     mesh_data.submeshes.size());
            model.meshes.push_back(std::move(mesh_data));
        }
    }

    cgltf_free(data);

    DIAG_FMT("import_gltf: imported %zu meshes, %zu textures from '%s'",
             model.meshes.size(), model.texture_paths.size(), path);
    return model;
}

} // namespace uz
