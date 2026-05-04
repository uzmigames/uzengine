# Proposal: phase11_11.7.0-niagara-spawn-rate-curve-editor

## Why
Today `ParticleEmitter::emissionRate` is a single float. Niagara lets authors vary spawn rate over emitter time via a curve (ramp up for explosions, taper for trailing fire, etc.). Without this, artists simulate time-varying spawn via hacks (cycling a second emitter on/off). Phase 11.5's curves editor provides the canvas; this task specializes it for spawn rate and plumbs the runtime sampling.

## What Changes
Add `spawnRateCurve: VFXCurve` to `ParticleEmitter` (optional; when empty, fall back to constant `emissionRate`). Each frame, `CalculateSpawnCount(dt)` samples the curve at `timeSinceRegistration` (clamped / looped per loop mode) and multiplies by `dt`. Editor UI: dedicated "Spawn Rate" panel inside the Emitter Spawn stage. When the user clicks into it, opens 11.5's curves editor bound to this curve with X-axis = emitter time (0..emitterDuration) and Y-axis = particles/sec.

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 47-spawn-rate-curve.md)
- Affected code: ParticleEmitter (`spawnRateCurve` field); `CalculateSpawnCount()` branch when curve has ≥ 1 key; 11.5 curves widget invoked in spawn-rate mode
- Breaking change: NO (constant rate stays default)
- User benefit: ramped / tapered / pulsed spawn without Lua scripting
- Estimated effort: S

## Source
UE5 `FNiagaraSpawnInfo::RateCurve` + phase 11.5 canvas widget.
