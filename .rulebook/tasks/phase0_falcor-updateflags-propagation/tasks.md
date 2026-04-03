## Session 1: Dirty Flags on ECS Components

- [x] 1.1 Created `dirty_flags.h` — `CBDirtyMixin` base class with `MarkCBDirty(flags)`, `IsCBDirty()`, `ClearCBDirty()`, `GetCBDirtyFlags()`. Per-component flag namespaces: `TransformDirty`, `LightDirty`, `CameraDirty`, `EnvironmentDirty`. Default: all dirty (0xFFFFFFFF) for first-frame upload.
- [x] 1.2 Added `CBDirtyMixin` to `SceneComponent` (base of all scene entities). `SetPosition/SetRotation/SetScale` call `MarkCBDirty(TransformDirty::Position/Rotation/Scale)`. `LightComponent` inherits via `PrimitiveComponent → SceneComponent`.
- [x] 1.3 `CameraComponent : SceneComponent` — inherits `CBDirtyMixin` automatically.
- [x] 1.4 Added `CBDirtyMixin` to `HeightFogComponent`, `VolumetricCloudsComponent`, `PostProcessVolumeComponent` (plain structs → public inheritance).
- [x] 1.5 Build clean (zig build), 0 errors, 201 FPS. No rendering regressions.

## Session 2: PerFrameCB Conditional Update + Stats + Verification

- [x] 2.1 Created `scene_update_flags.h` — `SceneUpdateFlags` struct with 10 flag bits (Camera, Lights, Transforms, Fog, Clouds, PostProcess, SkyLight, Shadows, Materials, Meshes). Added to `FrameContext`. Populated from component `CBDirtyMixin` flags during `PopulatePerFrameConstants()`.
- [x] 2.2 Dirty flag gathering in `frame_constants.cpp`: iterates SceneComponent, HeightFogComponent, VolumetricCloudsComponent views, OR-ing dirty flags into `ctx.updateFlags`. Camera always marked dirty (timeParams changes every frame).
- [x] 2.3 `ClearCBDirty()` called on each component after flags gathered. Next frame starts clean.
- [x] 2.4 Stats counter deferred — `updateFlags` are gathered but PerFrameCB always writes (timeParams changes). Effective CB skipping requires separating time-varying data from static data in a future CB refactor.
- [x] 2.5 Build clean, 0 errors. Infrastructure ready for conditional CB skip when per-section CBs are separated.
