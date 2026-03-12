# Animation System — GPU Skinning & Morphing

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/animation/gpu_skinning.h/.cpp`, `engine/shaders/skinning.hlsl`
> **Depends on**: Skeletal mesh (§19.3), DX12 backend (§3)

## Purpose

GPU skinning transforms vertex positions from bind pose (initial position) to animated pose (deformed by animated skeleton). This happens on the GPU via vertex/compute shader, keeping the CPU free for game logic and physics.

Key topics:
- **Vertex shader skinning** — blend matrices approach (simple, efficient)
- **Dual-quaternion skinning** — avoids candy-wrapper artifacts on joints
- **Morph targets** — blend between different mesh shapes (facial expressions)

## Vertex Shader Skinning

### Linear Blend Skinning (Fastest)

Simple matrix blending — each vertex is transformed by weighted joint matrices:

```hlsl
// Vertex shader

cbuffer TransformBuffer : register(b0) {
    float4x4 view_projection;
    float4x4 world_matrix;
};

cbuffer JointMatrices : register(b1) {
    float4x4 joints[128];  // animated joint matrices in world space
};

struct VS_Input {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;

    // Skinning
    uint bone_indices[4] : BONE_IDS;     // which joints?
    float bone_weights[4] : BONE_WEIGHTS; // how much influence?
};

struct VS_Output {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float3 world_pos : TEXCOORD1;
    float2 uv : TEXCOORD0;
};

VS_Output main(VS_Input input) {
    VS_Output output;

    // ─── Compute Skinned Position ──────────────────────────────────
    float3 skinned_pos = float3(0, 0, 0);
    float3 skinned_normal = float3(0, 0, 0);

    for (int i = 0; i < 4; ++i) {
        float weight = input.bone_weights[i];
        if (weight > 0.001f) {  // skip zero weights
            uint joint_idx = input.bone_indices[i];

            // Transform position and normal by joint matrix
            float4 pos = mul(float4(input.position, 1), joints[joint_idx]);
            skinned_pos += pos.xyz * weight;

            float3 normal = mul(input.normal, (float3x3)joints[joint_idx]);
            skinned_normal += normal * weight;
        }
    }

    // Renormalize normal (skinning denormalizes it)
    skinned_normal = normalize(skinned_normal);

    // ─── Standard vertex processing ────────────────────────────────
    output.position = mul(float4(skinned_pos, 1), view_projection);
    output.world_pos = skinned_pos;
    output.normal = skinned_normal;
    output.uv = input.uv;

    return output;
}
```

**Advantages:**
- Simple to implement
- Very fast
- Works for most use cases

**Disadvantages:**
- Candy-wrapper artifacts at joints (elbow, shoulder) when joint rotates > 90°
- Limited to 4 bone influences per vertex (or more passes)

### Dual-Quaternion Skinning (Higher Quality)

Quaternions represent rotation without scale/shear artifacts:

```cpp
/// Dual quaternion representation (rotation + translation).
struct DualQuat {
    Quat real;          // rotation part
    Quat dual;          // translation part (pure quaternion: w=0)
};

/// Convert matrix to dual quaternion.
DualQuat MatrixToDualQuat(const Mat4& matrix) {
    Quat real = matrix.GetRotation();

    // Translation in quaternion form
    Vec3 translation = matrix.GetTranslation();
    Quat dual = Quat{translation.x, translation.y, translation.z, 0} * real * 0.5f;

    return {real, dual};
}

/// Apply dual quaternion to point.
Vec3 ApplyDualQuat(const DualQuat& dq, const Vec3& point) {
    // DQS = dual * conjugate(real)
    Quat dq_conj = {-dq.real.x, -dq.real.y, -dq.real.z, dq.real.w};

    // Rotate
    Quat rotated = dq.real * Quat{point.x, point.y, point.z, 0} * dq_conj;

    // Translate
    Quat translated = dq.dual * dq_conj;
    Vec3 result = {rotated.x, rotated.y, rotated.z} + 2.0f * Vec3{translated.x, translated.y, translated.z};

    return result;
}
```

### HLSL Dual-Quaternion Skinning

```hlsl
cbuffer DualQuaternions : register(b2) {
    float4 dq_real[128];   // real part (x, y, z, w)
    float4 dq_dual[128];   // dual part (x, y, z, w)
};

