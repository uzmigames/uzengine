# VFX System — Multi-Emitter Compound Effects

Niagara-parity architecture for authoring a compound VFX effect (fireball,
explosion, magic burst) as **one asset / one entity / one component**
instead of N parallel entities synchronised by application code.

## Concept

A `VfxSystem` asset is an **ordered list of emitter references**. At
runtime, a `VfxSystemComponent` on an ECS entity produces a single
`VfxSystemInstance` owning one `ParticleEmitter` per reference. Moving
the entity propagates the world transform to every child emitter.

```
Entity
└── SceneComponent               world transform
└── VfxSystemComponent           asset path, emitting flag
         │
         ▼ (scene_vfx_sync.cpp)
    VfxSystemInstance            owns vector<unique_ptr<ParticleEmitter>>
         ├── ParticleEmitter #1  (FireballHead)    ─┐
         ├── ParticleEmitter #2  (FireballSparks)  │ registered with
         ├── ParticleEmitter #3  (FireballEmbers)  │ the global VFXSystem
         └── ParticleEmitter #4  (FireballSmoke)   ─┘
```

## Asset format — `.uzvfxsys`

```json
{
  "format":  "uzvfxsys",
  "version": 1,
  "name":    "Fireball",
  "emitters": [
    {"name": "head",   "path": "preset:FireballHead",   "id": "e1",
     "enabled": true,  "localSpace": true},
    {"name": "sparks", "path": "preset:FireballSparks", "id": "e2",
     "enabled": true,  "localSpace": true},
    {"name": "embers", "path": "preset:FireballEmbers", "id": "e3",
     "enabled": true,  "localSpace": true},
    {"name": "smoke",  "path": "preset:FireballSmoke",  "id": "e4",
     "enabled": true,  "localSpace": true}
  ]
}
```

### Emitter path schemes

| Prefix           | Resolution                                                   |
|------------------|--------------------------------------------------------------|
| `preset:<Name>`  | Resolved to a C++ `ApplyPreset_<Name>` function at init time |
| `<path>.uzfx`    | Loaded through `UzfxLoader` (same path used by single-emitter) |

The `preset:` scheme is cheaper because it avoids serialising/parsing a
per-child `.uzfx` asset when the data already lives in
`emitter_presets.h`. The `.uzfx` route is the intended long-term format
for artist-authored compounds.

### Field reference

| Field        | Required | Default | Meaning                                    |
|--------------|----------|---------|--------------------------------------------|
| `name`       | no       | `""`    | Human-readable label (editor/debug)        |
| `path`       | yes      | —       | Preset or `.uzfx` reference                |
| `id`         | no       | index   | Stable identifier (auto-generated if missing) |
| `enabled`    | no       | `true`  | Skip children with `false` during Init     |
| `localSpace` | no       | `true`  | If `false`, child stays at world origin    |

## Runtime flow

### 1. Sync

`scene_vfx_sync.cpp` iterates `VfxSystemComponent` entities. On the
first tick it:

1. `VfxSystem::Load(path, vfs)` — reads the `.uzvfxsys` JSON.
2. Constructs a `VfxSystemInstance` and calls `Init(asset, device, budget)`.
3. For each child `ParticleEmitter`:
   - Uploads initial GPU state (positions/ages/lifespans/sub-image).
   - Loads the sprite via `VFXMaterial::LoadTexture` (if the preset
     sets `spriteTexturePath`) — same path that the single-emitter
     component uses, unified in task 3.91.0.
   - Calls `activeVFX->RegisterEmitter(child)` so the global
     `VFXSystem` sees it alongside standalone emitters.

On subsequent ticks it only syncs transform + emitting state.

### 2. Transform propagation

`SetWorldTransform(pos)` iterates every child. Local-space children get
their `worldPosition` updated; world-space children are pinned to
`(0, 0, 0)` so their particles spawn at the origin regardless of where
the entity moved.

### 3. Simulation

Each child emitter runs the normal spawn → update → render pipeline.
The compound has no extra per-frame cost beyond the N single-emitter
costs plus one transform propagation.

### 4. Rendering aggregation

`SceneRenderer::RegisterParticlePasses` consumes
`VFXSystem::GetActiveEmitters()`, which returns the **flat list** of
every emitter registered across **all** components — standalone and
compound children alike. Depth sort, PSO batching, and indirect draw
registration happen uniformly. No compound-specific render path is
required.

## Example — fireball compound

`runtime/content/vfx/systems/fireball.uzvfxsys` ships with 4 children:

| Child              | Role                         | Blend          | Emissive | Flipbook |
|--------------------|------------------------------|----------------|----------|----------|
| `FireballHead`     | Hot core                     | AlphaComposite | 0.3      | 6×6 @ 30 |
| `FireballSparks`   | Velocity-aligned streaks     | Additive       | 1.0      | none     |
| `FireballEmbers`   | Buoyant specks, exponential decel | Additive  | 0.8      | none     |
| `FireballSmoke`    | Residual drift               | AlphaBlend     | 0.0      | 6×6 @ 20 |

Demo usage (`runtime/demos/vfx_viewport_app/`):

```cpp
Entity e = world.CreateEntity();
world.AddComponent<CName>(e, CName{"VFX_Fireball"});
world.AddComponent<SceneComponent>(e);
auto& sys = world.AddComponent<VfxSystemComponent>(e);
sys.systemAssetPath = "vfx/systems/fireball.uzvfxsys";
// SceneComponent::SetPosition later drives all 4 children via
// VfxSystemInstance::SetWorldTransform during the next sync tick.
```

## Relationship to existing types

- **`VfxEmitterComponent`** — unchanged. Use for single-emitter effects.
- **`VfxSystemComponent`** — additive. Use for compound effects.
- **`VFXSystem` (global singleton)** — unchanged aggregator. Both
  component paths register their `ParticleEmitter*` into it.
- **`VfxSystem` (asset)** — new. Asset-side description of a compound
  (note the case: `VfxSystem` asset vs `VFXSystem` runtime aggregator).

## Deferred / future work

Tracked in follow-up tasks; not required for the MVP:

- **Parameter store** (`VfxParameterStore`) — system-wide named values
  with per-emitter binding overrides (`intensity`, `scale`, etc.).
  Mirrors `FNiagaraUserRedirectionParameterStore`.
- **Event system** (`VfxEventGenerator` / `VfxEventReceiver`) — e.g.
  smoke particles spawn on fire-particle death events.
- **`.uzvfxsys` wrapper tool** — CLI that converts a list of emitter
  preset names into a `.uzvfxsys` JSON file.
- **Artist guide** (`docs/guides/authoring-vfx-compound.md`) — artist-
  facing how-to for building compound effects in the editor.

## See also

- `runtime/engine/vfx/system/vfx_system.h` / `.cpp` — asset type.
- `runtime/engine/vfx/system/vfx_system_instance.h` / `.cpp` — runtime.
- `runtime/engine/ecs/environment/vfx_system_component.h` — ECS component.
- `runtime/engine/renderer/scene/scene_vfx_sync.cpp` — sync path.
- `runtime/tests/engine/vfx/test_vfx_system_asset.cpp` — 19 parse tests.
- `runtime/tests/engine/vfx/test_vfx_system_instance.cpp` — runtime tests.
- `17-particle-flipbook-pipeline.md` — flipbook data flow used by the
  head/smoke children.
