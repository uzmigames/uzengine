/// UzEngine FBX Importer — Implementation
///
/// Parses FBX via ufbx, extracts geometry from the first mesh, generates
/// tangents when missing, extracts PBR materials, applies meshoptimizer
/// cache/fetch optimizations, and writes .uzmesh format.

#include "fbx_importer.h"
#include "log.h"
#include "mesh.h"
#include "profiler.h"
#include "sdf_baker.h"
#include "uzmesh_format.h"
#include "uzsdf_format.h"

#include <ufbx.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace uz {

namespace {

/// Generates flat (faceted) normals for a triangle mesh in-place.
///
/// For each triangle, computes the face normal from the cross product of
/// two edges and assigns it to all three vertices. Assumes the vertex array
/// is already populated with positions.
void GenerateFlatNormals(std::vector<VertexPosNormUV>& vertices, const std::vector<uint32_t>& indices) {
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto& v0 = vertices[indices[i]];
        auto& v1 = vertices[indices[i + 1]];
        auto& v2 = vertices[indices[i + 2]];

        // Edge vectors
        const float e1x = v1.position[0] - v0.position[0];
        const float e1y = v1.position[1] - v0.position[1];
        const float e1z = v1.position[2] - v0.position[2];

        const float e2x = v2.position[0] - v0.position[0];
        const float e2y = v2.position[1] - v0.position[1];
        const float e2z = v2.position[2] - v0.position[2];

        // Cross product
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;

        // Normalize
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }

        // Assign to all three vertices
        v0.normal[0] = nx; v0.normal[1] = ny; v0.normal[2] = nz;
        v1.normal[0] = nx; v1.normal[1] = ny; v1.normal[2] = nz;
        v2.normal[0] = nx; v2.normal[1] = ny; v2.normal[2] = nz;
    }
}

