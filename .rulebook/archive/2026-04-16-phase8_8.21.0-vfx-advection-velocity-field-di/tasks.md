# phase8_8.21.0-vfx-advection-velocity-field-di

Direct velocity override from a 3D velocity texture.

Reference: UE5 UNiagaraDataInterfaceVelocityGrid.

## 1. Advection DI

- [x] 1.1 VfxAdvectionFieldDI CPU struct + bindless SRV registration.
      `engine/vfx/data_interfaces/vfx_advection_field_di.h/.cpp` —
      `VfxAdvectionFieldConfig` carries the bindless SRV index, world-
      space AABB, and tightness. `IsEnabled()` requires both a bound
      SRV (`!= 0xFFFFFFFF`) and a non-degenerate AABB so legacy assets
      are safe.
- [x] 1.2 HLSL helper SampleAdvection(worldPos) with world-to-field remap.
      `VfxAdvectionEmitHLSLSnippet` returns a canonical
      `SampleAdvection` that remaps world → UV, guards out-of-bounds
      to zero velocity, and bindlessly samples the `Texture3D<float3>`
      with trilinear clamp. CPU mirror (`VfxAdvectionWorldToField` +
      `VfxAdvectionIsInside`) is the source of truth tests exercise
      directly.
- [x] 1.3 Update-CS integration with tightness blend factor (0=pure force, 1=pure advection).
      `VfxAdvectionBlendVelocity(current, advected, insideField,
      tightness)` clamps tightness to [0, 1] and short-circuits to
      current velocity when the particle exits the painted region so
      exiting particles retain their inertia rather than snap to zero.

## 2. Documentation and tests

- [x] 2.1 Docs: advection vs vector-field comparison + authoring guide.
      `docs/engine/25-vfx/38-advection-field.md` — DI-behaviour
      comparison table, config schema, world→field formula, tightness
      cheat-sheet, HLSL snippet, four-step authoring workflow, test
      index.
- [x] 2.2 Tests: sampler at cell centres, tightness blend correctness.
      `runtime/tests/engine/vfx/test_vfx_advection_field.cpp` — 16
      cases: config default disabled / enabled with SRV + AABB /
      degenerate AABB rejected / unbound SRV disables; remap center /
      min / max / OOB / inside predicate edges; blend out-of-field
      preserves / tightness 0 ignores / tightness 1 full follow /
      tightness 0.5 midpoint / negative + over-range clamped; HLSL
      snippet declares SampleAdvection + bindless sample + OOB guard.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/38-advection-field.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_advection_field.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
