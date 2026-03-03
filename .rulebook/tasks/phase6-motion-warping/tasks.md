## 1. MotionWarpTarget Data Structure
- [ ] 1.1 Define `MotionWarpTarget` struct in `src/animation/RootMotionSolver.h` with fields: `name` (string, 64-char max key), `position` (float3, target world-space position), `rotation` (quaternion, target world-space rotation), `window_start_s` (float, animation time in seconds), `window_end_s` (float, animation time in seconds)
- [ ] 1.2 Validate `MotionWarpTarget` on registration: `window_start_s < window_end_s`, `window_end_s > 0`; log an error and reject invalid targets
- [ ] 1.3 Add `std::unordered_map<std::string, MotionWarpTarget> warp_targets` to the `CAnimator` component (`src/scene/components.h`) as the runtime target registry

## 2. Root Motion Trajectory Pre-Pass
- [ ] 2.1 Create `src/animation/RootMotionSolver.cpp` implementing `RootMotionSolver::extract_unwarped_delta(const AnimClip& clip, float window_start_s, float window_end_s, float3& out_delta_pos, quaternion& out_delta_rot)` that samples the clip's root bone at `window_start_s` and `window_end_s` and computes the unwarped delta transform between those two times
- [ ] 2.2 Cache the extracted unwarped delta per active warp target at the start of each warp window (i.e., when `current_time` first enters `window_start_s`); do not re-extract every frame to avoid drift from floating-point accumulation

## 3. Per-Frame Warp Matrix Computation
- [ ] 3.1 In `RootMotionSolver::compute_warp_delta(const MotionWarpTarget& target, float current_time_s, const float3& cached_unwarped_total_delta, float3& out_warped_delta_pos, quaternion& out_warped_delta_rot)`, compute remaining window fraction `t = (window_end_s - current_time_s) / (window_end_s - window_start_s)` clamped to [0, 1]
- [ ] 3.2 Scale the per-frame root delta so that the accumulated remaining warp exactly reaches `target.position` by `window_end_s`; formula: `warped_delta = (target_position - current_root_position) / remaining_frames_approx` where remaining_frames is estimated from remaining window time and last frame's delta time
- [ ] 3.3 Apply the same proportional scaling to the rotation component, using spherical linear interpolation between current root rotation and `target.rotation` driven by window progress
- [ ] 3.4 Clamp the warp delta magnitude to 3x the unwarped delta magnitude per frame to prevent extreme stretching on the last frame when targets are far from the authored trajectory

## 4. AnimatorSystem Integration
- [ ] 4.1 In `AnimatorSystem::update()` (`src/animation/AnimatorSystem.cpp`), after sampling the base animation pose, check if the current animation time falls within any registered warp window for any active `MotionWarpTarget`
- [ ] 4.2 For each active warp window, call `RootMotionSolver::compute_warp_delta()` and apply the resulting additive warp transform to the root bone position and rotation before the bone palette is uploaded to the GPU
- [ ] 4.3 Remove a warp target from the registry automatically when its `window_end_s` has been passed (i.e., the current animation time exceeds `window_end_s`)

## 5. Lua Scripting API
- [ ] 5.1 Expose `ctx.anim:set_warp_target(name, pos, rot)` in `src/scripting/LuaAnimBindings.cpp` where `pos` is a `{x, y, z}` table and `rot` is a `{x, y, z, w}` table; the function also accepts optional `window_start` and `window_end` fields in a config table as a fourth argument, defaulting to the current active state's configured warp window if not provided
- [ ] 5.2 Expose `ctx.anim:clear_warp_target(name)` that removes the named target from the registry immediately
- [ ] 5.3 Expose `ctx.anim:clear_all_warp_targets()` for cleanup on state machine transitions
- [ ] 5.4 Update `docs/specs/lua-scripting.md` with all three Lua warp target API functions, parameter types, and an example usage for a ledge-grab animation

## 6. Testing
- [ ] 6.1 Create `tests/animation/test_root_motion_solver.cpp` with Google Test cases: `extract_unwarped_delta()` returns zero delta for a stationary clip, returns correct delta for a 2m/s forward-moving clip sampled over 0.5s window
- [ ] 6.2 Add warp computation tests: `compute_warp_delta()` at the midpoint of the window produces a delta that, when accumulated, will reach the target within 5mm, delta is clamped to 3x unwarped magnitude, at `window_end_s` the remaining warp is applied fully
- [ ] 6.3 Add integration tests: entity starting 1m left of authored target reaches within 5cm of target at `window_end_s`, target registered after `window_start_s` is silently ignored, warp target is removed from registry after `window_end_s` is passed
- [ ] 6.4 Add Lua API tests: `set_warp_target()` populates `CAnimator::warp_targets`, `clear_warp_target()` removes the named entry, `clear_all_warp_targets()` empties the map
- [ ] 6.5 Add edge case tests: two simultaneous warp targets with non-overlapping windows are both applied independently; overlapping windows produce a warning and only the first registered target is applied
- [ ] 6.6 Verify line coverage >= 95% and branch coverage >= 95% across `RootMotionSolver.cpp` and the `LuaAnimBindings.cpp` warp paths

## 7. Documentation
- [ ] 7.1 Update `docs/specs/animation-system.md` with `MotionWarpTarget` struct fields, warp window definition, warp computation algorithm, automatic target removal behavior, and the 3x magnitude clamp
- [ ] 7.2 Update `docs/specs/skeletal-mesh.md` with a note that motion warping requires the active clip to have root motion data; clips without root motion produce no warp effect
- [ ] 7.3 Update `docs/specs/lua-scripting.md` with `ctx.anim:set_warp_target()`, `ctx.anim:clear_warp_target()`, and `ctx.anim:clear_all_warp_targets()` API reference including a ledge-grab usage example
