# Skeletal Mesh System Spec

## Overview

UzEngine separates skeletal mesh data into three distinct assets (same rationale as Unreal Engine):

| Asset | File | Contents |
|-------|------|----------|
| **Skeleton** | `.uzskel` | Bone hierarchy, names, bind pose |
| **Skeletal Mesh** | `.uzmesh` (skinned flag) | Geometry, skin weights, morph targets; references `.uzskel` |
| **Physics Asset** | `.uzphys` | Per-bone collider shapes, joint constraints |

This separation allows multiple mesh LODs/variants to share one skeleton, and physics to be authored independently of art.

---

## 1. Skeleton

### Data

```cpp
struct BoneInfo {
    std::string name;
    int32_t     parent_index;            // -1 for root
    glm::vec3   bind_translation;        // local to parent in bind pose
    glm::quat   bind_rotation;           // local
    glm::vec3   bind_scale;              // local (usually [1,1,1])
    glm::mat4   inverse_bind_world;      // precomputed at load: inv(world_bind_matrix)
};

struct Skeleton {
    std::string name;
    std::vector<BoneInfo> bones;
    std::unordered_map<std::string, int32_t> name_to_index;

    void compute_inverse_bind_poses();   // called after load
    int32_t find_bone(std::string_view name) const;
};
```

### `.uzskel` File Format (text, AI-friendly)

```ini
[uzskeleton version=1 name="HumanoidSkeleton" bone_count=67]

[bone id=0 name=root parent=-1]
bind_translation = [0.0, 0.0, 0.0]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

[bone id=1 name=pelvis parent=0]
bind_translation = [0.0, 0.0, 95.0]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

[bone id=2 name=spine_01 parent=1]
bind_translation = [0.0, 0.0, 10.5]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]

; ... (all 67 bones)
```

---

## 2. Vertex Layout (Skinned)

```cpp
struct SkinnedVertex {
    glm::vec3  position;          // 12 bytes
    glm::vec3  normal;            // 12 bytes
    glm::vec4  tangent;           // 16 bytes (w = bitangent sign ±1)
    glm::vec2  uv0;               // 8 bytes
    uint8_t    bone_indices[4];   // 4 bytes (indices into bone palette, max 256 bones)
    uint8_t    bone_weights[4];   // 4 bytes (normalized 0..255, sum = 255)
    // Total: 56 bytes per vertex
};
```

Bone weight dequantization in HLSL: `float w = bone_weights[i] / 255.0f`

---

## 3. GPU Skinning Pipeline

Each frame, the skinning system computes a **bone palette** — an array of final matrices:

```
bone_palette[i] = current_pose_world[i] × skeleton.inverse_bind_world[i]
```

This matrix transforms a vertex from rest-pose model-space to the animated world-space of the bone.

**Uploaded as**: `StructuredBuffer<float4x4>` per draw call (or per character, batched).

### HLSL Vertex Shader (skinned)

```hlsl
StructuredBuffer<float4x4> BoneMatrices : register(t5);

VSOutput VS_Skinned(VSInput v)
{
    float4x4 skin =
        v.bone_weights.x / 255.0 * BoneMatrices[v.bone_indices.x] +
        v.bone_weights.y / 255.0 * BoneMatrices[v.bone_indices.y] +
        v.bone_weights.z / 255.0 * BoneMatrices[v.bone_indices.z] +
        v.bone_weights.w / 255.0 * BoneMatrices[v.bone_indices.w];

    float4 world_pos    = mul(skin, float4(v.position, 1.0));
    float3 world_normal = normalize(mul((float3x3)skin, v.normal));

    VSOutput o;
    o.position = mul(ViewProj, world_pos);
    o.normal   = world_normal;
    o.uv       = v.uv;
    return o;
}
```

---

## 4. Morph Targets (Blend Shapes)

Sparse delta storage — only vertices that actually move:

```cpp
struct MorphDelta {
    uint32_t  vertex_index;
    glm::vec3 delta_position;
    glm::vec3 delta_normal;
};

struct MorphTarget {
    std::string             name;     // "smile", "brow_raise_l", "viseme_A"
    std::vector<MorphDelta> deltas;
    float                   weight = 0.0f;  // 0..1, set by game code or anim system
};
```

**Compute shader morph pass** runs before GPU skinning:

```hlsl
// CS_ApplyMorphs.hlsl
struct MorphDelta { uint vertex_idx; float3 delta_pos; float3 delta_normal; };

StructuredBuffer<MorphDelta> Deltas  : register(t0);
StructuredBuffer<float>      Weights : register(t1);  // one per morph target
StructuredBuffer<uint>       DeltaMorphIdx : register(t2);  // morph target index per delta

RWStructuredBuffer<float3>   Positions : register(u0);
RWStructuredBuffer<float3>   Normals   : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= delta_count) return;
    MorphDelta d = Deltas[id.x];
    float w = Weights[DeltaMorphIdx[id.x]];
    if (abs(w) < 0.0001f) return;
    Positions[d.vertex_idx] += d.delta_pos    * w;
    Normals[d.vertex_idx]   += d.delta_normal * w;
}
```