/// Generates tangent vectors for VertexPosNormUV data using UV-space edge
/// vectors, following the standard Lengyel algorithm. Tangent/bitangent are
/// accumulated per-vertex, then orthogonalized via Gram-Schmidt.
///
/// Output is 4 floats per vertex: tangent xyz + bitangent sign (w).
void GenerateTangentsForPosNormUV(
    const std::vector<VertexPosNormUV>& vertices,
    const std::vector<uint32_t>& indices,
    std::vector<float>& tangentsOut) {

    const size_t vertexCount = vertices.size();
    // 4 floats per vertex: tangent xyz + handedness w
    tangentsOut.resize(vertexCount * 4, 0.f);

    if (vertexCount == 0 || indices.size() < 3) {
        // Set default tangent (+X, w=1) for all vertices
        for (size_t v = 0; v < vertexCount; ++v) {
            tangentsOut[v * 4 + 0] = 1.f;
            tangentsOut[v * 4 + 3] = 1.f;
        }
        return;
    }

    // Accumulation buffers for tangent and bitangent
    std::vector<float> tanAccum(vertexCount * 3, 0.f);
    std::vector<float> bitanAccum(vertexCount * 3, 0.f);

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];

        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue;
        }

        const auto& p0 = vertices[i0].position;
        const auto& p1 = vertices[i1].position;
        const auto& p2 = vertices[i2].position;

        const auto& uv0 = vertices[i0].uv;
        const auto& uv1 = vertices[i1].uv;
        const auto& uv2 = vertices[i2].uv;

        const float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
        const float e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];

        const float du1 = uv1[0] - uv0[0], dv1 = uv1[1] - uv0[1];
        const float du2 = uv2[0] - uv0[0], dv2 = uv2[1] - uv0[1];

        const float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-12f) {
            continue;
        }
        const float invDet = 1.0f / det;

        const float tx = (e1x * dv2 - e2x * dv1) * invDet;
        const float ty = (e1y * dv2 - e2y * dv1) * invDet;
        const float tz = (e1z * dv2 - e2z * dv1) * invDet;

        const float bx = (e2x * du1 - e1x * du2) * invDet;
        const float by = (e2y * du1 - e1y * du2) * invDet;
        const float bz = (e2z * du1 - e1z * du2) * invDet;

        for (uint32_t idx : {i0, i1, i2}) {
            tanAccum[idx * 3 + 0] += tx;
            tanAccum[idx * 3 + 1] += ty;
            tanAccum[idx * 3 + 2] += tz;
            bitanAccum[idx * 3 + 0] += bx;
            bitanAccum[idx * 3 + 1] += by;
            bitanAccum[idx * 3 + 2] += bz;
        }
    }

    // Orthogonalize, normalize, compute bitangent sign
    for (size_t v = 0; v < vertexCount; ++v) {
        const float nx = vertices[v].normal[0];
        const float ny = vertices[v].normal[1];
        const float nz = vertices[v].normal[2];

        float tx = tanAccum[v * 3 + 0];
        float ty = tanAccum[v * 3 + 1];
        float tz = tanAccum[v * 3 + 2];

        // Gram-Schmidt: T' = T - N * dot(N, T)
        const float nDotT = nx * tx + ny * ty + nz * tz;
        tx -= nx * nDotT;
        ty -= ny * nDotT;
        tz -= nz * nDotT;

        float len = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (len > 1e-8f) {
            const float inv = 1.0f / len;
            tx *= inv;
            ty *= inv;
            tz *= inv;
        } else {
            // Fallback tangent perpendicular to normal
            const float absX = std::abs(nx);
            const float absY = std::abs(ny);
            const float absZ = std::abs(nz);

            float ux = 0.f, uy = 0.f, uz = 0.f;
            if (absX <= absY && absX <= absZ) {
                ux = 1.f;
            } else if (absY <= absZ) {
                uy = 1.f;
            } else {
                uz = 1.f;
            }

            const float d = nx * ux + ny * uy + nz * uz;
            tx = ux - nx * d;
            ty = uy - ny * d;
            tz = uz - nz * d;

            len = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (len > 1e-8f) {
                const float inv = 1.0f / len;
                tx *= inv;
                ty *= inv;
                tz *= inv;
            } else {
                tx = 1.f; ty = 0.f; tz = 0.f;
            }
        }

        // Bitangent sign: w = sign(dot(cross(N, T), B))
        const float cx = ny * tz - nz * ty;
        const float cy = nz * tx - nx * tz;
        const float cz = nx * ty - ny * tx;

        const float bx = bitanAccum[v * 3 + 0];
        const float by = bitanAccum[v * 3 + 1];
        const float bz = bitanAccum[v * 3 + 2];

        const float sign = cx * bx + cy * by + cz * bz;
        const float w = (sign < 0.0f) ? -1.0f : 1.0f;

        tangentsOut[v * 4 + 0] = tx;
        tangentsOut[v * 4 + 1] = ty;
        tangentsOut[v * 4 + 2] = tz;
        tangentsOut[v * 4 + 3] = w;
    }
}

/// Resolves the best available filename string from a ufbx texture.
///
/// Prefers `relative_filename` (shorter, more portable) over `filename`
/// (which may be an absolute path from the DCC tool). Returns empty string
/// if the texture is null or has no filename.
[[nodiscard]] std::string ResolveUfbxTexturePath(const ufbx_texture* texture) {
    if (!texture) {
        return {};
    }

    // Prefer relative filename — it's more portable across machines
    if (texture->relative_filename.length > 0) {
        return std::string(texture->relative_filename.data, texture->relative_filename.length);
    }

    // Fall back to absolute filename from the DCC tool
    if (texture->filename.length > 0) {
        return std::string(texture->filename.data, texture->filename.length);
    }

    return {};
}

