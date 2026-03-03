# Phase 2 -- Shader Variant / Permutation System

## Why

The PBR uber-shader (`pbr.hlsl`) uses `#ifdef` to toggle features: texture presence, alpha modes, skinning, instancing, and shadow quality. Without a variant management system, the engine would either compile every possible combination upfront (combinatorial explosion) or recompile shaders at draw time (frame hitches). A variant key + PSO cache system ensures only the permutations actually used at runtime are compiled, cached, and hot-reloaded correctly.

## What Changes

- New `ShaderVariantKey` struct: 64-bit FNV-1a hash of sorted active defines.
- Auto-detection logic: material properties (textures, alpha_mode) and ECS components (skinned, instanced) generate the correct `#define` list automatically.
- PSO cache key extended to include `variant_key` alongside `shader_path`, `vertex_layout`, `blend_state`, `depth_state`.
- DXC compilation passes `-D` flags for active defines.
- Hot-reload recompiles ALL cached variants when a shader source file changes.
- Fallback magenta error shader when a variant fails to compile.
- Offline variant compilation in `UzBuild.exe` cook stage for shipping builds.
- New IPC handlers: `shader.listVariants`, `shader.compileVariant`.

## Dependencies

- **phase1-shader-hot-reload** (DXC integration, file watcher, PSO cache foundation)
- **phase2-pbr-renderer** (PBR shader needs variants for texture/alpha/etc.)

## Spec Reference

[Shader Variants Spec](../../../docs/specs/shader-variants.md)

## Impact
- Affected specs: `shader-system.md`, `renderer.md`, `shader-variants.md` (new)
- Affected code: `src/renderer/shader_compiler/`, `src/renderer/material/`
- Breaking change: NO (extends existing PSO cache, backward compatible)
- User benefit: Correct rendering with any material configuration; no manual shader management; instant hot-reload of all permutations
