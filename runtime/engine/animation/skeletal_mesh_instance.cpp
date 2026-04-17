/// UzEngine SkeletalMeshInstance — Implementation.

#include "skeletal_mesh_instance.h"
#include "skeleton.h"
#include "mesh.h"
#include "vertex.h"
#include "log.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Animation);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Bind + pose state
// ─────────────────────────────────────────────────────────────────────────────

void SkeletalMeshInstance::Bind(const Mesh* sourceMesh, const Skeleton* skeleton, uint32_t outputMeshId) {
    m_sourceMesh   = sourceMesh;
    m_skeleton     = skeleton;
    m_outputMeshId = outputMeshId;

    if (skeleton != nullptr) {
        m_palette.assign(skeleton->GetBoneCount(), Mat4::Identity());
        ResetToBindPose();
    }

    if (sourceMesh != nullptr) {
        const uint32_t vertexCount = sourceMesh->GetVertexCount();
        m_outputVertexData.assign(static_cast<size_t>(vertexCount) * sizeof(Vertex), 0);
        // Prime the output with the bind-pose copy so a first frame without
        // any anim update still draws correctly.
        if (!sourceMesh->vertexData.empty()) {
            const size_t bytes = std::min(m_outputVertexData.size(), sourceMesh->vertexData.size());
            std::memcpy(m_outputVertexData.data(), sourceMesh->vertexData.data(), bytes);
        }
        // Force one upload so the procedural GPU mesh starts with the bind
        // pose rather than uninitialized data. Cleared on first UploadAll.
        m_outputDirty = true;
    }
}

void SkeletalMeshInstance::ResetToBindPose() {
    if (!m_skeleton) return;
    std::lock_guard<std::mutex> lock(m_poseMutex);
    m_currentPose = Pose::FromBindPose(*m_skeleton);
}

void SkeletalMeshInstance::SetPose(const Pose& pose) {
    std::lock_guard<std::mutex> lock(m_poseMutex);
    m_currentPose = pose;
}

Pose SkeletalMeshInstance::GetPose() const {
    std::lock_guard<std::mutex> lock(m_poseMutex);
    return m_currentPose;
}

// ─────────────────────────────────────────────────────────────────────────────
// Palette compute — walks bone hierarchy in topological order
// ─────────────────────────────────────────────────────────────────────────────

