## 1. Skinned Vertex Layout

- [ ] 1.1 Add `SkinnedVertex` struct (see spec: 56 bytes with bone_indices/weights as uint8x4)
- [ ] 1.2 DX12 input layout for skinned mesh: POSITION, NORMAL, TANGENT, TEXCOORD0, BONE_INDICES, BONE_WEIGHTS
- [ ] 1.3 Separate PSO for skinned meshes (uses VS_Skinned entry point)

## 2. Bone Palette Upload

- [ ] 2.1 `BonePalette` struct: `std::vector<glm::mat4>` per skinned entity
- [ ] 2.2 Upload per-frame: `StructuredBuffer<float4x4>` in UPLOAD heap, slot t5
- [ ] 2.3 `compute_bone_palette(pose, skeleton, palette)`:
        `palette[i] = world_pose[i] * skeleton.bones[i].inverse_bind_world`
- [ ] 2.4 Palette size limit: 256 bones (uint8 indices)

## 3. Skinned Vertex Shader

- [ ] 3.1 Create `shaders/skinned.hlsl` — VS_Skinned entry point
- [ ] 3.2 `StructuredBuffer<float4x4> BoneMatrices : register(t5)`
- [ ] 3.3 Weighted sum of 4 bone matrices (dequantize uint8 weights)
- [ ] 3.4 Transform position + normal by skin matrix, then by model→ViewProj
- [ ] 3.5 Forward normal to pixel shader for PBR

## 4. Morph Target Compute Pass

- [ ] 4.1 Create `shaders/morph_apply.hlsl` — CS_ApplyMorphs compute shader
- [ ] 4.2 Input buffers: MorphDelta[], MorphWeights[], DeltaMorphIndex[]
- [ ] 4.3 Output: RWStructuredBuffer for positions and normals (working copy of base VB)
- [ ] 4.4 Dispatch: one thread per morph delta
- [ ] 4.5 Skinned VS reads from the working VB after morph pass

## 5. Pose Evaluation

- [ ] 5.1 `Pose` struct: `std::vector<BoneTransform>` (local space per bone)
- [ ] 5.2 `sample_clip(clip, time, pose)` — sample all bone channels
- [ ] 5.3 `compute_world_poses(local_pose, skeleton, world_out)` — propagate hierarchy
- [ ] 5.4 `blend_poses(a, b, alpha, out)` — SLERP rotation, LERP pos/scale

## 6. ECS Integration

- [ ] 6.1 Add `SkeletalMeshComponent` to entt: references mesh, skeleton, current pose
- [ ] 6.2 `system_update_skinning(registry)` — evaluates poses, uploads bone palettes
- [ ] 6.3 `system_render_skinned(registry, cmd_list)` — binds skinned PSO, draws
- [ ] 6.4 `StaticMeshComponent` uses static PSO; `SkeletalMeshComponent` uses skinned PSO

## 7. Physics Asset Editor (VSCode)

- [ ] 7.1 Physics asset panel: split view (`.uzphys` text editor + 3D overlay via JPEG stream)
- [ ] 7.2 IPC: `physics.previewAsset(path)` → engine renders skeleton + physics bodies
- [ ] 7.3 Per-body inspector: shape type, radius, height, mass
- [ ] 7.4 Per-constraint inspector: type, swing/twist angles
- [ ] 7.5 Selecting a body in the panel highlights its bone in the 3D view

## 8. Testing

- [ ] 8.1 Character.fbx renders with correct GPU skinning at 60fps
- [ ] 8.2 Morph target "smile" (weight=1.0) deforms face correctly
- [ ] 8.3 Multiple skinned characters in scene: no bone palette collision
- [ ] 8.4 Physics asset preview shows capsule colliders on correct bones
