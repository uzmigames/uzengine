## Session 1: ShaderVariantKey + ShaderDefineList

- [ ] 1.1 Define `ShaderVariantKey` struct in `pipeline_library.h` — sorted vector of `(string name, string value)` pairs, stable `std::size_t hash()` computed via FNV-1a over sorted entries, equality operator for unordered_map
- [ ] 1.2 Implement `ShaderDefineList` class — `Add(name, value)`, `Remove(name)`, `Merge(other)`, `Clear()`, `ToDefineString()` (returns `/D name=value` flags for DXC), `ToVariantKey()` → `ShaderVariantKey`
- [ ] 1.3 Add unit tests for `ShaderVariantKey` — same defines in different insertion order produce identical hash, different defines produce different hash, round-trip `ToDefineString()` → parse → `ToVariantKey()` equality
- [ ] 1.4 Build + verify: `zig build` compiles cleanly, unit tests pass

## Session 2: PSO Cache + Variant-Aware Compilation

- [ ] 2.1 Add `ShaderPermutationCache` class to `shader_manager.cpp` — `std::unordered_map<ShaderVariantKey, RhiPipelineHandle>` with thread-safe lookup, `GetOrCompile(ShaderDesc, ShaderDefineList)` → returns cached handle or compiles via DXC with defines appended to compile flags
- [ ] 2.2 Modify `ShaderManager::CreateGraphicsPipeline` and `CreateComputePipeline` to accept an optional `ShaderDefineList` parameter; forward defines to `ShaderPermutationCache`
- [ ] 2.3 Implement cache miss path — call DXC with defines from `DefineList.ToDefineString()`, store compiled PSO under the variant key, log cache miss at `UZ_LOG(Renderer, Debug, ...)`
- [ ] 2.4 Add `ShaderPermutationCache::InvalidateShader(shaderPath)` — removes all cache entries for a given shader source file (called on hot-reload file change event)
- [ ] 2.5 Build + verify: `zig build` compiles cleanly, no regressions in existing rendering

## Session 3: Material Defines + Hot-Reload + Verification

- [ ] 3.1 Add `MaterialType → ShaderDefineList` mapping in `material.h` — `GetMaterialDefines(MaterialType)` returns the define set for each type (e.g., `Standard → {}`, `Hair → {HAIR_SHADING=1}`, `ClearCoat → {CLEARCOAT=1}`)
- [ ] 3.2 Emit feature-flag defines from material bind: `HAS_NORMAL_MAP=1` when normal texture is bound, `HAS_DISPLACEMENT=1` when displacement is bound, `HAS_EMISSIVE=1` when emissive is bound — merge into material's `ShaderDefineList` at draw time
- [ ] 3.3 Wire material defines into GBuffer pass PSO selection: each draw call looks up `ShaderPermutationCache` with the material's current `ShaderDefineList`, uses the variant-specific PSO
- [ ] 3.4 Hot-reload integration: on shader source file change event, call `InvalidateShader(path)` so next draw recompiles the affected variants
- [ ] 3.5 Build + verify: run demo with two materials (one with normal map, one without) — confirm two distinct PSOs exist in cache via `--stats` output; change shader source → confirm recompile
