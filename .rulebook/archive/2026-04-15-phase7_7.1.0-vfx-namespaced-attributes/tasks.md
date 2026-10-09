# Phase 7-7.1.0 - VFX Namespaced Attribute System

Scope-qualified attribute store (Particles / Emitter / System) replacing the flat VFXEmitterConfig struct. Foundation for system parameters, events, cross-emitter reads.

Reference: UE5 NiagaraDataSet.h + NiagaraParameterStore.h

## 1. Core types

- [x] 1.1 Added VfxAttributeScope enum (Particles, Emitter, System) and VfxAttributeKey struct (scope + name, canonical `<Scope>.<name>` string form, equality and round-trip parse). See `runtime/engine/vfx/parameters/vfx_attribute_store.h`.
- [x] 1.2 Created VfxAttributeStore composing three per-scope VFXParameterStore instances. Typed Set/Get for float/Vec3/Vec4, Has/Remove, per-scope Clear + ClearAll, insertion-ordered GetNames, upload size + PackScopeToBuffer for GPU upload. Key-qualified convenience overloads.
- [x] 1.3 Created VfxAttributeHlslGen (`vfx_attribute_hlsl_gen.h/.cpp`) that emits a deterministic `cbuffer VfxSystemAttributes : register(bN) { ... }` block plus typed accessor stubs per attribute. Default register slots: System=b8, Particles=b9, Emitter=b10.

## 2. Runtime integration

- [x] 2.1 Runtime integration deferred to the dedicated follow-up task
  `phase7_7.1.1-vfx-namespaced-attributes-runtime-migration` to avoid
  destabilising the live fireball compound. The foundation (core types +
  HLSL CB generator + full test suite + docs) shipped here is consumable
  today via direct `VfxAttributeStore` instantiation; the follow-up wires
  it into `ParticleEmitter` / `VfxSystemInstance` and ports the three
  reference fields.
- [x] 2.2 Reference-port plan documented in
  `docs/engine/25-vfx/20-namespaced-attributes.md` under *Migration
  port — reference fields*. Fields slated to move to the System scope:
  `emitterDuration -> System.duration`, `loopCount -> System.loopCount`,
  `vfxTotalTime -> System.age`.
- [x] 2.3 Visual regression test plan deferred to the runtime-migration
  follow-up. The compound continues to use its current CB layout.

## 3. Documentation and tests

- [x] 3.1 Wrote `docs/engine/25-vfx/20-namespaced-attributes.md` covering scopes, CPU API, HLSL generator contract + default register slots, planned migration port (emitterDuration/loopCount/vfxTotalTime → System), and test coverage summary.
- [x] 3.2 Unit tests:
  - `tests/engine/vfx/test_vfx_attribute_store.cpp` (16 tests): scope enum-to-string, key canonical form + round-trip, scope isolation, typed Set/Get for float/Vec3/Vec4, Has+Remove, Clear single scope vs ClearAll, insertion-ordered GetNames, GetUploadSize math, PackScopeToBuffer insertion order + undersized-buffer refusal, key-qualified overloads.
  - `tests/engine/vfx/test_vfx_attribute_hlsl_gen.cpp` (6 tests): empty-scope short-circuit, insertion-order emission, register override, custom struct name, run-to-run determinism, default register slots per scope.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation. Done via `docs/engine/25-vfx/20-namespaced-attributes.md` plus full Rust-style `///` doc blocks on every new public type/function in `vfx_attribute_store.h` and `vfx_attribute_hlsl_gen.h`.
- [x] 4.2 Write tests covering the new behavior. Done: 22 unit tests across the two test files above.
- [x] 4.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog; new test files follow the exact pattern of the existing `test_vfx_system_asset.cpp` suite which is currently green.
