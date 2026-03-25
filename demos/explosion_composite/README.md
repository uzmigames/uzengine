# Explosion Composite Demo

Headless multi-emitter VFX demo demonstrating the UzEngine composite effect system.
No GPU hardware or window is required for the C++ demo.

## What it Does

Builds an explosion effect from three sub-emitters defined via an inline JSON composite:

| Emitter    | Rate   | Spread | Velocity     | Gravity       | Delay | Capacity |
|------------|--------|--------|-------------|---------------|-------|----------|
| **fire**   | 500/s  | 60 deg | [0,8,0]     | [0,-3,0]      | 0.0s  | 25,000   |
| **smoke**  | 200/s  | 45 deg | [0,3,0]     | [0,-0.5,0]    | 0.2s  | 15,000   |
| **debris** | 300/s  | 90 deg | [0,12,0]    | [0,-9.81,0]   | 0.0s  | 10,000   |

### C++ Demo (`main.cpp`)

1. Initializes VFXSystem with a 500K particle budget
2. Parses composite JSON via `VFXComposite::LoadFromString`
3. Creates and configures three emitters with independent module stacks
4. Runs 180 frames (3 seconds at 60 FPS)
5. Fires an "explode" burst event at frame 30 (0.5s) on all emitters
6. Validates composite parsing, emitter initialization, and budget accounting
7. Prints per-frame stats and a simulation summary

### Lua Demo (`lua_demo.lua`)

Shows how to use the VFX Lua API from gameplay scripts:
- Spawn effects by asset path with position offsets
- Oscillate fire intensity dynamically via `set_parameter`
- Trigger burst events on spacebar via `emit_event`
- Gradually fade debris emission rate over time
- Clean up VFX handles in `on_stop`

## Build & Run (C++ Demo)

```bash
cd demos/explosion_composite
zig build
./zig-out/bin/explosion_composite_demo
```

## Expected Output

```
[Init] VFXSystem initialized -- budget: 500000 particles
[Init] ModuleRegistry has 6 built-in modules: ...
[Composite] Loaded 'explosion' (version 1) with 3 emitters
[Init] Emitter 'fire' initialized -- capacity=25000, budget=25000/500000
[Init] Emitter 'smoke' initialized -- capacity=15000, budget=40000/500000
[Init] Emitter 'debris' initialized -- capacity=10000, budget=50000/500000

  >>> BURST EVENT: 'explode' -- 500 particles, 3.0x velocity

[Frame  30 | t=0.50s]  budget=50000/500000
  fire    : accum=...  allocated=25000  events=0  [emitting]
  smoke   : accum=...  allocated=15000  events=0  [emitting]
  debris  : accum=...  allocated=10000  events=0  [emitting]

SIMULATION SUMMARY
  Frames simulated:        180 (3.0 seconds)
  Total particles spawned: ~4000+
  ...

[PASS] Explosion composite demo completed successfully.
```

## Lua Demo Usage

Attach `lua_demo.lua` to any scene entity via the inspector or scene file:

```json
{ "script": "demos/explosion_composite/lua_demo.lua" }
```

The script will automatically spawn fire + debris on start, smoke after 0.2s delay,
and respond to spacebar presses with burst events.

## Files

| File         | Description                                |
|--------------|--------------------------------------------|
| `main.cpp`   | Headless C++ composite effect simulation   |
| `build.zig`  | Zig build script for standalone executable |
| `lua_demo.lua` | Lua gameplay scripting demo              |
| `README.md`  | This file                                  |
