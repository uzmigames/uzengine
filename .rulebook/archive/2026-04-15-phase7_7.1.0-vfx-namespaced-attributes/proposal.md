# Proposal: phase7_7.1.0-vfx-namespaced-attributes

## Why
Niagara exposes attributes in scoped namespaces (Particles.Position, Emitter.LoopCount, System.Age) so modules can bind to values at the right scope. UzEngine's current model is a flat per-emitter struct (VFXEmitterConfig) with no scope separation. This blocks system-wide parameter propagation, inter-emitter binding (sparks reading head.position), and generic module authoring. Foundational for tasks 7.2, 7.3, 7.9-7.14.

## What Changes
Introduce VfxAttributeKey (scope + name pair) and VfxAttributeStore with three scopes: Particles, Emitter, System. Lookup is scope-qualified. Migrate existing CB fields into scoped views. Refactor module snippets to read via scope-qualified aliases so the same HLSL works across scopes.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/params/, components/particle_emitter.h, shaders/runtime/vfx/
- Breaking change: NO (additive; existing fields remain accessible)
- User benefit: Enables system-wide parameter binding, inter-emitter references, and cleaner module authoring.

## Source
Reference: UE5 NiagaraDataSet.h (FNiagaraVariable + namespace enum), NiagaraParameterStore.h
