## 1. CSkeletalMesh LOD Level Definitions
- [ ] 1.1 Add `AnimLODLevel` enum to `src/components/CSkeletalMesh.h` with values: `LOD0` (0-10m, full skeleton), `LOD1` (10-25m, 50% bones), `LOD2` (25-50m, 25% bones), `LOD3` (50m+, 10% bones)
- [ ] 1.2 Add per-component distance threshold fields to `CSkeletalMesh`: `lod_distance_0` (float, default 10.0f), `lod_distance_1` (float, default 25.0f), `lod_distance_2` (float, default 50.0f); thresholds must be validated as strictly increasing on component creation
- [ ] 1.3 Add `current_lod_level` (AnimLODLevel) field to `CSkeletalMesh` updated each frame by `AnimatorSystem` based on camera distance
- [ ] 1.4 Serialize LOD distance thresholds in `src/scene/SceneSerializer.cpp` as `[skeletal_mesh.anim_lod]` INI section fields; default values must not be written to reduce file verbosity

## 2. BoneMask Asset
- [ ] 2.1 Create `src/animation/BoneMask.h` defining `BoneMask` struct with: `bone_flags` bitfield array (indexed by bone index), `always_active_bones[]` list of bone indices that override the mask, and `lod_level` tag
- [ ] 2.2 Create `src/animation/BoneMask.cpp` implementing `BoneMask::build_from_bone_list(const std::vector<std::string>& active_bone_names, const Skeleton& skeleton)` that populates the bitfield from name lookups
- [ ] 2.3 Define the set of always-active critical bones: root, pelvis, spine chain (spine_01 through spine_04), neck_01, head, and all bones tagged as weapon attach sockets in the skeleton asset; these must never be in the mask's inactive set
- [ ] 2.4 Add `BoneMask::is_active(int bone_index) const` returning true if the bone is in the active set or in the always-active override list
- [ ] 2.5 Load per-LOD bone mask from the skeleton asset file (`src/assets/SkeletonAsset.cpp`): the skeleton stores a `[lod_masks]` block listing bone names active at each LOD level

## 3. AnimatorSystem LOD Evaluation
- [ ] 3.1 In `AnimatorSystem::update()` (`src/animation/AnimatorSystem.cpp`), compute camera distance to each entity's world position each frame and update `CSkeletalMesh::current_lod_level` accordingly; use `squared_distance` comparison to avoid sqrt overhead
- [ ] 3.2 Apply the bone mask for the current LOD level before running IK and post-process passes: for each inactive bone in the mask, copy the parent bone's world transform directly without evaluating the animation curve
- [ ] 3.3 Skip `CFootIK` post-process pass entirely when `current_lod_level >= LOD2`
- [ ] 3.4 Skip `CJiggleBone` post-process pass entirely when `current_lod_level >= LOD2`
- [ ] 3.5 Skip `CAimOffset` additive pass entirely when `current_lod_level >= LOD2`
- [ ] 3.6 Add hysteresis to LOD level transitions: promote to a higher LOD (lower detail) only when distance exceeds threshold by 10%, demote to lower LOD (higher detail) only when distance drops below threshold by 10%; this prevents flickering near boundaries

## 4. Debug Visualization
- [ ] 4.1 Add console command `anim_lod_debug [0|1]` in `src/engine/debug/ConsoleCommands.cpp` that toggles a global `g_anim_lod_debug_enabled` bool
- [ ] 4.2 When `g_anim_lod_debug_enabled` is true, color-code active bones in the editor viewport bone overlay: LOD0 = white, LOD1 = yellow, LOD2 = orange, LOD3 = red; inactive bones are drawn grey
- [ ] 4.3 Display a per-entity HUD label showing current LOD level and camera distance when `anim_lod_debug` is enabled, rendered via the debug draw system

## 5. Testing
- [ ] 5.1 Create `tests/animation/test_bone_mask.cpp` with Google Test cases: always-active bones are never masked regardless of LOD level, `build_from_bone_list()` with valid names sets correct bits, unknown bone names are ignored with a warning, `is_active()` returns correct values for active and inactive bones
- [ ] 5.2 Create `tests/animation/test_animation_lod.cpp` covering: LOD level selection at exact boundary distances, LOD level selection inside each range, hysteresis prevents rapid flipping at boundary, always-active bones are evaluated at every LOD level
- [ ] 5.3 Add tests verifying post-process skip behavior: `CFootIK` is not called at LOD2, `CJiggleBone` is not called at LOD2, `CAimOffset` is not called at LOD2, all three are called at LOD1
- [ ] 5.4 Add serialization round-trip tests for LOD distance threshold fields in `CSkeletalMesh`
- [ ] 5.5 Add a performance regression test asserting that the total bone evaluation count for an entity at LOD3 is <= 10% of the full skeleton bone count
- [ ] 5.6 Verify line coverage >= 95% and branch coverage >= 95% across `BoneMask.cpp` and the new AnimatorSystem LOD code paths

## 6. Documentation
- [ ] 6.1 Update `docs/specs/animation-system.md` with LOD level table (distance ranges, bone counts, skipped passes), always-active bone list, and hysteresis behavior description
- [ ] 6.2 Update `docs/specs/skeletal-mesh.md` with `CSkeletalMesh` LOD distance threshold field reference and how to override thresholds per component
- [ ] 6.3 Update `docs/specs/debug-profiling.md` with `anim_lod_debug` console command documentation including the color coding legend
