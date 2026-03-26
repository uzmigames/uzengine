# VFX Advanced Modules Demo

Headless demo for four UzEngine VFX subsystems: depth-buffer collision,
instanced mesh rendering, distance/screen LOD evaluation, and significance-based
budget enforcement. No GPU, window, or asset files are required.

---

## Phases

### Phase 1 — Collision Module

Constructs a `CollisionModule` for each of the three `CollisionResponse` types:

| Response | Behaviour                                                        |
|----------|------------------------------------------------------------------|
| Kill     | Particle dies on contact (age set to lifespan).                  |
| Bounce   | Velocity reflected off the estimated depth-buffer surface normal.|
| Slide    | Velocity projected onto the surface plane.                       |

For each mode the demo verifies:
- `GetName()` returns the stable identifier `"collision"`.
- `GetShaderCode()` returns a non-empty HLSL snippet.
- `CollisionResponseToString()` / `CollisionResponseFromString()` round-trip correctly.
- `GetConfig()` reflects the values passed to the constructor.

Printed output: response name, shader code byte length, and all five config fields.

---

### Phase 2 — Mesh Render Module

Constructs a `MeshRenderModule` for each `MeshRotationMode`:

| Mode            | Behaviour                                                     |
|-----------------|---------------------------------------------------------------|
| None            | Identity orientation — all particles face the same direction. |
| VelocityAligned | Forward axis (+Z) tracks the velocity vector.                 |
| Random          | Deterministic per-particle rotation derived from seed index.  |

For each mode the demo verifies:
- `GetName()` returns the stable identifier `"mesh_render"`.
- `GetShaderCode()` returns a non-empty HLSL snippet.
- `MeshRotationModeToString()` / `MeshRotationModeFromString()` round-trip correctly.
- `GetConfig()` reflects the constructor values (mesh path, scale, shadow flag).
- At least two of the three snippets differ (mode-specific HLSL branching is present).

Printed output: config fields, shader byte length, first 120 characters of the snippet.

---

### Phase 3 — LOD System

Simulates a camera moving from **0 m to 300 m** away from a stationary emitter
in 31 equal steps. Uses default `VFXLodSettings` thresholds:

```
[  0,  50 m) → Full    (1.00 spawn factor)
[ 50, 100 m) → Reduced (0.50 spawn factor)
[100, 200 m) → Minimal (0.25 spawn factor)
[200,   ∞  ) → Culled  (0.00 spawn factor)
```

At each step `VFXLodEvaluator::Evaluate()` is called with:
- Bounding radius: 5 m
- Screen height: 1080 px
- Minimum screen coverage: 4 px

The combined criterion (max of distance-based and screen-size-based LOD) is printed
in a table alongside each individual criterion. The demo asserts that LOD levels
are monotone non-decreasing as distance grows.

A secondary screen-cull boundary test exercises `VFXScreenCuller::ShouldCull()`
at five representative distances to verify correct pixel-coverage arithmetic.

---

### Phase 4 — Budget Stress Test

Creates **50 `VFXEmitterSignificance` records** with a deterministic spread:

| Field               | Distribution                          |
|---------------------|---------------------------------------|
| `priority`          | (index % 10) + 1, range 1–10         |
| `distanceToCamera`  | 10 + index × 10 m, range 10–500 m    |
| `isFrustumVisible`  | false every 3rd emitter (off-screen)  |
| `particleDemand`    | (index + 1) × 500, range 500–25,000  |

**Primary run** (`cap = 100,000`):
- Calls `VFXSignificanceManager::EnforceBudget()`.
- Prints a ranked per-emitter table: ID, priority, distance, visibility, significance
  score, assigned LOD, and effective particle demand after reduction.
- Verifies total effective demand ≤ 100,000.
- Prints a `VFXBudgetStats` snapshot (utilization %, alive count, active/culled emitters).

**Quality-level sweep** (same 50 emitters, four separate enforcement runs):

| Quality | Max Particles |
|---------|--------------|
| Low     |    125,000   |
| Medium  |    250,000   |
| High    |    500,000   |
| Ultra   |  1,000,000   |

Each run verifies total effective demand fits within the quality-level cap.

---

## Source File Layout

| File               | Role                                           |
|--------------------|------------------------------------------------|
| `main.cpp`         | Entry point + Phase 1 (Collision) + Phase 2 (Mesh Render) |
| `phase_lod.cpp`    | Phase 3 — LOD evaluation (includes `vfx_lod.h` only)      |
| `phase_budget.cpp` | Phase 4 — Budget stress test (includes `vfx_significance_manager.h` + `vfx_budget.h`) |
| `build.zig`        | Zig build script                               |

`phase_lod.cpp` and `phase_budget.cpp` are isolated into their own translation
units because `vfx_lod.h` and `vfx_significance_manager.h` both declare
`VFXLodLevel` inside `namespace uz`. Combining them in one TU would produce a
redefinition error.

## Build

```bash
cd demos/vfx_advanced
zig build
```

Run:

```bash
./zig-out/bin/vfx_advanced_demo
# or
zig build run
```

Requires Zig 0.12+ and the UzEngine runtime source tree at `../../runtime/`.

---

## Expected Output (abbreviated)

```
══════════════════════════════════════════════════════════════
         UzEngine -- VFX Advanced Modules Demo
  Collision · Mesh Render · LOD · Significance Budget
══════════════════════════════════════════════════════════════

──────────────────────────────────────────────────────────────
  Phase 1 -- Collision Module (Kill / Bounce / Slide)
──────────────────────────────────────────────────────────────
  Response  Name      bounce  depthEnabled  offset  minBounceSpd  ShaderLen
──────────────────────────────────────────────────────────────
  Kill      collision    0.00  no            0.005           0.000  <N>
  Bounce    collision    0.60  yes           0.010           0.100  <N>
  Slide     collision    0.00  yes           0.020           0.000  <N>
  ...

  Phase 3 LOD table:
  Distance(m)  LOD(dist)  LOD(scrn)  LOD(fin)  SpawnFactor
  ──────────────────────────────────────────────────────────────
         0.0  Full       Full       Full      1.00
        40.0  Full       Full       Full      1.00
        50.0  Reduced    Full       Reduced   0.50
       100.0  Minimal    Full       Minimal   0.25
       200.0  Culled     Full       Culled    0.00
       ...

[PASS] VFX advanced modules demo completed successfully.
```

Actual shader code lengths and exact significance scores depend on the runtime
implementation; the demo validates correctness rather than specific values.
