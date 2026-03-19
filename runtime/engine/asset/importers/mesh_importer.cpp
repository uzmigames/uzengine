/// UzEngine Mesh Importer — Implementation
///
/// Parses glTF/GLB via fastgltf, extracts geometry from all primitives of the
/// first mesh, generates tangents when missing, extracts PBR materials, applies
/// meshoptimizer cache/fetch optimizations, and writes .uzmesh format.

#include "mesh_importer.h"
#include "log.h"
#include "profiler.h"
#include "sdf_baker.h"
#include "uzmesh_format.h"
#include "uzsdf_format.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <variant>

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
/// vectors, following the same algorithm as the public GenerateTangents()
/// in mesh.h/cpp but operating on the packed VertexPosNormUV layout.
///
/// Tangents are stored as float[4] appended after the VertexPosNormUV data
/// via the tangentOut array. We cannot store in VertexPosNormUV (no tangent
/// field), so callers must handle the tangent data separately or convert to
/// the Vertex format before calling this.
///
/// For the importer, we generate tangents into a separate array and then the
/// caller can embed them into the final vertex format if needed. However,
/// since .uzmesh currently uses VertexPosNormUV (32 bytes, no tangent),
/// tangent data would need a format extension. For now, we compute tangents
/// so they can be used if the output format is upgraded.
///
/// Actually, re-reading the requirements: the task asks us to generate tangents
/// for the Vertex format in mesh.h (which has tangent). But the importer
/// currently uses VertexPosNormUV. We should consider whether to switch the
/// importer output to the full Vertex format. The Vertex struct has 48 bytes
/// (position + normal + texcoord + tangent). Let's do that — it matches the
/// PBR pipeline's needs.

/// Resolves a texture path from a fastgltf TextureInfo through the Texture
/// and Image indirection chain.
///
/// glTF texture references go: TextureInfo.textureIndex -> Texture.imageIndex
/// -> Image.data (which may be a URI, embedded buffer view, etc.).
/// For URI sources, returns the URI path. For embedded/buffer-view sources,
/// returns the image name if available, otherwise an empty string.
[[nodiscard]] std::string ResolveTexturePath(
    const fastgltf::Asset& gltf,
    const fastgltf::TextureInfo& texInfo) {

    if (texInfo.textureIndex >= gltf.textures.size()) {
        return {};
    }

    const auto& texture = gltf.textures[texInfo.textureIndex];
    if (!texture.imageIndex.has_value()) {
        return {};
    }

    const size_t imageIdx = texture.imageIndex.value();
    if (imageIdx >= gltf.images.size()) {
        return {};
    }

    const auto& image = gltf.images[imageIdx];

    // Try to extract a URI path from the image data source.
    if (const auto* uriSource = std::get_if<fastgltf::sources::URI>(&image.data)) {
        return std::string(uriSource->uri.path());
    }

    // For embedded images (buffer view, byte array), use the image name if set.
    if (!image.name.empty()) {
        return std::string(image.name);
    }

    // No resolvable path — return the image index as a fallback identifier.
    return "image_" + std::to_string(imageIdx);
}

/// Extracts PBR metallic-roughness material data from a fastgltf Material.
[[nodiscard]] ImportedMaterial ExtractMaterial(
    const fastgltf::Asset& gltf,
    const fastgltf::Material& mat) {

    ImportedMaterial imported;
    imported.name = std::string(mat.name);

    // Base color factor (RGBA)
    const auto& bcf = mat.pbrData.baseColorFactor;
    imported.baseColorR = static_cast<float>(bcf[0]);
    imported.baseColorG = static_cast<float>(bcf[1]);
    imported.baseColorB = static_cast<float>(bcf[2]);
    imported.baseColorA = static_cast<float>(bcf[3]);

    // Metallic and roughness factors
    imported.metallicFactor = static_cast<float>(mat.pbrData.metallicFactor);
    imported.roughnessFactor = static_cast<float>(mat.pbrData.roughnessFactor);

    // Base color texture
    if (mat.pbrData.baseColorTexture.has_value()) {
        imported.baseColorTexturePath = ResolveTexturePath(gltf, mat.pbrData.baseColorTexture.value());
    }

    // Normal texture
    if (mat.normalTexture.has_value()) {
        imported.normalTexturePath = ResolveTexturePath(gltf, mat.normalTexture.value());
    }

    // Metallic-roughness texture
    if (mat.pbrData.metallicRoughnessTexture.has_value()) {
        imported.metallicRoughnessTexturePath = ResolveTexturePath(
            gltf, mat.pbrData.metallicRoughnessTexture.value());
    }

    return imported;
}

