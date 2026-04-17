<!-- doc-meta
title: UzEngine Skeletal Animation Implementation Guide
description: Concrete C++23 + Zig implementation patterns for skeleton, skeletal mesh, and animation
date: 2026-03-28
status: reference
-->

# UzEngine Skeletal Animation Implementation: C++23 Patterns

## Overview

This guide translates UE5's skeletal architecture into UzEngine's tech stack (C++23, Zig, EnTT ECS, Z-up).

**Key principles:**
- Names, not IDs (enables retargeting)
- ECS-based components (not monolithic scenes)
- GPU vertex shader skinning (not compute)
- JSON asset format (not binary)

---

## 1. Skeleton: Core Data Structure

### skeleton.h

```cpp
#pragma once

#include "uz/math/transform.h"
#include "uz/math/matrix.h"
#include <vector>
#include <string>
#include <string_view>
#include <optional>

namespace uz::animation {

/// A single joint in the skeleton hierarchy.
struct Joint {
    uint32_t id = 0;                   ///< unique within skeleton
    std::string name;                  ///< "Armature|Spine", "Armature|Leg_L"

    uint32_t parent_id = UINT32_MAX;   ///< parent joint (UINT32_MAX = root)
    std::vector<uint32_t> child_ids;   ///< children

    // ─── Bind Pose (rest position) ───────────────────────────────
    /// Local position relative to parent
    Vec3 local_position = {0, 0, 0};

    /// Local rotation relative to parent
    Quat local_rotation = {0, 0, 0, 1};

    /// Local scale relative to parent
    Vec3 local_scale = {1, 1, 1};

    // ─── Precomputed Matrices ───────────────────────────────────
    /// World space transform of bind pose (computed at load)
    Mat4 bind_matrix;

    /// Transform from world space back to bind pose local space
    /// Used in skinning: vertex_final = Σ(weight * inv_bind[i] * joint_world * vertex_local)
    Mat4 inverse_bind_matrix;

    // ─── Utility Methods ───────────────────────────────────────
    /// Returns local transform as a Transform
    Transform GetLocalTransform() const {
        return Transform{
            .position = local_position,
            .rotation = local_rotation,
            .scale = local_scale,
        };
    }
};

/// Complete skeleton: joint hierarchy + metadata.
class Skeleton {
public:
    Skeleton() = default;
    explicit Skeleton(std::string name) : name_(name) {}

    // ─── Skeleton Metadata ───────────────────────────────────────
    std::string GetName() const { return name_; }

    // ─── Joint Access ───────────────────────────────────────────
    /// Get total joint count
    uint32_t GetJointCount() const { return joints_.size(); }

    /// Get joint by ID (bounds-checked)
    const Joint* GetJoint(uint32_t id) const {
        if (id >= joints_.size()) return nullptr;
        return &joints_[id];
    }

    /// Get joint by name (O(n) linear search)
    const Joint* GetJointByName(std::string_view name) const {
        for (const auto& joint : joints_) {
            if (joint.name == name) return &joint;
        }
        return nullptr;
    }

    /// Get joint ID by name
    std::optional<uint32_t> FindJointID(std::string_view name) const {
        for (uint32_t i = 0; i < joints_.size(); ++i) {
            if (joints_[i].name == name) return i;
        }
        return std::nullopt;
    }

    /// Get joint (mutable) — for loading only
    Joint* GetJointMutable(uint32_t id) {
        if (id >= joints_.size()) return nullptr;
        return &joints_[id];
    }

    /// Add a joint to the skeleton (called during loading)
    uint32_t AddJoint(const std::string& name, uint32_t parent_id = UINT32_MAX) {
        uint32_t id = joints_.size();
        Joint joint;
        joint.id = id;
        joint.name = name;
        joint.parent_id = parent_id;
        joints_.push_back(joint);
        return id;
    }

    // ─── Hierarchy Traversal ───────────────────────────────────
    /// Get root joint (parent_id == UINT32_MAX)
    const Joint* GetRoot() const {
        for (const auto& joint : joints_) {
            if (joint.parent_id == UINT32_MAX) return &joint;
        }
        return nullptr;
    }

    /// Check if joint A is ancestor of joint B
    bool IsAncestor(uint32_t ancestor_id, uint32_t descendant_id) const;

    // ─── Pose Computation ───────────────────────────────────────
    /// Compute all joint world matrices from local poses.
    /// Returns std::vector<Mat4> where [i] = world transform of joint i
    std::vector<Mat4> ComputeWorldMatrices(
        const std::vector<Transform>& local_poses) const;

    /// Get bind pose as local transforms
    std::vector<Transform> GetBindPose() const;

    // ─── Validation ──────────────────────────────────────────────
    /// Validate skeleton integrity:
    /// - No cycles
    /// - All parent references valid
    /// - All inverse bind matrices computed
    /// Returns error message if invalid, empty string if OK
    std::string Validate() const;

private:
    std::string name_;
    std::vector<Joint> joints_;

    /// Build hierarchy from flat joint array (called after loading)
    void BuildHierarchy();

    /// Compute all bind/inverse bind matrices (called after loading)
    void ComputeBindMatrices();
};

}  // namespace uz::animation
```

