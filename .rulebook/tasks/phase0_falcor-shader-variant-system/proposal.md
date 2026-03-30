# Proposal: phase0_falcor-shader-variant-system

## Why
UzEngine has zero shader variant/permutation infrastructure — every material variation requires a manually written `#ifdef` block with no caching or lifecycle management. Falcor's `DefineList` system compiles separate PSOs per material/feature combination, enabling material types like cloth, hair, and glass that each require distinct shader permutations. Without a variant system, adding a new material type means forking the entire shader manually. This is the #1 architectural prerequisite for all advanced material work.

## What Changes
- New `ShaderVariantKey` struct: stable hash over a sorted set of `(name, value)` define pairs
- New `ShaderDefineList` class: add/remove/merge defines, generate a `ShaderVariantKey` from the current set
- New `ShaderPermutationCache` in `shader_manager.cpp`: maps `ShaderVariantKey → compiled PSO`; on cache miss, compiles with the associated defines via DXC
- Automatic PSO creation on cache miss: `ShaderManager::GetOrCompilePSO(desc, defines)` returns cached or newly compiled PSO
- Material-driven define injection: each `MaterialType` maps to a set of mandatory defines (e.g., `materialType=Hair → {HAIR_SHADING=1}`)
- Feature-flag defines: texture slots emit `HAS_NORMAL_MAP`, `HAS_DISPLACEMENT`, etc. based on whether textures are bound
- Hot-reload support: on shader source change, all cached PSO variants for that shader are invalidated and recompiled
- Affected files: `runtime/engine/renderer/shader_manager.cpp`, new `runtime/engine/renderer/shader_permutation_cache.cpp`, `runtime/engine/renderer/pipeline_library.h`

## Impact
- Affected specs: `docs/specs/shader-system.md`, `docs/specs/shader-variants.md`
- Affected code: `shader_manager.cpp`, `pipeline_library.h`, `material.h`, all pass PSO creation sites
- Breaking change: NO — existing single-variant paths continue to work; variant cache is additive
- User benefit: Multiple material types (cloth, hair, glass, clear-coat) become possible without manual shader forks; compilation only happens once per unique define set; hot-reload invalidates only affected variants
