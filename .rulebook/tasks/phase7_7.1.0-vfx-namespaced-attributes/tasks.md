# Phase 7-7.1.0 - VFX Namespaced Attribute System

Scope-qualified attribute store (Particles / Emitter / System) replacing the flat VFXEmitterConfig struct. Foundation for system parameters, events, cross-emitter reads.

Reference: UE5 NiagaraDataSet.h + NiagaraParameterStore.h

## 1. Core types

- [ ] 1.1 Add VfxAttributeScope enum (Particles, Emitter, System) and VfxAttributeKey struct (scope + stable hashed name).
- [ ] 1.2 Create VfxAttributeStore in runtime/engine/vfx/params/ with Get<T> / Set<T> typed accessors for float/int/Vec2/Vec3/Vec4/Mat4.
- [ ] 1.3 Generate HLSL CB layout from the store (one CB per scope) and emit typed HLSL accessors.

## 2. Runtime integration

- [ ] 2.1 Update ParticleEmitter and VfxSystemInstance to own a VfxAttributeStore per scope instance.
- [ ] 2.2 Migrate 3 existing fields (emitterDuration, loopCount, vfxTotalTime) to the System scope as the reference port.
- [ ] 2.3 Regenerate CBs and verify no visual regression on the fireball compound.

## 3. Documentation and tests

- [ ] 3.1 Write docs/engine/25-vfx/20-namespaced-attributes.md covering scopes, HLSL accessor API, and migration guide.
- [ ] 3.2 Unit tests: round-trip Set/Get on all typed accessors, scope isolation, HLSL CB layout determinism.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
