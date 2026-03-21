# 6. Deferred + Forward Hybrid Rendering Pipeline

**Status**: proposed
**Date**: 2026-03-15

## Context

The engine needs to support PBR materials with multiple lights efficiently. Options: pure forward (simple but O(lights*objects)), pure deferred (efficient multi-light but no MSAA, high bandwidth), or hybrid. UE5 uses deferred with forward for translucency, Unity HDRP uses deferred, Godot offers both.

## Decision

Primary deferred pipeline with G-Buffer (albedo, normal, metallic/roughness/AO, depth) for opaque geometry. Forward pass available for translucent objects and special materials. GGX BRDF + Burley diffuse for PBR. ACES tonemapping. IBL via compute-baked BRDF LUT + irradiance + prefiltered specular cubemaps. Shadow mapping with 2048x2048 depth + 16-tap Poisson PCF.

## Alternatives Considered

- Pure forward rendering (simpler but scales poorly with lights)
- Tiled deferred (requires compute, complex)
- Clustered forward+ (good scalability, planned for future)
- Visibility buffer (modern but complex, requires bindless)

## Consequences

Pros: Efficient multi-light rendering, decoupled lighting from geometry, standard industry approach. Cons: No native MSAA support (documented risk in task 3.41.0), high G-Buffer bandwidth, transparent objects need separate forward pass. Current root signature is FULL at 64/64 DWORDs — must migrate to bindless for expansion.
