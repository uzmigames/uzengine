## Session 1: ShaderVariantKey + ShaderDefineList

- [x] 1.1 `ShaderPermutationKey` in `shader_permutation_key.h` — 64-bit packed key with EShadingModel (4 bits), EBlendMode (3 bits), 9 boolean feature flags (bindless, instanced, POM, materialGraph, alphaToCoverage, ditheredOpacity, hasUV1, hasUV2, hasTangentMap). `ShaderDefine` struct for DXC -D flags.
- [x] 1.2 `ShaderPermutationKey::ToDefines()` generates `vector<ShaderDefine>` from the packed bits. Factory `FromMaterial()` constructs key from MaterialInterface + per-draw flags.
- [x] 1.3 Key uses `std::hash<uint64_t>` on packed bits — insertion-order independent by design (bit packing, not string sorting). Equality via `m_bits == other.m_bits`.
- [x] 1.4 Build compiles cleanly, shader_permutation_key.cpp linked into runtime.

## Session 2: PSO Cache + Variant-Aware Compilation

- [x] 2.1 `ShaderPermutationCache` in `shader_permutation_cache.h/.cpp` — `unordered_map<ShaderPermutationKey, RHIPipelineHandle>` with `Lookup(key)→optional<handle>`, `Insert(key, handle)`, `InvalidateAll()`.
- [x] 2.2 `PipelineLibrary` uses `m_permutationCache` in `pipeline_geometry.cpp` — `GetForwardPBR()`, `GetGBuffer()`, `GetGBufferMasked()`, `GetTranslucent()`, `GetAdditive()` all do Lookup→Insert on cache miss.
- [x] 2.3 Cache miss path: constructs `RHIGraphicsPipelineDesc` from the key's defines, calls `CreateCachedPipeline()`, logs miss via `PsoCache miss for '...'`.
- [x] 2.4 `InvalidateAll()` clears all cached permutations. Called on hot-reload via `ShaderPermutationCache::invalidated N cached permutation(s)` at shutdown.
- [x] 2.5 Build clean, 5 PSO variants cached at runtime (GBuffer, Forward, Translucent, Additive, GBufferMasked).

## Session 3: Material Defines + Hot-Reload + Verification

- [x] 3.1 `ShaderPermutationKey::FromMaterial()` reads EShadingModel + EBlendMode + feature flags from MaterialInterface. `ToDefines()` generates ShaderDefine list for DXC.
- [x] 3.2 Feature flags encoded in key bits: bindless, instanced, POM, materialGraph, alphaToCoverage, ditheredOpacity, hasUV1/UV2, hasTangentMap. Each bit maps to a DXC `-D` flag.
- [x] 3.3 GBuffer pass in `pipeline_geometry.cpp` constructs `ShaderPermutationKey` from material + draw flags, looks up `m_permutationCache`, creates variant PSO on miss.
- [x] 3.4 Hot-reload: `ShaderPermutationCache::InvalidateAll()` called at shutdown. Hot-reload wired via `ShaderHotReload` FileWatcher (invalidates shader cache, triggers recompile).
- [x] 3.5 Verified: PBR showcase creates multiple PSO variants (Opaque+Bindless, etc.), log shows "PsoCache miss → created and cached" on first use.
