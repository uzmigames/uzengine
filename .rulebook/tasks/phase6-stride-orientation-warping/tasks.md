## 1. Skeleton Configuration Asset for Stride/Orientation Warping
- [ ] 1.1 Add a `[stride_warp_config]` block to the skeleton asset INI format in `src/assets/SkeletonAsset.h` with fields: `left_foot_ik_bone` (string), `right_foot_ik_bone` (string), `hip_root_bone` (string)
- [ ] 1.2 Update `SkeletonAsset::load()` in `src/assets/SkeletonAsset.cpp` to parse the `[stride_warp_config]` block, resolve bone names to indices, and store them as `left_foot_ik_index`, `right_foot_ik_index`, `hip_root_index` (int fields)
- [ ] 1.3 Log a warning at load time if any of the three required bone names cannot be resolved in the skeleton; stride and orientation warping will be silently disabled for that skeleton until the config is corrected

## 2. Stride Vector Computation
- [ ] 2.1 Create `src/animation/StrideWarpingSolver.h` declaring `StrideWarpingSolver` with `update(float delta_time, EntityWorld& world, const SkeletonAsset& skeleton_config)` method
- [ ] 2.2 Create `src/animation/StrideWarpingSolver.cpp` implementing per-frame stride vector computation: extract the animation's baked root motion direction from the current clip's root motion track (forward vector), compute the angle between that direction and the character's actual world-space velocity direction using `atan2(cross, dot)`
- [ ] 2.3 For each foot, project the authored IK target position onto the actual movement direction: `warped_target = original_target + stride_axis_perpendicular * dot(original_target - hip_pos, perp_stride_dir)` where `perp_stride_dir` is the component of movement direction perpendicular to the animation's forward axis
- [ ] 2.4 Scale the IK target's distance from hip along the movement direction to match the actual stride length: `warped_target += move_dir * (original_stride_length - projected_stride_length)` ensuring the foot reaches the correct world-space position for the current movement vector

## 3. Hip Rotation Blend (Orientation Warping)
- [ ] 3.1 In `StrideWarpingSolver::update()`, compute the target hip rotation as the quaternion that rotates the character's forward axis to the velocity direction
- [ ] 3.2 Smoothly rotate the hip/pelvis bone toward the velocity direction at a configurable `hip_rotation_speed_deg_per_s` (default 180.0f degrees/second) using `rotate_toward()` helper; clamp total hip twist to `max_hip_twist_degrees` (default 60.0f) to prevent anatomically impossible poses
- [ ] 3.3 Apply the hip rotation as an additive rotation delta on the `hip_root_bone` local transform after stride warping and before foot IK passes so the foot IK positions are computed relative to the already-twisted hip

## 4. Motion Warping System Integration
- [ ] 4.1 Add `stride_warp_enabled` (bool, default false) and `orientation_warp_enabled` (bool, default false) fields to the animation state machine node config in the `.uzanim` state machine INI format
- [ ] 4.2 In `AnimatorSystem::update()`, check these flags per active state and pass the enable bits to `StrideWarpingSolver::update()` so stride and orientation warping can be selectively enabled per animation state without requiring separate components
- [ ] 4.3 Integrate with `RootMotionSolver` (phase6-motion-warping): when both motion warping and stride warping are active, apply motion warping first (root position warp), then stride warping (per-leg offset), to avoid double-warping the root

## 5. CCharacterController Integration
- [ ] 5.1 Add `stride_warp_config` (AssetHandle, optional) and `hip_rotation_speed_deg_per_s` (float, default 180.0f), `max_hip_twist_degrees` (float, default 60.0f) fields to `src/components/character_controller.h`
- [ ] 5.2 In `StrideWarpingSolver`, read velocity from the `CCharacterController` component's `velocity` field (world-space, m/s) for the actual movement direction computation; fall back to the entity's root motion direction if no `CCharacterController` is present

## 6. Testing
- [ ] 6.1 Create `tests/animation/test_stride_warping_solver.cpp` with Google Test cases: moving exactly in the animation's forward direction produces zero stride offset, moving 90 degrees left produces IK targets shifted fully to the left, at 45 degrees the targets are projected at 45-degree offset proportional to velocity angle
- [ ] 6.2 Add hip rotation tests: hip rotates toward velocity direction at configured speed, hip twist does not exceed `max_hip_twist_degrees`, at zero velocity hip rotation stays at current value (no snap-to-forward)
- [ ] 6.3 Add skeleton config tests: missing `left_foot_ik_bone` in skeleton asset logs a warning and disables warping for that skeleton, valid config resolves bone indices correctly
- [ ] 6.4 Add state machine integration tests: state with `stride_warp_enabled = false` does not call `StrideWarpingSolver`, state with `stride_warp_enabled = true` calls it; flag can be toggled between state transitions without crash
- [ ] 6.5 Add combined motion warping + stride warping integration test: root is warped by motion warping to target, stride offset is applied on top, final foot positions are within 5cm of expected warped positions
- [ ] 6.6 Verify line coverage >= 95% and branch coverage >= 95% across `StrideWarpingSolver.cpp` all code paths

## 7. Documentation
- [ ] 7.1 Update `docs/specs/animation-system.md` with `StrideWarpingSolver` algorithm description (stride vector projection, hip rotation blend), the `stride_warp_enabled`/`orientation_warp_enabled` state machine node flags, and the integration order relative to motion warping and foot IK
- [ ] 7.2 Update `docs/specs/skeletal-mesh.md` with the `[stride_warp_config]` skeleton asset block reference and the three required bone name fields with their anatomical meaning
