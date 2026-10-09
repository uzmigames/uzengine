# VFX Spline Data Interface

> Status: shipped 2026-04-25 (`phase16_vfx-spline-data-interface`).
> CB plumbing + GPU integration tests + editor "Spline Target" picker
> tracked separately by
> `phase16_vfx-spline-data-interface-hardware-validation`.

This document describes the `VfxSpline` Catmull-Rom evaluator, the
`VfxSplineBuffer` GPU upload wrapper, the bindless HLSL helpers, and
the three modules that consume them: `SpawnAlongSplineModule`,
`ForceTowardSplineModule`, `FollowSplineModule`.

## Overview

A spline data interface lets a particle's spawn position, force input,
or update-time position read from a parametric curve. The spawn CS
distributes particles along the curve; the update CS pulls them
toward it or steps them along it as they age. The flow:

```
gameplay code mutates -> VfxSpline::controlPoints -> mark dirty
                                       │
                                       ▼
                    VfxSplineBuffer::RebuildAndUpload
                    (rebuild arc-length LUT,
                     bake VfxSplinePoint[N] into upload heap)
                                       │
                                       ▼
              spawn / update CS reads via bindless heap using
              `cbSplineSrvIndex` / `cbSplinePointCount` / ...
                                       │
                                       ▼
       SplineSampleAtU / SplineSampleArclen / SplineNearest
                  (HLSL helpers in spline_di.hlsli)
```

## CPU evaluator (`VfxSpline`)

Lives in
`runtime/engine/vfx/data_interfaces/vfx_spline_di.{h,cpp}` (already
shipped pre-task). Implements centripetal Catmull-Rom interpolation
through `controlPoints` with a parameter-uniform `Sample(t)` and an
arc-length-uniform `SampleUniform(t)` backed by an on-demand LUT.

| Field | Meaning |
|-------|---------|
| `controlPoints` | Authored points in order (≥2 required). |
| `tension` | 0=loose, 0.5=standard (default), 1=chordal corners. |
| `looping` | When true, last control connects back to first. |
| `arcLengthSamples` | Resolution of the arc-length table (default 64). |
| `arcLengthTable` | Cumulative-length LUT — built lazily by `BuildArcLengthTable()`. |
| `totalLength` | Total arc length; valid after `BuildArcLengthTable()`. |

Out-of-range parameters clamp on open splines and wrap on looping
splines. Tangents are unit-normalised; the fallback for a degenerate
tangent (coincident control points at tension=0) is `(1,0,0)`.

## GPU upload (`VfxSplineBuffer`)

Lives in
`runtime/engine/vfx/data_interfaces/vfx_spline_buffer.{h,cpp}`. Owns
an upload-heap structured buffer of `VfxSplinePoint` elements that the
spawn / update CS reads through the bindless heap.

```cpp
struct VfxSplinePoint {
    float positionX;
    float positionY;
    float positionZ;
    float arcLength;   // cumulative from sample[0]
    float tangentX;
    float tangentY;
    float tangentZ;
    float _pad;
};   // exactly 32 bytes; static_assert in the header
```

### Lifecycle

```cpp
VfxSplineBuffer buf;
buf.Create(device, /*capacity=*/64);

// Each frame after gameplay mutates the spline:
buf.RebuildAndUpload(emitter.splineSource, device);

// Bind via the bindless heap using buf.GetBindlessSrvIndex(device).

buf.Destroy(device);
```

`RebuildAndUpload`:
1. Rebuilds the spline's arc-length LUT (force-fresh).
2. Bakes `min(arcLengthTable.size(), capacity)` `VfxSplinePoint`s by
   parameter-uniform `Sample(t)` so the GPU's linear-interp lookup is
   indistinguishable from the CPU evaluator at simulation distances.
3. Zeros trailing slots so the shader never sees stale data.
4. Records `pointCount` + `totalLength` for the consuming CS.

A degenerate spline (< 2 control points) returns `pointCount = 0` and
zeroes the buffer — the HLSL helpers gate on this so authors can
shrink a spline mid-run without tripping the simulation.

## HLSL helpers (`spline_di.hlsli`)

Three helpers consume the baked buffer. All accept the bindless SRV
index so any compute pass can include the header without a fixed
register binding.

```hlsl
// Sample at parametric u in [0, 1].
VfxSplinePoint SplineSampleAtU(uint splineSrvIndex,
                                uint pointCount,
                                float u);

// Sample at arc length s in [0, totalLength].
VfxSplinePoint SplineSampleArclen(uint splineSrvIndex,
                                   uint pointCount,
                                   float totalLength,
                                   float s);

// O(N) brute-force nearest-baked-sample lookup.
VfxSplinePoint SplineNearest(uint   splineSrvIndex,
                              uint   pointCount,
                              float3 worldPos);
```

Out-of-range parameters clamp; degenerate splines (`pointCount < 2`)
return a zero position with a default forward tangent so the calling
snippet does not have to guard.

