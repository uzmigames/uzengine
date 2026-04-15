# Phase 7-7.19.0 - VFX Effect Type Scalability Asset

Asset grouping for platform-wide scalability rules (max count, cull distance, LOD bias).

Reference: UE5 UNiagaraEffectType

## 1. Asset format

- [ ] 1.1 .uzvfxtype JSON schema: name, maxConcurrentInstances, cullDistanceMul, lodBias, platformOverrides map.
- [ ] 1.2 Loader + validator.

## 2. Runtime

- [ ] 2.1 VfxEffectType runtime struct with active-instance counter.
- [ ] 2.2 Emitters reference an effect type via .uzvfx field.
- [ ] 2.3 Enforce cap on VfxSystemInstance::Init - LRU-evict oldest low-priority instance if cap exceeded.
- [ ] 2.4 Cull distance multiplier applied to 3.89.0 frustum cull.

## 3. Baseline types

- [ ] 3.1 Ship 4 types: Ambient, Weapon, Spell, Cinematic.
- [ ] 3.2 Each tuned for sensible default caps and cull distances.

## 4. Docs and tests

- [ ] 4.1 Documentation on scalability tuning workflow.
- [ ] 4.2 Tests: LRU eviction ordering, platform override resolution.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
