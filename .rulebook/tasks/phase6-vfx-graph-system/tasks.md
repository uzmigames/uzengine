# Tasks: phase6-vfx-graph-system

## 1. `.uzfx` v2 Data Model

- [ ] 1.1 Define `EmitterNode` C++ struct with fields: `name`, `spawn_config: SpawnConfig`, `module_stack: vector<ModuleNode>`, `renderer: RendererNode`, `lod_config: LodConfig`, `sub_emitter_ref?`
- [ ] 1.2 Define `ModuleNode` struct: `{type, params: nlohmann::json, custom_hlsl_path?}`
- [ ] 1.3 Define `RendererNode` struct: `{type: billboard|mesh|ribbon|trail, material_path, texture_slots[]}`
- [ ] 1.4 Define `SpawnConfig` struct: `{rate, burst?, lifetime, spawn_shape}`
- [ ] 1.5 Define `LodConfig` struct: `{lod0_distance, lod1_distance, cull_distance, lod1_rate_scale}`
- [ ] 1.6 Define `EmitterGraph` struct: `{schema_version, emitters: vector<EmitterNode>, global_params}`
- [ ] 1.7 Implement full `nlohmann/json` (de)serialization for all structs

## 2. VFXGraphLoader

- [ ] 2.1 Create `src/vfx/VFXGraphLoader.h` and `src/vfx/VFXGraphLoader.cpp`
- [ ] 2.2 Implement `VFXGraphLoader::Load(path) -> EmitterGraph`: detect v1 (INI first byte `[`) vs. v2 (JSON first byte `{`); dispatch accordingly
- [ ] 2.3 Implement v1 → v2 conversion: call the Phase 3 INI parser then convert `ParticleEffect` flat struct to a single-emitter `EmitterGraph` for uniform downstream processing
- [ ] 2.4 Log a `DEBUG_VFX_V1_CONVERTED` message when v1 conversion occurs

## 3. ModuleCompiler

- [ ] 3.1 Create `src/vfx/ModuleCompiler.h` and `src/vfx/ModuleCompiler.cpp`
- [ ] 3.2 Implement `ModuleCompiler::Compile(emitter) -> CSO_Blob`: concatenate relevant HLSL includes from `src/vfx/modules/` based on the module stack bitmask
- [ ] 3.3 Invoke `IDxcCompiler3` to compile the permuted shader; return the compiled CSO blob
- [ ] 3.4 Implement blob caching: cache CSO keyed by `(module_bitmask, custom_shader_path_hash)`; return cached blob on cache hit without recompilation
- [ ] 3.5 For `CustomHLSL` modules: embed the user-supplied `.hlsl` source as an `#include` within the permuted shader

## 4. VFXHotReloader

- [ ] 4.1 Create `src/vfx/VFXHotReloader.h` and `src/vfx/VFXHotReloader.cpp`
- [ ] 4.2 Watch all referenced custom HLSL paths using `ReadDirectoryChangesW`
- [ ] 4.3 On file change: trigger recompile of affected emitter via `ModuleCompiler`; swap GPU PSO atomically without dropping a frame
- [ ] 4.4 Target: complete hot-reload in < 150ms from file save to viewport update

## 5. Lighting Modules

- [ ] 5.1 Implement `LightingPBR` module: writes particle quads to the G-Buffer when blend mode is opaque; reads from `ParticleLightingData` cbuffer for transparent blends
- [ ] 5.2 Implement `SubsurfaceScatter` module: injects particle density and scattering parameters into the volumetric froxel grid compute pass
- [ ] 5.3 Implement `CParticleLight` module: appends up to 64 per-emitter point light entries to the scene clustered light list

## 6. ParticleState Struct Upgrade

- [ ] 6.1 Expand `ParticleState` to 80-byte layout: add `custom0: float`, `custom1: float`, `emitter_id: uint32` fields for `CustomHLSL` use
- [ ] 6.2 Implement backwards-compatibility: pad Phase 3 GPU buffers to 80 bytes at load time
- [ ] 6.3 Update the simulate compute shader signature for the new 80-byte layout

