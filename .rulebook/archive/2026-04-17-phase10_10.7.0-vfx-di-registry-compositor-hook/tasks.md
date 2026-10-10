# phase10_10.7.0-vfx-di-registry-compositor-hook

DI registry + shader-composition hook in the module compositor.

Reference: UE5 UNiagaraDataInterface.

## 1. Registry

- [x] 1.1 `VfxDIRegistry` scoped to the compositor/subsystem instance (not a global singleton).
      `engine/vfx/modules/vfx_di_registry.h/.cpp` — owns a map of
      `string → unique_ptr<IVfxDataInterface>`. Not a singleton — the
      VFX subsystem owns the instance.
- [x] 1.2 `Register(unique_ptr<IVfxDataInterface>)` API. Each DI implements
      `EmitCBFields()`, `EmitHelperFunctions()`, and `EmitCallSite()`.
      Null and empty-name DIs are rejected with a log warning.
- [x] 1.3 `ComposeForEmitter(diNames)` maps `data_interfaces[]` entries
      to registered DI instances. Unknown names are skipped with a warning.
      Returns `ComposedDIFragments` with three concatenated HLSL strings.

## 2. Shader composition

- [x] 2.1 Composed update CS emits per-DI CB fields at shader-header scope.
      `ComposedDIFragments::cbFields` — the compositor inserts this block
      into the CB header section of the composed shader.
- [x] 2.2 Helper functions emitted at function scope before `main`.
      `ComposedDIFragments::helperFunctions` — the compositor inserts
      this block before the dispatch function.
- [x] 2.3 Per-DI call-site snippet emitted inside the dispatch loop.
      `ComposedDIFragments::callSite` — the compositor inserts this
      after all module snippets in the per-particle loop body.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `IVfxDataInterface`, `VfxDIRegistry`,
      `ComposedDIFragments`, with code examples.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_di_registry.cpp`: 12 tests
      covering register/find/clear/overwrite/null/names + composition
      for single/multiple/unknown/empty DI lists.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
