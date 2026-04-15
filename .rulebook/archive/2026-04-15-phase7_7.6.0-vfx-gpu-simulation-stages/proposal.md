# Proposal: phase7_7.6.0-vfx-gpu-simulation-stages

## Why
Niagara supports multiple named simulation stages per emitter (e.g. Fluid, Pressure, Advect) each running as its own CS dispatch. UzEngine currently runs one fixed spawn+update pair. Without stages, complex simulations (fluid, cellular automata, multi-pass blur of forces) cannot be expressed within the emitter model.

## What Changes
Introduce VfxSimulationStage - a named CS dispatch with declared read/write attribute sets and optional iteration count. Emitter config gains an ordered stages list; render graph inserts them between spawn and render with correct barriers.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/modules/, renderer/frame/frame_particles.cpp
- Breaking change: NO (default preserves current single-update behavior)
- User benefit: Unlocks multi-pass simulations (fluid, cellular, iterative solvers).

## Source
Reference: UE5 UNiagaraSimulationStageBase + FSimulationStageData.
