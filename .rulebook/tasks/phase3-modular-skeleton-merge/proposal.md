# Proposal: phase3-modular-skeleton-merge

## Why

Character customization is a core feature of MMO and RPG games. Players are not represented by a single monolithic skinned mesh — they are assembled from equipment pieces: head, hair, torso, arms, hands, legs, feet, plus optional pieces like cloaks, shoulder pads, and weapon sheaths. Each piece is a separate skeletal mesh that shares bones with the character's base skeleton. A fully-equipped character might have 8-12 separate equipment meshes, each needing to be skinned against the same bone palette evaluated from the character's animation.

In Unreal Engine 4, the solution is `SetMasterPoseComponent`: each equipment mesh component is told to copy bone transforms from a "master" mesh component (typically the body). This works but has significant hidden costs. Each slave component still issues its own draw call and has its own render state. With 200 visible players on screen, each with 8 equipment pieces, that's 1,600 skinned mesh components and 1,600 draw calls — before any scene geometry. The bone evaluation happens once (on the master) but the GPU skinning pass runs separately per equipment piece. Skeleton rebuild on equipment swap requires detaching and re-attaching master-slave relationships, which is expensive and causes a one-frame visual pop.

The `MergedSkeletonComponent` solves this fundamentally. Instead of N separate GPU skinning dispatches per character, it runs one compute dispatch that skins all equipment pieces in a single pass using a shared bone palette. Equipment swaps update only the bone remapping table and the affected equipment slot — no skeleton rebuild, no pop, no per-piece skinning overhead. For MMO servers expecting 200+ concurrent visible players with unique equipment loadouts, this is not an optional optimization: it is a prerequisite for achieving target frame budgets.

## What Changes

- **`src/engine/components/merged_skeleton.h`**: New component. Defines `CMergedSkeleton { base_skeleton, equipment_count (max 8), equipment[8], equipment_material[8], remap[8], bone_palette (GpuBuffer*) }`. Also defines `BoneRemapTable { count: uint16, remap[256]: uint16 }` mapping equipment bone indices to base skeleton bone indices.
- **`src/engine/systems/merged_skeleton_system.cpp`**: New ECS system. Per frame: (1) retrieve evaluated bone palette from animation system for the entity's `CAnimator`, (2) for each equipment slot with a mesh, upload the full bone palette to `bone_palette` GPU buffer, (3) dispatch `merged_skinning.hlsl` compute shader with dispatch size `(max_vertices_per_piece, equipment_count, 1)`.
- **`src/shaders/skinning/merged_skinning.hlsl`**: New HLSL compute shader. Reads from per-piece `g_InputVertices`, `g_Remap[8]` bone index remapping tables, and `g_BonePalette`. Applies 4-bone weighted skinning. Writes to `g_OutputVertices` (one skinned output buffer per piece, preallocated).
- **`src/asset/importer/bone_remapper.cpp`**: New file. `BoneRemapper::compute(equipment_skeleton, base_skeleton) -> BoneRemapTable`. Matches bones by name string, logs a warning for any equipment bone not found in the base skeleton (mapped to identity). Called at equipment attach time, not every frame.
- **`src/engine/api/character_system.cpp`**: Add `CharacterSystem::attach_equipment(entity_id, slot, mesh_handle, material_handle)`. Internally: updates `CMergedSkeleton` equipment slot, calls `BoneRemapper::compute` for the new mesh, marks the slot dirty so the skinning system re-uploads the remap table next frame. No skeleton rebuild needed.
- **`src/engine/api/character_system.cpp`**: Add `CharacterSystem::detach_equipment(entity_id, slot)`. Sets equipment slot to null handle; skinning system skips the slot.
- **Lua bindings**: `ctx.character:attach_equipment(entity, { slot, mesh, material })` and `ctx.character:detach_equipment(entity, slot)`. Expose equipment slot names as string constants: `"head"`, `"torso"`, `"arms"`, `"hands"`, `"legs"`, `"feet"`, `"cloak"`, `"extra"`.
- **IPC handler**: `character.setEquipment { entity_id, slot, mesh_path, material_path }` for editor equipment preview.
- **`src/renderer/render_system.cpp`**: Detect `CMergedSkeleton` on entities during GBuffer pass submission. If present, issue draw calls for each populated equipment slot using the pre-skinned output vertex buffers. Each slot still issues its own draw call (separate materials) but shares the one skinning dispatch.
- **Documentation**: Add modular skeleton section to `docs/specs/assets/skeletal-mesh.md`. Update `docs/specs/engine/optimization-systems.md` section 4.
- **Tests**: Integration test: create a base skeleton with 20 bones, create two equipment meshes with 10-bone subsets, verify `BoneRemapTable` correctly maps equipment bones to base skeleton bones. Verify that after `attach_equipment`, the skinning dispatch is issued with correct piece count. Verify that `detach_equipment` reduces dispatch piece count.

## Impact

- **Affected specs**: `docs/specs/assets/skeletal-mesh.md`, `docs/specs/assets/animation-system.md`, `docs/specs/engine/optimization-systems.md`
- **Affected code**: `src/engine/components/merged_skeleton.h` (new), `src/engine/systems/merged_skeleton_system.cpp` (new), `src/shaders/skinning/merged_skinning.hlsl` (new), `src/asset/importer/bone_remapper.cpp` (new), `src/engine/api/character_system.cpp`, `src/renderer/render_system.cpp`
- **Breaking change**: NO — additive component. Existing `CSkeletalMesh` entities without `CMergedSkeleton` are unaffected.
- **User benefit**: 6-8x fewer GPU skinning dispatches for fully-equipped characters. Equipment swaps take one frame and produce no visual pop. Enables 200+ uniquely-equipped characters on screen without exceeding GPU skinning time budget. Correct architecture for MMO/RPG character systems from the start.
