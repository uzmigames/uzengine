# Tasks: phase6-modular-skeleton-merge

## 1. CMergedSkeleton Component

- [ ] 1.1 Create `src/engine/components/merged_skeleton.h` defining `CMergedSkeleton` component struct
- [ ] 1.2 Define fields: `base_skeleton`, `equipment_count` (max 8), `equipment[8]: Handle<SkeletalMesh>`, `equipment_material[8]: Handle<Material>`, `remap[8]: BoneRemapTable`, `bone_palette: GpuBuffer*`
- [ ] 1.3 Define `BoneRemapTable` struct: `{count: uint16, remap[256]: uint16}` mapping equipment bone indices to base skeleton bone indices
- [ ] 1.4 Define equipment slot name constants: `"head"`, `"torso"`, `"arms"`, `"hands"`, `"legs"`, `"feet"`, `"cloak"`, `"extra"`
- [ ] 1.5 Initialize all equipment slots to null handles; `equipment_count` to 0

## 2. BoneRemapper

- [ ] 2.1 Create `src/asset/importer/bone_remapper.h` and `src/asset/importer/bone_remapper.cpp`
- [ ] 2.2 Implement `BoneRemapper::Compute(equipment_skeleton, base_skeleton) -> BoneRemapTable`
- [ ] 2.3 Match bones by name string comparison between equipment and base skeletons
- [ ] 2.4 Log `WARN_BONE_NOT_FOUND` for equipment bones not present in base skeleton; map them to identity transform (bone index 0)
- [ ] 2.5 The `BoneRemapper::Compute` call happens at equipment attach time only — never every frame

## 3. MergedSkinningCompute Shader

- [ ] 3.1 Create `src/shaders/skinning/merged_skinning.hlsl` compute shader
- [ ] 3.2 Reads: per-piece `g_InputVertices[]`, `g_Remap[8]` bone index remapping tables, `g_BonePalette` (full evaluated bone transforms)
- [ ] 3.3 Applies 4-bone weighted linear blend skinning per vertex using the remapped bone indices
- [ ] 3.4 Writes to `g_OutputVertices[]` — one pre-skinned output buffer per equipment piece
- [ ] 3.5 Dispatch size: `(max_vertices_per_piece, equipment_count, 1)`

## 4. MergedSkeletonSystem ECS System

- [ ] 4.1 Create `src/engine/systems/merged_skeleton_system.h` and `src/engine/systems/merged_skeleton_system.cpp`
- [ ] 4.2 Per frame: retrieve evaluated bone palette from the animation system for the entity's `CAnimator`
- [ ] 4.3 For each entity with `CMergedSkeleton`: upload the full bone palette to the `bone_palette` GPU buffer
- [ ] 4.4 Dispatch `merged_skinning.hlsl` compute shader with dispatch size `(max_vertices_per_piece, equipment_count, 1)`
- [ ] 4.5 Skip equipment slots with null mesh handles

## 5. CharacterSystem API

- [ ] 5.1 Add `CharacterSystem::attach_equipment(entity_id, slot, mesh_handle, material_handle)` to `src/engine/api/character_system.cpp`
- [ ] 5.2 On attach: update `CMergedSkeleton` equipment slot, call `BoneRemapper::Compute` for the new mesh, mark the slot dirty for remap table upload next frame
- [ ] 5.3 Add `CharacterSystem::detach_equipment(entity_id, slot)`: set equipment slot to null handle; `MergedSkeletonSystem` skips null slots automatically

## 6. Render System Integration

- [ ] 6.1 Update `src/renderer/render_system.cpp` to detect `CMergedSkeleton` on entities during GBuffer pass submission
- [ ] 6.2 When `CMergedSkeleton` is present, issue draw calls for each populated equipment slot using the pre-skinned output vertex buffers from the compute dispatch
- [ ] 6.3 Ensure each equipment slot issues its own draw call (separate materials) while sharing the single skinning compute dispatch

## 7. Lua Bindings

- [ ] 7.1 Add `ctx.character:attach_equipment(entity, { slot, mesh, material })` Lua binding calling `CharacterSystem::attach_equipment`
- [ ] 7.2 Add `ctx.character:detach_equipment(entity, slot)` Lua binding calling `CharacterSystem::detach_equipment`
- [ ] 7.3 Expose slot name constants in Lua: `"head"`, `"torso"`, `"arms"`, `"hands"`, `"legs"`, `"feet"`, `"cloak"`, `"extra"`

## 8. IPC Handler

- [ ] 8.1 Add `character.setEquipment { entity_id, slot, mesh_path, material_path }` IPC handler in `src/ipc/handlers/entity_handlers.cpp` for editor equipment preview
- [ ] 8.2 Register the IPC handler in the engine's IPC dispatch table

## 9. Testing (≥95% line + branch coverage)

- [ ] 9.1 Write `tests/core/test_bone_remapper.cpp`: create a base skeleton with 20 bones and an equipment skeleton with 10-bone subset; call `BoneRemapper::Compute`; assert `BoneRemapTable` correctly maps each equipment bone index to the corresponding base skeleton bone index; assert missing bones are mapped to index 0 with warning logged
- [ ] 9.2 Write `tests/core/test_merged_skeleton_attach_detach.cpp`: attach two equipment meshes to an entity; assert `equipment_count == 2` and compute dispatch issued with correct piece count; detach one; assert `equipment_count == 1` and dispatch re-runs next frame
- [ ] 9.3 Write `tests/core/test_merged_skeleton_no_physics.cpp`: create entity with `CMergedSkeleton` and `CCollider`; assert `CMergedSkeleton` entities still function (no physics body created for equipment meshes)
- [ ] 9.4 Write `tests/core/test_merged_skeleton_remap_table_upload.cpp`: attach equipment with known `BoneRemapTable`; assert the GPU buffer is updated on the next frame and the uploaded data matches the remap table
- [ ] 9.5 Write `tests/core/test_character_system_api.cpp`: call `attach_equipment` with mesh and material handles; assert `CMergedSkeleton.equipment[slot]` set correctly; call `detach_equipment`; assert slot cleared
- [ ] 9.6 Verify ≥95% line and branch coverage across all `src/engine/systems/merged_skeleton_system.cpp`, `src/asset/importer/bone_remapper.cpp`, and `src/engine/api/character_system.cpp` using OpenCppCoverage
