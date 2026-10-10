# Proposal: phase7_7.8.0-vfx-fixed-timestep-accumulator

## Why
Frame-rate-dependent particle simulation drifts in behavior across hardware. Niagara supports a FixedTickDeltaTime that accumulates frame-time into fixed sub-steps for determinism and stable physics/collision. UzEngine passes raw dt to the update CS, producing different results at 30 vs 144 Hz. Critical for competitive multiplayer and replay determinism.

## What Changes
Implement VfxTimestep modes (FrameRate, FixedRate(dt)). In FixedRate, accumulate frameDt in a timeAccumulator; run update CS N times with fixed_dt until accumulator drains. Clamp N to a max (8) to avoid spiral-of-death.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/components/particle_emitter.h, renderer/frame/frame_particles.cpp
- Breaking change: NO
- User benefit: Deterministic VFX for competitive multiplayer, replay systems, and cross-platform parity.

## Source
Reference: UE5 UNiagaraEmitter::FixedTickDeltaTime.