### skeleton.cpp

```cpp
#include "skeleton.h"
#include "uz/core/log.h"

namespace uz::animation {

std::vector<Mat4> Skeleton::ComputeWorldMatrices(
    const std::vector<Transform>& local_poses) const {
    UZ_CHECK(local_poses.size() == joints_.size(),
             "pose array size {} != joint count {}",
             local_poses.size(), joints_.size());

    std::vector<Mat4> world_matrices(joints_.size());

    for (uint32_t i = 0; i < joints_.size(); ++i) {
        const Joint& joint = joints_[i];
        Mat4 local_matrix = local_poses[i].ToMatrix();

        if (joint.parent_id == UINT32_MAX) {
            // Root joint — world space = local space
            world_matrices[i] = local_matrix;
        } else {
            // Child — world = parent_world * local
            UZ_CHECK(joint.parent_id < i,
                     "Parent {} must come before child {} in joint array",
                     joint.parent_id, i);
            world_matrices[i] = world_matrices[joint.parent_id] * local_matrix;
        }
    }

    return world_matrices;
}

std::vector<Transform> Skeleton::GetBindPose() const {
    std::vector<Transform> out;
    out.reserve(joints_.size());

    for (const auto& joint : joints_) {
        out.push_back(joint.GetLocalTransform());
    }

    return out;
}

std::string Skeleton::Validate() const {
    // Check all parent references are valid
    for (uint32_t i = 0; i < joints_.size(); ++i) {
        const Joint& joint = joints_[i];
        if (joint.parent_id != UINT32_MAX && joint.parent_id >= joints_.size()) {
            return fmt::format(
                "Invalid parent_id {} in joint '{}' (skeleton has {} joints)",
                joint.parent_id, joint.name, joints_.size());
        }
    }

    // Check for cycles (depth-first from all roots)
    std::vector<bool> visited(joints_.size(), false);
    std::vector<bool> in_progress(joints_.size(), false);

    std::function<bool(uint32_t)> has_cycle = [&](uint32_t id) -> bool {
        visited[id] = true;
        in_progress[id] = true;

        for (uint32_t child_id : joints_[id].child_ids) {
            if (!visited[child_id]) {
                if (has_cycle(child_id)) return true;
            } else if (in_progress[child_id]) {
                // Back-edge = cycle
                return true;
            }
        }

        in_progress[id] = false;
        return false;
    };

    for (uint32_t i = 0; i < joints_.size(); ++i) {
        if (!visited[i] && joints_[i].parent_id == UINT32_MAX) {
            if (has_cycle(i)) {
                return fmt::format("Cycle detected in skeleton at joint '{}'",
                                   joints_[i].name);
            }
        }
    }

    // Check all inverse bind matrices computed
    for (uint32_t i = 0; i < joints_.size(); ++i) {
        if (joints_[i].inverse_bind_matrix == Mat4::Identity()) {
            UZ_LOG(Warning, "Joint {} has uninitialized inverse_bind_matrix",
                   joints_[i].name);
        }
    }

    return "";  // Valid
}

void Skeleton::BuildHierarchy() {
    // Build child_ids from parent_id references
    for (uint32_t i = 0; i < joints_.size(); ++i) {
        uint32_t parent_id = joints_[i].parent_id;
        if (parent_id != UINT32_MAX) {
            joints_[parent_id].child_ids.push_back(i);
        }
    }
}

void Skeleton::ComputeBindMatrices() {
    auto bind_pose = GetBindPose();
    auto world_matrices = ComputeWorldMatrices(bind_pose);

    for (uint32_t i = 0; i < joints_.size(); ++i) {
        joints_[i].bind_matrix = world_matrices[i];
        joints_[i].inverse_bind_matrix = world_matrices[i].Inverse();
    }
}

bool Skeleton::IsAncestor(uint32_t ancestor_id, uint32_t descendant_id) const {
    uint32_t current = descendant_id;
    while (current != UINT32_MAX) {
        if (current == ancestor_id) return true;
        current = joints_[current].parent_id;
    }
    return false;
}

}  // namespace uz::animation
```