/// Extracts PBR material data from a ufbx material.
///
/// Reads base color, normal map, metallic, and roughness from the material's
/// PBR model. Texture paths are resolved via `ResolveUfbxTexturePath()`.
[[nodiscard]] ImportedMaterial ExtractFbxMaterial(const ufbx_material* material) {
    ImportedMaterial imported;

    if (!material) {
        imported.name = "default";
        return imported;
    }

    imported.name = std::string(material->name.data, material->name.length);

    // Base color factor (RGBA)
    if (material->pbr.base_color.has_value) {
        const ufbx_vec4& bc = material->pbr.base_color.value_vec4;
        imported.baseColorR = static_cast<float>(bc.x);
        imported.baseColorG = static_cast<float>(bc.y);
        imported.baseColorB = static_cast<float>(bc.z);
        imported.baseColorA = static_cast<float>(bc.w);
    }

    // Metallic factor
    if (material->pbr.metalness.has_value) {
        imported.metallicFactor = static_cast<float>(material->pbr.metalness.value_real);
    }

    // Roughness factor
    if (material->pbr.roughness.has_value) {
        imported.roughnessFactor = static_cast<float>(material->pbr.roughness.value_real);
    }

    // Base color texture
    imported.baseColorTexturePath = ResolveUfbxTexturePath(material->pbr.base_color.texture);

    // Normal map texture
    imported.normalTexturePath = ResolveUfbxTexturePath(material->pbr.normal_map.texture);

    // Metallic-roughness texture — try roughness first, then metalness
    imported.metallicRoughnessTexturePath = ResolveUfbxTexturePath(material->pbr.roughness.texture);
    if (imported.metallicRoughnessTexturePath.empty()) {
        imported.metallicRoughnessTexturePath = ResolveUfbxTexturePath(material->pbr.metalness.texture);
    }

    return imported;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// FbxImporter
// ─────────────────────────────────────────────────────────────────────────────

FbxImporter::FbxImporter()
    : m_extensions({".fbx"}) {}

const char* FbxImporter::Name() const {
    return "fbx";
}

uint32_t FbxImporter::Version() const {
    return 1;
}

const std::vector<std::string>& FbxImporter::Extensions() const {
    return m_extensions;
}

AssetImportResult FbxImporter::Import(
    const std::vector<uint8_t>& sourceData,
    const std::string& sourceName,
    const AssetImportOptions& /*options*/) {
    UZ_PROFILE_FUNCTION();

    AssetImportResult result;

    if (sourceData.empty()) {
        result.error = "Empty source data";
        UZ_LOG(Asset, Error, "FBX import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // ── Parse FBX via ufbx ──
    // Convert to UzEngine coordinate system: Z-up, left-handed, centimeters.
    // MODIFY_GEOMETRY bakes the transform into vertex data directly.
    ufbx_load_opts loadOpts = {};
    loadOpts.target_axes = ufbx_axes_left_handed_z_up;
    loadOpts.target_unit_meters = 0.01f; // centimeters
    loadOpts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    loadOpts.generate_missing_normals = true;

    ufbx_error fbxError = {};
    ufbx_scene* scene = ufbx_load_memory(
        sourceData.data(),
        sourceData.size(),
        &loadOpts,
        &fbxError);

    if (!scene) {
        result.error = "Failed to parse FBX: " +
                       std::string(fbxError.description.data, fbxError.description.length);
        UZ_LOG(Asset, Error, "FBX import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // RAII guard — ensure scene is freed on all exit paths
    struct SceneGuard {
        ufbx_scene* s;
        ~SceneGuard() { if (s) ufbx_free_scene(s); }
    } sceneGuard{scene};

    if (scene->meshes.count == 0) {
        result.error = "No meshes found in FBX file";
        UZ_LOG(Asset, Error, "FBX import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    const ufbx_mesh* mesh = scene->meshes.data[0];
    if (!mesh || mesh->num_faces == 0) {
        result.error = "First mesh has no faces";
        UZ_LOG(Asset, Error, "FBX import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    const bool hasNormals = mesh->vertex_normal.exists;
    const bool hasUVs = mesh->vertex_uv.exists;

    // ── Determine material count for submesh splitting ──
    // FBX meshes can have per-face material assignments. We group faces by
    // material index and create one submesh per material.
    const size_t materialCount = mesh->materials.count;

    // Extract materials from the mesh's material list
    std::vector<size_t> materialFaceCount(materialCount > 0 ? materialCount : 1, 0);

    for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
        size_t matIdx = 0;
        if (materialCount > 0 && mesh->face_material.data) {
            matIdx = static_cast<size_t>(mesh->face_material.data[fi]);
            if (matIdx >= materialCount) {
                matIdx = 0;
            }
        }
        materialFaceCount[matIdx]++;
    }

    // Extract material data
    for (size_t mi = 0; mi < materialCount; ++mi) {
        result.materials.push_back(ExtractFbxMaterial(mesh->materials.data[mi]));
    }

    // If no materials, add a default material
    if (materialCount == 0) {
        ImportedMaterial defaultMat;
        defaultMat.name = "default";
        result.materials.push_back(std::move(defaultMat));
    }

    // ── Triangulate and extract geometry, grouped by material ──
    // We process faces per-material so each material group becomes a submesh
    // with contiguous index ranges in the final buffer.

    std::vector<VertexPosNormUV> allVertices;
    std::vector<uint32_t> allIndices;
    std::vector<UzMeshSubmesh> submeshes;

    // Temporary buffer for triangulation output (max polygon size in ufbx)
    // ufbx_triangulate_face needs (num_indices - 2) * 3 uint32s for output.
    // We allocate generously for large n-gons.
    std::vector<uint32_t> triIndices(256);

    const size_t numMaterialGroups = materialCount > 0 ? materialCount : 1;

    for (size_t matGroup = 0; matGroup < numMaterialGroups; ++matGroup) {
        if (materialFaceCount[matGroup] == 0) {
            continue;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(allVertices.size());
        const uint32_t baseIndex = static_cast<uint32_t>(allIndices.size());

        // Per-material-group vertices — we build a unique vertex for each
        // triangulated index to ensure correct per-face-corner attributes,
        // then rely on meshopt to weld/optimize later.
        std::vector<VertexPosNormUV> groupVertices;
        std::vector<uint32_t> groupIndices;

        for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
            // Filter by material
            size_t faceMat = 0;
            if (materialCount > 0 && mesh->face_material.data) {
                faceMat = static_cast<size_t>(mesh->face_material.data[fi]);
                if (faceMat >= materialCount) {
                    faceMat = 0;
                }
            }
            if (faceMat != matGroup) {
                continue;
            }

            const ufbx_face& face = mesh->faces.data[fi];

            // Ensure triangulation buffer is large enough
            const size_t maxTriIndices = static_cast<size_t>((face.num_indices - 2)) * 3;
            if (triIndices.size() < maxTriIndices) {
                triIndices.resize(maxTriIndices);
            }

            // Triangulate the face — returns number of TRIANGLES (not indices).
            // Each triangle has 3 corner indices in the output buffer.
            const uint32_t numTriangles = ufbx_triangulate_face(
                triIndices.data(),
                triIndices.size(),
                mesh,
                face);
            const uint32_t numTriangulated = numTriangles * 3;

            // Each triangulated index is a face-corner index into the mesh's
            // vertex attribute arrays. We create a unique vertex per corner.
            for (uint32_t ti = 0; ti < numTriangulated; ++ti) {
                const uint32_t cornerIdx = triIndices[ti];

                VertexPosNormUV vert = {};

                // Position — always present
                const ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, cornerIdx);
                vert.position[0] = static_cast<float>(pos.x);
                vert.position[1] = static_cast<float>(pos.y);
                vert.position[2] = static_cast<float>(pos.z);

                // Normal — may not exist
                if (hasNormals) {
                    const ufbx_vec3 norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, cornerIdx);
                    vert.normal[0] = static_cast<float>(norm.x);
                    vert.normal[1] = static_cast<float>(norm.y);
                    vert.normal[2] = static_cast<float>(norm.z);
                }
                // else: normals will be generated as flat normals after all faces

                // UV — may not exist, default to (0, 0)
                // FBX UV: V=0 at bottom. stb_image: origin at top-left. Flip V.
                if (hasUVs) {
                    const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIdx);
                    vert.uv[0] = static_cast<float>(uv.x);
                    vert.uv[1] = 1.0f - static_cast<float>(uv.y);
                }

                const uint32_t vertIdx = static_cast<uint32_t>(groupVertices.size());
                groupVertices.push_back(vert);
                groupIndices.push_back(vertIdx);
            }
        }

        if (groupVertices.empty()) {
            continue;
        }

        // Reverse triangle winding from CW to CCW.
        // ufbx MODIFY_GEOMETRY converts positions to left-handed Z-up but does
        // not flip triangle winding, leaving faces CW. Our rasterizer uses
        // frontCounterClockwise = true (CCW = front), so we must reverse.
        for (size_t t = 0; t + 2 < groupIndices.size(); t += 3) {
            std::swap(groupIndices[t + 1], groupIndices[t + 2]);
        }

        // Generate flat normals if the FBX had no normal layer
        if (!hasNormals) {
            GenerateFlatNormals(groupVertices, groupIndices);
        }

        // Append to global arrays
        for (const uint32_t idx : groupIndices) {
            allIndices.push_back(baseVertex + idx);
        }
        allVertices.insert(allVertices.end(), groupVertices.begin(), groupVertices.end());

        // Record submesh
        UzMeshSubmesh submesh;
        submesh.vertexOffset = baseVertex;
        submesh.indexOffset = baseIndex;
        submesh.indexCount = static_cast<uint32_t>(groupIndices.size());
        submesh.materialIndex = static_cast<uint32_t>(matGroup);
        submeshes.push_back(submesh);

        UZ_LOG(Asset, Info,
               "FBX material group {} in '{}': {} vertices, {} indices",
               matGroup, sourceName, groupVertices.size(), groupIndices.size());
    }

    if (allVertices.empty()) {
        result.error = "No valid geometry extracted from FBX mesh";
        UZ_LOG(Asset, Error, "FBX import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // ── Generate tangents ──
    std::vector<float> allTangents;
    GenerateTangentsForPosNormUV(allVertices, allIndices, allTangents);

    // ── Meshopt: optimize vertex cache ordering ──
    std::vector<uint32_t> optimizedIndices(allIndices.size());
    meshopt_optimizeVertexCache(
        optimizedIndices.data(), allIndices.data(),
        allIndices.size(), allVertices.size());

    // ── Meshopt: optimize vertex fetch (reorders vertices for cache locality) ──
    std::vector<VertexPosNormUV> optimizedVertices(allVertices.size());
    const size_t finalVertexCount = meshopt_optimizeVertexFetch(
        optimizedVertices.data(), optimizedIndices.data(),
        optimizedIndices.size(), allVertices.data(),
        allVertices.size(), sizeof(VertexPosNormUV));

    optimizedVertices.resize(finalVertexCount);

    // Remap tangent data to match the new vertex order using the explicit
    // remap table from meshopt.
    std::vector<uint32_t> remap(allVertices.size());
    meshopt_optimizeVertexFetchRemap(
        remap.data(), optimizedIndices.data(),
        optimizedIndices.size(), allVertices.size());

    std::vector<float> remappedTangents(finalVertexCount * 4, 0.f);
    for (size_t oldIdx = 0; oldIdx < allVertices.size(); ++oldIdx) {
        const uint32_t newIdx = remap[oldIdx];
        if (newIdx < finalVertexCount) {
            remappedTangents[newIdx * 4 + 0] = allTangents[oldIdx * 4 + 0];
            remappedTangents[newIdx * 4 + 1] = allTangents[oldIdx * 4 + 1];
            remappedTangents[newIdx * 4 + 2] = allTangents[oldIdx * 4 + 2];
            remappedTangents[newIdx * 4 + 3] = allTangents[oldIdx * 4 + 3];
        }
    }

    // ── Convert to runtime Vertex format (48 bytes with tangents) ──
    // The extraction pipeline uses VertexPosNormUV (32 bytes) for meshopt
    // compatibility. Now combine with remapped tangent data into the runtime
    // Vertex struct that the GPU's StructuredBuffer expects.
    std::vector<Vertex> finalVertices(finalVertexCount);
    Vec3 boundsMin{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    Vec3 boundsMax{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};

    for (size_t i = 0; i < finalVertexCount; ++i) {
        const auto& src = optimizedVertices[i];
        auto& dst = finalVertices[i];

        dst.position = {src.position[0], src.position[1], src.position[2]};
        dst.normal   = {src.normal[0],   src.normal[1],   src.normal[2]};
        dst.texcoord = {src.uv[0],       src.uv[1]};
        dst.tangent  = {
            remappedTangents[i * 4 + 0],
            remappedTangents[i * 4 + 1],
            remappedTangents[i * 4 + 2],
            remappedTangents[i * 4 + 3]};

        // Track AABB
        if (dst.position.x < boundsMin.x) boundsMin.x = dst.position.x;
        if (dst.position.y < boundsMin.y) boundsMin.y = dst.position.y;
        if (dst.position.z < boundsMin.z) boundsMin.z = dst.position.z;
        if (dst.position.x > boundsMax.x) boundsMax.x = dst.position.x;
        if (dst.position.y > boundsMax.y) boundsMax.y = dst.position.y;
        if (dst.position.z > boundsMax.z) boundsMax.z = dst.position.z;
    }

    // ── Auto-generate LODs (additive — does NOT touch the existing pipeline) ──
    // Uses meshopt_simplify on the already-optimized vertices+indices.
    // LOD indices reference the same vertex buffer as LOD0.
    struct LODIndices { std::vector<uint32_t> indices; };
    std::vector<LODIndices> extraLODs; // LOD1, LOD2, LOD3

    {
        // meshopt_simplify requires welded vertices (shared positions) to
        // collapse edges. The existing pipeline may leave verts unwelded
        // (same position but different UV/normal). We weld ONLY for simplification,
        // then remap the simplified indices back to the original vertex buffer.
        std::vector<uint32_t> weldRemap(finalVertexCount);
        const size_t weldedCount = meshopt_generateVertexRemap(
            weldRemap.data(), optimizedIndices.data(), optimizedIndices.size(),
            optimizedVertices.data(), finalVertexCount, sizeof(VertexPosNormUV));

        std::vector<uint32_t> weldedIndices(optimizedIndices.size());
        meshopt_remapIndexBuffer(weldedIndices.data(), optimizedIndices.data(),
                                 optimizedIndices.size(), weldRemap.data());

        std::vector<float> pos(weldedCount * 3);
        for (size_t i = 0; i < finalVertexCount; ++i) {
            const uint32_t wi = weldRemap[i];
            if (wi < weldedCount) {
                pos[wi * 3 + 0] = optimizedVertices[i].position[0];
                pos[wi * 3 + 1] = optimizedVertices[i].position[1];
                pos[wi * 3 + 2] = optimizedVertices[i].position[2];
            }
        }

        // Build reverse lookup: for each welded index, one original index
        std::vector<uint32_t> unweldMap(weldedCount, UINT32_MAX);
        for (size_t i = 0; i < finalVertexCount; ++i) {
            if (weldRemap[i] < weldedCount && unweldMap[weldRemap[i]] == UINT32_MAX)
                unweldMap[weldRemap[i]] = static_cast<uint32_t>(i);
        }

        constexpr float kReductions[] = { 0.50f, 0.25f, 0.125f };
        for (int lod = 0; lod < 3; ++lod) {
            const size_t target = static_cast<size_t>(
                static_cast<float>(weldedIndices.size()) * kReductions[lod]);
            if (target < 3) break;
            std::vector<uint32_t> lodIdx(weldedIndices.size());
            float err = 0.0f;
            const size_t actual = meshopt_simplify(
                lodIdx.data(), weldedIndices.data(), weldedIndices.size(),
                pos.data(), weldedCount, sizeof(float) * 3,
                target, 1e10f, 0, &err);
            if (actual < 3) break;
            lodIdx.resize(actual);

            // Remap simplified indices back to original vertex buffer
            for (auto& idx : lodIdx) {
                if (idx < weldedCount && unweldMap[idx] != UINT32_MAX)
                    idx = unweldMap[idx];
            }

            meshopt_optimizeVertexCache(lodIdx.data(), lodIdx.data(), lodIdx.size(), finalVertexCount);
            extraLODs.push_back({std::move(lodIdx)});
            UZ_LOG(Asset, Debug, "FBX LOD{}: {} → {} indices ({:.0f}% reduction)",
                   lod + 1, weldedIndices.size(), actual,
                   100.0f * (1.0f - float(actual) / float(weldedIndices.size())));
        }
    }

    // ── Build UZMH v2 with all LOD indices concatenated ──
    const uint32_t submeshCount = static_cast<uint32_t>(submeshes.size());
    const size_t vertexDataBytes = finalVertexCount * sizeof(Vertex);

    // Concatenate LOD0 + LOD1..3 indices
    std::vector<uint32_t> allLodIndices(optimizedIndices.begin(), optimizedIndices.end());
    const uint32_t lodCount = 1 + static_cast<uint32_t>(extraLODs.size());

    Mesh outputMesh{};
    outputMesh.header.magic = kMeshMagic;
    outputMesh.header.version = kMeshVersion;
    outputMesh.header.vertexFormat = MeshVertexFormat::Standard;
    outputMesh.header.vertexStride = sizeof(Vertex);
    outputMesh.header.vertexCount = static_cast<uint32_t>(finalVertexCount);
    outputMesh.header.indexFormat = MeshIndexFormat::UInt32;
    outputMesh.header.indexCount = static_cast<uint32_t>(optimizedIndices.size()); // LOD0
    outputMesh.header.submeshCount = submeshCount;
    outputMesh.header.boundsMin = boundsMin;
    outputMesh.header.boundsMax = boundsMax;
    outputMesh.header.vertexDataOffset = sizeof(MeshHeader);
    outputMesh.header.indexDataOffset = sizeof(MeshHeader) + vertexDataBytes;
    outputMesh.header.lodCount = lodCount;
    std::memset(outputMesh.header.lods, 0, sizeof(outputMesh.header.lods));
    for (size_t i = 0; i < extraLODs.size() && i < 3; ++i) {
        outputMesh.header.lods[i].indexOffset =
            static_cast<uint32_t>(allLodIndices.size() * sizeof(uint32_t));
        outputMesh.header.lods[i].indexCount =
            static_cast<uint32_t>(extraLODs[i].indices.size());
        allLodIndices.insert(allLodIndices.end(),
                             extraLODs[i].indices.begin(), extraLODs[i].indices.end());
    }
    std::memset(outputMesh.header.reserved, 0, sizeof(outputMesh.header.reserved));

    const size_t indexDataBytes = allLodIndices.size() * sizeof(uint32_t);

    // Copy vertex data
    outputMesh.vertexData.resize(vertexDataBytes);
    std::memcpy(outputMesh.vertexData.data(), finalVertices.data(), vertexDataBytes);

    // Copy index data (all LODs concatenated)
    outputMesh.indexData.resize(indexDataBytes);
    std::memcpy(outputMesh.indexData.data(), allLodIndices.data(), indexDataBytes);

    // Map UzMeshSubmesh -> SubMesh (runtime format)
    for (const auto& sub : submeshes) {
        SubMesh sm;
        sm.startIndex = sub.indexOffset;
        sm.indexCount = sub.indexCount;
        sm.materialIndex = sub.materialIndex;
        outputMesh.submeshes.push_back(sm);
    }

    // ── Serialize as flat binary: MeshHeader + vertexData + indexData ──
    const size_t totalSize = sizeof(MeshHeader) + vertexDataBytes + indexDataBytes;
    result.data.resize(totalSize);
    size_t writeOffset = 0;

    std::memcpy(result.data.data() + writeOffset, &outputMesh.header, sizeof(MeshHeader));
    writeOffset += sizeof(MeshHeader);

    std::memcpy(result.data.data() + writeOffset, outputMesh.vertexData.data(), vertexDataBytes);
    writeOffset += vertexDataBytes;

    std::memcpy(result.data.data() + writeOffset, outputMesh.indexData.data(), indexDataBytes);

    result.success = true;

    UZ_LOG(Asset, Info,
           "Imported FBX '{}': {} vertices (48B stride), {} indices, {} submeshes, {} materials, {} LODs, {} bytes",
           sourceName, finalVertexCount, optimizedIndices.size(),
           submeshCount, result.materials.size(), lodCount, result.data.size());

    // ── Generate SDF sidecar ──
    {
        std::vector<float> positions(finalVertexCount * 3);
        for (size_t i = 0; i < finalVertexCount; ++i) {
            positions[i * 3 + 0] = optimizedVertices[i].position[0];
            positions[i * 3 + 1] = optimizedVertices[i].position[1];
            positions[i * 3 + 2] = optimizedVertices[i].position[2];
        }

        SdfBakerConfig sdfConfig;
        sdfConfig.maxResolution = 64;

        MeshSdfAsset sdfAsset = BakeMeshSdf(
            positions.data(),
            static_cast<uint32_t>(finalVertexCount),
            optimizedIndices.data(),
            static_cast<uint32_t>(optimizedIndices.size()),
            sdfConfig);

        if (sdfAsset.IsValid()) {
            std::vector<uint8_t> sdfData = WriteUzSdf(sdfAsset);
            if (!sdfData.empty()) {
                AssetImportSidecar sidecar;
                sidecar.extension = ".uzsdf";
                sidecar.data = std::move(sdfData);
                result.sidecars.push_back(std::move(sidecar));

                UZ_LOG(Asset, Info, "Generated SDF sidecar for '{}': {} bricks, {} MIP levels, {:.1f} KB",
                       sourceName, sdfAsset.GetTotalBrickCount(), sdfAsset.mipLevels,
                       static_cast<double>(result.sidecars.back().data.size()) / 1024.0);
            }
        } else {
            UZ_LOG(Asset, Warning, "SDF generation failed for '{}' — skipping .uzsdf sidecar", sourceName);
        }
    }

    return result;
}

} // namespace uz