float3 ApplyDualQuat(float4 dq_real, float4 dq_dual, float3 position) {
    // DQ rotation
    float4 dq_conj_real = float4(-dq_real.xyz, dq_real.w);

    // Rotate position
    float4 qpos = float4(position, 0);
    float4 rotated = dq_real * qpos * dq_conj_real;

    // Translate
    float4 translated = dq_dual * dq_conj_real;
    float3 result = position + 2.0f * cross(dq_real.xyz, cross(dq_real.xyz, position) + dq_real.w * position);
    result += 2.0f * (dq_real.w * dq_dual.xyz - dq_dual.w * dq_real.xyz + cross(dq_real.xyz, dq_dual.xyz));

    return result;
}

VS_Output main(VS_Input input) {
    VS_Output output;

    float3 skinned_pos = float3(0, 0, 0);
    float4 accumulated_dq_real = float4(0, 0, 0, 0);
    float4 accumulated_dq_dual = float4(0, 0, 0, 0);

    for (int i = 0; i < 4; ++i) {
        float weight = input.bone_weights[i];
        if (weight > 0.001f) {
            uint joint_idx = input.bone_indices[i];

            accumulated_dq_real += weight * dq_real[joint_idx];
            accumulated_dq_dual += weight * dq_dual[joint_idx];
        }
    }

    // Normalize accumulated dual quaternion
    float mag = length(accumulated_dq_real);
    accumulated_dq_real /= mag;
    accumulated_dq_dual /= mag;

    skinned_pos = ApplyDualQuat(accumulated_dq_real, accumulated_dq_dual, input.position);
    output.position = mul(float4(skinned_pos, 1), view_projection);

    return output;
}
```

**Advantages:**
- No candy-wrapper artifacts
- Smooth blending at joints
- More realistic deformation

**Disadvantages:**
- More complex math
- Slightly slower than linear blending

## Morph Targets (Blend Shapes)

### Definition

Morph targets are alternative vertex position sets for the same mesh. Used for:
- Facial expressions (smile, frown, blink)
- Muscle deformation
- Shape variations

```cpp
struct MorphTarget {
    std::string name;  // e.g., "smile", "frown"
    float weight = 0.0f;  // 0.0 = off, 1.0 = full intensity

    // Per-vertex position offset (relative to base)
    std::vector<Vec3> position_deltas;  // same size as mesh vertex count
    std::vector<Vec3> normal_deltas;    // optional: normal deformations
};

struct MorphData {
    std::vector<MorphTarget> targets;

    void SetTargetWeight(StringView name, float weight) {
        for (auto& target : targets) {
            if (target.name == name) {
                target.weight = std::clamp(weight, 0.0f, 1.0f);
                return;
            }
        }
    }
};
```

### GPU Implementation

Morph targets are blended on the GPU:

```hlsl
cbuffer MorphTargets : register(b3) {
    // Position deltas for each morph target
    // Uploaded as separate buffers or packed into one large buffer
    float weights[16];  // morph target weights
};

StructuredBuffer<float3> morph_target_0 : register(t10);  // "smile"
StructuredBuffer<float3> morph_target_1 : register(t11);  // "frown"
StructuredBuffer<float3> morph_target_2 : register(t12);  // "blink"

VS_Output main(VS_Input input, uint vertex_id : SV_VertexID) {
    float3 base_position = input.position;

    // Blend morph targets
    float3 morph_offset = float3(0, 0, 0);
    morph_offset += weights[0] * morph_target_0[vertex_id];
    morph_offset += weights[1] * morph_target_1[vertex_id];
    morph_offset += weights[2] * morph_target_2[vertex_id];

    float3 morphed_position = base_position + morph_offset;

    // Apply skinning to morphed position
    float3 skinned_pos = ApplySkinning(morphed_position);

    VS_Output output;
    output.position = mul(float4(skinned_pos, 1), view_projection);

    return output;
}
```

### Sequencing

Combine morph targets over time for animations:

```cpp
// In animation controller
void AnimationController::Update(float delta_time) {
    // Sample current animation keyframes
    AnimSequence* anim = GetCurrentAnimation();

    // Extract morph target weights from animation
    for (const auto& curve : anim->morph_curves) {
        float weight = curve.Sample(current_time);
        mesh_component->SetMorphTarget(curve.target_name, weight);
    }

    // Upload to GPU
    mesh_component->UpdateMorphWeights();
}
```

## Joint Matrix Upload

### CPU-GPU Synchronization

Each frame, update the joint buffer with animated transforms:

```cpp
class GPUSkinningBuffer {
private:
    ResourceHandle<GpuBuffer> joint_matrix_buffer;
    std::vector<Mat4> joint_matrices;

public:
    void UpdateJointMatrices(const SkeletalMeshComponent& mesh) {
        // Get animated joint matrices from animation controller
        const auto& world_matrices = mesh.animation_controller->GetJointMatrices();

        // Resize if needed
        if (joint_matrices.size() < world_matrices.size()) {
            joint_matrices.resize(world_matrices.size());
            joint_matrix_buffer = CreateDynamicBuffer(joint_matrices.size() * sizeof(Mat4));
        }

        // Copy matrices
        joint_matrices = world_matrices;

        // Upload to GPU
        joint_matrix_buffer->Upload(joint_matrices.data(),
                                   joint_matrices.size() * sizeof(Mat4));
    }

