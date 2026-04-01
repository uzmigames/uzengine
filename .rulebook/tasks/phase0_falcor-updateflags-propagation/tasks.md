## Session 1: Dirty Flags on ECS Components

- [ ] 1.1 Add `uint32_t dirtyFlags` field and flag bit enum to `TransformComponent` — bits: `POSITION_DIRTY = 1`, `ROTATION_DIRTY = 2`, `SCALE_DIRTY = 4`. Wrap existing setters to set flags; add `ClearDirty()` method called after CB write.
- [ ] 1.2 Add `dirtyFlags` to `DirectionalLightComponent` and `PointLightComponent` — bits: `COLOR_DIRTY = 1`, `INTENSITY_DIRTY = 2`, `DIRECTION_DIRTY = 4`, `ENABLED_DIRTY = 8`. Property setters set flags.
- [ ] 1.3 Add `dirtyFlags` to `CameraComponent` — bits: `POSITION_DIRTY = 1`, `PROJECTION_DIRTY = 2`, `FOV_DIRTY = 4`. Update camera property setters.
- [ ] 1.4 Add `dirtyFlags` to `HeightFogComponent`, `VolumetricCloudsComponent`, `PostProcessComponent` — one `PARAMS_DIRTY = 1` bit each (whole-component granularity is sufficient for these).
- [ ] 1.5 Build + verify: `zig build` compiles, no regressions in rendering output

## Session 2: PerFrameCB Conditional Update + Stats + Verification

- [ ] 2.1 Add frame-level `SceneUpdateFlags` struct in `per_frame_params.h` — bitfield aggregating all dirty state across all components that frame; computed by OR-ing component dirty flags during scene gather
- [ ] 2.2 Update `per_frame_params.cpp`: before each CB section write (transform block, light block, camera block, etc.), check `SceneUpdateFlags`; skip the write and ring allocation if the relevant bits are clear
- [ ] 2.3 After each CB section is written (or skipped), call `component.ClearDirty()` so the next frame starts with clean flags
- [ ] 2.4 Add stats counter `cbUpdatesSkipped` to the `--stats` overlay: incremented for each skipped CB section per frame. Display as "CB skip: N/M" where M is total CB sections.
- [ ] 2.5 Build + verify: run demo with a static scene (no Lua animation) for 60 frames, confirm `--stats` shows CB updates skipped > 0. Run demo with animated camera — confirm transforms are NOT skipped. Screenshot both modes to verify visual correctness.
