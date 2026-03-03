# Proposal: phase4-vfx-graph-system

## Why

The Phase 3 particle system ships with a flat INI-based `.uzfx` format that is easy to
hand-author but architecturally limited: every effect is a single emitter, simulation
behavior is fixed to a handful of hard-coded module flags, and there is no mechanism for
users to inject custom simulation logic. As games and real-time demos demand richer,
multi-emitter effects — explosions composed of shockwave, debris, fire, smoke, and light
simultaneously — a single-emitter flat file becomes a bottleneck. The `.uzfx` v2 JSON graph
format addresses this by promoting the effect definition from a flat property list to a
directed graph of named emitters, each owning an ordered module stack.

The second pain point is extensibility. Artists working on stylized or sci-fi titles need
custom per-particle simulation behaviors (logarithmic spiral orbits, attractor fields,
Lissajous paths) that no finite set of built-in modules can cover. The `CustomHLSL` module
closes this gap: users write a single HLSL function conforming to the `CustomUpdate`
interface, and the `ModuleCompiler` inlines it into the permuted simulate shader. Hot-reload
support means the authoring loop is sub-second: save the `.hlsl` file, see the change in the
viewport within 150 ms.

Finally, the Phase 3 system has no concept of physically-based particle lighting. Particles
draw in a separate forward pass disconnected from the scene's deferred lighting pipeline.
The `LightingPBR`, `SubsurfaceScatter`, and `CParticleLight` modules defined in this task
integrate particles into the deferred GBuffer pass, the volumetric froxel grid, and the
clustered shading light list — giving smoke, fire, and magic effects the same lighting
quality as static meshes without a separate render path.

## What Changes

- **`VFXGraphLoader`**: new C++ class parses `.uzfx` v2 JSON into `EmitterGraph` structs;
  detects v1 INI by first byte and calls the Phase 3 INI parser, then converts the result
  into a single-emitter `EmitterGraph` for uniform downstream processing.
- **`EmitterGraph` data model**: `EmitterNode`, `ModuleNode`, `RendererNode`, `SpawnConfig`,
  `LodConfig` C++ structs with full `nlohmann/json` (de)serialization, replacing the Phase 3
  `ParticleEffect` flat struct.
- **`ModuleCompiler`**: builds a permuted `simulate.hlsl` per emitter by concatenating the
  relevant HLSL includes from `src/vfx/modules/` based on the module stack; invokes DXC via
  `IDxcCompiler3`; caches CSO blobs keyed by module bitmask + custom shader path hash.
- **`CustomHLSL` module**: `ModuleCompiler` embeds the user-supplied `.hlsl` source as an
  `#include` within the permuted shader; `VFXHotReloader` watches all referenced custom HLSL
  paths via `ReadDirectoryChangesW` and triggers recompile + GPU PSO swap on change.
- **Lighting modules**: `LightingPBR` writes particle quads to the G-Buffer when blend is
  opaque, or reads from a per-emitter `ParticleLightingData` cbuffer for transparent blends;
  `SubsurfaceScatter` injects density + scattering into the froxel grid compute pass;
  `CParticleLight` appends up to 64 light entries per emitter to the scene light list.
- **`ParticleState` struct upgrade**: 80-byte layout adds `custom0`, `custom1` floats and
  `emitter_id` for `CustomHLSL` use; backwards-compatible with Phase 3 GPU buffers
  (old layout padded to 80 bytes at load time).
- **Extended `VFXSystem` C++ API**: `load_effect`, `hot_reload`, `set_param`,
  `set_emitter_enabled`, `render_thumbnail` methods added to `VFXGraphSystem` (subclass);
  Phase 3 `VFXSystem` public API unchanged.
- **15 built-in presets**: fire_small, fire_large, smoke_thin, smoke_dense, explosion_small,
  explosion_large, magic_portal, sparks_impact, rain, snow, blood_splatter, dust_kick,
  electric_arc, healing_burst, poison_cloud — all authored as `.uzfx` v2 JSON and stored in
  `res://vfx/presets/`.
- **Extended Lua API**: `ctx.vfx:set_param`, `ctx.vfx:set_emitter_enabled`,
  `ctx.vfx:attach`, `ctx.vfx:detach`, `ctx.vfx:get_stats` added to the `VFXLuaBinding`;
  Phase 3 methods (`spawn`, `stop`, `set_rate`, `is_alive`, `get_particle_count`) unchanged.
- **IPC handlers**: `vfx.loadGraph`, `vfx.saveGraph`, `vfx.hotReload`, `vfx.patchParam`,
  `vfx.setEmitterEnabled`, `vfx.renderThumbnail`, `vfx.getStats` added to the MCP server
  IPC dispatch table.
- **Unit tests**: `VFXGraphLoaderTest` validates v1→v2 conversion parity; `ModuleCompilerTest`
  checks CSO blob caching and CustomHLSL include injection; `LightingModuleTest` validates
  GBuffer writes and froxel injection produce correct values in mock render targets.

## Impact

- **Affected specs**: `docs/specs/rendering/vfx-system-ai.md` (primary), `docs/specs/rendering/particle-system.md` (section 8 references this task), `docs/specs/rendering/screen-space-effects.md` (froxel injection dependency)
- **Affected code**: `src/vfx/`, `src/renderer/VFXRenderer.cpp`, `src/scripting/VFXLuaBinding.cpp`, `mcp-server/src/ipc/vfxHandlers.ts`, `res://vfx/presets/` (new directory)
- **Breaking change**: NO — Phase 3 `.uzfx` v1 files continue to load unchanged; `ctx.vfx` Lua API is additive only
- **User benefit**: Multi-emitter effects with rich module stacks; custom HLSL simulation with < 150 ms hot-reload; particles participating in PBR lighting and volumetric passes; 15 production-ready preset effects available immediately