---

## 2. Animation Sequence: Keyframe Data

### animation_sequence.h

```cpp
#pragma once

#include "skeleton.h"
#include "uz/math/transform.h"
#include "uz/assets/asset.h"
#include <vector>
#include <string>
#include <map>

namespace uz::animation {

/// A single animation track for one joint.
struct AnimationTrack {
    std::string bone_name;  ///< must match joint name in skeleton

    /// Position keyframes
    std::vector<Vec3> translation_keys;

    /// Rotation keyframes
    std::vector<Quat> rotation_keys;

    /// Scale keyframes
    std::vector<Vec3> scale_keys;

    /// Frame times (seconds)
    std::vector<float> times;

    /// Sample this track at time t (seconds)
    /// Returns Transform in local space relative to parent joint
    Transform Sample(float time) const;
};

/// A complete animation sequence: playable animation targeting a specific skeleton.
class AnimationSequence : public IAsset {
public:
    AnimationSequence() = default;
    explicit AnimationSequence(ResourceHandle<Skeleton> skeleton)
        : skeleton_(skeleton) {}

    // ─── Asset Interface ───────────────────────────────────────
    static constexpr std::string_view AssetType = "uzanim";
    std::string_view GetAssetType() const override { return AssetType; }

    // ─── Skeleton & Duration ───────────────────────────────────
    ResourceHandle<Skeleton> GetSkeleton() const { return skeleton_; }
    float GetDuration() const { return duration_; }
    float GetSamplingRate() const { return sampling_rate_; }

    // ─── Track Management ───────────────────────────────────────
    const AnimationTrack* GetTrack(std::string_view bone_name) const {
        auto it = tracks_.find(std::string(bone_name));
        if (it != tracks_.end()) return &it->second;
        return nullptr;
    }

    AnimationTrack* GetTrackMutable(std::string_view bone_name) {
        auto it = tracks_.find(std::string(bone_name));
        if (it != tracks_.end()) return &it->second;
        return nullptr;
    }

    void AddTrack(std::string bone_name, AnimationTrack track) {
        tracks_[bone_name] = track;
    }

    // ─── Sampling ───────────────────────────────────────────────
    /// Sample all tracks at time t, return pose as local transforms
    /// Returns std::map<joint_name, Transform>
    std::map<std::string, Transform> Sample(float time) const;

    /// Sample all tracks, return as array aligned to skeleton joint order
    /// Returns std::vector<Transform> where [i] = pose of joint i
    std::vector<Transform> SampleAsArray(float time, const Skeleton& skeleton) const;

    // ─── Validation ──────────────────────────────────────────────
    /// Validate animation:
    /// - Skeleton exists
    /// - All track bone_names exist in skeleton
    /// - All keyframes have same number of keyframes
    std::string Validate() const;

private:
    ResourceHandle<Skeleton> skeleton_;
    std::map<std::string, AnimationTrack> tracks_;  // bone_name → track
    float duration_ = 0.0f;      ///< length in seconds
    float sampling_rate_ = 30.0f; ///< frames per second
};

}  // namespace uz::animation
```

