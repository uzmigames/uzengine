/// UzEngine Skeleton — Shared bone hierarchy asset for skeletal meshes.
///
/// A Skeleton defines a tree of bones with bind-pose transforms and precomputed
/// inverse bind matrices. One skeleton can be shared across multiple meshes and
/// animation clips. Joint matching between assets is done by bone name, not index.
///
/// # Limits
/// Maximum 256 bones per skeleton (sufficient for AAA characters; GPU skinning
/// constant buffers typically cap at 256 matrices).
///
/// # Lifecycle
/// 1. Add bones via `AddBone()` (parent must be added before children).
/// 2. Call `BuildNameIndex()` to enable name-based lookup.
/// 3. Call `ComputeInverseBindMatrices()` to precompute GPU skinning data.
///
/// # Thread Safety
/// Immutable after construction. Safe to share across threads once built.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "bone_transform.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum bones per skeleton. Matches typical GPU constant buffer limits
/// for bone matrices (256 x Mat4 = 16 KB, well within 64 KB cbuffer limit).
static constexpr uint32_t kMaxBones = 256;

/// Sentinel value indicating a root bone (no parent).
static constexpr int32_t kInvalidBoneIndex = -1;

// ─────────────────────────────────────────────────────────────────────────────
// BoneInfo — Per-bone metadata and bind pose
// ─────────────────────────────────────────────────────────────────────────────

/// Metadata and bind-pose data for a single bone in a skeleton.
///
/// Stores the bone's name, parent index, local bind-pose transform, and the
/// precomputed inverse bind-world matrix used for GPU skinning.
///
/// # Invariants
/// * `parentIndex` is either `kInvalidBoneIndex` (-1) for root bones, or a valid
///   index less than the bone's own index (parents are always added first).
/// * `inverseBindWorld` is only valid after `Skeleton::ComputeInverseBindMatrices()`.
struct BoneInfo {
    /// Human-readable bone name (e.g., "spine_01", "hand_l").
    /// Used for cross-asset joint matching between skeletons and animations.
    std::string name;

    /// Index of the parent bone, or kInvalidBoneIndex (-1) for root bones.
    int32_t parentIndex = kInvalidBoneIndex;

    /// Bone-local bind-pose translation (offset from parent in bind pose).
    Vec3 bindTranslation{0.0f, 0.0f, 0.0f};

    /// Bone-local bind-pose rotation (orientation relative to parent in bind pose).
    Quat bindRotation{0.0f, 0.0f, 0.0f, 1.0f};

    /// Bone-local bind-pose scale.
    Vec3 bindScale{1.0f, 1.0f, 1.0f};

    /// Inverse of the bone's world-space bind-pose matrix.
    ///
    /// Used in GPU skinning: `skinMatrix = boneWorldCurrent * inverseBindWorld`.
    /// This transforms a vertex from model space to bone-local space at bind time,
    /// so that the current bone world matrix can then move it to the animated position.
    ///
    /// Only valid after `Skeleton::ComputeInverseBindMatrices()` has been called.
    Mat4 inverseBindWorld = Mat4::Identity();

