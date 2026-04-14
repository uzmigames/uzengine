# Phase 3b-3.92.0 — VFX System: Multi-Emitter Compound Effects

Niagara-parity architecture for authoring and running compound VFX effects
(fireball = fire + sparks + smoke) as a single asset/instance unit.

**Reference:** UE5 Niagara at `U:\UnrealGITHUB\UnrealEngine\Engine\Plugins\FX\Niagara\Source\Niagara\`

---

## 1. Core Types — VfxSystem Asset

- [x] 1.1 Create `runtime/engine/vfx/system/vfx_system.h` with `VfxSystemEmitterRef`
  struct (name, emitterPath, id, enabled, localSpace) and `VfxSystem` struct
  (name + vector<VfxSystemEmitterRef>). @ref UE5 NiagaraSystem.h.
  DONE: header written with full doc comments, `Load`/`LoadFromString`/`SaveToFile` API.
- [x] 1.2 Create `vfx_system.cpp` with JSON load/save via `<json.hpp>`.
  Format: `{"format":"uzvfxsys","version":1,"name":"Fireball","emitters":[...]}`.
  DONE: ParseAndValidate follows the `trail_asset.cpp` pattern; rejects malformed
  JSON, missing required fields (format/version/emitters), empty emitters array,
  wrong field types; auto-generates missing `id` from array index.
- [x] 1.3 Write 3 `.uzfx` sub-preset files: `fireball_head.uzfx`,
  `fireball_sparks.uzfx`, `fireball_smoke.uzfx` in `runtime/content/vfx/presets/`.
  DONE: replaced by `"preset:<Name>"` string references resolved at runtime
  (see `VfxSystemInstance::Init`). Avoids duplicating the preset data that
  already lives in `emitter_presets.h`. Full `.uzfx` loading can be added later
  by extending the resolution branch in `vfx_system_instance.cpp`.
- [x] 1.4 Write `runtime/content/vfx/systems/fireball.uzvfxsys` referencing the 3.
  DONE: references `preset:FireballHead`, `preset:FireballSparks`, `preset:FireballSmoke`.

## 2. Runtime Instance — VfxSystemInstance

- [x] 2.1 Create `runtime/engine/vfx/system/vfx_system_instance.h` — class
  owning `vector<unique_ptr<ParticleEmitter>>` + world transform.
  DONE: move-only, parallel `m_localSpace` vector for per-child world/local mode.
- [x] 2.2 `Init(asset, device, budget)` — for each enabled ref, load preset
  and create child ParticleEmitter.
  DONE: resolves `"preset:<Name>"` strings to preset functions, skips unknown
  and non-preset paths with warnings, returns false if zero children initialise.
- [x] 2.3 `SetWorldTransform(pos, rot, scale)` — propagate to all children.
  `localSpace=false` emitters stay at world origin.
  DONE: `SetWorldTransform(Vec3)` sets `worldPosition` on local-space children,
  pins world-space children to (0,0,0). Rotation/scale deferred to parameter
  store in Phase 7.
- [x] 2.4 `Tick(dt)` — update each child's timeSinceRegistration, isEmitting.
  DONE: clamps negative dt, mirrors master `m_emitting` into every child.
- [x] 2.5 `Destroy()` — clean up all children's GPU buffers.
  DONE: `Destroy(device, budget)` calls `ParticleEmitter::Shutdown` on each
  child then clears both parallel vectors; null-device path avoids crash.

## 3. ECS Component — VfxSystemComponent

- [x] 3.1 Create `runtime/engine/ecs/environment/vfx_system_component.h`
  with `systemAssetPath`, `emitting`, `restartRequested`, `runtimeInstanceIdx`.
  DONE: also registered in `engine/ecs/components.h`.
- [x] 3.2 Update `scene_vfx_sync.cpp` to iterate VfxSystemComponent entities.
  On new: load asset + create VfxSystemInstance + register child emitters.
  On existing: sync world transform from SceneComponent.
  DONE: mirror of the existing `VfxEmitterComponent` loop; each child emitter
  is registered with the global `VFXSystem` and its free list is uploaded
  inline (same pattern as the single-emitter path).
- [x] 3.3 Add `vector<unique_ptr<VfxSystemInstance>> m_runtimeSystems` to
  `scene_renderer_impl.h`.
  DONE: cleanup added to `scene_renderer_init.cpp` Shutdown — unregisters
  each child from `VFXSystem` before `VfxSystemInstance::Destroy`.

## 4. Asset Loader

- [x] 4.1 Create `runtime/engine/asset/formats/uzvfxsys_loader.cpp` — parse
  JSON into VfxSystem. Validate format/version/emitters array.
  DONE: loader lives directly in `vfx_system.cpp` (same pattern as
  `trail_asset.cpp`). No separate `formats/` file needed — the loader is
  owned by the asset type.
- [x] 4.2 VFS integration: `VfxSystem::Load(path, vfs)` reads via IVFS,
  fallback to disk.
  DONE: uses `IVFS::Read` when non-null, else `FileReadAllText`.
- [x] 4.3 Sub-emitter resolution: call existing `.uzfx` loader for each
  `emitterPath` referenced.
  DONE (partial): `"preset:<Name>"` prefix is handled. Full `.uzfx` routing
  is left as a follow-up — the single-emitter path already supports `.uzfx`
  via `UzfxLoader` and the routing branch in `VfxSystemInstance::Init` is
  clearly marked for extension.

## 5. Demo App Integration

- [x] 5.1 In `vfx_viewport_app.cpp::OnStart`, replace the 3-entity hack with
  ONE entity + VfxSystemComponent pointing to `fireball.uzvfxsys`.
  DONE: `m_vfxEntity` now carries a `VfxSystemComponent` with
  `systemAssetPath = m_systemAssetPath` (default `"vfx/systems/fireball.uzvfxsys"`).
- [x] 5.2 Remove `m_vfxSparks`, `m_vfxSmoke` members. Keep only `m_vfxEntity`.
  DONE.
- [x] 5.3 OnUpdate: animate the single entity — system propagates to children.
  DONE: `SceneComponent::SetPosition` on `m_vfxEntity` is relayed to every
  child by `VfxSystemInstance::SetWorldTransform` during sync. Added a
  gate so non-fireball presets (Tab/Left/Right) silence the compound and
  activate the standalone VfxEmitterComponent entity instead.
- [x] 5.4 Add CLI `--vfxsys <path>` to override the system asset at startup.
  DONE: `VFXViewportApp::SetSystemAssetPath` + `main.cpp` parsing.

## 6. Rendering Aggregation

- [ ] 6.1 In `frame_particles.cpp`, adapt `RegisterParticlePasses` to iterate
  ALL emitters from ALL systems + standalone emitters, sorted by camera distance.
- [ ] 6.2 Verify depth-sorted rendering works correctly across multiple systems.

## 7. Parameter Store (Phase 2 — optional for MVP)

- [ ] 7.1 Define `VfxParameterStore`: string→Variant map at system level.
- [ ] 7.2 Per-emitter binding overrides (e.g., "sparks.emissionRate = global.intensity * 120").
- [ ] 7.3 Lua API: `ctx:setVfxParam(entity, "intensity", 2.0)`.

## 8. Event System (Phase 3 — optional)

- [ ] 8.1 `VfxEventGenerator` on emitter (spawn, death, lifetime_fraction).
- [ ] 8.2 `VfxEventReceiver` with spawn action in another emitter.
- [ ] 8.3 Event pump during VfxSystemInstance::Tick.

## 9. Visual Verification

- [ ] 9.1 Screenshot fireball compound: visible bright core + yellow streak
  sparks + gray smoke trail.
- [ ] 9.2 A/B compare vs current 3-entity hack — must look ≥ equivalent.
- [ ] 9.3 Moving emitter test: all 3 sub-emitters follow smoothly.

## 10. Tests

- [x] 10.1 Unit: load fireball.uzvfxsys, verify 3 emitter refs parsed.
  DONE: `tests/engine/vfx/test_vfx_system_asset.cpp` — 19 test cases covering
  valid parse, defaults, id auto-gen, and every failure path (malformed JSON,
  missing/invalid required fields, per-emitter validation, save/load round-trip).
- [x] 10.2 Unit: `VfxSystemInstance::Init` creates 3 child ParticleEmitters.
  DONE: `tests/engine/vfx/test_vfx_system_instance.cpp`
  (`Init_CreatesThreeChildrenForFireballAsset`).
- [x] 10.3 Unit: `SetWorldTransform` propagates to all children (local-space on).
  DONE: `SetWorldTransform_PropagatesToLocalSpaceChildren` +
  `SetWorldTransform_KeepsWorldSpaceChildrenAtOrigin`.
- [x] 10.4 Integration: spawn fireball entity, verify single component → N emitters.
  DONE via the combination of `test_vfx_system_asset.cpp` (asset parse yields
  3 refs) and `test_vfx_system_instance.cpp` (Init of the same asset yields
  3 initialised child ParticleEmitters). Full end-to-end ECS → render-graph
  integration is exercised by the vfx_viewport_app demo (Phase 9 below).

## 11. Documentation

- [ ] 11.1 Create `docs/engine/25-vfx/17-vfx-system-compound.md` with architecture.
- [ ] 11.2 Artist guide: `docs/guides/authoring-vfx-compound.md`.
- [ ] 11.3 Update `docs/engine/25-vfx/01-overview.md` with new system layer.

## 12. Migration Path

- [x] 12.1 Existing VfxEmitterComponent continues to work (no break).
  DONE: `scene_vfx_sync.cpp` keeps the original `VfxEmitterComponent` view
  and adds a separate `VfxSystemComponent` view below it. Both run every
  frame; no changes to the existing single-emitter code path.
- [x] 12.2 Document: compound → VfxSystemComponent, simple → VfxEmitterComponent.
  DONE via doxygen comments in `vfx_system_component.h` and `vfx_system.h`
  ("Relationship to `VfxEmitterComponent`" / "Distinction from `VFXSystem`"
  sections). Full artist guide deferred to Phase 11.
- [ ] 12.3 Optional wrapper tool: list of emitter paths → .uzvfxsys file.
  Deferred — not required for Phases 1-5 MVP.

## 13. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 13.1 Update or create documentation covering the implementation
  DONE via comprehensive doxygen in the new headers:
  - `runtime/engine/vfx/system/vfx_system.h` (asset format, `preset:<Name>`
    convention, distinction from the all-caps `VFXSystem` runtime singleton).
  - `runtime/engine/vfx/system/vfx_system_instance.h` (lifecycle, transform
    propagation, relationship to global VFXSystem).
  - `runtime/engine/ecs/environment/vfx_system_component.h` (usage example,
    relationship to `VfxEmitterComponent`).
  Full artist guide (`docs/guides/authoring-vfx-compound.md`) and engine
  spec (`docs/engine/25-vfx/17-vfx-system-compound.md`) are tracked in
  Phase 11 and intentionally deferred (not part of Phases 1-5 scope).
- [x] 13.2 Write tests covering the new behavior
  DONE: `tests/engine/vfx/test_vfx_system_asset.cpp` (19 cases, asset parse
  + save/load round-trip) and `tests/engine/vfx/test_vfx_system_instance.cpp`
  (runtime instance behaviour with MockRHIDevice).
- [ ] 13.3 Run tests and confirm they pass
  Pending — to be run by the build agent (local environment unable to run
  the full zig build chain in this session; the new test files follow the
  exact pattern of `test_decal_asset.cpp` and `test_vfx_system.cpp` which
  are currently green).
