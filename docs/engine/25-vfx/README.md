# VFX System

GPU particle system, VFX modules, VFX graph, decals, trails, and advanced effects.

> **Status**: Implemented (Phases 3.60–3.69 Complete; 3.66–5.3 Mostly Complete)
> **Source**: `runtime/engine/vfx/` + `runtime/shaders/runtime/vfx/`
> **Viewport**: `runtime/tools/uz_viewport.exe` (standalone VFX preview with full rendering stack)

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | VFX system overview, .uzfx asset format |
| [02-particle-system.md](02-particle-system.md) | GPU compute particle simulation — up to 65536 particles per emitter |
| [03-vfx-modules.md](03-vfx-modules.md) | Modular VFX components overview (architecture, concepts) |
| [04-module-api.md](04-module-api.md) | VFX Module System API reference — IVFXModule, ModuleRegistry, ModuleCompositor, builtin modules |
| [05-uzcurve-format.md](05-uzcurve-format.md) | .uzcurve JSON format — keyframe-based animation curves for particles |
| [06-gpu-noise.md](06-gpu-noise.md) | GPU noise functions — Perlin3D, SimplexNoise3D, FBM3D, CurlNoise3D |
| [04-vfx-graph.md](04-vfx-graph.md) | Node-based VFX graph for visual authoring |
| [05-decals.md](05-decals.md) | Projected decals — deferred decal rendering |
| [11-uzdecal-format.md](11-uzdecal-format.md) | .uzdecal JSON format specification — material, blend modes, lifetime, fade curves |
| [12-deferred-decal-pipeline.md](12-deferred-decal-pipeline.md) | Deferred decal rendering pipeline — G-Buffer pass, shaders, blending, sorting |
| [13-decal-pool-atlas.md](13-decal-pool-atlas.md) | Decal pool & atlas usage guide — object pooling, texture atlasing, performance optimization |
| [14-uztrail-format.md](14-uztrail-format.md) | .uztrail JSON format specification — ribbon width, color gradient, lifetime, sampling |
| [15-trail-rendering-pipeline.md](15-trail-rendering-pipeline.md) | Trail rendering pipeline — point sampling, camera-facing geometry, GPU shaders, soft depth fade |
| [16-trail-configuration-guide.md](16-trail-configuration-guide.md) | Trail configuration & usage guide — parameter tuning, common patterns, performance optimization |
| [17-collision-module.md](17-collision-module.md) | Depth-buffer particle collision — Kill / Bounce / Slide responses, surface normal estimation |
| [18-lod-budget-system.md](18-lod-budget-system.md) | LOD & budget system — distance/screen-size evaluation, significance ranking, budget enforcement |
| [19-mesh-particle-rendering.md](19-mesh-particle-rendering.md) | Instanced mesh particle rendering — rotation modes, compute instance transforms, graphics pass |
| [20-light-particles.md](20-light-particles.md) | Particle-emitted lights — GPU→CPU readback, light injection, clustered lighting integration |
| [21-render-graph-integration.md](21-render-graph-integration.md) | VFX render graph integration — pass ordering, barrier state machine, PSO registration, bindless status |
| [07-uzvfx-format.md](07-uzvfx-format.md) | .uzvfx JSON format specification — emitter, modules, parameters, events |
| [08-uzvfx-composite-format.md](08-uzvfx-composite-format.md) | .uzvfx_composite format — multi-emitter effects with delay and offset |
| [09-lua-vfx-api.md](09-lua-vfx-api.md) | Lua VFX API reference — spawn, set_parameter, emit_event, lifecycle control |
| [10-hot-reload.md](10-hot-reload.md) | VFX hot-reload workflow — edit → save → see changes (dev-only) |
