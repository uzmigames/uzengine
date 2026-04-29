# VFX Spline Data Interface

> Status: shipped 2026-04-28 (`phase16_vfx-spline-data-interface-hardware-validation`).

The VFX spline data interface enables emitters to spawn particles along,
attract toward, or follow 3D curves authored as ECS entities. A
`SplineComponent` on any entity holds control points, tension, and arc-length
sampling parameters. Emitters reference the spline via the Niagara Parameters
panel and route particle behavior through spline modules
(`SpawnAlongSplineModule`, `ForceTowardSplineModule`, `FollowSplineModule`).

## Authoring path

### 1. Author a SplineComponent

Add `SplineComponent` to any ECS entity. The component is a header-only POD
holding a `controlPoints` array (Vec3), `tension` (Catmull-Rom parameter),
`looping` (bool), and `arcLengthSamples` (resolution hint). Cite
`runtime/engine/ecs/environment/spline_component.h`.

### 2. Add spline modules to the emitter stack

Select one or more spline modules from the module registry:
- `SpawnAlongSplineModule` — spawn particles uniformly distributed along the
  curve (stratified or random)
- `ForceTowardSplineModule` — attract particles toward the nearest spline sample
  with configurable stiffness and max acceleration
- `FollowSplineModule` — advance particles along the spline parametrically based
  on lifespan progression

Cite `runtime/engine/vfx/modules/spline_modules.h` for per-module config fields
(stratified, jitter, stiffness, attract_distance, speed, loop).

### 3. Pick the spline entity

In the Niagara Parameters panel, a "Spline Target" entity-picker row appears
(conditionally visible only when a spline module is present on the emitter's
stack). Pick the spline-bearing entity to establish the reference. Cite
`runtime/engine/vfx/editor/vfx_editor_overlay.cpp:820–890` for the row builder
pattern.

## Per-frame data flow

### Step 1: Sync

`scene_vfx_sync.cpp:148–205` resolves `VfxEmitterComponent::splineTargetEntity`,
world-transforms `SplineComponent::controlPoints` via
`SceneComponent::GetWorldMatrix()`, copies the geometry into `emitter->splineSource`,
and invalidates the arc-length sample cache. This runs once per frame in the
scene sync pass (gated on spline-module presence).

### Step 2: Detect + upload

`frame_vfx_inline.cpp:300–378` detects spline modules on the emitter via
`dynamic_cast` on each spawn/update module. When detected, a
`VfxSplineBuffer` is lazy-created per emitter (sized to `kVfxSplineDefaultResolution`).
`RebuildAndUpload(emitter->splineSource, m_device)` is called once per frame to
bake sample positions, tangents, and arc-length LUT into GPU buffers.

### Step 3: CB fill

At spawn time (`frame_vfx_inline.cpp:464–481`), the `ParticleSpawnConstants`
cbuffer is populated with:
- `cbSplineSrvIndex` — bindless SRV index into the device's ResourceDescriptorHeap
- `cbSplinePointCount` — total baked sample count
- `cbSplineTotalLength` — arc-length parameter range
- Per-module config: `cbSplineSpawnStratified`, `cbSplineSpawnJitter`, etc.

At update time (`frame_vfx_inline.cpp:737–760`), `ParticleUpdateConstants`
receives:
- Same SRV index and metadata
- Per-module config: `cbSplineAttractDistance`, `cbSplineStiffness`,
  `cbSplineMaxAccel`, `cbSplineFollowSpeed`, `cbSplineFollowLoop`

Cite `runtime/engine/renderer/passes/vfx/particle_simulation_pass.h:138–158`
(spawn) and `:316–342` (update) for struct definitions and asserted sizes
(304 / 432 bytes).

### Step 4: CS dispatch

`ParticleSpawnCS` + update-module CS routines run. The HLSL snippets in
`runtime/shaders/runtime/vfx/spline_di.hlsli` read the bindless SRV via
`ResourceDescriptorHeap[cbSplineSrvIndex]` and gate all spline logic on
`cbSplinePointCount >= 2`. When disabled, particles spawn / update normally.

### Step 5: Result

Particles spawn along the curve (uniformly distributed if stratified),
are attracted toward the nearest sample point, or follow the spline as a
parametric path animation based on lifespan age.

