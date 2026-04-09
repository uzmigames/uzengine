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
#include "skeleton.h"
#include "skeleton_io.h"
#include "uzmesh_format.h"
#include "uzsdf_format.h"

#include <ufbx.h>
#include <meshoptimizer.h>

#include "engine/asset/mesh/normal_smoother.h"
#include "engine/asset/mesh/mikktspace_tangents.h"
#include "engine/asset/mesh/packed_normal.h"
#include "import_sidecar.h"
#include "cvar.h"

// ── Mesh import CVars ──
static auto* cv_MeshNormalMode    = UZ_CVAR_INT("r.Mesh.NormalImportMode", 1, "0=Import, 1=Recompute, 2=Auto", 0, 2);
static auto* cv_MeshSmoothAngle  = UZ_CVAR_FLOAT("r.Mesh.SmoothingAngle", 60.0f, "Hard edge angle threshold (degrees)", 0.0f, 180.0f);
static auto* cv_MeshTangentMode  = UZ_CVAR_INT("r.Mesh.TangentMode", 0, "0=MikkTSpace, 1=Lengyel, 2=Import", 0, 2);

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>

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

// ─────────────────────────────────────────────────────────────────────────────
// Skeleton Extraction — builds Skeleton from ufbx_skin_deformer
// ─────────────────────────────────────────────────────────────────────────────

/// Decomposes a ufbx_matrix bind_to_world into a BoneTransform (position, rotation, scale).
///
/// Uses ufbx_matrix_to_transform which correctly decomposes arbitrary affine matrices
/// into translation, rotation (quaternion), and scale — independent of coordinate convention.
[[nodiscard]] BoneTransform DecomposeUfbxMatrix(const ufbx_matrix& mat) {
    ufbx_transform t = ufbx_matrix_to_transform(&mat);
    BoneTransform bt;
    bt.position = Vec3{
        static_cast<float>(t.translation.x),
        static_cast<float>(t.translation.y),
        static_cast<float>(t.translation.z)};
    bt.rotation = Quat{
        static_cast<float>(t.rotation.x),
        static_cast<float>(t.rotation.y),
        static_cast<float>(t.rotation.z),
        static_cast<float>(t.rotation.w)};
    bt.scale = Vec3{
        static_cast<float>(t.scale.x),
        static_cast<float>(t.scale.y),
        static_cast<float>(t.scale.z)};
    return bt;
}

/// Computes a bone-local BoneTransform from the world-space bind poses of the bone
/// and its parent. This inverts the BoneTransform::ComposeWith() operation:
///   world.scale    = parent.scale * local.scale
///   world.rotation = (parent.rotation * local.rotation).normalize()
///   world.position = parent.position + parent.rotation * (parent.scale * local.position)
///
/// Solving for local:
///   local.scale    = world.scale / parent.scale  (component-wise)
///   local.rotation = parent.rotation.inverse() * world.rotation
///   local.position = (parent.rotation.inverse() * (world.position - parent.position)) / parent.scale
[[nodiscard]] BoneTransform ComputeLocalFromWorldTRS(
    const BoneTransform& world,
    const BoneTransform& parent) {

    BoneTransform local;

    // Scale: component-wise division
    local.scale = Vec3{
        world.scale.x / parent.scale.x,
        world.scale.y / parent.scale.y,
        world.scale.z / parent.scale.z};

    // Rotation: remove parent rotation
    const Quat parentInv = parent.rotation.inverse();
    local.rotation = (parentInv * world.rotation).normalize();

    // Position: undo parent's rotation and scale contribution
    const Vec3 delta = world.position - parent.position;
    const Vec3 invRotDelta = parentInv * delta;
    local.position = Vec3{
        invRotDelta.x / parent.scale.x,
        invRotDelta.y / parent.scale.y,
        invRotDelta.z / parent.scale.z};

    return local;
}

/// Result of skeleton extraction from a ufbx_skin_deformer.
struct SkeletonExtractResult {
    Skeleton skeleton;
    /// Maps cluster index (in skin->clusters[]) to the bone index in the Skeleton.
    /// Used to translate per-vertex cluster_index to skeleton bone index.
    std::vector<uint32_t> clusterToBoneIndex;
    bool success = false;
};

