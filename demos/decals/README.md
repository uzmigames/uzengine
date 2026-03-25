# Decal System Demo

Headless proof-of-concept demonstrating the UzEngine decal subsystem.
No GPU hardware or window is required.

## What it Does

Exercises the full decal lifecycle through four phases:

| Phase | Component | What it Tests |
|-------|-----------|---------------|
| **1. Asset Loading** | `DecalAsset` | `LoadFromString` for 5 assets (bullet hole + 4 blend modes) |
| **2. Pool Operations** | `DecalPool` | Acquire, return, exhaustion (nullptr), `Clear`, re-acquire recovery |
| **3. Atlas Packing** | `DecalAtlas` | Shelf-pack 12 textures (128–1024 px), occupancy, `HasSpace`, `Reset` |
| **4. Full Simulation** | `DecalManager` | 120 bullet holes on 5 walls + 100 blend-mode showcase decals, 15 s loop |

## Wall Surfaces (Phase 4A)

Bullet holes are scattered across five axis-aligned walls of a 4 m × 3 m × 4 m room.
Each decal gets a random rotation around the surface normal via `DecalManager::SpawnDecal`.

| Wall | Normal | Half-extents |
|------|--------|-------------|
| `north_wall` | (0, 0, -1) | 3.0 m × 1.5 m |
| `south_wall` | (0, 0, +1) | 3.0 m × 1.5 m |
| `east_wall`  | (-1, 0, 0) | 3.0 m × 1.5 m |
| `west_wall`  | (+1, 0, 0) | 3.0 m × 1.5 m |
| `ceiling`    | (0, -1, 0) | 3.5 m × 3.5 m |

## Blend Mode Showcase (Phase 4B)

Four 5×5 grids are tiled on the north wall, one per `DecalBlendMode`:

| Mode | Material | Effect |
|------|----------|--------|
| `Opaque`   | `crack_opaque.uzmat`    | Replaces surface albedo, normal, roughness |
| `Additive` | `scorch_additive.uzmat` | Adds decal colour — brightens (glow, scorch) |
| `Multiply` | `dirt_multiply.uzmat`   | Multiplies with surface — darkens (dirt, grime) |
| `Screen`   | `wetspot_screen.uzmat`  | Inverse multiply — brightens (wet highlights) |

## Simulation Timeline

```
t = 0.0 s   Phase 4A: 120 bullet holes spawned (24 per wall, round-robin)
t = 0.0 s   Phase 4B: 100 showcase decals spawned (25 per blend mode)
            Peak simultaneous decals: ~220

t = 6.5 s   Showcase decals begin fading (lifetime 8 s, fade-out 1.5 s)
t = 8.0 s   Showcase decals fully expired — pool partially freed

t = 8.0 s   Bullet holes begin fading (lifetime 10 s, fade-out 2.0 s)
t =10.0 s   All bullet holes expired — pool fully freed

t =15.0 s   Simulation ends — active count must equal 0
```

## Inline Decal Assets

| Asset | Blend Mode | Lifetime | Fade | Half-Extents |
|-------|-----------|----------|------|--------------|
| `bullet_hole.uzmat`     | Opaque   | 10.0 s | 2.0 s | [0.15, 0.15, 0.10] |
| `crack_opaque.uzmat`    | Opaque   |  8.0 s | 1.5 s | [0.40, 0.40, 0.12] |
| `scorch_additive.uzmat` | Additive |  8.0 s | 1.5 s | [0.40, 0.40, 0.15] |
| `dirt_multiply.uzmat`   | Multiply |  8.0 s | 1.5 s | [0.50, 0.50, 0.12] |
| `wetspot_screen.uzmat`  | Screen   |  8.0 s | 1.5 s | [0.35, 0.35, 0.10] |

## Build & Run

```bash
cd demos/decals
zig build
./zig-out/bin/decal_demo
```

Or from the project root:

```bash
cd demos/decals && zig build && ./zig-out/bin/decal_demo
```

## Expected Output

```
==========================================================
         UzEngine -- Decal System Demo
   Bullet holes, blend modes, pool management (headless)
==========================================================

== Phase 1: DecalAsset Loading ==============================
  [OK] bullet_hole (Opaque)           blend=Opaque      life=10.0s ...
  [OK] crack_opaque (Opaque)          blend=Opaque      life= 8.0s ...
  [OK] scorch_additive (Add)          blend=Additive    life= 8.0s ...
  [OK] dirt_multiply (Mul)            blend=Multiply    life= 8.0s ...
  [OK] wetspot_screen (Screen)        blend=Screen      life= 8.0s ...

== Phase 2: DecalPool Exhaustion & Recovery =================
  [OK]  Pool exhaustion correctly returns nullptr
  [OK]  Pool Clear restored full capacity (10 slots)

== Phase 3: DecalAtlas Packing ==============================
  [OK]  bullet_hole_albedo            256x256   UV[0.0000,0.0000]-[0.1250,0.1250]
  ...
  Atlas stats: entries=12  occupancy=X.XX%

== Phase 4: Full Simulation (15 seconds) ====================
[Phase 4A] Spawned 120 / 120  (active=120  pool_avail=880)
[Phase 4B] Opaque     25 / 25 decals spawned
           Additive   25 / 25 decals spawned
           ...

  [Frame    0 | t= 0.00s]  active=220   in_use=220   avail=780   atlas= 0.00%  frame=X.XXXms
  [Frame   60 | t= 1.00s]  active=220   ...
  ...
  [Frame  899 | t=14.98s]  active=  0   in_use=  0   avail=1000  atlas= 0.00%  ...

SIMULATION SUMMARY
  Bullet holes placed:        120 / 120
  Showcase decals placed:     100 / 100
  Peak simultaneous decals:   220
  Final active count:           0
  Pool available at exit:    1000 / 1000

[PASS] Decal system demo completed successfully.
```

## Parameters

All demo parameters are `constexpr` in `main.cpp`:

| Parameter | Value | Description |
|-----------|-------|-------------|
| `kPoolSize` | 1000 | Maximum simultaneous decals |
| `kAtlasSize` | 2048 | Square atlas dimension (pixels) |
| `kDt` | 1/60 | Frame delta time (seconds) |
| `kTotalFrames` | 900 | 15 seconds at 60 FPS |
| `kReportInterval` | 60 | Print overlay every second |
| `kBulletHoleCount` | 120 | Bullet holes across 5 walls |
| `kBulletHoleLifetime` | 10.0 s | Bullet hole decal duration |
| `kShowcaseDecalsPerMode` | 25 | Decals per blend-mode set (5×5 grid) |
| `kShowcaseLifetime` | 8.0 s | Showcase decal duration |

## Files

| File | Description |
|------|-------------|
| `main.cpp` | Headless C++ decal system demo (four phases) |
| `build.zig` | Zig build script for standalone executable |
| `README.md` | This file |