## 7. Extended VFXSystem C++ API

- [ ] 7.1 Create `VFXGraphSystem` as a subclass of Phase 3 `VFXSystem`; Phase 3 public API unchanged
- [ ] 7.2 Add `load_effect(path) -> EffectHandle` method
- [ ] 7.3 Add `hot_reload(handle)` method
- [ ] 7.4 Add `set_param(handle, emitter_name, module_name, param_name, value)` method
- [ ] 7.5 Add `set_emitter_enabled(handle, emitter_name, enabled)` method
- [ ] 7.6 Add `render_thumbnail(handle, time, resolution) -> JPEG_base64` method

## 8. IPC Handlers

- [ ] 8.1 Add `vfx.loadGraph`, `vfx.saveGraph`, `vfx.hotReload`, `vfx.patchParam`, `vfx.setEmitterEnabled`, `vfx.renderThumbnail`, `vfx.getStats` IPC handlers in the MCP server IPC dispatch table (`mcp-server/src/ipc/vfxHandlers.ts`)
- [ ] 8.2 Implement `vfx.patchParam` handler: apply float/float4 value overrides to live cbuffer without shader recompile; for structural changes call `hot_reload`

## 9. Extended Lua API

- [ ] 9.1 Add `ctx.vfx:set_param(handle, emitter, module, param, value)` Lua binding
- [ ] 9.2 Add `ctx.vfx:set_emitter_enabled(handle, emitter_name, enabled)` Lua binding
- [ ] 9.3 Add `ctx.vfx:attach(entity_id, effect_handle, socket_name)` Lua binding
- [ ] 9.4 Add `ctx.vfx:detach(entity_id, socket_name)` Lua binding
- [ ] 9.5 Add `ctx.vfx:get_stats(handle) -> {particle_count, active_emitters, gpu_time_ms}` Lua binding
- [ ] 9.6 Keep Phase 3 methods (`spawn`, `stop`, `set_rate`, `is_alive`, `get_particle_count`) unchanged

## 10. Built-in Presets

- [ ] 10.1 Author all 15 built-in preset `.uzfx` v2 JSON files in `res://vfx/presets/`: `fire_small`, `fire_large`, `smoke_thin`, `smoke_dense`, `explosion_small`, `explosion_large`, `magic_portal`, `sparks_impact`, `rain`, `snow`, `blood_splatter`, `dust_kick`, `electric_arc`, `healing_burst`, `poison_cloud`
- [ ] 10.2 Validate all 15 presets load and play without errors in engine

## 11. Testing (≥95% line + branch coverage)

- [ ] 11.1 Write `tests/renderer/test_vfx_graph_loader.cpp`: load a Phase 3 v1 `.uzfx` file; assert `VFXGraphLoader` produces a single-emitter `EmitterGraph` with correct parameter values matching the v1 source
- [ ] 11.2 Write `tests/renderer/test_module_compiler_cache.cpp`: compile same emitter twice; assert second compile returns cached CSO blob without calling DXC; change one module flag; assert DXC called again
- [ ] 11.3 Write `tests/renderer/test_module_compiler_custom_hlsl.cpp`: provide a `CustomHLSL` module with a test `.hlsl` file; assert the custom source is `#include`-d in the permuted shader
- [ ] 11.4 Write `tests/renderer/test_lighting_module_gbuffer.cpp`: mock render targets; run `LightingPBR` module in opaque mode; assert G-Buffer channels receive expected values
- [ ] 11.5 Write `tests/renderer/test_vfx_hot_reload.cpp`: trigger `VFXHotReloader` on a known HLSL file; assert PSO swap completes in < 150ms; assert particle simulation continues without interruption
- [ ] 11.6 Verify ≥95% line and branch coverage across all `src/vfx/VFXGraphLoader.cpp`, `src/vfx/ModuleCompiler.cpp`, and `src/vfx/VFXHotReloader.cpp` using OpenCppCoverage