/// Extracts a Skeleton from the first skin deformer on a mesh.
///
/// Collects all bone_nodes from the skin's clusters, sorts them topologically
/// (parents before children), extracts bind-pose local TRS per bone, and builds
/// the uz::Skeleton. Also produces a cluster-to-bone-index mapping for per-vertex
/// weight extraction.
[[nodiscard]] SkeletonExtractResult ExtractSkeleton(const ufbx_skin_deformer* skin) {
    SkeletonExtractResult result;

    if (!skin || skin->clusters.count == 0) {
        UZ_LOG(Asset, Error, "Skin deformer has no clusters");
        return result;
    }

    // ── Step 1: Collect all valid bone nodes and their world bind transforms ──
    struct BoneCandidate {
        ufbx_node* node = nullptr;
        size_t clusterIdx = 0;
        BoneTransform worldBind;
    };

    std::vector<BoneCandidate> candidates;
    std::unordered_map<ufbx_node*, size_t> nodeToCandidateIdx;

    for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
        const ufbx_skin_cluster* cluster = skin->clusters.data[ci];
        if (!cluster || !cluster->bone_node) {
            continue;
        }

        ufbx_node* boneNode = cluster->bone_node;

        // Skip duplicate bone nodes (multiple clusters can reference the same bone)
        if (nodeToCandidateIdx.count(boneNode) > 0) {
            continue;
        }

        BoneCandidate cand;
        cand.node = boneNode;
        cand.clusterIdx = ci;
        cand.worldBind = DecomposeUfbxMatrix(cluster->bind_to_world);

        nodeToCandidateIdx[boneNode] = candidates.size();
        candidates.push_back(cand);
    }

    if (candidates.empty()) {
        UZ_LOG(Asset, Error, "No valid bone nodes found in skin deformer");
        return result;
    }

    // ── Step 2: Resolve parent indices (walk up chain to find a bone in our set) ──
    // parentCandidate[i] = index into candidates[] of the parent bone, or -1 for roots
    const size_t numCandidates = candidates.size();
    std::vector<int32_t> parentCandidate(numCandidates, kInvalidBoneIndex);

    for (size_t i = 0; i < numCandidates; ++i) {
        ufbx_node* walk = candidates[i].node->parent;
        while (walk) {
            auto it = nodeToCandidateIdx.find(walk);
            if (it != nodeToCandidateIdx.end()) {
                parentCandidate[i] = static_cast<int32_t>(it->second);
                break;
            }
            walk = walk->parent;
        }
    }

    // ── Step 3: Topological sort (Kahn's algorithm — parents before children) ──
    // Build adjacency: count children per node
    std::vector<int32_t> childCount(numCandidates, 0);
    for (size_t i = 0; i < numCandidates; ++i) {
        if (parentCandidate[i] != kInvalidBoneIndex) {
            childCount[static_cast<size_t>(parentCandidate[i])]++;
        }
    }

    // Kahn's: start with roots (in-degree 0 for parent edges means root)
    // We use "in-degree" as having a parent, so roots have no parent dep.
    // Actually, we need parents sorted before children. Use a simple approach:
    // compute depth, then sort by depth.
    std::vector<int32_t> depth(numCandidates, 0);
    // Compute depth iteratively
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < numCandidates; ++i) {
            if (parentCandidate[i] != kInvalidBoneIndex) {
                const int32_t expectedDepth = depth[static_cast<size_t>(parentCandidate[i])] + 1;
                if (depth[i] < expectedDepth) {
                    depth[i] = expectedDepth;
                    changed = true;
                }
            }
        }
    }

    // Build sorted order: indices into candidates[], sorted by depth then by original order
    std::vector<size_t> sortedOrder(numCandidates);
    std::iota(sortedOrder.begin(), sortedOrder.end(), 0);
    std::sort(sortedOrder.begin(), sortedOrder.end(),
        [&depth](size_t a, size_t b) {
            if (depth[a] != depth[b]) return depth[a] < depth[b];
            return a < b; // Stable: preserve original order within same depth
        });

    // ── Step 4: Build the Skeleton in sorted order ──
    // candidateToSkelIdx maps candidate index -> skeleton bone index (in sorted order)
    std::vector<int32_t> candidateToSkelIdx(numCandidates, kInvalidBoneIndex);

    // World bind TRS per skeleton bone (indexed by skeleton bone index),
    // needed for computing local transforms of children.
    std::vector<BoneTransform> worldBindPerBone;
    worldBindPerBone.reserve(numCandidates);

    int32_t maxDepth = 0;

    for (size_t si = 0; si < numCandidates; ++si) {
        const size_t candIdx = sortedOrder[si];
        const auto& cand = candidates[candIdx];
        const int32_t skelIdx = static_cast<int32_t>(si);
        candidateToSkelIdx[candIdx] = skelIdx;

        BoneInfo bone;
        bone.name = std::string(cand.node->name.data, cand.node->name.length);

        // Resolve parent in skeleton space
        if (parentCandidate[candIdx] != kInvalidBoneIndex) {
            bone.parentIndex = candidateToSkelIdx[static_cast<size_t>(parentCandidate[candIdx])];
        }

        // Compute local bind TRS
        const BoneTransform& worldTRS = cand.worldBind;
        if (bone.parentIndex == kInvalidBoneIndex) {
            // Root bone: world IS local
            bone.bindTranslation = worldTRS.position;
            bone.bindRotation = worldTRS.rotation;
            bone.bindScale = worldTRS.scale;
        } else {
            // Child bone: compute local from parent world and this world
            const BoneTransform& parentWorld = worldBindPerBone[static_cast<size_t>(bone.parentIndex)];
            const BoneTransform localTRS = ComputeLocalFromWorldTRS(worldTRS, parentWorld);
            bone.bindTranslation = localTRS.position;
            bone.bindRotation = localTRS.rotation;
            bone.bindScale = localTRS.scale;
        }

        worldBindPerBone.push_back(worldTRS);

        if (depth[candIdx] > maxDepth) {
            maxDepth = depth[candIdx];
        }

        if (!result.skeleton.AddBone(std::move(bone))) {
            UZ_LOG(Asset, Error, "Failed to add bone '{}' (index {}) to skeleton",
                   cand.node->name.data, skelIdx);
            return result;
        }
    }

    result.skeleton.BuildNameIndex();
    result.skeleton.ComputeInverseBindMatrices();

    // ── Step 5: Build cluster-to-bone-index mapping ──
    // For each cluster in skin->clusters[], find the corresponding skeleton bone index.
    result.clusterToBoneIndex.resize(skin->clusters.count, 0);
    for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
        const ufbx_skin_cluster* cluster = skin->clusters.data[ci];
        if (!cluster || !cluster->bone_node) {
            result.clusterToBoneIndex[ci] = 0; // fallback to bone 0
            continue;
        }

        auto it = nodeToCandidateIdx.find(cluster->bone_node);
        if (it != nodeToCandidateIdx.end()) {
            result.clusterToBoneIndex[ci] = static_cast<uint32_t>(candidateToSkelIdx[it->second]);
        } else {
            result.clusterToBoneIndex[ci] = 0; // fallback to bone 0
        }
    }

    // ── Verification logging ──
    const uint32_t boneCount = result.skeleton.GetBoneCount();
    const int32_t rootIdx = result.skeleton.GetRootBoneIndex();
    const std::string rootName = (rootIdx >= 0) ? result.skeleton.GetBone(static_cast<uint32_t>(rootIdx)).name : "(none)";

    UZ_LOG(Asset, Info, "Extracted skeleton: {} bones, root='{}', max depth={}",
           boneCount, rootName, maxDepth);

    result.success = true;
    return result;
}

