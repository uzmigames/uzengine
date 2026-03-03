## 1. AimOffset Asset Type
- [ ] 1.1 Create `src/assets/AimOffsetAsset.h` defining `AimOffsetAsset` struct with fields: `grid_rows` (int, pitch count), `grid_cols` (int, yaw count), `pitch_range_min` (float, degrees), `pitch_range_max` (float, degrees), `yaw_range_min` (float, degrees), `yaw_range_max` (float, degrees), and `poses[grid_rows][grid_cols]` array of additive pose handles
- [ ] 1.2 Create `src/assets/AimOffsetAsset.cpp` implementing `AimOffsetAsset::load_from_file(const char* path)` that parses the `.uzaimoffset` INI format and populates all fields
- [ ] 1.3 Implement INI serialization: `AimOffsetAsset::save_to_file(const char* path)` writing `[aim_offset]` section with `grid_rows`, `grid_cols`, `pitch_range_min/max`, `yaw_range_min/max`, and `[[pose]]` blocks per grid cell
- [ ] 1.4 Register `.uzaimoffset` extension in the asset registry (`src/assets/AssetRegistry.cpp`) so the asset manager resolves and caches these files by handle
- [ ] 1.5 Add `AimOffsetAsset` to `docs/specs/asset-pipeline.md` documenting the INI format and all supported fields

## 2. AimOffsetSampler
- [ ] 2.1 Create `src/animation/AimOffsetSampler.h` declaring `AimOffsetSampler` with method `sample(const AimOffsetAsset& asset, float pitch_deg, float yaw_deg, Pose& out_additive_pose)`
- [ ] 2.2 Implement bilinear interpolation in `AimOffsetSampler::sample()`: compute normalized UV from pitch/yaw within range, find the four surrounding grid poses, blend them with 2D lerp using `Pose::blend_additive()`
- [ ] 2.3 Clamp input pitch/yaw to asset range before interpolation; log a warning when clamping occurs more than once per second per entity to avoid log spam
- [ ] 2.4 Handle edge cases: single-row grid (pure yaw blend, skip pitch axis), single-column grid (pure pitch blend), 1x1 grid (direct additive apply)

## 3. CAimOffset ECS Component
- [ ] 3.1 Add `CAimOffset` struct to `src/scene/components.h` with fields: `aim_offset_handle` (AssetHandle), `current_pitch_deg` (float), `current_yaw_deg` (float), `interp_speed_deg_per_s` (float, default 360.0f)
- [ ] 3.2 Register `CAimOffset` in the ECS component registry (`src/scene/ComponentRegistry.cpp`) with reflection metadata for editor inspector display
- [ ] 3.3 Implement serialization of `CAimOffset` in `src/scene/SceneSerializer.cpp` as an `[aim_offset]` INI section per entity

## 4. AnimatorSystem Integration
- [ ] 4.1 Add additive blending post-process stage in `AnimatorSystem::update()` in `src/animation/AnimatorSystem.cpp` that runs after the base state machine evaluation and before bone palette upload
- [ ] 4.2 In the additive stage, retrieve `CAimOffset` for the entity, call `AimOffsetSampler::sample()`, and apply the result via `Pose::apply_additive()` on the base pose output
- [ ] 4.3 Implement smooth input interpolation: each tick, advance `current_pitch_deg` and `current_yaw_deg` toward the target values at `interp_speed_deg_per_s * delta_time` using `move_toward()` helper
- [ ] 4.4 Skip the aim offset stage if the entity's animation LOD level is LOD2 or LOD3 (consistent with phase6-animation-lod specification)
- [ ] 4.5 Add a profiling marker `PROFILE_SCOPE("AimOffsetSampler")` around the additive stage for Pix/Optick visibility

## 5. Lua Scripting API
- [ ] 5.1 Expose `ctx.anim:set_aim_direction(pitch_deg, yaw_deg)` in `src/scripting/LuaAnimBindings.cpp` that writes target pitch/yaw into the entity's `CAimOffset` component; assert the entity has `CAimOffset` and log an error if missing
- [ ] 5.2 Expose `ctx.anim:get_aim_direction()` returning `{pitch, yaw}` table with the current interpolated values
- [ ] 5.3 Expose `ctx.anim:set_aim_interp_speed(degrees_per_sec)` for runtime tuning of interpolation speed
- [ ] 5.4 Update `docs/specs/lua-scripting.md` with the three new `ctx.anim` functions, parameter types, and usage examples

## 6. Testing
- [ ] 6.1 Create `tests/animation/test_aim_offset_asset.cpp` with Google Test cases: load valid `.uzaimoffset`, reject missing required keys, verify round-trip save/load preserves all fields
- [ ] 6.2 Create `tests/animation/test_aim_offset_sampler.cpp` covering: bilinear interpolation at grid corners returns exact pose values, interpolation at grid center is blend of four poses, clamping at range boundaries, 1x1 grid direct apply, single-row/column degenerate grids
- [ ] 6.3 Create `tests/animation/test_aim_offset_component.cpp` covering: `CAimOffset` serialization round-trip, smooth interpolation advances toward target by correct delta, interpolation does not overshoot target
- [ ] 6.4 Create `tests/animation/test_aim_offset_integration.cpp` covering: `AnimatorSystem` applies additive pose when `CAimOffset` present, skips additive stage at LOD2, skips additive stage at LOD3, applies no offset when aim asset handle is null
- [ ] 6.5 Add Lua binding unit tests in `tests/scripting/test_lua_anim_bindings.cpp` for `set_aim_direction`, `get_aim_direction`, and `set_aim_interp_speed` including the missing-component error path
- [ ] 6.6 Verify line coverage >= 95% and branch coverage >= 95% across all new source files using OpenCppCoverage; fix any uncovered branches before committing

## 7. Documentation
- [ ] 7.1 Update `docs/specs/animation-system.md` with the `CAimOffset` component field reference, `AimOffsetSampler` algorithm description, and the additive blending stage order diagram
- [ ] 7.2 Update `docs/specs/asset-pipeline.md` with `.uzaimoffset` INI format specification including a complete file example