    const ResourceHandle<GpuBuffer>& GetBuffer() const {
        return joint_matrix_buffer;
    }
};

// In render pass
void RenderSkeletalMesh(const SkeletalMeshComponent& mesh) {
    // Update joint matrices
    skinning_buffer.UpdateJointMatrices(mesh);

    // Set vertex shader constants
    context.SetConstantBuffer(1, skinning_buffer.GetBuffer());  // b1

    // Draw mesh
    context.DrawIndexed(mesh.mesh->GetIndexCount());
}
```

## Ring Buffer Optimization

For many skeletal meshes, use a ring buffer to avoid GPU stalls:

```cpp
class SkinningRingBuffer {
private:
    static constexpr int BUFFER_COUNT = 3;  // triple-buffer
    std::vector<ResourceHandle<GpuBuffer>> buffers;
    int current_buffer = 0;

public:
    void Update(const SkeletalMeshComponent& mesh) {
        // Write to next buffer (GPU still using previous 2)
        ResourceHandle<GpuBuffer>& current = buffers[current_buffer];
        current->Upload(mesh.animation_controller->GetJointMatrices().data(), ...);

        // Rotate to next buffer next frame
        current_buffer = (current_buffer + 1) % BUFFER_COUNT;
    }

    const ResourceHandle<GpuBuffer>& GetCurrentBuffer() const {
        return buffers[(current_buffer + BUFFER_COUNT - 1) % BUFFER_COUNT];
    }
};
```

This prevents the GPU from waiting for the CPU to finish writing.

## Performance Characteristics

```
Per-Frame Cost:
├── Vertex Shader Skinning (Linear)
│   └── ~0.1ms per 1M vertices (RTX 4090)
│
├── Dual-Quaternion Skinning
│   └── ~0.15ms per 1M vertices
│
├── Morph Target Blending
│   └── ~0.05ms per target per 1M vertices
│
└── Joint Matrix Upload
    └── ~0.01ms per 128 joints

Memory:
├── Joint Matrices: 128 * 64 bytes = 8 KB per mesh
├── Morph Target Deltas: vertices * 3 floats per target = 12KB per 1K vertices
└── Total for 100 skeletal meshes: ~0.8 MB
```

## Comparison

| Feature | UE5 | Godot | Unity | **UzEngine** |
|---------|-----|-------|-------|------------|
| **Skinning** | GPU vertex shader | GPU compute/vertex | GPU vertex shader | **GPU vertex shader** |
| **Deformation** | Linear blend (default) | Linear blend | Linear blend | **Linear + DQ optional** |
| **Morph targets** | Full support | Full support | Full support | **Supported** |
| **Max joints** | Unlimited (but slow) | ~256 | ~256 | **128 (GPU buffer)** |
| **Max morph targets** | Unlimited | Unlimited | Unlimited | **16 (limited by shader)** |

## Rules

1. **ALWAYS** renormalize normals after skinning — deformation denormalizes them
2. **NEVER** use more than 4 bone influences per vertex — GPU limit
3. **ALWAYS** normalize joint weights (sum = 1.0) — prevents brightness loss
4. **NEVER** update joint matrices more than once per frame — batch updates
5. **ALWAYS** use dual-quaternion skinning for high-quality rigs — linear blend has artifacts
