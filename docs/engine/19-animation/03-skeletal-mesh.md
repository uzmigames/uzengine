# Animation System — Skeletal Mesh & Joint Hierarchy

> **Status**: Planned (Phase 3)
> **Source**: `engine/animation/skeleton.h/.cpp`, `engine/components/skeletal_mesh_component.h`
> **Depends on**: Mesh pipeline (§34), ECS (§10)

## Purpose

A skeletal mesh is a geometric mesh (vertices) bound to a skeleton (joint hierarchy). The skeleton defines how the mesh deforms:

- **Joint hierarchy** — parent-child relationships (spine → arm → hand)
- **Bind pose** — rest position used for default appearance
- **Inverse bind matrices** — transform from world space back to joint-local space for GPU skinning
- **Vertex weights** — which joints influence each vertex, and by how much

## Skeleton Structure

### Joint Definition

```cpp
/// A joint in the skeleton — a transform node.
struct Joint {
    uint32_t id;                      // unique within skeleton
    std::string name;                 // "Armature|Spine", "Armature|Arm_L"
    uint32_t parent_id = UINT32_MAX;  // parent joint (-1 = root)
    std::vector<uint32_t> child_ids;

    // Bind pose (default/rest position)
    Vec3 position = {0, 0, 0};
    Quat rotation = {0, 0, 0, 1};
    Vec3 scale = {1, 1, 1};

    // Inverse bind matrix — transforms from world space back to joint-local space
    // Used in GPU skinning: vertex_world = sum(weight[i] * inv_bind[i] * joint_matrix[i] * vertex_local)
    Mat4 inverse_bind_matrix;
};

/// Complete skeleton (joint hierarchy + metadata).
class Skeleton {
public:
    std::vector<Joint> joints;

    /// Get joint by ID or name
    const Joint* GetJoint(uint32_t id) const;
    const Joint* GetJointByName(StringView name) const;

    /// Build hierarchy from flat joint array
    void BuildHierarchy();

    /// Compute all joint matrices given animation poses
    void ComputeJointMatrices(const std::vector<Transform>& local_poses,
                              std::vector<Mat4>& out_world_matrices) const;

    /// Get number of joints
    uint32_t GetJointCount() const { return joints.size(); }

    /// Validate skeleton for consistency (no cycles, all parents exist)
    bool Validate(std::string* out_error = nullptr) const;
};
```

### Bind Pose Computation

The bind pose is the rest state — skeleton without animation applied:

```cpp
void Skeleton::ComputeBindPose(std::vector<Transform>& out_local_poses) const {
    out_local_poses.resize(joints.size());

    for (size_t i = 0; i < joints.size(); ++i) {
        out_local_poses[i] = Transform{
            .position = joints[i].position,
            .rotation = joints[i].rotation,
            .scale = joints[i].scale,
        };
    }
}

void Skeleton::ComputeJointMatrices(const std::vector<Transform>& local_poses,
                                    std::vector<Mat4>& out_world_matrices) const {
    out_world_matrices.resize(joints.size());

    for (size_t i = 0; i < joints.size(); ++i) {
        Mat4 local_matrix = local_poses[i].ToMatrix();

        if (joints[i].parent_id == UINT32_MAX) {
            // Root joint — world space = local space
            out_world_matrices[i] = local_matrix;
        } else {
            // Child — world = parent_world * local
            out_world_matrices[i] = out_world_matrices[joints[i].parent_id] * local_matrix;
        }
    }
}
```

## Vertex Weights

Vertices are bound to joints with weights:

```cpp
/// Skinning data for a single vertex.
struct VertexSkinData {
    // Up to 4 joints influence this vertex (typical max)
    uint32_t joint_ids[4] = {0, 0, 0, 0};
    float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};  // sum = 1.0

    Vec3 ApplySkinning(const std::vector<Mat4>& joint_matrices,
                       const Vec3& original_position) const {
        Vec3 result = {0, 0, 0};

        for (int i = 0; i < 4; ++i) {
            if (weights[i] > 0.0f) {
                Vec4 pos = joint_matrices[joint_ids[i]] * Vec4{original_position, 1.0f};
                result += (Vec3)pos * weights[i];
            }
        }

        return result;
    }
};
```

## File Format — `.uzskeleton`

Skeleton data is stored in JSON:

```json
{
  "format": "uzskeleton",
  "version": 1,
  "name": "character_armature",
  "joints": [
    {
      "id": 0,
      "name": "Armature",
      "parent_id": -1,
      "position": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    },
    {
      "id": 1,
      "name": "Armature|Spine",
      "parent_id": 0,
      "position": [0, 1.5, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    },
    {
      "id": 2,
      "name": "Armature|Spine|Chest",
      "parent_id": 1,
      "position": [0, 0.8, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    },
    {
      "id": 3,
      "name": "Armature|Spine|Chest|ArmL",
      "parent_id": 2,
      "position": [-1.0, 0.5, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    },
    {
      "id": 4,
      "name": "Armature|Spine|Chest|ArmL|ForearmL",
      "parent_id": 3,
      "position": [-1.0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    },
    {
      "id": 5,
      "name": "Armature|Spine|Chest|ArmL|ForearmL|HandL",
      "parent_id": 4,
      "position": [-0.8, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    }
  ]
}
```

## SkeletalMeshComponent

The game-facing API:

```cpp
/// Skeletal mesh renderer — renders a mesh deformed by a skeleton.
class SkeletalMeshComponent : public PrimitiveComponent {
public:
    // ─── Assets ─────────────────────────────────────────────────────
    ResourceHandle<Mesh> mesh;              // .uzmesh with vertex weights
    ResourceHandle<Skeleton> skeleton;      // .uzskeleton
    ResourceHandle<Material> material;      // material to render with

    // ─── Animation Controller ───────────────────────────────────────
    AnimationController* animation_controller = nullptr;  // nullptr = bind pose

    // ─── Morph Targets (blend shapes) ───────────────────────────────
    void SetMorphTarget(StringView name, float weight);  // 0.0-1.0
    float GetMorphTarget(StringView name) const;

    // ─── Skeletal Rendering ─────────────────────────────────────────
    /// Get computed joint matrices (world space) from animation controller.
    std::vector<Mat4> GetJointMatrices() const;

    /// Force update of joint matrices and GPU buffers (called each frame).
    void UpdateSkinning();

    /// Get bone transform in world space (for attaching items, effects).
    Transform GetBoneTransform(StringView bone_name) const;
};
```

### Usage

```cpp
// Create a skeletal mesh entity
Entity character = world.SpawnActor("res://characters/hero.uzactor");

auto skeletal_mesh = character.GetComponent<SkeletalMeshComponent>();
skeletal_mesh->mesh = AssetManager::Load<Mesh>("res://meshes/hero.uzmesh");
skeletal_mesh->skeleton = AssetManager::Load<Skeleton>("res://skeletons/hero.uzskeleton");
skeletal_mesh->material = AssetManager::Load<Material>("res://materials/hero.uzmat");

// Attach animation controller
auto anim_controller = new AnimationController();
anim_controller->Load("res://anims/hero.uzanimcontroller");
skeletal_mesh->animation_controller = anim_controller;

// Each frame, update joint matrices and GPU skinning
skeletal_mesh->UpdateSkinning();
```

## Mesh Skinning Data

### Vertex Format

```cpp
struct SkinnedVertex {
    Vec3 position;

    // Skinning
    uint8_t bone_ids[4];        // indices into joint palette
    uint8_t bone_weights[4];    // 0-255, normalized to 0.0-1.0 on GPU

    // Standard rendering
    Vec3 normal;
    Vec2 uv;
};
```

### File Format

Mesh files (`.uzmesh`) include skinning data:

```cpp
struct MeshLod {
    // ... vertex/index buffers ...

    // Skinning
    std::vector<VertexSkinData> vertex_skin_data;
    ResourceHandle<Skeleton> skeleton_ref;
};
```

## GPU Joint Palette

All joint matrices are uploaded to a GPU buffer, accessed by vertex shader:

```cpp
// In game code
std::vector<Mat4> joint_matrices = skeletal_mesh->GetJointMatrices();

// Upload to GPU buffer
GpuBuffer joint_buffer;
joint_buffer.Upload(joint_matrices.data(), joint_matrices.size() * sizeof(Mat4));

// Pass to vertex shader
context.SetConstantBuffer(0, joint_buffer);  // register b0 = joint palette
```

### HLSL Vertex Shader