### animation_sequence.cpp

```cpp
#include "animation_sequence.h"
#include "uz/core/log.h"
#include <algorithm>

namespace uz::animation {

Transform AnimationTrack::Sample(float time) const {
    // Clamp time to valid range
    float clamped_time = std::clamp(time, 0.0f, times.empty() ? 0.0f : times.back());

    if (times.empty()) {
        return Transform{};  // Default bind pose
    }

    // Find the two keyframes bracketing time
    auto it = std::lower_bound(times.begin(), times.end(), clamped_time);

    if (it == times.begin()) {
        // Before first frame
        return Transform{
            .position = translation_keys.empty() ? Vec3{0, 0, 0} : translation_keys[0],
            .rotation = rotation_keys.empty() ? Quat{0, 0, 0, 1} : rotation_keys[0],
            .scale = scale_keys.empty() ? Vec3{1, 1, 1} : scale_keys[0],
        };
    }

    if (it == times.end()) {
        // After last frame
        uint32_t last_idx = times.size() - 1;
        return Transform{
            .position = translation_keys.empty() ? Vec3{0, 0, 0} : translation_keys[last_idx],
            .rotation = rotation_keys.empty() ? Quat{0, 0, 0, 1} : rotation_keys[last_idx],
            .scale = scale_keys.empty() ? Vec3{1, 1, 1} : scale_keys[last_idx],
        };
    }

    // Interpolate between keyframes
    uint32_t next_idx = std::distance(times.begin(), it);
    uint32_t prev_idx = next_idx - 1;

    float t0 = times[prev_idx];
    float t1 = times[next_idx];
    float alpha = (clamped_time - t0) / (t1 - t0);  // 0-1

    return Transform{
        .position = Vec3::Lerp(translation_keys[prev_idx], translation_keys[next_idx], alpha),
        .rotation = Quat::Slerp(rotation_keys[prev_idx], rotation_keys[next_idx], alpha),
        .scale = Vec3::Lerp(scale_keys[prev_idx], scale_keys[next_idx], alpha),
    };
}

std::map<std::string, Transform> AnimationSequence::Sample(float time) const {
    std::map<std::string, Transform> result;

    for (const auto& [bone_name, track] : tracks_) {
        result[bone_name] = track.Sample(time);
    }

    return result;
}

std::vector<Transform> AnimationSequence::SampleAsArray(
    float time, const Skeleton& skeleton) const {
    std::vector<Transform> result(skeleton.GetJointCount());

    // Initialize with skeleton bind pose
    auto bind_pose = skeleton.GetBindPose();
    result = bind_pose;

    // Override with animation tracks
    for (const auto& [bone_name, track] : tracks_) {
        auto* joint = skeleton.GetJointByName(bone_name);
        if (!joint) {
            UZ_LOG(Warning, "Animation track '{}' not found in skeleton", bone_name);
            continue;
        }

        result[joint->id] = track.Sample(time);
    }

    return result;
}

std::string AnimationSequence::Validate() const {
    if (!skeleton_) {
        return "Animation skeleton reference is null";
    }

    // Check all track bone names exist in skeleton
    for (const auto& [bone_name, track] : tracks_) {
        if (!skeleton_->GetJointByName(bone_name)) {
            return fmt::format(
                "Animation track '{}' not found in skeleton '{}'",
                bone_name, skeleton_->GetName());
        }
    }

    // Warn if skeleton has bones without animation tracks
    for (uint32_t i = 0; i < skeleton_->GetJointCount(); ++i) {
        const Joint* joint = skeleton_->GetJoint(i);
        if (joint && tracks_.find(joint->name) == tracks_.end()) {
            UZ_LOG(Warning,
                   "Skeleton joint '{}' has no animation track (will use bind pose)",
                   joint->name);
        }
    }

    return "";  // Valid
}

}  // namespace uz::animation
```

