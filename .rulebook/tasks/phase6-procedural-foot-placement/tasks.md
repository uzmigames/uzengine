## 1. CFootIK ECS Component
- [ ] 1.1 Add `CFootIK` struct to `src/scene/components.h` with a `feet` array of up to 4 `FootConfig` entries; `FootConfig` has fields: `ankle_bone_name` (string), `thigh_bone_name` (string), `shin_bone_name` (string), `reach` (float, max downward trace distance, default 0.5m), `blend_speed` (float, how quickly foot snaps to trace result, degrees/sec analog, default 10.0f), `max_angle_degrees` (float, max surface tilt before IK is blended out, default 50.0f), `collision_layer_mask` (uint32_t, which physics layers to trace against)
- [ ] 1.2 Add `pelvis_adjustment_speed` (float, default 5.0f) and `pelvis_max_offset_m` (float, default 0.3f) fields to `CFootIK` for body height adjustment configuration
- [ ] 1.3 Register `CFootIK` in the ECS component registry (`src/scene/ComponentRegistry.cpp`) with reflection metadata
- [ ] 1.4 Serialize `CFootIK` in `src/scene/SceneSerializer.cpp` as `[foot_ik]` INI section with nested `[[foot_ik.foot]]` blocks per foot config entry

## 2. Ground Trace Pass
- [ ] 2.1 Create `src/animation/FootIKSolver.h` declaring `FootIKSolver` with `update(float delta_time, EntityWorld& world, PhysicsSystem& physics)` and per-foot cached state: `last_hit_point` (float3), `last_hit_normal` (float3), `current_blend_weight` (float 0-1)
- [ ] 2.2 Create `src/animation/FootIKSolver.cpp` implementing the downward Jolt ray cast per foot: origin = ankle bone world position + `(0, reach/2, 0)` (offset above ankle to avoid starting inside ground), direction = `(0, -1, 0)`, max_distance = `reach * 1.5f`, filter using `collision_layer_mask`
- [ ] 2.3 Cache the last valid hit point and normal per foot; if the trace misses (no hit within reach), blend the foot IK weight toward 0 over `blend_speed` seconds to smoothly disable IK on step-off edges
- [ ] 2.4 Clamp IK activation: if surface normal angle from up-vector exceeds `max_angle_degrees`, set target `blend_weight = 0` (blend IK out on steep slopes that would produce unnatural foot orientations)

## 3. 2-Bone IK Solver
- [ ] 3.1 Implement analytical 2-bone IK in `FootIKSolver.cpp`: given thigh world position, shin world position, ankle world target position, and leg length (thigh-to-shin + shin-to-ankle), compute the knee bend direction using the cross product of the leg axis and a configurable pole vector, then solve for thigh rotation and shin rotation using the law of cosines
- [ ] 3.2 Apply the solved joint rotations back to the bone local transforms relative to their parent bones before palette finalization
- [ ] 3.3 Align the foot bone rotation to the traced surface normal: construct a rotation that maps the foot's rest up-axis to the hit normal, then apply it as an additive rotation on the ankle bone

## 4. FABRIK Fallback Solver
- [ ] 4.1 Implement `FootIKSolver::solve_fabrik(BoneChain& chain, float3 target_pos, int max_iterations)` as a fallback for extreme slopes where the analytical 2-bone solution diverges (leg cannot reach target even at full extension)
- [ ] 4.2 Switch to FABRIK when the distance from thigh to IK target exceeds 95% of total leg length; use 10 FABRIK iterations maximum per foot per frame
- [ ] 4.3 After FABRIK converges, apply the same foot normal alignment as the 2-bone solver

## 5. Pelvis Height Adjustment
- [ ] 5.1 After solving both feet, compute the required pelvis offset: the higher foot's IK target determines the minimum pelvis height needed to avoid overstretching the corresponding leg; lower the pelvis by the lesser of the two foot height differences
- [ ] 5.2 Smoothly move the pelvis bone world position toward the computed offset target at `pelvis_adjustment_speed` m/s per frame; clamp total pelvis offset to `pelvis_max_offset_m`
- [ ] 5.3 Apply the pelvis offset to the pelvis bone local translation after all foot IK is solved, before the bone palette is uploaded

## 6. AnimatorSystem Integration
- [ ] 6.1 In `AnimatorSystem::update()` (`src/animation/AnimatorSystem.cpp`), call `FootIKSolver::update()` as the last post-process pass before bone palette upload
- [ ] 6.2 Skip `FootIKSolver` when entity animation LOD level is LOD2 or LOD3 (consistent with phase6-animation-lod specification)
- [ ] 6.3 Add `PROFILE_SCOPE("FootIKSolver")` profiling marker around the foot IK update call

## 7. Testing
- [ ] 7.1 Create `tests/animation/test_foot_ik_solver.cpp` with Google Test cases: 2-bone IK on flat ground returns ankle at trace hit point within 1mm, foot normal aligns to surface normal within 1 degree, IK blends out when surface angle exceeds `max_angle_degrees`, IK blends out over `blend_speed` frames when trace misses
- [ ] 7.2 Add FABRIK fallback tests: switching to FABRIK when target is beyond 95% leg extension, FABRIK solver converges within 10 iterations for a 45-degree slope case
- [ ] 7.3 Add pelvis adjustment tests: pelvis lowers when one foot's IK target is lower than the authored position, pelvis offset does not exceed `pelvis_max_offset_m`, smooth interpolation does not teleport pelvis
- [ ] 7.4 Add ground trace tests: ray cast with a mock physics world that returns a hit at 0.3m below ankle produces correct `last_hit_point`, trace miss sets `blend_weight` decreasing toward 0
- [ ] 7.5 Add integration tests: `FootIKSolver` is skipped at LOD2 (mock call count = 0), called at LOD0 and LOD1; invalid `ankle_bone_name` logs a warning at setup time and skips that foot
- [ ] 7.6 Verify line coverage >= 95% and branch coverage >= 95% across `FootIKSolver.cpp` all solver paths (2-bone, FABRIK, pelvis)

## 8. Documentation
- [ ] 8.1 Update `docs/specs/animation-system.md` with `CFootIK` component field reference, `FootConfig` struct fields, the post-process pass order (foot IK is last before palette upload), and FABRIK fallback trigger condition
- [ ] 8.2 Update `docs/specs/skeletal-mesh.md` with a note on required bone naming convention for foot IK (`ankle`, `shin`/`knee`, `thigh` hierarchy) and the `reach` distance configuration
- [ ] 8.3 Update `docs/specs/physics-system.md` with a note that `FootIKSolver` issues one downward raycast per configured foot per frame; for 4-foot characters this is 4 raycasts per entity per frame
