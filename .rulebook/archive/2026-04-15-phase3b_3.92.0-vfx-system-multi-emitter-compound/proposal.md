# Proposal: VFX System — Multi-Emitter Compound Effects (Niagara Parity)

## Why

UzEngine's current VFX design is **single-emitter-per-entity**. Compound
effects like a fireball projectile (fire head + sparks + smoke) can only be
faked by creating multiple Entities and manually syncing their transforms
in the application code. This doesn't scale:

- Artists cannot author a single "fireball" asset as one unit.
- Each compound effect needs bespoke app-level orchestration code.
- No shared parameters (e.g., "fireball intensity") apply across child emitters.
- No natural way to express "sparks spawn when fire particle dies".
- Rendering + sorting happens per-entity, not per-system — missed batching.

UE5 Niagara solves this with `UNiagaraSystem` assets containing arrays of
`NiagaraEmitterHandle` references, runtime `FNiagaraSystemInstance` objects
owning `TArray<FNiagaraEmitterInstanceRef>`, and a parameter store with
per-emitter binding overrides. A single component spawns the whole system;
the system propagates world transform to all emitter instances and tick
orchestration is done once per frame.

Attempts to fake compound effects in UzEngine (e.g., the `FireballProjectile`
preset in `demos/vfx_viewport_app/`) have consistently looked wrong — you
cannot get a convincing fireball with fire + sparks + smoke as a single
emitter because the parameters conflict (stretch for sparks vs. round for
head vs. puff for smoke).

## What Changes

Introduce a **VfxSystem** abstraction that owns multiple emitters as one
asset/instance unit, following Niagara's architecture.

### Core types (new)

1. `VfxSystem` — asset struct with array of `VfxSystemEmitterRef` (name, path,
   enabled flag, local-space flag, override params).
2. `VfxSystemInstance` — runtime counterpart owning `std::vector<std::unique_ptr<ParticleEmitter>>`.
   Handles transform propagation, tick orchestration, and renderer aggregation.
3. `VfxSystemComponent` — ECS component referencing a VfxSystem asset.
   Replaces the N-entity hack; one entity = one compound effect.

### Parameter system

4. `VfxParameterStore` — system-level parameter store (e.g., "intensity",
   "spawnRate"). Per-emitter bindings resolve which emitter uses which value.
   Inspired by `FNiagaraUserRedirectionParameterStore`.

### Event system (optional for fireball, but required for proper Niagara parity)

5. `VfxEventGenerator` / `VfxEventReceiver` — per-emitter event hooks.
   Example: fire emitter generates `ParticleDeath` events, smoke emitter
   receives them and spawns smoke particles at each death position.

### Existing single-emitter path preserved

- `ParticleEmitter`, `VfxEmitterComponent` remain for simple cases.
- `VfxSystemComponent` is additive — scene renderer checks both.

### Asset format

- `.uzvfxsys` JSON format: `{ "emitters": [{ "path": "fire_head.uzfx", "id": "...", "localSpace": true }, ...], "parameters": { ... } }`
- `.uzfx` (individual emitter asset) format already exists.
- Loader creates VfxSystem asset from JSON; runtime instantiates.

## Impact

- **Affected specs**: `docs/engine/25-vfx/` — add new pipeline section for VfxSystem.
- **Affected code**:
  - `runtime/engine/vfx/system/vfx_system.h/.cpp` (new)
  - `runtime/engine/vfx/system/vfx_system_instance.h/.cpp` (new)
  - `runtime/engine/ecs/environment/vfx_system_component.h` (new)
  - `runtime/engine/vfx/params/vfx_parameter_store.h/.cpp` (new)
  - `runtime/engine/renderer/scene/scene_vfx_sync.cpp` — register VfxSystemComponent
  - `runtime/engine/asset/formats/uzvfxsys_loader.cpp` (new, JSON parse)
  - `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` — use VfxSystem for fireball
- **Breaking change**: NO (additive; single-emitter path preserved).
- **User benefit**:
  - Author compound effects as one asset.
  - Artists can build fireball/explosion/magic-burst without C++ changes.
  - System-level parameter tweaks (e.g., intensity scale) work automatically.
  - One entity per effect (no entity proliferation).
  - Rendering batches naturally across emitters sharing a transform.

## Source

- Analysis: Niagara system architecture summary (captured as part of
  task 3.92.0 research — see section 4 of agent report).
- Reference: `U:\UnrealGITHUB\UnrealEngine\Engine\Plugins\FX\Niagara\Source\Niagara\`
  - `Public/NiagaraSystem.h` (asset structure)
  - `Public/NiagaraEmitterHandle.h` (emitter references)
  - `Private/NiagaraSystemInstance.h` (runtime instance)
  - `Public/NiagaraComponent.h` (scene component)
- Current UzEngine pain point: fireball preset attempts at
  `runtime/engine/vfx/components/emitter_presets.h:ApplyPreset_FireballProjectile`
  and `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` (m_vfxEntity +
  m_vfxSparks + m_vfxSmoke ad-hoc).