---

## 3. Skeletal Mesh Component

### skeletal_mesh_component.h

```cpp
#pragma once

#include "skeleton.h"
#include "uz/engine/components/primitive_component.h"
#include "uz/assets/mesh.h"
#include "uz/assets/material.h"
#include <vector>

namespace uz::animation {

class AnimationController;

/// Renders a mesh deformed by a skeleton (skinned mesh).
class SkeletalMeshComponent : public PrimitiveComponent {
public:
    // ─── Assets ─────────────────────────────────────────────────
    ResourceHandle<Mesh> mesh;              ///< geometry + skinning data
    ResourceHandle<Skeleton> skeleton;      ///< joint hierarchy
    ResourceHandle<Material> material;      ///< rendering material

    // ─── Animation ───────────────────────────────────────────────
    AnimationController* animation_controller = nullptr;  ///< nullptr = bind pose

    // ─── Component Lifecycle ─────────────────────────────────────
    void OnStart() override;
    void OnUpdate(float delta_time) override;
    void OnDestroy() override;

    // ─── Skinning ───────────────────────────────────────────────
    /// Update GPU joint palette buffer from current animation pose.
    /// Called each frame by animation system.
    void UpdateSkinning();

    /// Get current joint world transforms (for attachments, effects, etc.)
    const std::vector<Mat4>& GetJointMatrices() const { return joint_matrices_; }

    /// Get bone transform in world space given joint name.
    /// Returns std::nullopt if bone not found.
    std::optional<Transform> GetBoneTransform(std::string_view bone_name) const;

    // ─── Morph Targets (Phase 4) ─────────────────────────────────
    void SetMorphTarget(std::string_view name, float weight);
    float GetMorphTarget(std::string_view name) const;

    // ─── Bone Attachments ────────────────────────────────────────
    /// Attach a component to follow a bone (swords in hands, glasses on head).
    void AttachToBone(std::string_view bone_name, SceneComponent* component);

    /// Remove bone attachment
    void DetachFromBone(std::string_view bone_name);

    /// Update all bone attachments (called each frame)
    void UpdateBoneAttachments();

private:
    // ─── Cached GPU Data ────────────────────────────────────────
    std::vector<Mat4> joint_matrices_;        ///< world space transforms
    GpuBuffer joint_palette_buffer_;          ///< GPU constant buffer with matrices

    // ─── Morph Targets ──────────────────────────────────────────
    std::map<std::string, float> morph_weights_;

    // ─── Bone Attachments ───────────────────────────────────────
    std::map<std::string, SceneComponent*> bone_attachments_;

    // ─── Validation ──────────────────────────────────────────────
    bool ValidateAssets();
};

}  // namespace uz::animation
```

### skeletal_mesh_component.cpp

