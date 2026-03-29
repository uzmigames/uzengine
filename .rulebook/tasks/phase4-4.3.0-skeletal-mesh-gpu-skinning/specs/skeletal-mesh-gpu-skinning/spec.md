# Skeletal Mesh & GPU Skinning — Specification

## ADDED Requirements

### Requirement: Skeleton Data Structure
The animation system SHALL define a bone hierarchy with bind-pose transforms and inverse bind matrices.

#### Scenario: Skeleton Creation
Given a glTF file with 64 bones
When the skeleton is extracted
Then a Skeleton SHALL be created with 64 BoneInfo entries
And each BoneInfo SHALL contain: name, parent_index (-1 for root), bind_translation, bind_rotation, bind_scale, inverse_bind_world (precomputed Mat4)

#### Scenario: Bone Lookup
Given a Skeleton with bone "LeftHand" at index 23
When FindBoneIndex("LeftHand") is called
Then the result SHALL be 23
And FindBoneIndex("NonExistent") SHALL return -1

### Requirement: .uzskel Format
The skeleton SHALL be stored in a human-readable text format.

#### Scenario: Skeleton Save
Given a Skeleton with 3 bones (root, spine, head)
When saved to .uzskel
Then the file SHALL contain INI-like sections per bone:
```
[bone.0]
name = root
parent = -1
translation = 0.0 0.0 0.0
rotation = 0.0 0.0 0.0 1.0
```

### Requirement: Skinned Vertex Format
The mesh system SHALL support a skinned vertex layout with bone influences.

#### Scenario: Skinned Vertex Layout
Given a skinned mesh being imported
When vertices are converted to SkinnedVertex format
Then each vertex SHALL have: position (float3), normal (float3), tangent (float4), uv0 (float2), bone_indices (uint8[4]), bone_weights (uint8[4])
And bone_weights SHALL be normalized (sum to 255)
And total vertex size SHALL be 56 bytes

### Requirement: GPU Skinning Compute Shader
The renderer SHALL transform skinned vertices on GPU using bone matrices.

#### Scenario: Skinning Dispatch
Given a skeletal mesh with 10000 vertices and 64 bones
When the GPU skinning compute pass runs
Then a StructuredBuffer<float4x4> with 64 bone palette matrices SHALL be uploaded
And the compute shader SHALL read each vertex's bone_indices and bone_weights
And position SHALL be: sum(weight[i] * (bonePalette[index[i]] * position)) for i in 0..3
And normal SHALL be transformed by the 3x3 rotation part of the bone matrix
And output SHALL be written to a separate skinned vertex buffer (SRV for rendering)

#### Scenario: Bone Palette Computation
Given a Skeleton with current pose (array of local BoneTransform)
When the bone palette is computed
Then for each bone: world_transform = parent_world * local_pose[bone]
And palette[bone] = world_transform * inverse_bind_matrix[bone]

### Requirement: SkeletalMeshComponent
The ECS SHALL provide a component for rendering skinned meshes.

#### Scenario: Skeletal Mesh Entity
Given an entity with SkeletalMeshComponent(mesh="character.uzmesh", skeleton="character.uzskel")
When the renderer processes the entity
Then GPU skinning SHALL be dispatched before the geometry pass
And the skinned vertex buffer SHALL be used for rendering instead of the static vertex buffer