void SkeletalMeshInstance::UpdatePaletteFromPose() {
    if (!m_skeleton) return;

    const uint32_t boneCount = m_skeleton->GetBoneCount();
    if (m_palette.size() != boneCount) {
        m_palette.assign(boneCount, Mat4::Identity());
    }

    // Snapshot the pose under the lock to avoid partial reads if SetPose races.
    Pose pose;
    {
        std::lock_guard<std::mutex> lock(m_poseMutex);
        pose = m_currentPose;
    }

    // Fall back to bind pose if the pose is empty or sized differently.
    if (pose.GetBoneCount() != boneCount) {
        pose = Pose::FromBindPose(*m_skeleton);
    }

    // Compute world matrix of each bone in topological order (parents already
    // done when the child is processed). Row-vector convention:
    //     world[i] = local[i] * world[parent[i]].
    // Then palette[i] = inverseBind[i] * world[i], so the shader can skin via
    //     skinnedPoint = bindPoint * palette[i].
    std::vector<Mat4> worldMatrices(boneCount, Mat4::Identity());

    for (uint32_t i = 0; i < boneCount; ++i) {
        const BoneInfo& bone = m_skeleton->GetBone(i);
        const Mat4 local = pose.localTransforms[i].ToMatrix();

        if (bone.parentIndex < 0) {
            worldMatrices[i] = local;
        } else {
            worldMatrices[i] = local * worldMatrices[static_cast<size_t>(bone.parentIndex)];
        }

        m_palette[i] = bone.inverseBindWorld * worldMatrices[i];
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// CPU skinning — applies LBS to every source vertex, writes Vertex[]
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Adds `scale * src` into `dst` element-wise. Row-major storage.
inline void MatAddScaled(Mat4& dst, const Mat4& src, float scale) {
    float* d = dst.data();
    const float* s = src.data();
    for (int k = 0; k < 16; ++k) d[k] += s[k] * scale;
}

/// Transforms a point `p` by matrix `m` using row-vector convention:
///     out = p * m   (homogeneous w=1).
inline Vec3 MulRowPoint(const Vec3& p, const Mat4& m) {
    return Vec3{
        p.x * m(0, 0) + p.y * m(1, 0) + p.z * m(2, 0) + m(3, 0),
        p.x * m(0, 1) + p.y * m(1, 1) + p.z * m(2, 1) + m(3, 1),
        p.x * m(0, 2) + p.y * m(1, 2) + p.z * m(2, 2) + m(3, 2)};
}

/// Transforms a direction vector `d` by matrix `m` using row-vector convention,
/// ignoring the translation row.
inline Vec3 MulRowDir(const Vec3& d, const Mat4& m) {
    return Vec3{
        d.x * m(0, 0) + d.y * m(1, 0) + d.z * m(2, 0),
        d.x * m(0, 1) + d.y * m(1, 1) + d.z * m(2, 1),
        d.x * m(0, 2) + d.y * m(1, 2) + d.z * m(2, 2)};
}

} // namespace

void SkeletalMeshInstance::SkinCpu() {
    if (!m_sourceMesh) return;
    if (m_sourceMesh->skinnedVertexData.empty()) {
        // Static mesh — nothing to skin; keep the bind-pose copy.
        return;
    }

    // TEMP: bypass CPU skinning while the bind-pose palette identity bug
    // and associated math are still under investigation. `Bind()` primes
    // `m_outputVertexData` with the 48 B bind-pose stream, and the dirty-
    // flag gate in UploadAll ensures that copy is uploaded exactly once.
    // This keeps the mesh visible in its correct rest pose and preserves
    // interactive framerate while the skinning rig is still wrong. Revert
    // once UpdatePaletteFromPose produces an identity palette at bind.
    return;

    const uint32_t vertexCount = m_sourceMesh->GetVertexCount();
    const size_t srcStride = sizeof(SkinnedVertex);
    const size_t dstStride = sizeof(Vertex);

    if (m_outputVertexData.size() != vertexCount * dstStride) {
        m_outputVertexData.assign(static_cast<size_t>(vertexCount) * dstStride, 0);
    }
    if (m_palette.empty()) {
        // No palette yet — copy bind pose as a fallback.
        const size_t bytes = std::min(m_outputVertexData.size(), m_sourceMesh->vertexData.size());
        std::memcpy(m_outputVertexData.data(), m_sourceMesh->vertexData.data(), bytes);
        m_outputDirty = true;
        return;
    }

    const uint8_t* srcBase = m_sourceMesh->skinnedVertexData.data();
    uint8_t*       dstBase = m_outputVertexData.data();
    const uint32_t paletteSize = static_cast<uint32_t>(m_palette.size());

    const float inv255 = 1.0f / 255.0f;

    for (uint32_t i = 0; i < vertexCount; ++i) {
        const SkinnedVertex* sv = reinterpret_cast<const SkinnedVertex*>(srcBase + i * srcStride);
        Vertex*              dv = reinterpret_cast<Vertex*>(dstBase + i * dstStride);

        const float w0 = static_cast<float>(sv->boneWeights[0]) * inv255;
        const float w1 = static_cast<float>(sv->boneWeights[1]) * inv255;
        const float w2 = static_cast<float>(sv->boneWeights[2]) * inv255;
        const float w3 = static_cast<float>(sv->boneWeights[3]) * inv255;

        auto paletteAt = [&](uint8_t idx) -> const Mat4& {
            const uint32_t uidx = std::min<uint32_t>(idx, paletteSize - 1);
            return m_palette[uidx];
        };

        // Blend four bone matrices element-wise (linear blend skinning).
        // Vertices that were exported with zero influences (decorative static
        // geometry attached to a skinned mesh) must stay in their bind-pose
        // position — otherwise the `w0 + w1 + w2 + w3 = 0` case produces a
        // zero skin matrix and the vertex collapses to the origin.
        const float weightSum = w0 + w1 + w2 + w3;
        if (weightSum <= 1e-5f) {
            dv->position = sv->position;
            dv->normal   = sv->normal;
            dv->tangent  = sv->tangent;
            dv->texcoord = sv->texcoord;
            continue;
        }

        Mat4 skin = Mat4::Zero();
        if (w0 > 0.0f) MatAddScaled(skin, paletteAt(sv->boneIndices[0]), w0);
        if (w1 > 0.0f) MatAddScaled(skin, paletteAt(sv->boneIndices[1]), w1);
        if (w2 > 0.0f) MatAddScaled(skin, paletteAt(sv->boneIndices[2]), w2);
        if (w3 > 0.0f) MatAddScaled(skin, paletteAt(sv->boneIndices[3]), w3);

        dv->position = MulRowPoint(sv->position, skin);

        const Vec3 sn = sv->normal;
        const Vec3 skinnedNormal = MulRowDir(sn, skin);
        const float nLen = std::sqrt(skinnedNormal.x * skinnedNormal.x +
                                     skinnedNormal.y * skinnedNormal.y +
                                     skinnedNormal.z * skinnedNormal.z);
        dv->normal = (nLen > 1e-6f)
            ? Vec3{skinnedNormal.x / nLen, skinnedNormal.y / nLen, skinnedNormal.z / nLen}
            : sn;

        const Vec3 st{sv->tangent.x, sv->tangent.y, sv->tangent.z};
        const Vec3 skinnedTangent = MulRowDir(st, skin);
        const float tLen = std::sqrt(skinnedTangent.x * skinnedTangent.x +
                                     skinnedTangent.y * skinnedTangent.y +
                                     skinnedTangent.z * skinnedTangent.z);
        dv->tangent = (tLen > 1e-6f)
            ? Vec4{skinnedTangent.x / tLen, skinnedTangent.y / tLen, skinnedTangent.z / tLen, sv->tangent.w}
            : sv->tangent;

        dv->texcoord = sv->texcoord;
    }

    // Every vertex in m_outputVertexData was rewritten — mark for GPU re-upload.
    m_outputDirty = true;
}

} // namespace uz
