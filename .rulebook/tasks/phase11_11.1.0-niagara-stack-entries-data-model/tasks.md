# phase11_11.1.0-niagara-stack-entries-data-model

Niagara-parity stack data model: explicit 7 fixed stages × ordered module instances.

Reference: UE5 `UNiagaraStack` + analysis at `docs/analysis/niagara-minimal-viable-editor.md`.

## 1. Data model

- [ ] 1.1 `VfxStage` enum (exists) extended with explicit names: `SystemSpawn`, `SystemUpdate`, `EmitterSpawn`, `EmitterUpdate`, `ParticleSpawn`, `ParticleUpdate`, `Render`. Static `VfxStageName(stage)` helper.
- [ ] 1.2 `VfxModuleInstance` struct in `runtime/engine/vfx/stack/vfx_module_instance.h`:
      - `std::string moduleTypeName` (matches ModuleRegistry key)
      - `std::string configJson` (per-instance parameters)
      - `bool enabled = true`
      - `enum class OverrideFlag : uint8_t { None, Inherited, Added, Replaced }`
      - `std::string instanceId` (stable UUID for undo/redo + overrides)
- [ ] 1.3 `VfxStackEntry` struct in `runtime/engine/vfx/stack/vfx_stack_entry.h`:
      - `VfxStage stage`
      - `std::vector<VfxModuleInstance> modules` (ordered, execution order)
      - Helpers: `Add(VfxModuleInstance)`, `Remove(size_t idx)`, `MoveUp(idx)`, `MoveDown(idx)`, `Find(instanceId)`.
- [ ] 1.4 `VfxStack` class in `runtime/engine/vfx/stack/vfx_stack.h`:
      - `std::array<VfxStackEntry, 7> entries` (indexed by `VfxStage`)
      - `VfxStackEntry& GetEntry(VfxStage)`, `const` variant.
      - Iteration over all modules in execution order (flat list across stages).

## 2. Integration into ParticleEmitter

- [ ] 2.1 Add `VfxStack stack` field to `ParticleEmitter` (`runtime/engine/vfx/components/particle_emitter.h`).
- [ ] 2.2 Emitter serializer writes/reads the stack as a new `stages[]` JSON array in `.uzvfx`.
      Each entry: `{"stage": "ParticleUpdate", "modules": [{"type": "Gravity", "config": {...}, "enabled": true}, ...]}`.
- [ ] 2.3 Legacy `.uzvfx` files (no `stages[]`) still load — empty stack means the inline flat-config path (today's behaviour) stays in effect.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 3.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/44-niagara-stack-model.md` explaining the 7-stage contract + module-instance lifecycle.
- [ ] 3.2 Write tests covering the new behavior
      Unit tests: stage enum round-trip, `VfxStackEntry` add/remove/reorder, `VfxStack` iteration order, JSON round-trip, legacy-asset upgrade path.
- [ ] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` + (if possible) `zig build test` for the new stack module tests.
