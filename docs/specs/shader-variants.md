<!-- doc-meta
title: Shader Variants
category: rendering
status: current
updated: 2026-04-01
-->

# Shader Variants

## SM5 / Bindless Strategy

UzEngine shaders are compiled for SM6.6 (bindless) by default. SM5 fallback is provided via `#ifdef UZ_BINDLESS` guards in every shader.

### Bindless Path (SM6.6+, default)

- `UZ_BINDLESS=1` defined automatically by the shader compiler for SM6.0+
- All textures accessed via `ResourceDescriptorHeap[srvIndex]`
- SRV indices passed via root constants (b0) or PerFrameCB (b1)
- `kSentinelSrvIndex` (0xFFFFFFFF) used as "not bound" sentinel

### SM5 Fallback Path

- `UZ_BINDLESS` undefined
- Textures bound at explicit `register(tN)` slots via `SetFragmentTexture()`
- Root signature uses SRV descriptor tables instead of root constants for indices

### Permutation Defines

Shader permutations are compiled via `LoadBytecodeFromSource(name, source, defines)`:

```cpp
// Example: compile luminance pyramid with different pass defines
auto mip0Bytecode = pipeline.LoadBytecodeFromSource(
    "envmap_luminance_pyramid_mip0_cs", source, "PYRAMID_MIP0=1");
auto reduceBytecode = pipeline.LoadBytecodeFromSource(
    "envmap_luminance_pyramid_reduce_cs", source, "PYRAMID_REDUCE=1");
```

**Naming convention**: The shader name MUST end with `_cs`, `_vs`, or `_ps` suffix for the compiler to infer the correct shader stage. Custom permutation names must preserve this suffix.

### Cache

Compiled bytecode is cached on disk at `build/bin/cache/shaders/dx12_sm68/`. Cache key is a content-hash of source + includes + defines + target, so any change invalidates automatically.