```cpp
#include "skeletal_mesh_component.h"
#include "animation_controller.h"
#include "uz/core/log.h"

namespace uz::animation {

void SkeletalMeshComponent::OnStart() {
    if (!ValidateAssets()) {
        enabled = false;
        return;
    }

    joint_matrices_.resize(skeleton->GetJointCount());
}

void SkeletalMeshComponent::OnUpdate(float delta_time) {
    if (!enabled || !skeleton || !mesh) return;

    UpdateSkinning();
    UpdateBoneAttachments();
}

bool SkeletalMeshComponent::ValidateAssets() {
    if (!skeleton) {
        UZ_LOG(Error, "SkeletalMeshComponent has no skeleton");
        return false;
    }

    if (!mesh) {
        UZ_LOG(Error, "SkeletalMeshComponent has no mesh");
        return false;
    }

    // Verify mesh skeleton reference matches our skeleton
    // (this is a warning-level check; meshes can be retargeted)
    if (mesh->skeleton_ref && mesh->skeleton_ref != skeleton) {
        UZ_LOG(Warning,
               "SkeletalMeshComponent skeleton {} differs from mesh's ref skeleton {}. "
               "Proceeding (retargeting enabled).",
               skeleton->GetName(), mesh->skeleton_ref->GetName());
    }

    return true;
}

void SkeletalMeshComponent::UpdateSkinning() {
    if (!skeleton) return;

    std::vector<Transform> local_poses;

    if (animation_controller) {
        // Get current animation pose
        local_poses = animation_controller->GetLocalPoses(*skeleton);
    } else {
        // Use bind pose
        local_poses = skeleton->GetBindPose();
    }

    // Compute world matrices (local → parent → world)
    joint_matrices_ = skeleton->ComputeWorldMatrices(local_poses);

    // Upload to GPU
    joint_palette_buffer_.SetData(joint_matrices_.data(),
                                  joint_matrices_.size() * sizeof(Mat4),
                                  true);  // GPU_CONSTANT_BUFFER flag
}

std::optional<Transform> SkeletalMeshComponent::GetBoneTransform(
    std::string_view bone_name) const {
    const Joint* joint = skeleton->GetJointByName(bone_name);
    if (!joint) return std::nullopt;

    if (joint->id >= joint_matrices_.size()) return std::nullopt;

    Mat4 world_mat = joint_matrices_[joint->id];
    return Transform::FromMatrix(world_mat);
}

void SkeletalMeshComponent::SetMorphTarget(std::string_view name, float weight) {
    morph_weights_[std::string(name)] = std::clamp(weight, 0.0f, 1.0f);
    // GPU update happens in render pass
}

float SkeletalMeshComponent::GetMorphTarget(std::string_view name) const {
    auto it = morph_weights_.find(std::string(name));
    if (it == morph_weights_.end()) return 0.0f;
    return it->second;
}

void SkeletalMeshComponent::AttachToBone(std::string_view bone_name,
                                         SceneComponent* component) {
    if (!component) return;

    // Verify bone exists
    if (!skeleton->GetJointByName(bone_name)) {
        UZ_LOG(Error, "Cannot attach to bone '{}' — not found in skeleton", bone_name);
        return;
    }

    bone_attachments_[std::string(bone_name)] = component;
}

void SkeletalMeshComponent::DetachFromBone(std::string_view bone_name) {
    bone_attachments_.erase(std::string(bone_name));
}

void SkeletalMeshComponent::UpdateBoneAttachments() {
    for (const auto& [bone_name, component] : bone_attachments_) {
        auto bone_transform = GetBoneTransform(bone_name);
        if (bone_transform) {
            component->SetWorldTransform(*bone_transform);
        }
    }
}

}  // namespace uz::animation
```

---

## 4. JSON File Format: Skeleton

### Example: hero.uzskeleton

```json
{
  "format": "uzskeleton",
  "version": 1,
  "name": "humanoid_base",
  "joints": [
    {
      "id": 0,
      "name": "Armature",
      "parent_id": -1,
      "position": [0.0, 0.0, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    },
    {
      "id": 1,
      "name": "Armature|Hips",
      "parent_id": 0,
      "position": [0.0, 100.0, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    },
    {
      "id": 2,
      "name": "Armature|Hips|Spine",
      "parent_id": 1,
      "position": [0.0, 12.5, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    },
    {
      "id": 3,
      "name": "Armature|Hips|Spine|Chest",
      "parent_id": 2,
      "position": [0.0, 12.5, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    },
    {
      "id": 4,
      "name": "Armature|Hips|Spine|Chest|Shoulder.L",
      "parent_id": 3,
      "position": [-8.0, 8.0, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    },
    {
      "id": 5,
      "name": "Armature|Hips|Spine|Chest|Shoulder.L|Arm.L",
      "parent_id": 4,
      "position": [-15.0, 0.0, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    },
    {
      "id": 6,
      "name": "Armature|Hips|Spine|Chest|Shoulder.L|Arm.L|Forearm.L",
      "parent_id": 5,
      "position": [-15.0, 0.0, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    },
    {
      "id": 7,
      "name": "Armature|Hips|Spine|Chest|Shoulder.L|Arm.L|Forearm.L|Hand.L",
      "parent_id": 6,
      "position": [-12.0, 0.0, 0.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "scale": [1.0, 1.0, 1.0]
    }
  ]
}
```

