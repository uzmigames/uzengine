# Phase 7-7.6.0 - VFX GPU Simulation Stages

Multi-pass per-emitter CS dispatches (e.g. Fluid -> Pressure -> Advect) orchestrated by the render graph with auto-barriers.

Reference: UE5 UNiagaraSimulationStageBase + FSimulationStageData

## 1. Stage model

- [ ] 1.1 Define VfxSimulationStage struct (name, csPath, iterationCount, readSet, writeSet).
- [ ] 1.2 Extend emitter JSON schema with stages ordered list (default: implicit single Update stage preserves current behavior).

## 2. Render graph integration

- [ ] 2.1 Render graph inserts N stage-dispatch passes between spawn and render.
- [ ] 2.2 Auto-barrier emission on read/write set intersection.
- [ ] 2.3 Support iteration count per stage for relaxation solvers.

## 3. Demo

- [ ] 3.1 Ship Curl Advection sample: 2-pass emitter (compute curl, advect positions) demonstrating multi-stage flow.
- [ ] 3.2 Documentation with ordering + barrier rules.

## 4. Tests

- [ ] 4.1 Stage ordering regression: disable a stage -> verify read set is no longer populated downstream.
- [ ] 4.2 Barrier correctness: GPU validation layer passes clean.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
