## 1. ShaderVariantKey

- [ ] 1.1 Create `src/renderer/shader_compiler/shader_variant.h` -- `ShaderVariantKey` struct (64-bit FNV-1a hash)
- [ ] 1.2 Implement `compute_variant_key(Span<const char*> defines)` -- sort, concatenate, hash
- [ ] 1.3 Unit test: same defines in different order produce same key
- [ ] 1.4 Unit test: different defines produce different keys (no collisions in common cases)

## 2. Define Auto-Detection

- [ ] 2.1 Create `src/renderer/shader_compiler/variant_builder.h/cpp` -- `VariantBuilder` class
- [ ] 2.2 Texture presence detection: check each material texture slot, add `HAS_<SLOT>_TEX`
- [ ] 2.3 Alpha mode detection: read `alpha_mode` from material, add `ALPHA_MODE_<mode>`
- [ ] 2.4 Skinning detection: if draw source is `CSkinnedMesh`, add `SKINNED`
- [ ] 2.5 Instancing detection: if draw call is instanced batch, add `INSTANCED`
- [ ] 2.6 Shadow quality: read `renderer.shadow_quality` from project settings, add `SHADOW_QUALITY=N`
- [ ] 2.7 Cascade count: read `renderer.shadow_cascades` from project settings, add `NUM_CASCADES=N`
- [ ] 2.8 Custom defines: parse `[variants]` section from `.uzmat` file
- [ ] 2.9 `variant_build_defines(material, mesh_component, project_settings) -> Array<const char*>`

## 3. PSO Cache Extension

- [ ] 3.1 Extend `PsoCacheKey` to include `ShaderVariantKey variant_key` field
- [ ] 3.2 Update hash function to incorporate variant_key
- [ ] 3.3 Cache lookup: build key from material + mesh + pass state, check HashMap
- [ ] 3.4 Cache miss: queue background compilation, return fallback PSO (magenta error shader)
- [ ] 3.5 Cache insertion: after background compile succeeds, insert PSO into cache (thread-safe)

## 4. DXC Variant Compilation

- [ ] 4.1 Update `ShaderCompiler::compile_variant()` to accept `Span<const char*> defines`
- [ ] 4.2 Build `-D NAME=VALUE` argument list from defines
- [ ] 4.3 Pass defines to `IDxcCompiler3::Compile()` via `DxcDefine` array
- [ ] 4.4 Return `CompileResult` with bytecode, diagnostics, and include list

## 5. Hot-Reload for Variants

- [ ] 5.1 On `.hlsl` file change: find all `PsoCacheKey` entries matching `shader_path`
- [ ] 5.2 Recompile each cached variant with its original defines on background thread
- [ ] 5.3 On success: swap `ID3D12PipelineState*` atomically after `WaitForGpu()`
- [ ] 5.4 On failure: keep old PSO, report errors via IPC
- [ ] 5.5 Log variant recompilation count and total time

## 6. Fallback Error Shader

- [ ] 6.1 Create `shaders/builtin/error.hlsl` -- solid magenta output
- [ ] 6.2 Pre-compile error shader PSO at engine startup (always available)
- [ ] 6.3 Return error PSO when variant compilation fails or is pending

## 7. Offline Compilation (UzBuild)

- [ ] 7.1 Scan all `.uzmat` files in project during cook stage
- [ ] 7.2 Compute define list for each material (same auto-detection logic)
- [ ] 7.3 Compile all required variants with release flags (`-O3`)
- [ ] 7.4 Store compiled DXIL blobs in `.uzpak` keyed by `{shader_path, variant_key}`
- [ ] 7.5 Runtime: load pre-compiled blobs from `.uzpak` in shipping builds

## 8. IPC Handlers

- [ ] 8.1 `shader.listVariants` -- return all cached variants for a shader path
- [ ] 8.2 `shader.compileVariant` -- force-compile a specific variant with given defines

## 9. Testing

- [ ] 9.1 Unit test: variant key uniqueness for common define combinations
- [ ] 9.2 Integration test: load material with albedo texture, verify `HAS_ALBEDO_TEX` define generated
- [ ] 9.3 Integration test: change material alpha_mode, verify correct variant compiled
- [ ] 9.4 Integration test: hot-reload shader, verify all cached variants recompiled
- [ ] 9.5 Integration test: invalid shader returns fallback magenta PSO
- [ ] 9.6 Performance test: compile 50 variants < 2 seconds on background thread

## 10. Documentation

- [ ] 10.1 Update `docs/specs/shader-variants.md` with any implementation changes
- [ ] 10.2 Update `docs/specs/shader-system.md` to reference variant system
- [ ] 10.3 Update `docs/modules/renderer/README.md` shader_compiler subsystem table
