# Trail/Ribbon VFX System Demo

Headless demonstration of the UzEngine trail and ribbon system.
No window, no GPU, no RHI backend required.

## Overview

The demo runs three self-contained phases that exercise `TrailComponent`,
`TrailMeshBuilder`, and `TrailSystem` at increasing complexity.

---

## Phase 1 — Projectile Trail (300 frames / 5 s)

A simulated projectile launches at `(0, 0, 0)` with an initial velocity of
`(12, 15, 0) m/s` and falls under gravity (`-9.81 m/s²`). The fire trail asset
defines:

| Property        | Value                        |
|-----------------|------------------------------|
| Width (head)    | 0.5 m                        |
| Width (tail)    | 0.05 m                       |
| Color (head)    | `{1.0, 0.8, 0.2, 1.0}` (orange) |
| Color (tail)    | `{1.0, 0.2, 0.0, 0.0}` (transparent red) |
| UV scroll rate  | 1.5 UV/s (animated fire texture) |
| Point lifetime  | 2.0 s                        |

Mesh statistics (point count, arc length, vertex/index counts) are printed
every second (every 60 frames).

---

## Phase 2 — Sword Swipe (180 frames / 3 s)

A sword tip sweeps a 180-degree arc of radius 1.2 m in 3 seconds. The trail
is wide at the centre of the swing and tapers rapidly at the tips:

| Property        | Value                         |
|-----------------|-------------------------------|
| Width (head)    | 1.0 m                         |
| Width (tail)    | 0.1 m                         |
| Color (head)    | `{0.8, 0.9, 1.0, 1.0}` (pale blue) |
| Color (tail)    | `{0.4, 0.6, 1.0, 0.0}` (transparent) |
| Point lifetime  | 0.3 s (fast disappear)        |

Mesh statistics are printed at the arc midpoint (frame 90).

---

## Phase 3 — Performance Test (50 trails, 600 frames / 10 s)

50 simultaneous trails are registered with `TrailSystem`. Each trail orbits in
a circular path; the radius and phase vary across all 50 so paths are distinct
and non-overlapping. A performance overlay is printed every second:

```
[Frame  120 | t= 2.00s]  trails=50  segments=2347  verts=4694   idx=14082  update=0.312ms
```

All 50 trails are unregistered at the end of the phase to verify clean teardown.

---

## Build

```bash
cd demos/trails
zig build
```

## Run

```bash
./zig-out/bin/trail_demo          # Linux / macOS
zig-out\bin\trail_demo.exe        # Windows
```

Or combine:

```bash
zig build run
```

## Expected Output

```
==========================================================
         UzEngine -- Trail/Ribbon VFX System Demo
   Projectile, sword swipe, and 50-trail performance test
==========================================================

== Phase 1: Projectile Trail (parabolic arc, 300 frames) ====
  Asset loaded: material='materials/vfx/fire_trail.uzmat' ...
  [Frame    0 | t= 0.00s]  pos=(0.20, 0.23)  points=1  length=0.000m  verts=0   idx=0
  [Frame   60 | t= 1.00s]  pos=(12.00, 9.54)  points=22  length=2.103m  verts=44  idx=126
  ...

== Phase 2: Sword Swipe (arc sweep, 180 frames) =============
  [Midpoint | t=1.50s]  angle=90.0°  points=18  length=1.885m  verts=36  idx=102
  [OK]  Sword swipe produced geometry at arc midpoint

== Phase 3: Performance Test (50 trails, 600 frames) ========
  Registered 50 trails  (handles [0..49])
  [Frame    0 | t= 0.00s]  trails=50  segments=50  verts=0  idx=0  update=0.041ms
  [Frame   60 | t= 1.00s]  trails=50  segments=1800  verts=3600  idx=10800  update=0.280ms
  ...
  [OK]  All 50 trails unregistered cleanly

==========================================================
[PASS] Trail system demo completed successfully.
```

## Source Files

| File       | Purpose                                              |
|------------|------------------------------------------------------|
| `main.cpp` | Demo entry point: three phases, all headless         |
| `build.zig`| Zig build script; links trail subsystem from runtime |