### JSON Loader

```cpp
// In asset_manager.cpp
std::unique_ptr<Skeleton> LoadSkeletonFromJSON(const std::string& path) {
    nlohmann::json doc = nlohmann::json::parse(std::ifstream(path));

    auto skeleton = std::make_unique<Skeleton>(doc["name"]);

    for (const auto& joint_data : doc["joints"]) {
        uint32_t parent_id = joint_data["parent_id"];
        if (parent_id == UINT32_MAX) parent_id = -1;

        uint32_t id = skeleton->AddJoint(
            joint_data["name"],
            parent_id == UINT32_MAX ? UINT32_MAX : (uint32_t)parent_id);

        Joint* joint = skeleton->GetJointMutable(id);
        joint->local_position = Vec3{
            joint_data["position"][0],
            joint_data["position"][1],
            joint_data["position"][2]
        };
        joint->local_rotation = Quat{
            joint_data["rotation"][0],
            joint_data["rotation"][1],
            joint_data["rotation"][2],
            joint_data["rotation"][3]
        };
        joint->local_scale = Vec3{
            joint_data["scale"][0],
            joint_data["scale"][1],
            joint_data["scale"][2]
        };
    }

    // Validate
    std::string error = skeleton->Validate();
    if (!error.empty()) {
        UZ_LOG(Error, "Skeleton validation failed: {}", error);
        return nullptr;
    }

    return skeleton;
}
```

---

## 5. GPU Vertex Shader Skinning

### skinning_vertex.hlsl

```hlsl
/// Skeletal mesh vertex shader with GPU skinning
/// Transforms vertex by weighted joint matrices

cbuffer JointMatrices : register(b0) {
    float4x4 joint_matrices[128];  // up to 128 joints
};

cbuffer FrameParams : register(b1) {
    float4x4 view_projection;
    float3 camera_pos;
};

cbuffer MaterialParams : register(b2) {
    float4 base_color;
    float metallic;
    float roughness;
};

struct SkinnedVertex {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;

    // Skinning
    uint bone_ids[4] : BONE_IDS;      // 4 bones per vertex
    uint bone_weights[4] : BONE_WEIGHTS; // 0-255, normalized on GPU
};

struct VS_Output {
    float4 position : SV_POSITION;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float2 uv : TEXCOORD0;
};

VS_Output main(SkinnedVertex input) {
    VS_Output output;

    // ─── Apply Skinning ─────────────────────────────────────────
    float3 skinned_pos = float3(0, 0, 0);
    float3 skinned_normal = float3(0, 0, 0);

    for (int i = 0; i < 4; ++i) {
        // Normalize weight to 0-1
        float weight = float(input.bone_weights[i]) / 255.0f;

        if (weight > 0.001f) {
            int joint_idx = input.bone_ids[i];

            // Transform position
            float4 pos = mul(float4(input.position, 1.0f), joint_matrices[joint_idx]);
            skinned_pos += pos.xyz * weight;

            // Transform normal
            float3 normal = mul(input.normal, (float3x3)joint_matrices[joint_idx]);
            skinned_normal += normal * weight;
        }
    }

    // ─── Project to Screen ──────────────────────────────────────
    output.position = mul(float4(skinned_pos, 1.0f), view_projection);
    output.world_pos = skinned_pos;
    output.world_normal = normalize(skinned_normal);
    output.uv = input.uv;

    return output;
}
```

