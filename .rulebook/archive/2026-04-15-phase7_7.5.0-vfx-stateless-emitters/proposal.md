# Proposal: phase7_7.5.0-vfx-stateless-emitters

## Why
UE5.3+ introduced stateless emitters: particles with closed-form analytical motion (no per-frame compute shader simulation). Great for bursts of simple ballistic particles (muzzle flashes, spark showers) that dominate low-end hardware scenarios. UzEngine's current model always runs the update CS - wasted cycles for effects that could be fully described by spawn-time parameters.

## What Changes
Implement VfxStatelessEmitter variant with a dedicated stateless path: spawn-only CS computes position(t) = spawnPos + spawnVel*t + 0.5*g*t*t directly in the billboard VS using currentTime - spawnTime. No update CS, no ping-pong. Limited to simple physics (gravity + linear damping) but massively cheaper.

## Impact
- Affected specs: docs/engine/25-vfx/18-vfx-system-compound.md
- Affected code: runtime/engine/vfx/, shaders/runtime/vfx/particle_stateless_vs.hlsl (new)
- Breaking change: NO (new mode, opt-in)
- User benefit: 2-4x cheaper muzzle/impact/spark effects on low-end hardware.

## Source
Reference: UE5 UNiagaraStatelessEmitter.h + FNiagaraStatelessEmitterCompiler.
