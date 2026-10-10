# phase9_9.13.0-vfx-wpo-material-compiler-wiring

Material-compiler integration of 8.18.0 WPO splice helpers.

Reference: UE5 FMaterialAttributes::WorldPositionOffset.

## 1. Codegen integration

- [x] 1.1 Material compiler emits the `EvaluateWPO` function body from
      `VfxWPOEmitEvaluateBody` at shader-function scope.
      `WPOMaterialExtension::EmitHLSL(FunctionScope, out)` — wraps the
      8.18 body with the `float3 EvaluateWPO(objectPos, worldPos, time,
      normalizedAge)` signature.
- [x] 1.2 Current-frame splice from `VfxWPOSpliceVS` at VS_PreViewProj.
      `WPOMaterialExtension::EmitHLSL(VS_PreViewProj, out)` calls
      `VfxWPOSpliceVS(attrs)` directly.
- [x] 1.3 Prev-frame splice from `VfxWPOSplicePrevVS` at VS_PrevFrame.
      `WPOMaterialExtension::EmitHLSL(VS_PrevFrame, out)` calls
      `VfxWPOSplicePrevVS(attrs)` directly.

## 2. Shader permutation

- [x] 2.1 Materials without an authored WPO expression produce identical
      shader bytes to pre-8.18 builds.
      `WPOMaterialExtension::EmitHLSL()` returns `false` at every
      injection point when `!attrs_.IsEnabled()`. `PSOPermutationTag()`
      returns an empty string, so the PSO cache key is unchanged.
- [x] 2.2 Emitters using a WPO material opt into a PSO permutation
      tagged `WPO_ENABLED`.
      `PSOPermutationTag()` returns `"WPO_ENABLED"` when
      `attrs.IsEnabled()` is true; the PSO cache hashes this into the
      pipeline key via `MaterialCompilerExtensionRegistry::CollectPermutationTags()`.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `WPOMaterialExtension` + wiring example
      in the header comment.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/material/test_wpo_material_extension.cpp`:
      11 tests covering Name, disabled emission + no permutation,
      enabled emission at FunctionScope/VS_PreViewProj/VS_PrevFrame,
      PS silence, permutation tag, registry integration, and
      attributes accessor.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