## CB layout

| Field | Offset | Type | Size | Purpose |
|-------|--------|------|------|---------|
| `cbSplineSrvIndex` | 272 | uint | 4 | Bindless SRV index (spawn) |
| `cbSplinePointCount` | 276 | uint | 4 | Baked sample count |
| `cbSplineTotalLength` | 280 | float | 4 | Arc-length parameter range |
| `cbSplineSpawnStratified` | 284 | uint | 4 | Spawn mode (stratified vs random) |
| `cbSplineSpawnJitter` | 288 | float | 4 | Jitter amplitude |
| `cbSplineSpawnOffsetU` | 292 | float | 4 | Parametric offset [0, 1] |
| `cbSplineSpawnSeed` | 296 | uint | 4 | Random seed (if random mode) |
| `cbSplineSpawnOffset` | 300 | float | 4 | Spatial offset from spline |
| `cbSplineAttractDistance` | 384 | float | 4 | Attraction radius (update) |
| `cbSplineStiffness` | 388 | float | 4 | Force magnitude (N / 100 cm offset) |
| `cbSplineMaxAccel` | 392 | float | 4 | Max acceleration (cm/s²) |
| `cbSplineFollowSpeed` | 396 | float | 4 | Parametric speed along spline |
| `cbSplineFollowLoop` | 400 | uint | 4 | Loop at end vs clamp to endpoint |
| ... (padding to 432) | ... | ... | ... | ... |

ParticleSpawnConstants assert size = 304 (spawn table ends at 303 + padding).
ParticleUpdateConstants assert size = 432 (update table ends at 431 + padding).

## Validation + tests

56 GPU-style integration tests span three files (1419 LOC total):

- `test_spline_spawn_distribution.cpp` — spawn 1024 particles via
  `SpawnAlongSplineModule(stratified=true)`, read back positions, assert
  chord-distance variance is below 5% of expected mean
- `test_spline_force_convergence.cpp` — pre-seed 100 particles 200 cm
  perpendicular from a flat spline, run 60 update ticks with
  `stiffness=15.0, attract_distance=300.0`, assert mean distance to
  nearest sample < 5 cm
- `test_spline_follow_endpoints.cpp` — spawn 8 particles with
  `Lifespan=4.0, FollowSplineModule(speed=1, loop=false)`, read position
  at age=0.0 (must equal controlPoints[0]) and age=4.0 (must equal
  controlPoints.back()), tolerance 1e-3 cm

Filter: `--gtest_filter="*Spline*Spawn*:*Spline*Force*:*Spline*Follow*"`.

Coverage gaps: real DX12 UAV readback of `Positions[]` is the primary gap;
tests validate stratified-U distribution, force direction + bounds, and
endpoint arithmetic on MockRHIDevice.

## Sample assets

Three test `.uzfx` assets ship in `sample/content/vfx/tests/`:

| Asset | Modules | Exercises |
|-------|---------|-----------|
| `spline_distribute.uzfx` | `SpawnAlongSplineModule(stratified=true)` | Uniform distribution along a 4-point curve |
| `spline_attract.uzfx` | `ForceTowardSplineModule(stiffness=15, attract_distance=300)` | Attraction convergence from off-spline seeding |
| `spline_follow.uzfx` | `FollowSplineModule(speed=1, loop=false)` | Parametric path animation end-to-end |

Smoke-test (CWD = `e:/UzmiEngine/uzengine`):

```powershell
$exe = 'runtime/demos/vfx_viewport_app/build/bin/vfx_viewport_app.exe'
$vfs = 'e:/UzmiEngine/uzengine/sample/content'
& $exe --vfs-root $vfs --vfx-asset 'vfx/tests/spline_attract.uzfx' --headless --frames 30 --verbose
```

## Cross-references

- **Sister docs**: `88-mesh-particle-render-path.md`, `99-sort-pipeline.md`
- **CB architecture**: `90-particle-simulation-pass.md` (simulation dispatch, cbuffer
  packing rules)
- **Scalability**: `100-scalability-cvars.md` (spline resolution limits)
- **Predecessor task** (CPU surface shipped): `.rulebook/archive/2026-04-25-phase16_vfx-spline-data-interface/`
