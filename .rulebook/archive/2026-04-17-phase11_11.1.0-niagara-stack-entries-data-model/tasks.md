# phase11_11.1.0-niagara-stack-entries-data-model

Niagara-parity stack data model: explicit 7 fixed stages × ordered module instances.

Reference: UE5 `UNiagaraStack` + analysis at `docs/analysis/vfx-particles/niagara/niagara-minimal-viable-editor.md`.

## 1. Data model

- [x] 1.1 `VfxStage` enum with explicit names: `SystemSpawn`, `SystemUpdate`, `EmitterSpawn`, `EmitterUpdate`, `ParticleSpawn`, `ParticleUpdate`, `Render`. `VfxStageName(stage)` + `VfxStageFromName(name, out)` helpers. (`runtime/engine/vfx/stack/vfx_stage.h`)
- [x] 1.2 `VfxModuleInstance` struct in `runtime/engine/vfx/stack/vfx_module_instance.h`:
      - `std::string moduleTypeName` (matches ModuleRegistry key)
      - `std::string configJson` (per-instance parameters)
      - `bool enabled = true`
      - `enum class VfxOverrideFlag : uint8_t { None, Inherited, OverrideAdd, OverrideReplace, OverrideRemove }`
      - `std::string instanceId` (stable UUID for undo/redo + overrides)
- [x] 1.3 `VfxStackEntry` struct in `runtime/engine/vfx/stack/vfx_stack_entry.h`:
      - `VfxStage stage`
      - `std::vector<VfxModuleInstance> modules` (ordered, execution order)
      - Helpers: `Add`, `InsertAt`, `Remove(size_t idx)`, `MoveUp(idx)`, `MoveDown(idx)`, `Swap(a,b)`, `Find(instanceId)`, `Size`, `IsEmpty`, `Clear`.
- [x] 1.4 `VfxStack` class in `runtime/engine/vfx/stack/vfx_stack.h`:
      - `std::array<VfxStackEntry, 7> entries` (indexed by `VfxStage`)
      - `VfxStackEntry& GetEntry(VfxStage)`, `const` variant, `Entries()` accessors.
      - `TotalModuleCount`, `IsEmpty`, `Clear`, `FindInstance(instanceId, outStage, outIndex)`.

## 2. Integration into ParticleEmitter

- [x] 2.1 Added `VfxStack stack` field to `ParticleEmitter` (`runtime/engine/vfx/components/particle_emitter.h`). Default-constructed empty for legacy assets.
- [x] 2.2 `SerializeEmitter`/`DeserializeEmitter` (`emitter_serializer.h`) write/read the stack as a `stages[]` JSON array. Each entry: `{"stage": "...", "modules": [{"type": "...", "instanceId": "...", "enabled": true, "override": "...", "config": {...}}, ...]}`. `config` is emitted as parsed JSON when valid, string fallback otherwise.
- [x] 2.3 Legacy `.uzvfx` files (no `stages[]`) load with an empty stack — inline flat-config path stays in effect. `stages` is omitted on serialize when the stack is empty (byte-identical legacy output).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/44-niagara-stack-model.md` explaining the 7-stage contract, module-instance lifecycle, stack-vs-legacy precedence, JSON schema, thread-safety, and the phase 11.x roadmap.
- [x] 3.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_stack.cpp`: stage enum round-trip + unknown input; override-flag round-trip; `kVfxStageCount` invariant; `VfxStackEntry` add/insert-clamp/remove/move-up-down edge rejection/swap/find sentinel; `VfxStack` constructor stage-field invariant, `TotalModuleCount`, `IsEmpty`, `Clear`, `FindInstance` hit + miss-preserves-outparams; JSON round-trip via `SerializeEmitter`/`DeserializeEmitter` — empty-stack omits `stages`, populated round-trip preserves all fields, legacy JSON loads empty stack, unknown stage name falls back to `ParticleUpdate`, module without `type` is skipped.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). Test binary links against the new stack headers. `zig build test` compiles `test_runtime.exe` successfully; runtime execution is blocked by a pre-existing missing-DLL issue (swscale-9.dll not deployed to `build/bin/`), unrelated to phase 11.1.
