# Proposal: phase3-skeletal-mesh

## Why

All meshes in UzEngine are currently rigid — every vertex is fixed relative to the mesh transform. This makes it impossible to render characters, creatures, or any deforming object. GPU skinning with a bone matrix palette is the standard foundation for character rendering and is a prerequisite for the full animation system, ragdoll physics, and morph-target driven facial animation.

## What Changes

- Add bone hierarchy representation: `CBone` with parent index, inverse bind-pose matrix, and local transform
- Upload bone palette each frame as a `StructuredBuffer<float4x4>` bound to vertex shader at slot `t10`
- Implement skinned vertex shader: reads up to 4 bone indices + weights per vertex, accumulates transformed position/normal/tangent
- Add morph target support: compute shader accumulates delta buffers (position + normal) per active morph before skinning pass
- Import bone hierarchy and skin weights from glTF `skin` extension and FBX skin deformer during asset import
- Store skinned mesh binary with bone count, inverse bind matrices, per-vertex bone indices/weights in `.uzmesh` format
- Expose bone transform API to Lua (`ctx.mesh:set_bone_transform(name, matrix)`) for procedural overrides
- Integrate with Physics Asset: per-bone collider shapes authored in editor for ragdoll and hit-box queries

## Impact

- Affected specs: `docs/specs/skeletal-mesh.md`, `docs/specs/animation-system.md`, `docs/specs/renderer.md`, `docs/specs/asset-pipeline.md`
- Affected code: `src/renderer/skinning_pass.cpp`, `src/assets/gltf_importer.cpp`, `src/assets/fbx_importer.cpp`, `shaders/skinning.hlsl`
- Breaking change: NO
- User benefit: Enables rendering of animated characters and deforming objects, which is required for all character-based gameplay