---

## 6. Implementation Rules & Anti-Patterns

### ✅ DO

1. **Use joint names, not IDs** — enables retargeting
   ```cpp
   struct AnimationTrack {
       std::string bone_name;  // "Spine", not joint_id
   };
   ```

2. **Validate on load** — catch bad skeletons early
   ```cpp
   std::string error = skeleton->Validate();
   UZ_CHECK(error.empty(), "Invalid skeleton: {}", error);
   ```

3. **Compute bind matrices once** — cache inverse bind for GPU
   ```cpp
   void Skeleton::ComputeBindMatrices() { /* once per load */ }
   ```

4. **Store joint hierarchy in order** — parent always before child
   ```cpp
   // Joint 0 = root (parent_id == UINT32_MAX)
   // Joint 1 = child of 0
   // Joint 2 = child of 1 or 0
   ```

5. **Use Z-up everywhere** — consistent with engine
   ```cpp
   // Position in centimeters, Z is up
   Vec3 local_position = {0, 0, 100};  // 1 meter up (Z-up)
   ```

### ❌ DON'T

1. **Hardcode joint IDs in animation data**
   ```cpp
   // WRONG: brittle to skeleton changes
   struct AnimationTrack {
       uint32_t joint_id;
   };
   ```

2. **Embed skeleton in mesh**
   ```cpp
   // WRONG: can't retarget or share
   struct SkeletalMesh {
       Skeleton skeleton;  // ← embedded
   };
   ```

3. **Store physics in skeleton**
   ```cpp
   // WRONG: conflates animation and physics
   struct Skeleton {
       std::vector<RigidBody> physics_bodies;
   };
   ```

4. **Allow skeleton mutation after load**
   ```cpp
   // WRONG: breaks inverse bind matrices
   skeleton->AddJoint(...);  // after loading
   ```

5. **Assume O(1) bone lookup**
   ```cpp
   // SLOW: linear search each frame
   Joint* joint = skeleton->GetJointByName("Spine");  // O(n)
   ```
   **Fix:** Cache joint IDs during loading, use array indexing at runtime

---

## 7. Testing Checklist (Phase 3)

- [ ] Skeleton loads from JSON without validation errors
- [ ] Skeleton detects cycles and malformed hierarchies
- [ ] Bind pose computation matches FBX import
- [ ] Animation samples correctly between keyframes
- [ ] Skeletal mesh renders with correct bind pose
- [ ] GPU joint palette uploads correctly
- [ ] Bone attachment follows moving bone
- [ ] Retargeting: same animation on two different meshes plays correctly
- [ ] Performance: 128-joint skeleton with 60 FPS on target hardware

---

## 8. Performance Targets

| Operation | Target | Notes |
|-----------|--------|-------|
| Skeleton load | < 10 ms | 200 joint skeleton from JSON |
| Animation sample | < 0.1 ms | Linear interpolation, 200 joints |
| Joint matrix compute | < 0.5 ms | 200 joints, parent-child walk |
| GPU skinning | < 2 ms | 50K vertex mesh, 128 bones (compute shader) |
| Bone attachment update | < 0.1 ms | 20 attachments |

---

## 9. Phase 3→4 Upgrade Path

When adding physics (Phase 4), no breaking changes needed:

```cpp
// Phase 3: No physics
struct SkeletalMeshComponent {
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Skeleton> skeleton;
    ResourceHandle<Material> material;
};

// Phase 4: Add physics
struct SkeletalMeshComponent {
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Skeleton> skeleton;
    ResourceHandle<PhysicsAsset> physics;  // ← new, optional
    ResourceHandle<Material> material;

    enum ERagdollMode { Disabled, Simulating, BlendedIK };
    ERagdollMode ragdoll_mode = Disabled;
};
```

No changes to skeleton, animation, or mesh loading. PhysicsAsset is an optional addon.

