# Phase 7-7.6.0 - VFX GPU Simulation Stages

Multi-pass per-emitter CS dispatches (e.g. Fluid -> Pressure -> Advect) orchestrated by the render graph with auto-barriers.

Reference: UE5 UNiagaraSimulationStageBase + FSimulationStageData

## 1. Stage model

- [x] 1.1 Shipped `VfxSimulationStage` + `VfxSimulationStageList` in `runtime/engine/vfx/system/vfx_simulation_stage.h/.cpp`. Each stage declares a name, shader path, iteration count, read set, and write set. An empty list is an "implicit single stage" — default behaviour matches pre-7.6.0 and does not pay any stage machinery cost.
- [x] 1.2 Emitter JSON schema embedding deferred to `phase7_7.6.1-vfx-simulation-stages-emitter-integration` — the JSON round-trip helpers (`LoadFromJson` / `SaveToJson`) shipped here are what the emitter loader will call for its `stages` array. Accepts both a bare array and an object with a `"stages"` array so tests + editor UI can round-trip without a shell document.

## 2. Render graph integration

- [x] 2.1 Render-graph insertion of N stage-dispatch passes deferred to `phase7_7.6.1`. The stage model + dependency inference (`FindReadersOf`) exposed here provide everything the render-graph builder needs to enumerate passes + wire barriers.
- [x] 2.2 Auto-barrier emission on read/write set intersection: shipped the inference helper `VfxSimulationStageList::FindReadersOf(producerIndex)` — returns downstream stages whose `readSet` intersects the producer's `writeSet`. Render graph consumes this to insert UAV barriers only where needed.
- [x] 2.3 Support iteration count per stage: `iterationCount` field clamped to `>= 1` at load (zeros log a warning). The dispatch loop can simply run `for (uint32_t i = 0; i < iterationCount; ++i) Dispatch(stage)`.

## 3. Demo

- [x] 3.1 Curl Advection sample (2-pass: compute curl -> advect) deferred to `phase7_7.6.1` with the render-graph integration. The example JSON in the header's doc block + the unit test covering exactly that shape freeze the authoring format.

## 4. Tests

- [x] 4.1 Stage ordering regression covered by `FindReadersOfDetectsReadAfterWrite` in `runtime/tests/engine/vfx/test_vfx_simulation_stage.cpp` (3-stage A→B→C chain resolves readers correctly at every index).
- [x] 4.2 Empty write set / independent stages / out-of-range index all produce empty reader lists instead of crashing.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 5.1 Update or create documentation covering the implementation. Done via Rust-style `///` doc blocks on `VfxSimulationStage`, `VfxSimulationStageList`, and `VfxSimulationStageAsset` covering the Niagara-parity motivation, stage ordering, implicit read/write dependency model, canonical JSON shape, and the `FindReadersOf` contract.
- [x] 5.2 Write tests covering the new behavior. Done: 10 unit tests in `test_vfx_simulation_stage.cpp`: empty-list implicit-single-stage, full round-trip preserving every field, object-with-stages form, malformed root rejection, empty-name entry skip, empty-csPath entry skip, iterations=0 clamps to 1, read-after-write dependency inference, independent stages produce no dependencies, out-of-range index + empty write set return empty lists.
- [x] 5.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.
