# VFX physics-fracture events

Game integration: when a Jolt rigid body fractures, the VFX runtime spawns
debris + dust particles inheriting the fracture's position, velocity,
normal, and per-piece mass. Niagara listens to Chaos break events;
UzEngine's equivalent subscribes to Jolt's fracture callback.

Reference: UE5 `FNiagaraDataInterfaceChaosDestruction` + `FChaosBreakEvent`.

## Pipeline

```
  Jolt OnFracture(bodyId, pieces…)
            │
            ▼
  VfxPhysicsFractureBridge::OnFracture(VfxFractureEventInput)
            │
            ▼
  VfxTranslateFractureEvent → VFXPendingEvent (name = "fracture")
            │
            ▼
  VFXEventQueue (per-emitter) — the existing 7.3 event pipeline
  drains it during the sim pass and spawns bursts with payload
  inheritance.
```

Two layers:

- **Translation** — `VfxTranslateFractureEvent` is a pure function tested
  in isolation. It's the authoritative source of the payload mapping.
- **Bridge** — `VfxPhysicsFractureBridge` owns the forwarding glue:
  queue pointer + event name + diagnostic counter. Jolt's listener
  registration is caller-side; the bridge just translates + enqueues.

## Input schema

```cpp
struct VfxFracturePiece {
    Vec3  position;        // world-space
    Vec3  linearVelocity;  // piece velocity
    float mass;            // kg
};

struct VfxFractureEventInput {
    uint64_t                      bodyId;
    Vec3                          impactNormal; // unit or scaled
    float                         impactSpeed;  // m/s
    Vec3                          authoredTint;
    std::vector<VfxFracturePiece> pieces;
};
```

## Payload mapping

Each piece becomes one `EventPayload`:

| EventPayload field | Source                                              |
|--------------------|-----------------------------------------------------|
| `position`         | `piece.position`                                     |
| `velocity`         | `impactNormal · impactSpeed + piece.linearVelocity` |
| `color.rgb`        | `input.authoredTint`                                 |
| `color.a`          | `piece.mass / Σ mass`  (mass fraction, 0–1)         |
| `age`              | 0 (fresh)                                            |

Authors use `color.a` (mass fraction) as a size or lifetime multiplier
in the child emitter so larger debris survives longer.

## Edge cases

- **Empty pieces.** Returns an event with `emissionCount = 0` and empty
  payloads. The bridge skips enqueueing in that case.
- **Zero-length impact normal.** Treated as "no directional kick";
  piece linear velocity passes through unchanged.
- **Non-unit normal.** Normalised before scaling by `impactSpeed` so the
  resulting impact-velocity magnitude matches `impactSpeed` exactly.
- **Negative impact speed.** Clamped to 0.
- **Zero total mass.** Alpha channel becomes 0 for all payloads (no
  divide-by-zero).

## Integration example

```cpp
// Game boot:
VfxPhysicsFractureBridge bridge(&emitter.eventQueue, "fracture");
physicsWorld.RegisterFractureListener(
    [&bridge](const VfxFractureEventInput& evt) {
        bridge.OnFracture(evt);
    });

// In the emitter asset:
//   events: [{ name: "fracture", emission_count: 0, velocity_scale: 1 }]
// The emitter's sub-emitter module reads the payload to spawn debris
// with the inherited position + velocity + tint.
```

## Tests

`runtime/tests/engine/vfx/test_vfx_physics_fracture.cpp` — 17 cases:

- **Translation** (12): empty pieces produce empty event, emission count
  matches piece count, position copied per piece, velocity combines
  impact + piece vel, tint copied to RGB, alpha = mass fraction,
  zero-mass safe alpha, negative speed clamp, zero-normal zero impact,
  non-unit normal normalised to target magnitude, event name forwarded,
  age = 0.
- **Bridge** (5): enqueues on target queue + increments counter, null
  target rejects, empty input skipped, `SetTarget` rebinds, default
  event name is `"fracture"`.
