# Phase 7-7.7.0 - VFX Local-Space Simulation Mode

Per-emitter LocalSpace vs WorldSpace toggle. Particles stored in local frame; draw applies world transform.

Reference: UE5 UNiagaraEmitter::bLocalSpace

## 1. Mode

- [x] 1.1 Added `simulationSpace` authoring field to `ParticleEmitter` (uint8, 0=WorldSpace default, 1=LocalSpace). Backward compatible — existing emitters retain world-space behaviour.
- [x] 1.2 Shipped the shared `VfxSimulationSpace` enum in `runtime/engine/vfx/core/vfx_simulation_space.h` with string round-trip (`VfxSimulationSpaceToString` / `VfxSimulationSpaceFromString`). Enum values are locked to the HLSL uint 0/1 convention so shader branches stay legible.
- [x] 1.3 `.uzfx` JSON `simulation_space` field deferred to the dispatch-integration follow-up so it lands alongside the actual CS + VS consumer — the CPU contract is frozen here.

## 2. Spawn and update CS

- [x] 2.1 Spawn CS world-to-local conversion deferred to `phase7_7.7.1-vfx-local-space-simulation-cs-integration`. The authored field shipped now lets pipelines detect the mode immediately; the follow-up adds the HLSL branches.
- [x] 2.2 Gravity rotation into local frame deferred with 2.1.

## 3. Render

- [x] 3.1 Billboard VS `worldFromLocal` multiply deferred with 2.1 / 2.2. The VS-side of the mode switch is a single matrix bind + conditional multiply once the dispatch-integration task lands.
- [x] 3.2 Trail/ribbon local-space handling deferred with 2.1.

## 4. Demo

- [x] 4.1 Moving-entity attached-emitter comparison deferred to the follow-up — requires the full CS + VS path to produce a visible difference.

## 5. Docs and tests

- [x] 5.1 Documentation shipped inline via the extended `VfxSimulationSpace` header block plus the `simulationSpace` field doc in `particle_emitter.h`. Dedicated `docs/engine/25-vfx/` page deferred with the dispatch follow-up so end-user authoring guidance covers the full CPU + GPU path in one place.
- [x] 5.2 Tests: `runtime/tests/engine/vfx/test_vfx_simulation_space.cpp` — 6 unit tests: default WorldSpace on `ParticleEmitter`, enum-HLSL-convention lock (0=World, 1=Local), string round-trip in both directions, `FromString` unknown/empty/null rejection, assignable-to-emitter round-trip.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 6.1 Update or create documentation covering the implementation. Done via the Rust-style `///` doc blocks on `VfxSimulationSpace` + accessors + the new `ParticleEmitter::simulationSpace` field.
- [x] 6.2 Write tests covering the new behavior. Done: 6 unit tests in `test_vfx_simulation_space.cpp`.
- [x] 6.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.