`SplineNearest` is a linear scan — O(pointCount) per particle. At the
default 64-sample bake and tens of thousands of particles per frame
the cost is sub-millisecond on baseline hardware. Authors with denser
splines should switch to a coarser bake, a separate broadphase
(e.g. spatial hash), or a binary-search-on-arc-length variant; the
header documents the contract for `SplineSampleArclen_Binary` so the
follow-up task can introduce it without breaking callers.

### SM5 fallback

Bindless `ResourceDescriptorHeap[]` is SM6.6+. SM5 callers must
declare an explicit register binding and `#define UZ_SPLINE_BIND` to
its identifier before including the header — the helper compiles to
that binding instead of the bindless lookup.

## Modules

All three modules live in
`runtime/engine/vfx/modules/spline_modules.{h,cpp}` and share the
same per-emitter `VfxSplineBuffer`. The compositor stitches each
module's HLSL snippet into the host CS; the snippet text references
shared cbuffer fields documented at the top of `spline_modules.h`.

### `SpawnAlongSplineModule`

Stage: `ParticleSpawn`.

| Field | Default | Effect |
|-------|--------:|--------|
| `stratified` | `true` | Each new particle's `u` is `(spawnIndex + 0.5) / spawnCount` so the dispatch is uniform along arc length. When `false`, `u` is a hash of `particleIdx`. |
| `jitter_radius` | `0.0` | Scatters spawn position perpendicular to the tangent within a disc of this radius (cm). |
| `tangent_offset` | `0.0` | Pre-displaces the particle along the tangent at spawn (cm). Useful for staggering streamers. |

The snippet also sets the particle's initial velocity to the unit
tangent so any downstream velocity-aligned module (mesh rotation,
stretch billboards) picks up the path direction for free.

### `ForceTowardSplineModule`

Stage: `ParticleUpdate`.

| Field | Default | Effect |
|-------|--------:|--------|
| `stiffness` | `10.0` | Spring constant. Force = stiffness × (nearest sample − pos). 0 disables. |
| `max_acceleration` | `2000.0` | Hard cap on per-frame force magnitude (cm/s²). Prevents blow-up when a particle spawns far from a short spline. |
| `attract_distance` | `0.0` | When > 0, only particles within this radius (cm) feel the force. `0` = unlimited range. |

The snippet uses `SplineNearest` so the force always pulls toward the
closest baked sample. Combine with a small damping `UpdateModule` to
absorb residual velocity once a particle settles on the curve.

### `FollowSplineModule`

Stage: `ParticleUpdate`.

| Field | Default | Effect |
|-------|--------:|--------|
| `speed` | `1.0` | Per-particle `u` advances at `speed × lifeFrac` over the lifetime. 1.0 = traverses full spline once over `Lifespan`. Negative values reverse. |
| `loop` | `false` | When true, `u > 1.0` wraps to `0` so particles cycle the path. |
| `start_u` | `0.0` | Initial `u` at spawn. `0.5` seeds particles mid-path — useful for "rivers". |

The snippet OVERRIDES the particle's position each tick and writes a
velocity along the tangent scaled by `speed × totalLength /
lifespan` so the heading is correct for downstream rendering.

## Sample assets

Three demo `.uzfx` assets ship under `sample/content/vfx/tests/`:

| Asset | Module exercised |
|-------|------------------|
| `spline_distribute.uzfx` | `SpawnAlongSplineModule` (200 stratified additive sparks). |
| `spline_attract.uzfx` | `ForceTowardSplineModule` (400 alpha-blended particles snapping to the curve). |
| `spline_follow.uzfx` | `FollowSplineModule` (100 premul-alpha trail heads riding the path once). |

Each is in the editor `stages` schema (consumed by `DeserializeEmitter`
+ `ApplyEmitterStackToModuleStack`). The production `.uzvfx` schema
parser does not yet expose the three spline modules — that wiring
lands with the CB plumbing in the hardware-validation follow-up.

## Test coverage

`tests/engine/vfx/test_spline_data_interface.cpp` (32 tests across 7
suites):

| Suite | What it covers |
|-------|----------------|
| `VfxSpline` (3) | Endpoint accuracy, monotonic arc-length LUT, degenerate-spline safety. |
| `VfxSplineBuffer` (5) | Create/Destroy balance, capacity clamping, degenerate-spline upload, valid bake produces samples + length, repeated rebuild does not leak. |
| `SpawnAlongSpline` (4) | Default config, name, snippet non-empty, `ParseConfig` reads all keys, `GetParameters` exposes 3 named entries. |
| `ForceTowardSpline` (4) | Same shape as Spawn — defaults, name, parse, parameters. |
| `FollowSpline` (4) | Defaults, name, `ParseConfig`, `ApplyJsonConfig`. |
| `SplineModuleRegistry` (1) | Three modules registered + creatable + dynamic-castable. |
| `SplineUzfx` (4) | Each sample asset round-trips through `DeserializeEmitter` + `ApplyEmitterStackToModuleStack` and ends up with the right module slot populated + the right config values. |

36 tests when combined with the pre-existing `VfxSpline` shipped
helpers; 36/36 PASS on `MockRHIDevice`. CB plumbing + GPU integration
tests + editor picker land in
`phase16_vfx-spline-data-interface-hardware-validation`.
