# Lua Runtime Demo Infrastructure

## Priority: P0 — Maximum (Blocks all future demo/game development)

## Why

Creating demos currently requires building standalone C++ executables with individual `build.zig` files, duplicating engine initialization boilerplate. This is unsustainable — every new demo or game prototype requires C++ compilation, a new build target, and deep knowledge of engine internals.

The fix: integrate LuaJIT scripting + `.uzproject` loader so demos become **data-driven projects** (`demo.uzproject` + `.uzscene` + `.lua` scripts) that run on the engine runtime directly via `uzengine.exe --project path/to/demo.uzproject`. Zero C++ compilation for demo authors.

**First conversion target**: `pbr_test` demo — the most feature-rich demo (5 PBR cubes, 4 point lights, deferred/forward toggle, G-Buffer debug, shadow atlas).

## What Changes

### 1. LuaJIT Integration
- Add LuaJIT 2.1 to build system (vcpkg or manual lib)
- `LuaVM` subsystem — sandboxed Lua state (no `os`, `io`, `load`, `require`)
- `LuaScriptComponent` — ECS component that loads `.lua` with `on_start(ctx)` / `on_update(ctx, dt)` / `on_stop(ctx)` lifecycle

### 2. Lua Engine API
- `uz.log(level, msg)` — engine logging
- `uz.delta_time` / `uz.total_time` / `uz.frame` — timing
- `uz.quit()` — exit
- `uz.scene.*` — entity queries, transform get/set, component access
- `uz.camera.*` — position, look_at, fov, aspect
- `uz.input.*` — key/mouse state (is_key_down, is_key_just_pressed, get_mouse_delta)
- `uz.renderer.*` — render path toggle, debug mode, stats

### 3. .uzproject Loader
- Parse `demo.uzproject` JSON → configure window, renderer, physics
- Load `default_scene` → parse `.uzscene` JSON → spawn entities with components
- Discover and attach `.lua` scripts referenced in scene components

### 4. Runtime Standalone Mode
- `uzengine.exe --project <path>` entry point
- Loads project → initializes subsystems → loads scene → runs main loop
- Screenshot support (`--screenshot`, `--screenshot-frame`)

### 5. PBR Test Demo Conversion
- Create `demos/pbr_test/demo.uzproject` — window/render config
- Create `demos/pbr_test/scenes/main.uzscene` — 5 cubes, ground plane, lights, camera
- Create `demos/pbr_test/scripts/pbr_demo.lua` — input handling (F toggle, debug modes), camera orbit
- Delete C++ demo files after conversion validated visually

## Impact
- **Affected code**: `runtime/engine/` (new subsystems), `runtime/build.zig` (LuaJIT linking), `runtime/demos/pbr_test/` (conversion)
- **Breaking change**: No — existing C++ demos continue to work
- **User benefit**: 10x faster demo creation, zero-compilation iteration, hot-reload ready
- **Enables**: All future demos, game prototypes, editor play-mode, modding

## Dependencies
- EnTT ECS (already integrated)
- Scene schema (already defined: `uzscene.schema.json`)
- Project schema (already defined: `uzproject.schema.json`)
- RHI + Renderer (already working)
- DemoApplication base class (reuse for runtime mode)

## Risk
- LuaJIT Windows/Zig build compatibility — may need pre-built `.lib`
- Lua API surface for renderer is large — start minimal, expand per-demo needs
- Scene-driven PBR requires component factories for mesh/material/light — may need new factories