```hlsl
cbuffer JointMatrices : register(b0) {
    float4x4 joint_matrices[128];  // up to 128 joints
};

struct SkinnedVertex {
    float3 position : POSITION;
    uint bone_ids[4] : BONE_IDS;
    uint bone_weights[4] : BONE_WEIGHTS;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VS_Output {
    float4 position : SV_POSITION;
    float3 world_normal : NORMAL;
    float2 uv : TEXCOORD0;
};

VS_Output main(SkinnedVertex input, uint instance_id : SV_InstanceID) {
    VS_Output output;

    // Skin vertex using joint palette
    float3 skinned_pos = float3(0, 0, 0);
    float3 skinned_normal = float3(0, 0, 0);

    for (int i = 0; i < 4; ++i) {
        float weight = input.bone_weights[i] / 255.0f;
        if (weight > 0.0f) {
            int joint_idx = input.bone_ids[i];

            float4 pos = mul(float4(input.position, 1), joint_matrices[joint_idx]);
            skinned_pos += pos.xyz * weight;

            float3 normal = mul(input.normal, (float3x3)joint_matrices[joint_idx]);
            skinned_normal += normal * weight;
        }
    }

    output.position = mul(float4(skinned_pos, 1), view_projection);
    output.world_normal = normalize(skinned_normal);
    output.uv = input.uv;

    return output;
}
```

## Bone Attachment

Attach objects to bones (swords in hands, glasses on head):

```cpp
/// Attach a component to a bone (follows bone transform).
void SkeletalMeshComponent::AttachToBone(StringView bone_name,
                                         SceneComponent* component) {
    bone_attachments[bone_name] = component;
}

void SkeletalMeshComponent::UpdateBoneAttachments() {
    for (const auto& [bone_name, component] : bone_attachments) {
        Transform bone_transform = GetBoneTransform(bone_name);
        component->SetWorldTransform(bone_transform);
    }
}

// Usage
auto sword = world.SpawnActor<WeaponActor>();
skeletal_mesh->AttachToBone("Hand.R", sword->GetRootComponent());
// Now sword follows right hand
```

## Validation

### Skeleton Integrity Checks

```cpp
bool Skeleton::Validate(std::string* out_error) const {
    // Check for cycles
    std::vector<bool> visited(joints.size(), false);
    std::function<bool(uint32_t)> has_cycle = [&](uint32_t id) {
        visited[id] = true;
        const auto& joint = joints[id];

        for (uint32_t child_id : joint.child_ids) {
            if (!visited[child_id]) {
                if (has_cycle(child_id)) return true;
            } else {
                // Back-edge found — cycle
                if (out_error) {
                    *out_error = fmt::format("Cycle detected in skeleton: {} → {}", joint.name, joints[child_id].name);
                }
                return true;
            }
        }

        return false;
    };

    for (uint32_t i = 0; i < joints.size(); ++i) {
        if (!visited[i] && joints[i].parent_id == UINT32_MAX) {
            if (has_cycle(i)) return false;
        }
    }

    // Check that all parent references are valid
    for (const auto& joint : joints) {
        if (joint.parent_id != UINT32_MAX && joint.parent_id >= joints.size()) {
            if (out_error) {
                *out_error = fmt::format("Invalid parent reference in joint: {}", joint.name);
            }
            return false;
        }
    }

    return true;
}
```

## Comparison

| Feature | UE5 | Godot | Unity | **UzEngine** |
|---------|-----|-------|-------|------------|
| **Skeleton format** | FBX (binary) | Binary (.tres) | YAML/Binary | **JSON (.uzskeleton)** |
| **Bind pose** | Implicit in engine | Implicit | Implicit | **Explicit in skeleton** |
| **Joint hierarchy** | Scene-based bones | Bone nodes | Humanoid/Generic | **Explicit parent_id** |
| **Bone attachment** | Socket system | Attachment point | Bone transform | **AttachToBone()** |
| **Skinning** | GPU compute | GPU | GPU | **GPU vertex shader** |
| **Joint limit** | Unlimited | Unlimited | Unlimited | **~128 per mesh (GPU buffer)** |

## Rules

1. **NEVER** modify skeleton at runtime — skeletons are immutable assets
2. **ALWAYS** validate skeleton on load — catch malformed hierarchies early
3. **NEVER** exceed 128 joints per mesh — GPU buffer limitation
4. **ALWAYS** normalize vertex weights (sum = 1.0) — ensures no brightness loss
5. **NEVER** use float weights in vertex format — use uint8_t, normalize on GPU
