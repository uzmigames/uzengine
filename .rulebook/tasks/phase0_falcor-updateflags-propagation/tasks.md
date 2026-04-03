## Session 1: Dirty Flags on ECS Components

- [x] 1.1 Created `dirty_flags.h` — `CBDirtyMixin` base class with `MarkCBDirty(flags)`, `IsCBDirty()`, `ClearCBDirty()`, `GetCBDirtyFlags()`. Per-component flag namespaces: `TransformDirty`, `LightDirty`, `CameraDirty`, `EnvironmentDirty`. Default: all dirty (0xFFFFFFFF) for first-frame upload.
- [x] 1.2 Added `CBDirtyMixin` to `SceneComponent` (base of all scene entities). `SetPosition/SetRotation/SetScale` call `MarkCBDirty(TransformDirty::Position/Rotation/Scale)`. `LightComponent` inherits via `PrimitiveComponent → SceneComponent`.
- [x] 1.3 `CameraComponent : SceneComponent` — inherits `CBDirtyMixin` automatically.
- [x] 1.4 Added `CBDirtyMixin` to `HeightFogComponent`, `VolumetricCloudsComponent`, `PostProcessVolumeComponent` (plain structs → public inheritance).
- [x] 1.5 Build clean (zig build), 0 errors, 201 FPS. No rendering regressions.

## Session 2: PerFrameCB Conditional Update + Stats + Verification

- [ ] 2.1 Add frame-level `SceneUpdateFlags` struct in `per_frame_params.h` — bitfield aggregating all dirty state across all components that frame; computed by OR-ing component dirty flags during scene gather
- [ ] 2.2 Update `per_frame_params.cpp`: before each CB section write (transform block, light block, camera block, etc.), check `SceneUpdateFlags`; skip the write and ring allocation if the relevant bits are clear
- [ ] 2.3 After each CB section is written (or skipped), call `component.ClearDirty()` so the next frame starts with clean flags
- [ ] 2.4 Add stats counter `cbUpdatesSkipped` to the `--stats` overlay: incremented for each skipped CB section per frame. Display as "CB skip: N/M" where M is total CB sections.
- [ ] 2.5 Build + verify: run demo with a static scene (no Lua animation) for 60 frames, confirm `--stats` shows CB updates skipped > 0. Run demo with animated camera — confirm transforms are NOT skipped. Screenshot both modes to verify visual correctness.
