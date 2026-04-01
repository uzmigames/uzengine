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

    // Temporary array of world-space bind matrices.
    // Bones are in topological order (parents before children), so a single
    // forward pass computes all world transforms without recursion.
    std::vector<Mat4> worldBindMatrices(boneCount);

    for (uint32_t i = 0; i < boneCount; ++i) {
        auto& bone = m_bones[i];

        // Compute this bone's local bind matrix from its TRS components.
        const Mat4 localBind = Mat4::TRS(bone.bindTranslation, bone.bindRotation, bone.bindScale);

        if (bone.parentIndex == kInvalidBoneIndex) {
            // Root bone: local = world.
            worldBindMatrices[i] = localBind;
        } else {
            // Child bone: world = local * parentWorld.
            // DirectX row-vector convention: vertex * local * parent = vertex * world.
            worldBindMatrices[i] = localBind * worldBindMatrices[static_cast<uint32_t>(bone.parentIndex)];
        }

        // Inverse bind matrix: transforms from model space to bone-local bind space.
        bone.inverseBindWorld = worldBindMatrices[i].inverse();
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
