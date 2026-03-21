# Proposal: FBX Skeletal Animation Pipeline (Phase 3)

## Why

UzEngine currently only imports static meshes via glTF/GLB (fastgltf). There is zero support for:
- FBX file loading (the dominant format for game-ready character assets from Mixamo, Maya, Blender exports)
- Skeletal meshes (bone hierarchy, vertex weights, bind pose)
- Animation playback (keyframe sampling, joint matrices, GPU skinning)
- Animation state machines (idle/walk/run transitions)

The `fbx_test` demo already has a complete Mixamo character asset (MushroomMonster_Low.fbx with 17 animation FBX files and 3 PBR texture maps). This task implements the full pipeline from FBX file parsing to animated skeletal character rendering.

**Library**: [ufbx](https://github.com/ufbx/ufbx) v0.14+ — MIT licensed, single-file C library, no Autodesk SDK dependency. Already designated as the FBX library in the agent configuration.

## What Changes

### New Systems

1. **ufbx thirdparty dependency** — Add ufbx to `scripts/setup.ps1` and integrate into the Zig build system
2. **FbxImporter** (`runtime/engine/asset/importers/fbx_importer.h/.cpp`) — Parses FBX via ufbx, extracts mesh geometry (positions, normals, UVs, tangents), materials, bone weights, and skeleton data. Outputs .uzmesh (extended with skinning data)
3. **Skeleton** (`runtime/engine/animation/skeleton.h/.cpp`) — Joint hierarchy, bind pose, inverse bind matrices. Loaded from FBX bone data
4. **SkinnedVertex format** (`runtime/engine/asset/formats/uzmesh_format.h`) — Extended vertex layout with bone_ids[4] + bone_weights[4] for GPU skinning
5. **AnimSequence** (`runtime/engine/animation/anim_sequence.h/.cpp`) — Keyframe data per joint (position, rotation, scale) sampled from FBX animation curves
6. **GPU Skinning vertex shader** (`runtime/shaders/runtime/skinned_vs.hlsl`) — Linear blend skinning in vertex shader using joint palette constant buffer
7. **SkeletalMeshRenderer** (`runtime/engine/renderer/skeletal_mesh_renderer.h/.cpp`) — Manages joint palette upload, skinned PSO, draw calls for skeletal meshes
8. **AnimationController** (`runtime/engine/animation/anim_controller.h/.cpp`) — State machine with states, transitions, parameters (float/bool/trigger), crossfade blending
9. **fbx_test demo** (`runtime/demos/fbx_test/`) — Complete demo loading MushroomMonster with textures, skeleton, and walk animation playing

### Refactoring

- `uzmesh_format.h` — Add flag for skinned vertex layout (bone IDs + weights) alongside existing `VertexPosNormUV`
- `scripts/setup.ps1` — Add ufbx download entry
- `runtime/build.zig` — Add ufbx source compilation and include paths

## Impact
- Affected specs: `docs/engine/19-animation/`, `docs/engine/08-resource-system/02-importers.md`
- Affected code: `runtime/engine/asset/`, `runtime/engine/animation/` (new), `runtime/engine/renderer/`, `runtime/shaders/`, `runtime/demos/fbx_test/`
- Breaking change: NO — additive only, existing glTF pipeline unchanged
- User benefit: Load and render animated FBX characters — the most common artist workflow
- Enables: Phase 4 blend trees, IK solvers, root motion, montages, morph targets