/// Packs per-vertex bone weights for a single vertex into 4-bone uint8 arrays.
///
/// Takes the raw ufbx skin weight data for a single vertex, selects the top 4
/// influences, normalizes to uint8 (sum=255), and fills boneIndices/boneWeights.
/// Vertices with fewer than 4 influences get zero-weight padding (boneIndex=0, weight=0).
void PackBoneWeights(
    const ufbx_skin_deformer* skin,
    uint32_t vertexIdx,
    const std::vector<uint32_t>& clusterToBoneIndex,
    std::array<uint8_t, 4>& outBoneIndices,
    std::array<uint8_t, 4>& outBoneWeights) {

    outBoneIndices = {0, 0, 0, 0};
    outBoneWeights = {0, 0, 0, 0};

    if (vertexIdx >= skin->vertices.count) {
        return;
    }

    const ufbx_skin_vertex& sv = skin->vertices.data[vertexIdx];
    const uint32_t numWeights = sv.num_weights;

    if (numWeights == 0) {
        return;
    }

    // Collect up to 4 influences (already sorted by decreasing weight in ufbx)
    const uint32_t count = (numWeights < 4) ? numWeights : 4;
    float rawWeights[4] = {0.f, 0.f, 0.f, 0.f};
    uint32_t boneIdxs[4] = {0, 0, 0, 0};
    float weightSum = 0.f;

    for (uint32_t i = 0; i < count; ++i) {
        const ufbx_skin_weight& sw = skin->weights.data[sv.weight_begin + i];
        const uint32_t clusterIdx = sw.cluster_index;
        const float w = static_cast<float>(sw.weight);

        if (clusterIdx < clusterToBoneIndex.size()) {
            boneIdxs[i] = clusterToBoneIndex[clusterIdx];
        }
        rawWeights[i] = (w > 0.f) ? w : 0.f;
        weightSum += rawWeights[i];
    }

    // Normalize and convert to uint8 (sum = 255)
    if (weightSum > 1e-6f) {
        const float invSum = 1.0f / weightSum;
        uint32_t totalU8 = 0;
        uint32_t largestIdx = 0;
        uint32_t largestVal = 0;

        for (uint32_t i = 0; i < 4; ++i) {
            const float normalized = rawWeights[i] * invSum;
            const uint32_t u8 = static_cast<uint32_t>(normalized * 255.0f + 0.5f);
            outBoneWeights[i] = static_cast<uint8_t>(u8);
            outBoneIndices[i] = static_cast<uint8_t>(boneIdxs[i]);
            totalU8 += u8;
            if (u8 > largestVal) {
                largestVal = u8;
                largestIdx = i;
            }
        }

        // Adjust the largest weight so the sum is exactly 255
        if (totalU8 != 255 && largestVal > 0) {
            const int32_t diff = 255 - static_cast<int32_t>(totalU8);
            const int32_t adjusted = static_cast<int32_t>(outBoneWeights[largestIdx]) + diff;
            if (adjusted > 0 && adjusted <= 255) {
                outBoneWeights[largestIdx] = static_cast<uint8_t>(adjusted);
            }
        }
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
    return 5; // v5: axis remap (FBX.Z,X,Y) → (UE.X,Y,Z) — Z-up correct orientation
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
    // Do NOT set target_axes — the FBX file already uses Z-up vertex positions.
    // Any axis conversion via target_axes remaps the axes and breaks orientation.
    // MODIFY_GEOMETRY bakes all node transforms (scale, rotation, translation) into
    // vertex positions so we get a clean, flat vertex buffer without a scene hierarchy.
    ufbx_load_opts loadOpts = {};
    loadOpts.target_unit_meters = 0.01f; // normalize to centimeters
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

    // ── Detect skinned mesh and extract skeleton ──
    // Skinned import disabled until GPU skinning pipeline is complete.
    // Force static mesh path (UZMH) so mesh renders correctly without bone transforms.
    const bool isSkinned = false; // mesh->skin_deformers.count > 0;
    SkeletonExtractResult skelResult;
    const ufbx_skin_deformer* skin = nullptr;

    if (isSkinned) {
        skin = mesh->skin_deformers.data[0];
        skelResult = ExtractSkeleton(skin);

        if (!skelResult.success) {
            result.error = "Failed to extract skeleton from skinned FBX mesh";
            UZ_LOG(Asset, Error, "FBX import failed for '{}': {}", sourceName, result.error);
            return result;
        }
    }

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

    // Parallel arrays for skinned mesh bone data (one entry per vertex in allVertices).
    // Only populated when isSkinned is true.
    std::vector<std::array<uint8_t, 4>> allBoneIndices;
    std::vector<std::array<uint8_t, 4>> allBoneWeights;

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
        std::vector<bool> groupSmoothFlags; // Per-triangle smoothing flag from FBX

        // Per-group bone data (parallel to groupVertices, only used when skinned)
        std::vector<std::array<uint8_t, 4>> groupBoneIndices;
        std::vector<std::array<uint8_t, 4>> groupBoneWeights;

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

            // Record per-triangle smoothing flag from FBX face_smoothing
            {
                bool faceSmooth = true; // default: smooth
                if (mesh->face_smoothing.count > fi) {
                    faceSmooth = mesh->face_smoothing.data[fi];
                }
                for (uint32_t t = 0; t < numTriangles; ++t) {
                    groupSmoothFlags.push_back(faceSmooth);
                }
            }

            // Each triangulated index is a face-corner index into the mesh's
            // vertex attribute arrays. We create a unique vertex per corner.
            for (uint32_t ti = 0; ti < numTriangulated; ++ti) {
                const uint32_t cornerIdx = triIndices[ti];

                VertexPosNormUV vert = {};

                // Position — always present
                // Axis remap: FBX raw data is (X=lateral, Y=height, Z=depth).
                // UzEngine is Z-up: map FBX.Y→UE.Z, FBX.Z→UE.X, FBX.X→UE.Y.
                // Cyclic permutation (det=+1), preserves winding order.
                const ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, cornerIdx);
                vert.position[0] = static_cast<float>(pos.z);
                vert.position[1] = static_cast<float>(pos.x);
                vert.position[2] = static_cast<float>(pos.y);

                // Normal — may not exist (same axis remap as position)
                if (hasNormals) {
                    const ufbx_vec3 norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, cornerIdx);
                    vert.normal[0] = static_cast<float>(norm.z);
                    vert.normal[1] = static_cast<float>(norm.x);
                    vert.normal[2] = static_cast<float>(norm.y);
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

                // Extract per-vertex bone weights for skinned meshes
                if (isSkinned) {
                    // cornerIdx is a face-corner index; the actual vertex index for
                    // skin weight lookup is mesh->vertex_indices.data[cornerIdx]
                    const uint32_t meshVertexIdx = mesh->vertex_indices.data[cornerIdx];

                    std::array<uint8_t, 4> bi{};
                    std::array<uint8_t, 4> bw{};
                    PackBoneWeights(skin, meshVertexIdx, skelResult.clusterToBoneIndex, bi, bw);
                    groupBoneIndices.push_back(bi);
                    groupBoneWeights.push_back(bw);
                }
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

        // Compute smooth normals based on import mode.
        // Priority: .uzimport per-asset options > CVar globals.
        int normalMode = static_cast<int>(cv_MeshNormalMode->value);
        float smoothAngle = cv_MeshSmoothAngle->value;

        // Check for per-asset .uzimport sidecar override
        {
            const std::string sidecarPath = sourceName + ".uzimport";
            auto sidecarResult = ReadSidecarFile(sidecarPath);
            if (sidecarResult.success) {
                auto it = sidecarResult.sidecar.options.find("normalMode");
                if (it != sidecarResult.sidecar.options.end()) {
                    normalMode = std::atoi(it->second.c_str());
                }
                it = sidecarResult.sidecar.options.find("smoothingAngle");
                if (it != sidecarResult.sidecar.options.end()) {
                    smoothAngle = static_cast<float>(std::atof(it->second.c_str()));
                }
            }
        }
        if (normalMode == 1 || (normalMode == 2 && !hasNormals)) {
            ComputeSmoothNormals(groupVertices, groupIndices, smoothAngle, groupSmoothFlags);
        }

        // Append to global arrays
        for (const uint32_t idx : groupIndices) {
            allIndices.push_back(baseVertex + idx);
        }
        allVertices.insert(allVertices.end(), groupVertices.begin(), groupVertices.end());

        if (isSkinned) {
            allBoneIndices.insert(allBoneIndices.end(), groupBoneIndices.begin(), groupBoneIndices.end());
            allBoneWeights.insert(allBoneWeights.end(), groupBoneWeights.begin(), groupBoneWeights.end());
        }

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
    // Tangent generation based on CVar r.Mesh.TangentMode.
    // 0=MikkTSpace (industry standard), 1=Lengyel (built-in fallback), 2=Import (skip)
    std::vector<float> allTangents;
    const int tangentMode = static_cast<int>(cv_MeshTangentMode->value);
    if (tangentMode == 0) {
        ComputeMikkTSpaceTangents(allVertices, allIndices, allTangents);
    } else if (tangentMode == 1) {
        GenerateTangentsForPosNormUV(allVertices, allIndices, allTangents);
    } else {
        // Import mode — no tangent generation (allTangents stays empty)
        allTangents.resize(allVertices.size() * 4, 0.0f);
    }

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

    // Remap bone data parallel arrays using the same meshopt remap table
    std::vector<std::array<uint8_t, 4>> remappedBoneIndices;
    std::vector<std::array<uint8_t, 4>> remappedBoneWeights;
    if (isSkinned) {
        remappedBoneIndices.resize(finalVertexCount, {0, 0, 0, 0});
        remappedBoneWeights.resize(finalVertexCount, {0, 0, 0, 0});
        for (size_t oldIdx = 0; oldIdx < allVertices.size(); ++oldIdx) {
            const uint32_t newIdx = remap[oldIdx];
            if (newIdx < finalVertexCount) {
                remappedBoneIndices[newIdx] = allBoneIndices[oldIdx];
                remappedBoneWeights[newIdx] = allBoneWeights[oldIdx];
            }
        }
    }

    // ── Convert to runtime vertex format ──
    // Static meshes: Vertex (48 bytes). Skinned meshes: SkinnedVertex (56 bytes).
    // The extraction pipeline uses VertexPosNormUV (32 bytes) for meshopt
    // compatibility. Now combine with remapped tangent + bone data into the
    // runtime vertex struct that the GPU's StructuredBuffer expects.

    // Compute AABB (shared between both paths)
    Vec3 boundsMin{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    Vec3 boundsMax{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};

    for (size_t i = 0; i < finalVertexCount; ++i) {
        const float px = optimizedVertices[i].position[0];
        const float py = optimizedVertices[i].position[1];
        const float pz = optimizedVertices[i].position[2];
        if (px < boundsMin.x) boundsMin.x = px;
        if (py < boundsMin.y) boundsMin.y = py;
        if (pz < boundsMin.z) boundsMin.z = pz;
        if (px > boundsMax.x) boundsMax.x = px;
        if (py > boundsMax.y) boundsMax.y = py;
        if (pz > boundsMax.z) boundsMax.z = pz;
    }

    // Vertex data as raw bytes — either Vertex[] or SkinnedVertex[]
    std::vector<uint8_t> finalVertexData;
    uint32_t finalVertexStride = 0;

    if (isSkinned) {
        // ── Skinned path: build SkinnedVertex array (56 bytes each) ──
        finalVertexStride = sizeof(SkinnedVertex);
        std::vector<SkinnedVertex> skinnedVertices(finalVertexCount);

        for (size_t i = 0; i < finalVertexCount; ++i) {
            const auto& src = optimizedVertices[i];
            auto& dst = skinnedVertices[i];

            dst.position = {src.position[0], src.position[1], src.position[2]};
            dst.normal   = {src.normal[0],   src.normal[1],   src.normal[2]};
            dst.texcoord = {src.uv[0],       src.uv[1]};
            dst.tangent  = {
                remappedTangents[i * 4 + 0],
                remappedTangents[i * 4 + 1],
                remappedTangents[i * 4 + 2],
                remappedTangents[i * 4 + 3]};

            std::memcpy(dst.boneIndices, remappedBoneIndices[i].data(), 4);
            std::memcpy(dst.boneWeights, remappedBoneWeights[i].data(), 4);
        }

        const size_t byteSize = finalVertexCount * sizeof(SkinnedVertex);
        finalVertexData.resize(byteSize);
        std::memcpy(finalVertexData.data(), skinnedVertices.data(), byteSize);
    } else {
        // ── Static path: Vertex (48B) or VertexPacked (32B) based on CVar ──
        static auto* cv_PackedNormals = UZ_CVAR_BOOL("r.Mesh.PackedNormals", false,
            "Export meshes with packed int8x4 normals/tangents (32B vs 48B)");

        if (cv_PackedNormals->value >= 0.5f) {
            // Packed format: 32 bytes/vertex (33% savings)
            finalVertexStride = sizeof(VertexPacked);
            std::vector<VertexPacked> packedVertices(finalVertexCount);

            for (size_t i = 0; i < finalVertexCount; ++i) {
                const auto& src = optimizedVertices[i];
                auto& dst = packedVertices[i];

                dst.position = {src.position[0], src.position[1], src.position[2]};
                auto pn = PackedNormal::Encode(src.normal[0], src.normal[1], src.normal[2]);
                dst.packedNormal[0] = pn.x;
                dst.packedNormal[1] = pn.y;
                dst.packedNormal[2] = pn.z;
                dst.packedNormal[3] = pn.w;
                dst.texcoord = {src.uv[0], src.uv[1]};
                auto pt = PackedTangent::Encode(
                    remappedTangents[i * 4 + 0], remappedTangents[i * 4 + 1],
                    remappedTangents[i * 4 + 2], remappedTangents[i * 4 + 3]);
                dst.packedTangent[0] = pt.x;
                dst.packedTangent[1] = pt.y;
                dst.packedTangent[2] = pt.z;
                dst.packedTangent[3] = pt.w;
                dst._pad = 0;
            }

            const size_t byteSize = finalVertexCount * sizeof(VertexPacked);
            finalVertexData.resize(byteSize);
            std::memcpy(finalVertexData.data(), packedVertices.data(), byteSize);
        } else {
            // Standard format: 48 bytes/vertex
            finalVertexStride = sizeof(Vertex);
            std::vector<Vertex> finalVertices(finalVertexCount);

            for (size_t i = 0; i < finalVertexCount; ++i) {
                const auto& src = optimizedVertices[i];
                auto& dst = finalVertices[i];

                dst.position = {src.position[0], src.position[1], src.position[2]};
                dst.normal   = {src.normal[0],   src.normal[1],   src.normal[2]};
                dst.texcoord = {src.uv[0],       src.uv[1]};
                dst.tangent  = {
                    remappedTangents[i * 4 + 0], remappedTangents[i * 4 + 1],
                    remappedTangents[i * 4 + 2], remappedTangents[i * 4 + 3]};
            }

            const size_t byteSize = finalVertexCount * sizeof(Vertex);
            finalVertexData.resize(byteSize);
            std::memcpy(finalVertexData.data(), finalVertices.data(), byteSize);
        }
    }

    // ── Auto-LOD generation (LOD1 50%, LOD2 25%, LOD3 12.5%) ──
    // meshopt_simplify requires welded vertices. FBX exports unique verts per
    // triangle corner, so we create a position-only weld remap, simplify in
    // welded space, then map simplified indices back to the original vertex buffer.
    constexpr uint32_t kMaxLODLevels = 3;
    constexpr float kLODReductions[kMaxLODLevels] = {0.5f, 0.25f, 0.125f};

    std::vector<std::vector<uint32_t>> lodIndexBuffers(kMaxLODLevels);
    uint32_t generatedLODCount = 0;

    if (optimizedIndices.size() >= 300) {
        // Extract positions as flat float array for meshopt welding
        std::vector<float> positionsFlat(finalVertexCount * 3);
        for (size_t i = 0; i < finalVertexCount; ++i) {
            positionsFlat[i * 3 + 0] = optimizedVertices[i].position[0];
            positionsFlat[i * 3 + 1] = optimizedVertices[i].position[1];
            positionsFlat[i * 3 + 2] = optimizedVertices[i].position[2];
        }

        // Position-only weld remap: vertices with identical positions but
        // different normals/UVs/tangents get the same welded index.
        std::vector<unsigned int> weldRemap(finalVertexCount);
        const size_t weldedCount = meshopt_generateVertexRemap(
            weldRemap.data(), optimizedIndices.data(),
            optimizedIndices.size(), positionsFlat.data(),
            finalVertexCount, sizeof(float) * 3);

        // Build welded position array
        std::vector<float> weldedPositions(weldedCount * 3);
        for (size_t i = 0; i < finalVertexCount; ++i) {
            const unsigned int dst = weldRemap[i];
            weldedPositions[dst * 3 + 0] = positionsFlat[i * 3 + 0];
            weldedPositions[dst * 3 + 1] = positionsFlat[i * 3 + 1];
            weldedPositions[dst * 3 + 2] = positionsFlat[i * 3 + 2];
        }

        // Remap LOD0 indices into welded space
        std::vector<uint32_t> weldedIndices(optimizedIndices.size());
        for (size_t i = 0; i < optimizedIndices.size(); ++i) {
            weldedIndices[i] = weldRemap[optimizedIndices[i]];
        }

        // Reverse lookup: welded vertex index -> one original vertex index.
        // Any of the coincident originals is valid since they share position.
        std::vector<uint32_t> weldToOriginal(weldedCount, 0);
        for (size_t i = 0; i < finalVertexCount; ++i) {
            weldToOriginal[weldRemap[i]] = static_cast<uint32_t>(i);
        }

        for (uint32_t lod = 0; lod < kMaxLODLevels; ++lod) {
            size_t targetCount = static_cast<size_t>(
                static_cast<float>(weldedIndices.size()) * kLODReductions[lod]);
            targetCount = (targetCount / 3) * 3;
            if (targetCount < 3) {
                targetCount = 3;
            }

            std::vector<uint32_t> simplified(weldedIndices.size());
            const size_t simplifiedCount = meshopt_simplify(
                simplified.data(), weldedIndices.data(),
                weldedIndices.size(), weldedPositions.data(),
                weldedCount, sizeof(float) * 3,
                targetCount, 1e10f);

            simplified.resize(simplifiedCount);
            if (simplified.empty()) {
                break;
            }

            // Map simplified indices back to the original (unwelded) vertex buffer
            for (auto& idx : simplified) {
                idx = weldToOriginal[idx];
            }

            // Optimize vertex cache ordering for this LOD
            meshopt_optimizeVertexCache(
                simplified.data(), simplified.data(),
                simplified.size(), finalVertexCount);

            UZ_LOG(Asset, Info, "FBX LOD{}: {} \xe2\x86\x92 {} indices ({:.0f}% reduction)",
                   lod + 1, optimizedIndices.size(), simplified.size(),
                   (1.0f - static_cast<float>(simplified.size()) /
                    static_cast<float>(optimizedIndices.size())) * 100.0f);

            lodIndexBuffers[lod] = std::move(simplified);
            ++generatedLODCount;
        }
    }

    // ── Serialize mesh data ──
    const size_t vertexDataBytes = finalVertexData.size();
    const size_t lod0IndexBytes = optimizedIndices.size() * sizeof(uint32_t);
    size_t totalIndexBytes = lod0IndexBytes;
    for (uint32_t i = 0; i < generatedLODCount; ++i) {
        totalIndexBytes += lodIndexBuffers[i].size() * sizeof(uint32_t);
    }

    // Derive skeleton sidecar path from source name (skinned meshes only)
    std::string skeletonPath;
    if (isSkinned) {
        const size_t dotPos = sourceName.rfind('.');
        if (dotPos != std::string::npos) {
            skeletonPath = sourceName.substr(0, dotPos) + ".uzskel";
        } else {
            skeletonPath = sourceName + ".uzskel";
        }
    }

    if (isSkinned) {
        // ── Skinned path: use WriteUzMesh (UZMS format) with skeleton reference ──
        // Concatenate all LOD indices into a single index buffer
        const size_t lod0IndexCount = optimizedIndices.size();
        size_t totalIndexCount = lod0IndexCount;
        for (uint32_t li = 0; li < generatedLODCount; ++li) {
            totalIndexCount += lodIndexBuffers[li].size();
        }

        std::vector<uint8_t> indexBytes(totalIndexCount * sizeof(uint32_t));
        std::memcpy(indexBytes.data(), optimizedIndices.data(), lod0IndexCount * sizeof(uint32_t));
        {
            size_t writeOff = lod0IndexCount * sizeof(uint32_t);
            for (uint32_t li = 0; li < generatedLODCount; ++li) {
                const size_t lodBytes = lodIndexBuffers[li].size() * sizeof(uint32_t);
                std::memcpy(indexBytes.data() + writeOff, lodIndexBuffers[li].data(), lodBytes);
                writeOff += lodBytes;
            }
        }

        // Build LOD descriptors
        const uint32_t totalLODCount = 1 + generatedLODCount;
        std::vector<UzMeshLOD> lods;
        lods.reserve(totalLODCount);

        UzMeshLOD lod0;
        lod0.lodLevel = 0;
        lod0.submeshOffset = 0;
        lod0.submeshCount = static_cast<uint32_t>(submeshes.size());
        lod0.screenHeightThreshold = 0.0f;
        lods.push_back(lod0);

        uint32_t lodSubmeshStart = static_cast<uint32_t>(submeshes.size());
        size_t lodIndexOffset = lod0IndexCount;
        for (uint32_t li = 0; li < generatedLODCount; ++li) {
            UzMeshLOD lodDesc;
            lodDesc.lodLevel = li + 1;
            lodDesc.submeshOffset = lodSubmeshStart;
            lodDesc.submeshCount = 1;
            lodDesc.screenHeightThreshold = 0.5f / static_cast<float>(li + 1);
            lods.push_back(lodDesc);

            UzMeshSubmesh lodSub;
            lodSub.vertexOffset = 0;
            lodSub.indexOffset = static_cast<uint32_t>(lodIndexOffset);
            lodSub.indexCount = static_cast<uint32_t>(lodIndexBuffers[li].size());
            lodSub.materialIndex = 0;
            submeshes.push_back(lodSub);

            lodSubmeshStart++;
            lodIndexOffset += lodIndexBuffers[li].size();
        }

        UzMeshHeader uzHeader;
        uzHeader.flags = kUzMeshFlag_Skinned;
        uzHeader.lodCount = totalLODCount;
        uzHeader.totalSubmeshCount = static_cast<uint32_t>(submeshes.size());
        uzHeader.vertexCount = static_cast<uint32_t>(finalVertexCount);
        uzHeader.indexCount = static_cast<uint32_t>(totalIndexCount);
        uzHeader.vertexStride = finalVertexStride;
        uzHeader.indexStride = 4;

        result.data = WriteUzMesh(uzHeader, lods, submeshes, finalVertexData, indexBytes, skeletonPath);

        // Write .uzskel sidecar
        std::vector<uint8_t> skelData = WriteSkeleton(skelResult.skeleton);
        if (!skelData.empty()) {
            AssetImportSidecar skelSidecar;
            skelSidecar.extension = ".uzskel";
            skelSidecar.data = std::move(skelData);
            result.sidecars.push_back(std::move(skelSidecar));
        }

        // Verification logging: bone weight distribution
        uint32_t influenceCount[5] = {0, 0, 0, 0, 0};
        for (size_t i = 0; i < finalVertexCount; ++i) {
            uint32_t count = 0;
            for (uint32_t j = 0; j < 4; ++j) {
                if (remappedBoneWeights[i][j] > 0) {
                    ++count;
                }
            }
            if (count <= 4) {
                influenceCount[count]++;
            }
        }

        UZ_LOG(Asset, Info,
               "Skinned mesh '{}': {} bones, skeleton='{}', weight distribution: "
               "1-bone={}, 2-bone={}, 3-bone={}, 4-bone={}",
               sourceName, skelResult.skeleton.GetBoneCount(), skeletonPath,
               influenceCount[1], influenceCount[2], influenceCount[3], influenceCount[4]);

        UZ_LOG(Asset, Info,
               "Imported FBX '{}': {} vertices ({}B stride), {} indices, {} LODs, {} submeshes, "
               "{} materials, {} bytes [SKINNED]",
               sourceName, finalVertexCount, finalVertexStride, totalIndexCount,
               totalLODCount, submeshes.size(), result.materials.size(), result.data.size());
    } else {
        // ── Static path: use old UZMH format (MeshHeader, 128 bytes) ──
        // This preserves exact compatibility with LoadMeshFromMemory().
        const uint32_t submeshCount = static_cast<uint32_t>(submeshes.size());

        Mesh outputMesh{};
        outputMesh.header.magic = kMeshMagic;
        outputMesh.header.version = kMeshVersion;
        outputMesh.header.vertexFormat = MeshVertexFormat::Standard;
        outputMesh.header.vertexStride = sizeof(Vertex);
        outputMesh.header.vertexCount = static_cast<uint32_t>(finalVertexCount);
        outputMesh.header.indexFormat = MeshIndexFormat::UInt32;
        outputMesh.header.indexCount = static_cast<uint32_t>(optimizedIndices.size());
        outputMesh.header.submeshCount = submeshCount;
        outputMesh.header.boundsMin = boundsMin;
        outputMesh.header.boundsMax = boundsMax;
        outputMesh.header.vertexDataOffset = sizeof(MeshHeader);
        outputMesh.header.indexDataOffset = sizeof(MeshHeader) + vertexDataBytes;
        std::memset(outputMesh.header.reserved, 0, sizeof(outputMesh.header.reserved));

        // Fill LOD header fields (UZMH v2)
        outputMesh.header.lodCount = 1 + generatedLODCount;
        {
            size_t lodByteOff = lod0IndexBytes;
            for (uint32_t i = 0; i < generatedLODCount; ++i) {
                outputMesh.header.lods[i].indexOffset = static_cast<uint32_t>(lodByteOff);
                outputMesh.header.lods[i].indexCount = static_cast<uint32_t>(lodIndexBuffers[i].size());
                lodByteOff += lodIndexBuffers[i].size() * sizeof(uint32_t);
            }
        }

        // Copy vertex data
        outputMesh.vertexData = finalVertexData;

        // Copy index data (LOD0 + LOD1-3 concatenated)
        outputMesh.indexData.resize(totalIndexBytes);
        std::memcpy(outputMesh.indexData.data(), optimizedIndices.data(), lod0IndexBytes);
        {
            size_t lodWriteOff = lod0IndexBytes;
            for (uint32_t i = 0; i < generatedLODCount; ++i) {
                const size_t lodBytes = lodIndexBuffers[i].size() * sizeof(uint32_t);
                std::memcpy(outputMesh.indexData.data() + lodWriteOff,
                            lodIndexBuffers[i].data(), lodBytes);
                lodWriteOff += lodBytes;
            }
        }

        // Map UzMeshSubmesh -> SubMesh (runtime format)
        for (const auto& sub : submeshes) {
            SubMesh sm;
            sm.startIndex = sub.indexOffset;
            sm.indexCount = sub.indexCount;
            sm.materialIndex = sub.materialIndex;
            outputMesh.submeshes.push_back(sm);
        }

        // Serialize as flat binary: MeshHeader + vertexData + indexData
        const size_t totalSize = sizeof(MeshHeader) + vertexDataBytes + totalIndexBytes;
        result.data.resize(totalSize);
        size_t writeOffset = 0;

        std::memcpy(result.data.data() + writeOffset, &outputMesh.header, sizeof(MeshHeader));
        writeOffset += sizeof(MeshHeader);

        std::memcpy(result.data.data() + writeOffset, outputMesh.vertexData.data(), vertexDataBytes);
        writeOffset += vertexDataBytes;

        std::memcpy(result.data.data() + writeOffset, outputMesh.indexData.data(), totalIndexBytes);

        UZ_LOG(Asset, Info,
               "Imported FBX '{}': {} vertices (48B stride), {} indices, {} LODs, {} submeshes, {} materials, {} bytes",
               sourceName, finalVertexCount, optimizedIndices.size(),
               outputMesh.header.lodCount, submeshCount, result.materials.size(), result.data.size());
    }

    result.success = true;

    // ── Generate SDF sidecar ──
    // SDF baker hangs on meshes with >50K indices — skip for large meshes.
    if (optimizedIndices.size() <= 50000) {
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
