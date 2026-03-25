# VFX Graph Asset Format — .uzvfx Loader, Parameters, Events, Composition

## Summary

Implement the VFX graph asset system: .uzvfx JSON format loading, parameter binding (runtime tweaking without reload), event system (burst/impact triggers), and composite effects (multi-emitter assets). This connects the module system (3.61.0) to data-driven authoring, enabling effects to be defined as JSON files rather than hardcoded C++.

## Why

Without a data-driven asset format, every particle effect requires C++ code changes and recompilation. The .uzvfx format enables:

1. **Data-driven authoring**: Artists define effects in JSON, no C++ knowledge needed
2. **Hot-reload**: Edit .uzvfx in editor, see changes instantly (FileWatcher integration)
3. **Parameter exposure**: Runtime tweaking via Lua API (`emitter:set_parameter("intensity", 2.0)`)
4. **Events**: Trigger one-shot bursts programmatically (bullet impacts, footsteps)
5. **Composition**: Combine multiple emitters into a single effect (explosion = fire + smoke + debris)
6. **Version control friendly**: JSON is diffable, mergeable, human-readable

## UE5 Niagara Reference (Clean-Room)

1. **FNiagaraSystem/FNiagaraEmitter**: Niagara separates System (container) from Emitter (individual particle source). UzEngine mirrors this with VFXGraph (container) → Emitter configs.
2. **FNiagaraParameterStore**: Rich parameter binding system with inheritance. UzEngine uses simpler flat parameter map with typed access (float/Vec3/Vec4).
3. **Niagara Events (FNiagaraEventScriptProperties)**: Events pass data between emitters (e.g., death event spawns child particles). UzEngine Phase 3 implements simple burst events; cross-emitter events are Phase 4.

## Impact

- **New files**: `runtime/engine/vfx/asset/` — VFX graph loader and parameter system
- **New format**: `.uzvfx` JSON (emitter config + modules + parameters + events)
- **Hot-reload**: FileWatcher integration for live .uzvfx editing
- **Lua API**: ParticleSystem Lua bindings for parameter/event control

## Dependencies

- VFX Modules (3.61.0) — module types and registry
- GPU Particle Core (3.60.0) — emitter component and buffer management
- Asset system (✅ exists) — resource loading patterns
- FileWatcher (✅ exists) — hot-reload infrastructure

## Acceptance Criteria

1. VFXGraph class loads .uzvfx JSON files with module list, parameters, events, emitter config
2. Module instantiation from JSON: type lookup via ModuleRegistry, config deserialization
3. Parameter system: typed parameters (float/Vec3/Vec4) with defaults, min/max ranges
4. Parameters modifiable at runtime without asset reload (SetParameter API)
5. Event system: named events with emission_count, velocity_scale overrides
6. EmitEvent("burst") spawns one-shot particle burst with event-defined count
7. Composite effects (.uzvfx_composite): multiple emitters with offset/delay
8. Hot-reload: FileWatcher detects .uzvfx change → reload → recompile compute shader → update active emitters
9. Lua API: ParticleSystem:set_parameter(name, value), :emit_event(name), :start(), :stop(), :pause()
10. Demo: explosion composite (fire + smoke + debris emitters, triggered by event)
11. All unit tests pass, 95%+ coverage
