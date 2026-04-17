/// UzEngine Skeleton — Implementation of bone hierarchy and inverse bind matrix computation.

#include "skeleton.h"

#include "engine_assert.h"
#include "log.h"

UZ_DEFINE_LOG_CATEGORY(Animation, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

bool Skeleton::AddBone(BoneInfo bone) {
    // Validate capacity.
    if (m_bones.size() >= kMaxBones) {
        UZ_LOG(Animation, Error, "Skeleton::AddBone — cannot add bone '{}': at capacity ({} bones)",
               bone.name, kMaxBones);
        return false;
    }

    // Validate bone name is not empty.
    if (bone.name.empty()) {
        UZ_LOG(Animation, Error, "Skeleton::AddBone — bone name must not be empty");
        return false;
    }

    // Validate parent index.
    if (bone.parentIndex != kInvalidBoneIndex) {
        if (bone.parentIndex < 0 ||
            static_cast<uint32_t>(bone.parentIndex) >= static_cast<uint32_t>(m_bones.size())) {
            UZ_LOG(Animation, Error,
                   "Skeleton::AddBone — bone '{}' has invalid parentIndex {} (skeleton has {} bones)",
                   bone.name, bone.parentIndex, m_bones.size());
            return false;
        }
    }

    m_bones.push_back(std::move(bone));

    // Invalidate cached state — caller must rebuild after adding all bones.
    m_nameIndexBuilt = false;
    m_inverseBindComputed = false;

    return true;
}

void Skeleton::BuildNameIndex() {
    m_nameIndex.clear();
    m_nameIndex.reserve(m_bones.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(m_bones.size()); ++i) {
        const auto& name = m_bones[i].name;
        auto [it, inserted] = m_nameIndex.emplace(name, static_cast<int32_t>(i));
        if (!inserted) {
            UZ_LOG(Animation, Warning,
                   "Skeleton::BuildNameIndex — duplicate bone name '{}' at index {} (first at index {})",
                   name, i, it->second);
        }
    }

    m_nameIndexBuilt = true;
}

void Skeleton::ComputeInverseBindMatrices() {
    const uint32_t boneCount = static_cast<uint32_t>(m_bones.size());
    if (boneCount == 0) {
        m_inverseBindComputed = true;
        return;
    }

    // Walk the bone hierarchy via TRS composition (BoneTransform::ComposeWith)
    // and only flatten to a 4x4 matrix at the leaves. The FBX importer
    // (`ComputeLocalFromWorldTRS`) decomposed every bone with the inverse of
    // this exact composition, so reconstructing the world transform here with
    // anything other than the same TRS chain produces a different matrix
    // whenever a bone has non-uniform scale (FBX rigs frequently bake non-
    // trivial scale into bones via axis conversion / pre-/post-rotation).
    // Mismatched composition = correct bind pose (the same algorithm runs on
    // both sides of `palette = inverseBind * world` so they cancel) but
    // incorrect animated poses.
    //
    // This mirrors UE5's `FillUpComponentSpaceTransforms`
    // (Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:2683-2701)
    // which composes `FTransform * FTransform` and only converts to FMatrix
    // once per bone, when finally writing into the GPU palette.
    std::vector<BoneTransform> worldBindTRS(boneCount);

    for (uint32_t i = 0; i < boneCount; ++i) {
        auto& bone = m_bones[i];
        const BoneTransform localBind = bone.GetBindTransform();

        if (bone.parentIndex == kInvalidBoneIndex) {
            worldBindTRS[i] = localBind;
        } else {
            worldBindTRS[i] = localBind.ComposeWith(
                worldBindTRS[static_cast<uint32_t>(bone.parentIndex)]);
        }

        // Flatten the world TRS to a matrix and invert. The result is the
        // inverse of the bind-pose component-space matrix — the standard
        // skinning "inverse bind" matrix despite the misleading "World"
        // suffix on the field name.
        const Mat4 worldBindMatrix = Mat4::TRS(
            worldBindTRS[i].position, worldBindTRS[i].rotation, worldBindTRS[i].scale);
        bone.inverseBindWorld = worldBindMatrix.inverse();
    }

    m_inverseBindComputed = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

int32_t Skeleton::FindBoneIndex(const std::string& name) const {
    if (!m_nameIndexBuilt) {
        UZ_LOG(Animation, Warning,
               "Skeleton::FindBoneIndex — name index not built, call BuildNameIndex() first");
        return kInvalidBoneIndex;
    }

    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end()) {
        return it->second;
    }
    return kInvalidBoneIndex;
}

const BoneInfo& Skeleton::GetBone(uint32_t index) const {
    UZ_CHECK(index < static_cast<uint32_t>(m_bones.size()));
    return m_bones[index];
}

BoneInfo& Skeleton::GetBoneMut(uint32_t index) {
    UZ_CHECK(index < static_cast<uint32_t>(m_bones.size()));
    return m_bones[index];
}

int32_t Skeleton::GetRootBoneIndex() const {
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_bones.size()); ++i) {
        if (m_bones[i].parentIndex == kInvalidBoneIndex) {
            return static_cast<int32_t>(i);
        }
    }
    return kInvalidBoneIndex;
}

std::vector<int32_t> Skeleton::GetRootBoneIndices() const {
    std::vector<int32_t> roots;
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_bones.size()); ++i) {
        if (m_bones[i].parentIndex == kInvalidBoneIndex) {
            roots.push_back(static_cast<int32_t>(i));
        }
    }
    return roots;
}

std::vector<int32_t> Skeleton::GetChildBoneIndices(int32_t parentIdx) const {
    std::vector<int32_t> children;
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_bones.size()); ++i) {
        if (m_bones[i].parentIndex == parentIdx) {
            children.push_back(static_cast<int32_t>(i));
        }
    }
    return children;
}

} // namespace uz
