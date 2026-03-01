# Phase 2 -- Shader Include / Library System

## Why

Every shader in the engine needs access to shared math utilities (PI, EPSILON, safe_normalize), PBR BRDF functions (GGX, Schlick, Cook-Torrance), light evaluation, shadow sampling, skinning, tonemapping, and noise. Without a shared include library, these functions would be copy-pasted across shaders, leading to bugs when one copy is updated but not others. A centralized `shaders/include/` directory with DXC `-I` include path and dependency tracking for hot-reload solves this.

Additionally, global per-frame parameters (time, wind, fog) need a single cbuffer (`b15`) available to all shaders, avoiding duplicate definitions.

## What Changes

- Create `shaders/include/` directory with 8 `.hlsli` files: `common`, `brdf`, `lighting`, `shadow`, `skinning`, `tonemapping`, `noise`, `global_cbuffer`.
- DXC compiler configured with `-I shaders/include` include path.
- `ShaderCompiler` API: `set_include_path()`, `get_includes()` for dependency tracking.
- `IncludeTracker`: forward and reverse dependency maps for hot-reload cascade.
- `GlobalParams` cbuffer at register `b15`: `g_Time`, `g_DeltaTime`, `g_WindDirection`, `g_WindStrength`, `g_FogColor`, `g_FogDensity`, `g_FogStart`, `g_FogEnd`.
- CPU-side `GlobalParamsData` struct uploaded once per frame via root signature slot.
- Hot-reload: when a `.hlsli` file changes, all shaders that include it (transitively) are recompiled.

## Dependencies

- **phase1-shader-hot-reload** (DXC integration, file watcher)
- **phase2-shader-variants** (recompilation of all variants when include changes)

## Spec Reference

[Shader Library Spec](../../../docs/specs/shader-library.md)

## Impact
- Affected specs: `shader-system.md`, `renderer.md`, `shader-library.md` (new)
- Affected code: `src/renderer/shader_compiler/`, `shaders/include/` (new), root signature layout
- Breaking change: NO (new include files, new cbuffer slot b15)
- User benefit: Clean shader authoring with shared functions; consistent BRDF across all materials; global fog/wind accessible everywhere; correct hot-reload of include dependencies
