# Proposal: phase16_vfx-spline-data-interface-hardware-validation

## Why

`phase16_vfx-spline-data-interface` shipped the full CPU surface of the
spline data interface (32 unit tests, 36 total spline-related tests
green): `VfxSpline` Catmull-Rom evaluator with arc-length LUT,
`VfxSplineBuffer` GPU upload wrapper, `SpawnAlongSplineModule`,
`ForceTowardSplineModule`, `FollowSplineModule`, the bindless HLSL
helpers in `spline_di.hlsli`, three sample `.uzfx` assets, module
registry entries, and the `ApplyEmitterStackToModuleStack` round-trip.

The remaining items each require either DX12 hardware to validate
end-to-end (CB plumbing + sim dispatch) or editor-runtime integration
that depends on the live overlay panel:

1. **CB plumbing** — the `Spawn` / `Update` CS CB layouts do NOT yet
   carry `cbSplineSrvIndex` / `cbSplinePointCount` / `cbSplineTotalLength`
   plus the per-module config fields (`cbSplineSpawnStratified`, etc.)
   that the snippet text references. Wiring is a frame_vfx_inline.cpp
   change that detects spline modules on an emitter and fills the CB
   from the emitter's `VfxSplineBuffer`. Verification needs the snippet
   to actually compile inside the host CS, which only happens on a
   real DX12 device.
2. **GPU integration tests** — `test_spline_spawn_distribution`
   (1024 spawned particles must distribute uniformly along arc),
   `test_spline_force_convergence` (particles 200 cm off-spline must
   reach the nearest sample within `attract_distance` after N frames),
   `test_spline_follow_endpoints` (particle at `lifeFrac=0` is at
   `controlPoints[0]`, at `lifeFrac=1` is at the last control point).
3. **Editor "Spline Target" picker** (item 1.9 from the parent task) —
   needs the Niagara Parameters panel layout shipped in phase 12 to
   surface a `vfx_emitter_component.spline_target_entity` reference,
   plus the gameplay-side spline component (the engine has no
   `SplineComponent` ECS type yet — the data interface authors splines
   programmatically against `VfxSpline` for now).

## What Changes

### CB plumbing (`frame_vfx_inline.cpp`)

- Detect when an emitter's spawn / update module stack contains any
  spline module via `dynamic_cast` on each module pointer.
- When detected, ensure the emitter has a backing `VfxSplineBuffer`
  (lazy-create at first dispatch, sized to
  `kVfxSplineDefaultResolution` slots).
- Call `RebuildAndUpload(emitter->splineSource, m_device)` once per
  frame before the simulation dispatch.
- Extend `ParticleSpawnConstants` and `ParticleUpdateConstants` with
  the cbuffer fields documented in `spline_modules.h`. Append at the
  end so existing field offsets are unchanged.
- Mirror the additions on the HLSL side (spawn_cs, update_cs cbuffers).

### GPU integration tests

- `test_spline_spawn_distribution.cpp` — spawn 1024 particles via
  `SpawnAlongSplineModule(stratified=true)`, read back positions,
  assert the chord-distance variance is below 5% of the expected mean.
- `test_spline_force_convergence.cpp` — pre-seed 100 particles 200 cm
  perpendicular from a flat spline, run 60 update ticks with
  `stiffness=15.0, attract_distance=300.0, max_acceleration=2000.0`,
  assert mean distance to nearest sample < 5 cm.
- `test_spline_follow_endpoints.cpp` — spawn 8 particles with
  `Lifespan=4.0, FollowSplineModule(speed=1, loop=false)`. Read
  position at age=0.0 (must equal `controlPoints[0]`) and age=4.0
  (must equal `controlPoints.back()`). Tolerance: 1e-3 cm.

### Editor + ECS

- Add `SplineComponent` ECS type with an authored `controlPoints`
  array (Vec3) + `tension` + `looping`. `SyncVfxEmitters` reads it
  from `VfxEmitterComponent::splineTargetEntity` and copies the
  geometry into the runtime emitter's `splineSource`.
- Niagara Parameters panel: a "Spline Target" entity-picker row that
  filters by `SplineComponent` presence; selecting an entity stores
  its handle on `VfxEmitterComponent`.

## Impact

- Affected specs: VFX spline data interface (existing).
- Affected code:
  - `runtime/engine/renderer/frame/frame_vfx_inline.cpp` — spline
    module detection + per-frame `VfxSplineBuffer::RebuildAndUpload`.
  - `runtime/shaders/runtime/vfx/particle_spawn_cs.hlsl` and
    `particle_update_cs.hlsl` — extend cbuffer with spline fields.
  - `runtime/engine/renderer/passes/vfx/particle_simulation_pass.h` —
    add CPU mirrors for the new cbuffer fields.
  - New: `runtime/engine/ecs/environment/spline_component.{h,cpp}`.
  - `runtime/engine/renderer/scene/scene_vfx_sync.cpp` — read
    `SplineComponent` and copy its control points to the runtime
    emitter.
  - `runtime/engine/vfx/editor/vfx_overlay_parameters_panel.cpp` —
    "Spline Target" picker.
  - New: `runtime/tests/engine/vfx/test_spline_spawn_distribution.cpp`,
    `test_spline_force_convergence.cpp`,
    `test_spline_follow_endpoints.cpp`.
- Breaking change: NO. The cbuffer extensions append fields; without
  spline modules on the emitter the new fields stay zero and the
  shader paths gate on `cbSplinePointCount >= 2`.
- User benefit: spline-following effects authored in the editor by
  picking a spline entity, validated end-to-end against DX12 hardware
  with regression tests that catch CB-layout drift.

## Dependencies

- Builds on `phase16_vfx-spline-data-interface` (already archived).
- Requires DX12-capable host for the GPU integration tests.
