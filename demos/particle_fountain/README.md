# Particle Fountain Demo

Headless proof-of-concept demonstrating the UzEngine GPU particle system's
CPU-side logic. No GPU hardware or window is required.

## What it Does

Creates a single particle emitter configured as an upward fountain:
- **Velocity**: `[0, 10, 0]` (upward)
- **Gravity**: `[0, -9.81, 0]` (pulling down)
- **Damping**: `0.98`
- **Lifetime**: `3.0 seconds`
- **Emission rate**: `1000 particles/sec`
- **Max particles**: `50,000`

Runs 180 frames of simulation (3 seconds at 60 FPS) and prints:
- Per-frame spawn counts at 30-frame intervals
- Accumulator state (fractional particle carry-over)
- Budget usage
- Final validation summary

## Build & Run

```bash
cd demos/particle_fountain
zig build
./zig-out/bin/particle_fountain
```

## Expected Output

```
Frame     Spawned       Accumulator   Budget Used     Emitters
──────── ──────────── ──────────── ────────────── ──────────
0         16            0.666667      50000           1
29        16            0.666667      50000           1
...
179       16            0.666667      50000           1

SIMULATION SUMMARY
  Frames simulated:       180 (3.0 seconds)
  Total particles spawned: 3000
  ...

[PASS] Particle fountain demo completed successfully.
```

## Parameters

All parameters are defined as `constexpr` at the top of `main.cpp` and can be
modified for experimentation.