Frame pipeline for a skinned character:
```
1. CS_ApplyMorphs    → working VB (base + morph deltas)
2. VS_Skinned        → transforms vertices by bone matrices
3. PS_PBR            → PBR shading
```

---

## 5. Physics Asset

`.uzphys` file format (text, AI-friendly):

```ini
[uzphysics version=1 name="HumanoidPhysics"]
skeleton = res://skeletons/humanoid.uzskel

[body name=pelvis]
bone   = pelvis
shape  = capsule
radius = 0.12
height = 0.30
offset_translation = [0, 0, 0]
offset_rotation    = [0, 0, 0, 1]
mass = 10.0

[body name=head]
bone  = head
shape = sphere
radius = 0.11
mass = 3.0

[body name=thigh_l]
bone   = thigh_l
shape  = capsule
radius = 0.07
height = 0.44
mass   = 6.0

[constraint name=spine_01]
body_a = pelvis
body_b = spine_01
type   = swing_twist
swing_angle_y = 30.0
swing_angle_z = 30.0
twist_min     = -20.0
twist_max     =  20.0

[constraint name=knee_l]
body_a = thigh_l
body_b = calf_l
type   = hinge
axis   = [1, 0, 0]
min    = -130.0
max    =   10.0
```

---

## 6. Ragdoll Integration (Jolt Physics)

Jolt's `RagdollSettings` maps directly to the `.uzphys` format:

```cpp
JPH::RagdollSettings* PhysicsAsset::build_jolt_ragdoll(const Skeleton& sk)
{
    auto* settings = new JPH::RagdollSettings();
    for (auto& body_def : bodies) {
        JPH::RagdollSettings::Part part;
        // Shape
        if (body_def.shape == "capsule")
            part.mBodySettings.SetShape(new JPH::CapsuleShape(body_def.height/2, body_def.radius));
        else if (body_def.shape == "sphere")
            part.mBodySettings.SetShape(new JPH::SphereShape(body_def.radius));
        // Initial pose from bind pose
        int bi = sk.find_bone(body_def.bone);
        part.mBodySettings.mPosition = ToJolt(sk.bones[bi].world_bind_translation);
        part.mBodySettings.mRotation = ToJolt(sk.bones[bi].world_bind_rotation);
        part.mBodySettings.mMassPropertiesOverride.mMass = body_def.mass;
        // Constraint
        if (!body_def.constraint.empty()) {
            auto& c = find_constraint(body_def.constraint);
            if (c.type == "swing_twist") {
                auto* cs = new JPH::SwingTwistConstraintSettings();
                cs->mNormalHalfConeAngle = JPH::DegreesToRadians(c.swing_angle_y);
                cs->mPlaneHalfConeAngle  = JPH::DegreesToRadians(c.swing_angle_z);
                cs->mTwistMinAngle       = JPH::DegreesToRadians(c.twist_min);
                cs->mTwistMaxAngle       = JPH::DegreesToRadians(c.twist_max);
                part.mConstraintSettings = cs;
            }
        }
        part.mParentBodyIndex = sk.bones[bi].parent_index;
        settings->mParts.push_back(part);
    }
    settings->Stabilize();
    return settings;
}
```

### Ragdoll Activation on Death

```cpp
void activate_ragdoll(const std::vector<glm::mat4>& current_anim_pose)
{
    ragdoll = ragdoll_settings->CreateRagdoll(0, this, &physics_system);
    // Pose to current animation state (seamless visual transition)
    for (int i = 0; i < bone_count; ++i) {
        physics.GetBodyInterface().SetPositionAndRotation(
            ragdoll->GetBodyID(i),
            ToJolt(glm::vec3(current_anim_pose[i][3])),
            ToJolt(glm::mat4_to_quat(current_anim_pose[i])),
            JPH::EActivation::Activate);
        // Inherit character velocity
        physics.GetBodyInterface().SetLinearVelocity(
            ragdoll->GetBodyID(i), ToJolt(character_velocity));
    }
}
```

---

## 7. VSCode Editor — Skeleton / Physics Panels

### Skeleton Preview (part of Model Preview)

- Show bone hierarchy as a collapsible tree alongside the model viewport
- Clicking a bone selects it, highlights the capsule/sphere collider in viewport
- Bone names, parent, bind translation shown in a secondary inspector

### Physics Asset Editor

- Opens `.uzphys` file in a split view: text editor + 3D overlay
- Viewport shows the skeleton mesh + physics bodies as colored wireframes
- Per-body inspector: shape, dimensions, mass
- Per-constraint inspector: type, swing/twist angles, axis

---

## 8. IPC Handlers (skeletal mesh)

```
scene.setMorphWeight  { node_id, morph_name, weight }
scene.getMorphs       { node_id }  → [{ name, weight }]
scene.playAnimation   { node_id, clip_path, loop?, speed? }
scene.stopAnimation   { node_id }
scene.setAnimParam    { node_id, param, value }  → state machine param
scene.activateRagdoll { node_id }
scene.deactivateRagdoll { node_id }
```
