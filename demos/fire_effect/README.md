# Fire Effect Demo

Headless proof-of-concept demonstrating the UzEngine VFX module system.
No GPU hardware or window is required.

## What it Does

Builds a fire-like particle effect using composable VFX modules:

| Module            | Role                                   | Key Config                        |
|-------------------|----------------------------------------|-----------------------------------|
| **SpawnModule**   | Particle emission with cone spread     | rate=500/s, spread=60deg          |
| **UpdateModule**  | Euler integration (gravity + drag)     | gravity=[0,-3,0], drag=0.98       |
| **TurbulenceModule** | FBM noise for organic flame flicker | strength=2.0, freq=0.5, octaves=3 |
| **SpriteRenderModule** | Billboard sprite configuration    | 0.5 x 0.5 world units            |
| **DecayModule**   | Age tracking and particle recycling    | (no params, uses lifespan range)  |

The demo:
1. Initializes VFXSystem with a 500K particle budget
2. Creates an emitter with 25K max particles
3. Builds an EmitterModuleStack with fire parameters
4. Composes the update compute shader via ModuleCompositor and prints the HLSL
5. Runs 120 frames (2 seconds at 60 FPS) with periodic stats
6. Validates module counts, HLSL output, and spawn totals
7. Prints a full parameter summary for every module

## Build & Run

```bash
cd demos/fire_effect
zig build
./zig-out/bin/fire_effect
```

## Expected Output

```
[Init] VFXSystem initialized -- budget: 500000 particles
[Init] ModuleRegistry has 6 built-in modules: ...
[Init] Emitter initialized -- capacity: 25000, budget used: 25000/500000
[Init] Module stack: 1 spawn, 3 update, render=sprite_render

-- Composed Update Shader (HLSL) --
... (generated HLSL from module snippets) ...

Frame     Spawned       Accumulator   Budget Used     Emitters
-------- ------------ ------------ -------------- ----------
0         8             0.333333      25000           1
...
119       8             0.333333      25000           1

SIMULATION SUMMARY
  Frames simulated:       120 (2.0 seconds)
  Total particles spawned: 1000
  ...

[PASS] Fire effect demo completed successfully.
```

## Parameters

All parameters are defined as `constexpr` at the top of `main.cpp` and can be
modified for experimentation.
