# VFX trail-decal streaks

Swept decal emission along a particle's path. Produces tire marks, blood
trails, magic grooves — one decal per cadence step along the movement path
rather than one at the death position.

Reference: UE5 `UNiagaraDecalRendererProperties` trail mode.

## Architecture

```
ParticleEmitter.pos  →  VfxTrailDecalEmitter.TryEmit(id, pos, now)
                                 │
                                 ├── updates per-particle cadence state
                                 │
                                 └── when cadence met →
                                      VfxTrailDecalEvent → DecalManager
```

- `VfxTrailDecalEmitter` is a CPU-side accumulator. No GPU resources.
- `VfxTrailDecalParticleState` tracks the last emit position, last emit
  time, and accumulated travel distance since the last emission.
- `VfxTrailDecalEvent` carries the center pose + dimensions + material
  for one decal. The caller forwards it to the existing `DecalManager`
  which owns the render-pass side.

## Config

```cpp
VfxTrailDecalConfig cfg {
    .sourceEmitter    = "wheel_particles",
    .decalMaterial    = "materials/tire_mark.uzmat",
    .cadence          = VfxTrailDecalCadence::Distance,
    .cadenceValue     = 0.5f,   // emit every 0.5 m of travel
    .width            = 0.5f,
    .length           = 0.5f,
    .lifetime         = 10.0f,
    .skipFirstSample  = true,
};
VfxTrailDecalEmitter emitter(cfg);
```

Two cadence modes:

- **Distance** — emit every `cadenceValue` metres of travel since the
  last emission. Rate of decal emission is proportional to particle speed.
- **Time** — emit every `cadenceValue` seconds regardless of motion.
  Useful for dripping or pulsing effects.

## Per-particle state

```cpp
struct VfxTrailDecalParticleState {
    Vec3  lastEmitPos;           // updated after each emit
    float lastEmitTime;          // updated after each emit
    float accumulatedDistance;   // since lastEmitPos
    Vec3  prevPos;               // for distance accumulation
    bool  primed;                // true after first sample
};
```

The first call to `TryEmit` for a particle primes the state and returns
`false` — authors almost always want the first mark after the first cadence
step, not at the spawn point.

## Event payload

```cpp
struct VfxTrailDecalEvent {
    Vec3  centerPos;  // midpoint of swept segment (last → current)
    Vec3  forward;    // unit tangent
    float width, length, lifetime;
    std::string material;
};
```

Center-at-midpoint makes consecutive decals butt cleanly when
`cadenceValue == length` (the default), so a long streak reads as one
continuous mark.

## Determinism

`VfxShouldEmitTrailDecal` is a pure predicate over config + state + time.
Given the same particle path and the same cadence config, two emitters
produce identical decal streams — tested by
`IdenticalInputsProduceIdenticalStreams`.

## Tests

`runtime/tests/engine/vfx/test_vfx_trail_decal.cpp` — 12 cases:
predicate (unprimed/zero-cadence/distance threshold/time delta),
first-sample primes without emitting, distance + time cadence
emission cycles, event forward matches path direction, per-particle
isolation, ForgetParticle, Reset, event payload carries config,
determinism across two emitters with the same input path.