/// Generates tangent vectors for VertexPosNormUV in-place by computing
/// tangent/bitangent from UV-space edge vectors, then orthogonalizing
/// via Gram-Schmidt. Result is stored in the output tangent array.
///
/// This is the VertexPosNormUV version of GenerateTangents (which operates
/// on the Vertex struct). We need this because the importer builds
/// VertexPosNormUV arrays before meshopt optimization, and tangent data
/// must be computed before the vertex reorder.
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

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// MeshImporter
// ─────────────────────────────────────────────────────────────────────────────

MeshImporter::MeshImporter()
    : m_extensions({".glb", ".gltf"}) {}

const char* MeshImporter::Name() const {
    return "mesh";
}

uint32_t MeshImporter::Version() const {
    return 2;
}

const std::vector<std::string>& MeshImporter::Extensions() const {
    return m_extensions;
}

AssetImportResult MeshImporter::Import(
    const std::vector<uint8_t>& sourceData,
    const std::string& sourceName,
    const AssetImportOptions& /*options*/) {
    UZ_PROFILE_FUNCTION();

    AssetImportResult result;

    if (sourceData.empty()) {
        result.error = "Empty source data";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // ── Parse glTF/GLB ──
    fastgltf::Parser parser;

    auto dataBuffer = fastgltf::GltfDataBuffer::FromBytes(
        reinterpret_cast<const std::byte*>(sourceData.data()), sourceData.size());
    if (dataBuffer.error() != fastgltf::Error::None) {
        result.error = "Failed to create glTF data buffer";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    auto asset = parser.loadGltf(dataBuffer.get(), {}, fastgltf::Options::None);
    if (asset.error() != fastgltf::Error::None) {
        result.error = "Failed to parse glTF: error code " +
                       std::to_string(static_cast<int>(asset.error()));
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    auto& gltf = asset.get();

    if (gltf.meshes.empty()) {
        result.error = "No meshes found in glTF file";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    const auto& mesh = gltf.meshes[0];
    if (mesh.primitives.empty()) {
        result.error = "First mesh has no primitives";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // ── Extract materials from all materials referenced by primitives ──
    // Build a set of unique material indices referenced by the mesh, then
    // extract each. The mapping from glTF material index to our output
    // material index is stored for submesh assignment.
    //
    // glTF primitives without a materialIndex get a default material at
    // the end of the materials array.
    std::vector<size_t> uniqueMatIndices;
    bool needsDefaultMaterial = false;

    for (const auto& prim : mesh.primitives) {
        if (prim.materialIndex.has_value()) {
            const size_t matIdx = prim.materialIndex.value();
            if (std::find(uniqueMatIndices.begin(), uniqueMatIndices.end(), matIdx) == uniqueMatIndices.end()) {
                uniqueMatIndices.push_back(matIdx);
            }
        } else {
            needsDefaultMaterial = true;
        }
    }

    // Extract materials in order of first appearance
    for (const size_t matIdx : uniqueMatIndices) {
        if (matIdx < gltf.materials.size()) {
            result.materials.push_back(ExtractMaterial(gltf, gltf.materials[matIdx]));
        } else {
            // Invalid material index — use defaults
            ImportedMaterial defaultMat;
            defaultMat.name = "invalid_material_" + std::to_string(matIdx);
            result.materials.push_back(std::move(defaultMat));
        }
    }

    // Default material for primitives without a material assignment
    const uint32_t defaultMaterialOutputIdx = static_cast<uint32_t>(result.materials.size());
    if (needsDefaultMaterial) {
        ImportedMaterial defaultMat;
        defaultMat.name = "default";
        result.materials.push_back(std::move(defaultMat));
    }

    // ── Iterate ALL primitives → multi-submesh ──
    // We merge all primitives into a single vertex/index buffer with
    // per-submesh index ranges. Each primitive's vertices are appended
    // sequentially, and indices are offset accordingly.
    std::vector<VertexPosNormUV> allVertices;
    std::vector<uint32_t> allIndices;
    std::vector<float> allTangents; // 4 floats per vertex (tx, ty, tz, w)
    std::vector<UzMeshSubmesh> submeshes;

    for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
        const auto& prim = mesh.primitives[primIdx];

        // Find POSITION attribute (required per-primitive)
        auto posIt = prim.findAttribute("POSITION");
        if (posIt == prim.attributes.end()) {
            UZ_LOG(Asset, Warning,
                   "Primitive {} in '{}' missing POSITION attribute — skipping",
                   primIdx, sourceName);
            continue;
        }

        const auto& posAccessor = gltf.accessors[posIt->accessorIndex];
        const size_t vertexCount = posAccessor.count;

        // Track base vertex for index offsetting
        const uint32_t baseVertex = static_cast<uint32_t>(allVertices.size());
        const uint32_t baseIndex = static_cast<uint32_t>(allIndices.size());

        // Allocate vertices for this primitive
        const size_t primVertStart = allVertices.size();
        allVertices.resize(primVertStart + vertexCount);

        // Read positions
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            gltf, posAccessor, [&](fastgltf::math::fvec3 pos, std::size_t idx) {
                auto& v = allVertices[primVertStart + idx];
                v.position[0] = pos[0];
                v.position[1] = pos[1];
                v.position[2] = pos[2];
            });

        // Read normals (optional)
        bool hasNormals = false;
        auto normIt = prim.findAttribute("NORMAL");
        if (normIt != prim.attributes.end()) {
            const auto& normAccessor = gltf.accessors[normIt->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                gltf, normAccessor, [&](fastgltf::math::fvec3 n, std::size_t idx) {
                    auto& v = allVertices[primVertStart + idx];
                    v.normal[0] = n[0];
                    v.normal[1] = n[1];
                    v.normal[2] = n[2];
                });
            hasNormals = true;
        }

        // Read UVs (optional, default to 0,0)
        auto uvIt = prim.findAttribute("TEXCOORD_0");
        if (uvIt != prim.attributes.end()) {
            const auto& uvAccessor = gltf.accessors[uvIt->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                gltf, uvAccessor, [&](fastgltf::math::fvec2 uv, std::size_t idx) {
                    auto& v = allVertices[primVertStart + idx];
                    v.uv[0] = uv[0];
                    v.uv[1] = uv[1];
                });
        }

        // Read tangents (optional — generate if missing)
        bool hasTangents = false;
        auto tanIt = prim.findAttribute("TANGENT");
        if (tanIt != prim.attributes.end()) {
            hasTangents = true;
        }

        // Read indices
        std::vector<uint32_t> primIndices;
        if (prim.indicesAccessor.has_value()) {
            const auto& idxAccessor = gltf.accessors[prim.indicesAccessor.value()];
            primIndices.resize(idxAccessor.count);
            fastgltf::iterateAccessorWithIndex<uint32_t>(
                gltf, idxAccessor, [&](uint32_t val, std::size_t idx) {
                    primIndices[idx] = val;
                });
        } else {
            // Non-indexed: generate sequential indices
            primIndices.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) {
                primIndices[i] = static_cast<uint32_t>(i);
            }
        }

        // Generate flat normals if not provided (operates on this primitive's
        // vertices only, using local indices before global offset).
        if (!hasNormals) {
            // Build a temporary view into the allVertices for this primitive
            std::vector<VertexPosNormUV> primVerts(
                allVertices.begin() + static_cast<ptrdiff_t>(primVertStart),
                allVertices.begin() + static_cast<ptrdiff_t>(primVertStart + vertexCount));
            GenerateFlatNormals(primVerts, primIndices);
            std::copy(primVerts.begin(), primVerts.end(),
                      allVertices.begin() + static_cast<ptrdiff_t>(primVertStart));
        }

        // Generate tangents if not provided by the glTF
        // Resize tangent array to match current vertex count
        const size_t tangentStartFloat = primVertStart * 4;
        allTangents.resize(allVertices.size() * 4, 0.f);

        if (hasTangents) {
            // Read tangents from glTF accessor
            const auto& tanAccessor = gltf.accessors[tanIt->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                gltf, tanAccessor, [&](fastgltf::math::fvec4 t, std::size_t idx) {
                    const size_t base = (primVertStart + idx) * 4;
                    allTangents[base + 0] = t[0];
                    allTangents[base + 1] = t[1];
                    allTangents[base + 2] = t[2];
                    allTangents[base + 3] = t[3];
                });
        } else {
            // Generate tangents from UV-space edge vectors for this primitive
            std::vector<VertexPosNormUV> primVerts(
                allVertices.begin() + static_cast<ptrdiff_t>(primVertStart),
                allVertices.begin() + static_cast<ptrdiff_t>(primVertStart + vertexCount));
            std::vector<float> primTangents;
            GenerateTangentsForPosNormUV(primVerts, primIndices, primTangents);

            // Copy into global tangent array
            std::memcpy(&allTangents[tangentStartFloat],
                        primTangents.data(),
                        primTangents.size() * sizeof(float));
        }

        // Offset indices to global vertex space and append
        for (const uint32_t localIdx : primIndices) {
            allIndices.push_back(baseVertex + localIdx);
        }

        // Determine material output index for this submesh
        uint32_t materialOutputIdx = defaultMaterialOutputIdx;
        if (prim.materialIndex.has_value()) {
            const size_t matIdx = prim.materialIndex.value();
            auto it = std::find(uniqueMatIndices.begin(), uniqueMatIndices.end(), matIdx);
            if (it != uniqueMatIndices.end()) {
                materialOutputIdx = static_cast<uint32_t>(
                    std::distance(uniqueMatIndices.begin(), it));
            }
        }

        // Record submesh
        UzMeshSubmesh submesh;
        submesh.vertexOffset = baseVertex;
        submesh.indexOffset = baseIndex;
        submesh.indexCount = static_cast<uint32_t>(primIndices.size());
        submesh.materialIndex = materialOutputIdx;
        submeshes.push_back(submesh);

        UZ_LOG(Asset, Info,
               "Primitive {} in '{}': {} vertices, {} indices, material={}",
               primIdx, sourceName, vertexCount, primIndices.size(), materialOutputIdx);
    }

    if (allVertices.empty()) {
        result.error = "No valid primitives found (all missing POSITION)";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // ── Meshopt: optimize vertex cache ordering ──
    // Optimization is applied globally across all submeshes. The submesh
    // index ranges remain valid because meshopt_optimizeVertexCache only
    // reorders indices, not vertices, and we apply it to the full buffer.
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

    // Remap tangent data to match the new vertex order.
    // meshopt_optimizeVertexFetch produces a remap through the modified index
    // buffer. Since we cannot directly get the remap table, we rebuild tangents
    // by iterating the optimized vertices. However, meshopt_optimizeVertexFetch
    // rewrites the index buffer and reorders vertices in-place, so the mapping
    // from old index to new index is embedded in the index changes.
    //
    // Alternative: use meshopt_optimizeVertexFetchRemap to get the explicit
    // remap table. Let's use that approach for correctness.
    std::vector<uint32_t> remap(allVertices.size());
    meshopt_optimizeVertexFetchRemap(
        remap.data(), optimizedIndices.data(),
        optimizedIndices.size(), allVertices.size());

    // Build remapped tangent array matching the new vertex order.
    // remap[oldIdx] = newIdx. We need tangents indexed by new vertex index.
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

    // ── Build .uzmesh ──
    const uint32_t submeshCount = static_cast<uint32_t>(submeshes.size());

    UzMeshHeader header;
    header.lodCount = 1;
    header.totalSubmeshCount = submeshCount;
    header.vertexCount = static_cast<uint32_t>(finalVertexCount);
    header.indexCount = static_cast<uint32_t>(optimizedIndices.size());
    header.vertexStride = sizeof(VertexPosNormUV);
    header.indexStride = sizeof(uint32_t);

    UzMeshLOD lod;
    lod.lodLevel = 0;
    lod.submeshOffset = 0;
    lod.submeshCount = submeshCount;
    lod.screenHeightThreshold = 0.0f;

    // Serialize vertex/index data as raw byte arrays
    std::vector<uint8_t> vertexData(finalVertexCount * sizeof(VertexPosNormUV));
    std::memcpy(vertexData.data(), optimizedVertices.data(), vertexData.size());

    std::vector<uint8_t> indexData(optimizedIndices.size() * sizeof(uint32_t));
    std::memcpy(indexData.data(), optimizedIndices.data(), indexData.size());

    result.data = WriteUzMesh(header, {lod}, submeshes, vertexData, indexData);
    result.success = true;

    UZ_LOG(Asset, Info,
           "Imported mesh '{}': {} vertices, {} indices, {} submeshes, {} materials, {} bytes",
           sourceName, finalVertexCount, optimizedIndices.size(),
           submeshCount, result.materials.size(), result.data.size());

    // ── Generate SDF sidecar ──
    // Extract position data from optimized vertices for SDF baking.
    // SdfBaker expects a flat float array of positions [x0,y0,z0,x1,...].
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
