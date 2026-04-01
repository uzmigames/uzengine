# Proposal: phase4-4.3.0-skeletal-mesh-gpu-skinning

## Why
UzEngine can only render static meshes. Animated characters, creatures, and any deformable mesh require skeletal mesh support with GPU skinning. This is the data layer: skeleton bone hierarchy, skinned vertex format, bone palette upload, and the GPU compute shader that transforms vertices by bone matrices. Without this, the animation system (phase4-4.3.1) has nothing to drive.

## What Changes
- Skeleton data structure: bone hierarchy with names, parent indices, bind poses, inverse bind matrices
- .uzskel text format (INI-like, human-readable) for skeleton assets
- SkinnedVertex format: standard vertex + bone_indices[4] + bone_weights[4] (56 bytes)
- GPU skinning compute shader: reads bone palette StructuredBuffer, transforms vertices
- Bone palette upload: per-mesh StructuredBuffer<float4x4> updated each frame
- MeshImporter v2: extract skeleton + skin weights from glTF/FBX
- SkeletalMeshComponent ECS component referencing .uzmesh + .uzskel

## Impact
- New directory: `runtime/engine/animation/` (skeleton data structures)
- Modified: `runtime/engine/renderer/mesh/` (skinned vertex support)
- New shader: `runtime/shaders/runtime/compute/gpu_skinning_cs.hlsl`
- Depends on: existing mesh pipeline, FBX importer (phase3-3.0.0)
- Breaking change: NO (additive — static meshes unaffected)
- Performance target: 100 skinned characters (64 bones each) in < 1ms GPU skinning
