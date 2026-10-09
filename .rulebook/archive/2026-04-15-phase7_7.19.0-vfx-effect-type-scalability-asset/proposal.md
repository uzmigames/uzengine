# Proposal: phase7_7.19.0-vfx-effect-type-scalability-asset

## Why
Niagara's UNiagaraEffectType groups effects (ambient / weapon / cinematic / spell) and applies platform-wide scalability rules (max count, cull distance, LOD bias) per group. UzEngine's VFXLodSettings is per-emitter. Without groups, scalability tuning is per-preset busywork and cannot be tweaked globally per platform.

## What Changes
Add .uzvfxtype asset (name, maxConcurrentInstances, cullDistanceMul, lodBias, platformOverrides). Emitters reference an effect type; the VFXSystem enforces group-level caps with LRU eviction when exceeded.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/, content/vfx/types/ (new)
- Breaking change: NO
- User benefit: Centralized scalability tuning per effect category.

## Source
Reference: UE5 UNiagaraEffectType.
