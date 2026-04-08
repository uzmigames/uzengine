## 1. Skeleton Data Structure

- [ ] 1.1 Create `runtime/engine/animation/skeleton.h` — Skeleton class: vector<BoneInfo> bones. BoneInfo: name (string), parent_index (int, -1=root), bind_translation (Vec3), bind_rotation (Quat), bind_scale (Vec3), inverse_bind_world (Mat4, precomputed). FindBoneIndex(name) → int. GetBoneCount(). Max 256 bones.
- [ ] 1.2 Create `runtime/engine/animation/skeleton.cpp` — ComputeInverseBindMatrices(): for each bone, compute world = parent_world * local_bind, then inverse_bind = inverse(world). BuildNameIndex() for O(1) name lookup via hash map.
- [ ] 1.3 Create `runtime/engine/animation/bone_transform.h` — BoneTransform: position (Vec3), rotation (Quat), scale (Vec3). Interpolate(a, b, t) for lerp/slerp/lerp. ToMatrix() → Mat4.

## 2. .uzskel Format

- [ ] 2.1 Create `runtime/engine/animation/skeleton_io.h/.cpp` — SaveSkeleton(Skeleton&, path): INI-like text with [bone.N] sections containing name, parent, translation, rotation, scale. LoadSkeleton(path) → Skeleton. Human-readable for AI/editor tooling.

## 3. Skinned Vertex Format

- [ ] 3.1 Create `runtime/engine/renderer/mesh/skinned_vertex.h` — SkinnedVertex: position (float3, 12B), normal (float3, 12B), tangent (float4, 16B), uv0 (float2, 8B), bone_indices (uint8[4], 4B), bone_weights (uint8[4], 4B) = 56B total. Static assert on size. WeightsNormalized() check (sum=255).
- [ ] 3.2 Add skinned vertex input layout to RHI — D3D12_INPUT_ELEMENT_DESC for BLENDINDICES (R8G8B8A8_UINT) and BLENDWEIGHT (R8G8B8A8_UNORM). Register in PipelineLibrary as "SkinnedVertex" layout.

## 4. GPU Skinning Compute Shader

- [ ] 4.1 Create `runtime/shaders/runtime/compute/gpu_skinning_cs.hlsl` — Entry point `main`. Reads StructuredBuffer<SkinnedVertex> (input), StructuredBuffer<float4x4> bonePalette (64-256 matrices), writes RWStructuredBuffer<Vertex> (output, standard vertex without bone data). Per-vertex: transform position by 4-weight blend of bonePalette[indices[i]], transform normal by 3x3 rotation. Thread group [256,1,1]. SM6.6 bindless + SM5 fallback.
- [ ] 4.2 Create `runtime/engine/renderer/passes/compute/gpu_skinning_pass.h/.cpp` — GpuSkinningPass: render graph compute pass. For each skeletal mesh: upload bonePalette to StructuredBuffer, dispatch skinning CS with vertex_count/256 groups, UAV barrier, output buffer used as SRV in geometry pass.

## 5. Bone Palette Upload

- [ ] 5.1 Create `runtime/engine/animation/bone_palette.h/.cpp` — BonePalette: ComputePalette(Skeleton&, Pose& current_pose) → vector<Mat4>. For each bone: world = parent_world * current_pose.local[bone], palette[bone] = world * inverse_bind[bone]. Upload to StructuredBuffer<float4x4> via CBRingAllocator or staging buffer.

## 6. Mesh Importer v2 (Skinning)

- [ ] 6.1 Extend glTF importer — Extract skin data: joints array, inverse bind matrices, vertex bone indices + weights. Normalize weights to uint8[4]. Export .uzskel alongside .uzmesh.
- [ ] 6.2 Extend FBX importer — Extract FbxSkin cluster data: bone influences per vertex, bind poses. Convert to SkinnedVertex format. Handle >4 influences by keeping top-4 by weight.

## 7. SkeletalMeshComponent

- [ ] 7.1 Create `runtime/engine/ecs/components/skeletal_mesh_component.h` — SkeletalMeshComponent: mesh_path (string), skeleton_path (string), Skeleton* (loaded), current_pose (vector<BoneTransform>), skinned_vb (RHIBufferHandle, GPU output). SetPose(pose) updates current_pose.
- [ ] 7.2 Add to ComponentSerializerRegistry — JSON: mesh_path, skeleton_path.
- [ ] 7.3 Wire to renderer — RenderSubsystem detects SkeletalMeshComponent, dispatches GPU skinning pass before geometry pass, uses skinned VB for draw call.

## 8. Testing

- [ ] 8.1 Unit tests for Skeleton — create, bone lookup, inverse bind computation.
- [ ] 8.2 Unit tests for SkinnedVertex — layout size, weight normalization.
- [ ] 8.3 Unit tests for BonePalette — identity pose → palette matches inverse bind.
- [ ] 8.4 Unit tests for .uzskel I/O — save/load round-trip.
- [ ] 8.5 Integration test: load skinned glTF, verify vertex count and bone count match.

## 9. Demo

- [ ] 9.1 Create `demos/skeletal_mesh/` — Load skinned character from glTF, render in bind pose (T-pose). Verify correct vertex positions via GPU skinning with identity pose. Headless + screenshot.

## 10. Documentation

- [ ] 10.1 Update `docs/engine/19-animation/` with skeleton, skinned vertex, GPU skinning pipeline.
- [ ] 10.2 Document .uzskel format specification.

## 11. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 11.1 Update or create documentation covering the implementation
- [ ] 11.2 Write tests covering the new behavior
- [ ] 11.3 Run tests and confirm they pass