    /// Returns the bone-local bind-pose as a BoneTransform.
    [[nodiscard]] BoneTransform GetBindTransform() const {
        return BoneTransform{bindTranslation, bindRotation, bindScale};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Skeleton — Bone hierarchy + bind pose
// ─────────────────────────────────────────────────────────────────────────────

/// Shared bone hierarchy asset for skeletal meshes and animation.
///
/// Owns a flat array of bones where each bone's parent index refers to an earlier
/// entry (topological order). Provides name-based bone lookup for cross-asset
/// joint matching, and precomputes inverse bind matrices for GPU skinning.
///
/// # Usage
/// ```cpp
/// Skeleton skel;
/// BoneInfo root;
/// root.name = "root";
/// root.parentIndex = kInvalidBoneIndex;
/// skel.AddBone(std::move(root));
///
/// BoneInfo spine;
/// spine.name = "spine_01";
/// spine.parentIndex = 0;
/// spine.bindTranslation = {0, 0, 50};
/// skel.AddBone(std::move(spine));
///
/// skel.BuildNameIndex();
/// skel.ComputeInverseBindMatrices();
///
/// int32_t idx = skel.FindBoneIndex("spine_01"); // 1
/// const Mat4& ibm = skel.GetBone(1).inverseBindWorld;
/// ```
class Skeleton {
public:
    Skeleton() = default;
    ~Skeleton() = default;

    // Move semantics — skeletons are typically loaded once and shared.
    Skeleton(Skeleton&&) noexcept = default;
    Skeleton& operator=(Skeleton&&) noexcept = default;

    // Copying is allowed (e.g., for editor duplication) but explicit.
    Skeleton(const Skeleton&) = default;
    Skeleton& operator=(const Skeleton&) = default;

    // ─── Construction ────────────────────────────────────────────────────

    /// Adds a bone to the skeleton.
    ///
    /// Bones must be added in topological order: a bone's parent must already
    /// exist in the skeleton before the child is added. Root bones have
    /// `parentIndex == kInvalidBoneIndex`.
    ///
    /// # Arguments
    /// * `bone` — Bone info to add (moved into internal storage).
    ///
    /// # Returns
    /// `true` if the bone was added successfully, `false` if:
    /// * The skeleton is at capacity (kMaxBones).
    /// * The parent index is invalid (out of range or not yet added).
    /// * The bone name is empty.
    [[nodiscard]] bool AddBone(BoneInfo bone);

    /// Builds the name-to-index lookup table.
    ///
    /// Must be called after all bones are added and before `FindBoneIndex()`.
    /// Calling this multiple times is safe (rebuilds from scratch).
    void BuildNameIndex();

    /// Computes inverse bind-world matrices for all bones.
    ///
    /// Walks the bone hierarchy from roots to leaves, computing each bone's
    /// world-space bind matrix by composing local transforms up the parent chain.
    /// Then inverts each world matrix to produce the inverse bind matrix used
    /// for GPU skinning.
    ///
    /// Must be called after all bones are added. Safe to call multiple times
    /// (recomputes from scratch).
    void ComputeInverseBindMatrices();

    // ─── Queries ─────────────────────────────────────────────────────────

    /// Finds a bone index by name.
    ///
    /// Requires `BuildNameIndex()` to have been called first.
    ///
    /// # Arguments
    /// * `name` — Bone name to search for (case-sensitive).
    ///
    /// # Returns
    /// Bone index if found, `kInvalidBoneIndex` (-1) if not found or name index not built.
    [[nodiscard]] int32_t FindBoneIndex(const std::string& name) const;

    /// Returns the total number of bones in this skeleton.
    [[nodiscard]] uint32_t GetBoneCount() const { return static_cast<uint32_t>(m_bones.size()); }

    /// Returns the bone at the given index.
    ///
    /// # Arguments
    /// * `index` — Bone index (must be < GetBoneCount()).
    ///
    /// # Panics
    /// Asserts if index is out of range.
    [[nodiscard]] const BoneInfo& GetBone(uint32_t index) const;

    /// Returns a mutable reference to the bone at the given index.
    ///
    /// # Arguments
    /// * `index` — Bone index (must be < GetBoneCount()).
    ///
    /// # Panics
    /// Asserts if index is out of range.
    [[nodiscard]] BoneInfo& GetBoneMut(uint32_t index);

    /// Returns read-only access to the full bone array.
    ///
    /// Bones are stored in topological order (parents before children).
    [[nodiscard]] const std::vector<BoneInfo>& GetBones() const { return m_bones; }

    /// Returns true if the name-to-index lookup has been built.
    [[nodiscard]] bool IsNameIndexBuilt() const { return m_nameIndexBuilt; }

    /// Returns true if inverse bind matrices have been computed.
    [[nodiscard]] bool AreInverseBindMatricesComputed() const { return m_inverseBindComputed; }

    /// Returns the index of the first root bone (parentIndex == kInvalidBoneIndex).
    ///
    /// # Returns
    /// Root bone index, or kInvalidBoneIndex if the skeleton is empty.
    [[nodiscard]] int32_t GetRootBoneIndex() const;

    /// Returns indices of all root bones (bones with no parent).
    ///
    /// Most skeletons have a single root, but multiple roots are supported
    /// (e.g., separate body and weapon hierarchies in a single skeleton).
    [[nodiscard]] std::vector<int32_t> GetRootBoneIndices() const;

    /// Returns indices of all direct children of a given bone.
    ///
    /// # Arguments
    /// * `parentIdx` — Index of the parent bone.
    ///
    /// # Returns
    /// Vector of child bone indices (may be empty for leaf bones).
    [[nodiscard]] std::vector<int32_t> GetChildBoneIndices(int32_t parentIdx) const;

private:
    /// Flat array of bones in topological order (parents before children).
    std::vector<BoneInfo> m_bones;

    /// Name-to-index lookup table. Built by BuildNameIndex().
    std::unordered_map<std::string, int32_t> m_nameIndex;

    /// Whether BuildNameIndex() has been called.
    bool m_nameIndexBuilt = false;

    /// Whether ComputeInverseBindMatrices() has been called.
    bool m_inverseBindComputed = false;
};

} // namespace uz
