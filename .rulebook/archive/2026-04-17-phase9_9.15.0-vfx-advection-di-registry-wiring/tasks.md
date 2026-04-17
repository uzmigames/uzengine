# phase9_9.15.0-vfx-advection-di-registry-wiring

Module-compositor + asset-loader integration of the 8.21.0 advection DI.

Reference: UE5 UNiagaraDataInterface registry.

## 1. Registry entry

- [x] 1.1 Register the advection DI type via
      `VfxAdvectionFieldDataInterface` (implements `IVfxDataInterface`,
      phase 10.7). Caller registers at subsystem init:
      `registry.Register(std::make_unique<VfxAdvectionFieldDataInterface>())`.
      Emitter assets reference it by name via `data_interfaces:
      ["Advection"]` — `TypeName()` returns `"Advection"`.
- [x] 1.2 Asset loader parses `advection_field: { texture, aabbMin,
      aabbMax, tightness }` into `VfxAdvectionFieldConfig`.
      The existing 8.21.0 `VfxAdvectionFieldConfig` struct is
      populated by the emitter asset loader; the DI implementation
      emits HLSL that references the CB fields the loader fills.
- [x] 1.3 Bindless SRV index allocated when the texture is loaded.
      The asset loader allocates a bindless SRV for the field texture
      and writes the index into `VfxAdvectionFieldConfig::fieldSrvIndex`;
      the CB field `advectionFieldSrvIdx` is populated from it.

## 2. Shader composition

- [x] 2.1 CB fields emitted into the composed update CS via
      `EmitCBFields()`: `advectionFieldSrvIdx`, `advectionWorldMin`,
      `advectionWorldMax`, `advectionTightness`.
- [x] 2.2 `VfxAdvectionEmitHLSLSnippet()` appended at function scope
      via `EmitHelperFunctions()`.
- [x] 2.3 Velocity lerp inserted at the update dispatch site via
      `EmitCallSite()`: `velocity = lerp(velocity,
      SampleAdvection(position), advectionTightness)` guarded by
      the unbound-SRV short-circuit.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `VfxAdvectionFieldDataInterface`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_advection_field_di_impl.cpp`:
      6 tests covering TypeName, CB fields, helper functions, call
      site blend + guard, registry compose, unknown-DI fallthrough.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
