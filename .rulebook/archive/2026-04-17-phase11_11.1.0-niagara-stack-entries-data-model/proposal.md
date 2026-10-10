# Proposal: phase11_11.1.0-niagara-stack-entries-data-model

## Why
Niagara authors think in terms of 7 fixed stages (System Spawn, System Update, Emitter Spawn, Emitter Update, Particle Spawn, Particle Update, Render) with ordered module cards inside each. UzEngine has the module system (48 built-ins) but no explicit stack-entry container — the editor overlay today lists modules flat under a `VfxStage` enum. Before any Niagara-parity UI can land, the data model needs an explicit ordered per-stage module list with enable flag + override markers.

## What Changes
Add `VfxStackEntry` (one per stage) + `VfxModuleInstance` (one per module within a stage) in `runtime/engine/vfx/stack/`. Each `VfxStackEntry` owns an ordered `std::vector<VfxModuleInstance>`. Each `VfxModuleInstance` carries: the module's registered name, a JSON config blob (for per-instance parameters), an `enabled` bool, and an `overrideFlag` enum (`None | Inherited | Added | Replaced`) for 11.11's inheritance-indicator work. `VfxStack` aggregates the 7 entries for a `ParticleEmitter` and exposes iteration + Add/Remove/Reorder helpers. JSON round-trip via the existing `.uzvfx` format (extension, not break).

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 44-niagara-stack-model.md)
- Affected code: runtime/engine/vfx/stack/* (new), ParticleEmitter (adds `stack: VfxStack` field), emitter_serializer.h (JSON fields for stack entries)
- Breaking change: NO (existing flat config stays valid; stack is additive and defaults to empty)
- User benefit: unblocks 11.2 (stack panel UI) and 11.3 (module picker)
- Estimated effort: S

## Source
UE5 `UNiagaraStack::StackEntry` + `FNiagaraStackModuleData` + analysis at `docs/analysis/vfx-particles/niagara/niagara-minimal-viable-editor.md` (Section 1.1).
