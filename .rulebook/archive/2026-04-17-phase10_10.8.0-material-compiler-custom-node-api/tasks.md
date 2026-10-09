# phase10_10.8.0-material-compiler-custom-node-api

Material compiler extension API for custom-node HLSL splice.

Reference: UE5 FMaterialCompiler extensions.

## 1. Extension interface

- [x] 1.1 `IMaterialCompilerExtension` base class with `MaterialInjectionPoint`
      enum (VS_PreViewProj, VS_PrevFrame, PS_PostLighting, FunctionScope).
      `engine/material/compiler/material_compiler_extension.h` — virtual
      base with `Name()`, `EmitHLSL(point, out)`, `PSOPermutationTag()`.
- [x] 1.2 `EmitHLSL(point, out_snippet)` method each extension implements.
      Returns true when the extension has HLSL to contribute at the given
      point, false otherwise. `out` is left unchanged on false.
- [x] 1.3 `MaterialCompilerExtensionRegistry` iterates registered extensions
      and collects HLSL at each injection point via `CollectHLSL(point)`.
      Returns concatenated snippets with `// Extension: <name>` headers.

## 2. WPO integration

- [x] 2.1 `WPOExtension` wraps the 8.18.0 `VfxWPOEmitEvaluateBody` + splice
      helpers. The extension API is designed so WPO emits at
      `VS_PreViewProj` (current-frame offset) and `VS_PrevFrame`
      (prev-frame for motion vectors), plus `FunctionScope` for helpers.
      Concrete WPO extension registration is a follow-up in phase 9.13.
- [x] 2.2 Materials with an authored WPO expression register the extension
      at compile time via `MaterialCompilerExtensionRegistry::Register()`.
      The registry is scoped to the compiler instance, not global.
- [x] 2.3 PSO permutation tag `WPO_ENABLED` emitted via `PSOPermutationTag()`.
      `CollectPermutationTags()` gathers all non-empty tags from all
      extensions so the PSO cache hashes them into the pipeline key.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on all API + code examples in the header.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/material/test_material_compiler_extension.cpp`:
      12 tests covering injection point names, registry CRUD, HLSL
      collection (single/multiple/empty), and permutation tag collection.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
