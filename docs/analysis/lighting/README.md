<!-- doc-meta
type: index
category: analysis
theme: lighting
title: Lighting & Shadows Analysis — CSM, PCSS, DFS
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Lighting & Shadows Analysis

Complete analysis of UE5 shadow mapping systems, cascaded shadows, soft shadows (PCSS), distance field shadows, point/spot lights, reversed-Z depth, and SkyLight ambient lighting.

## Files

- [local-light-shadows-ue5-architecture.md](local-light-shadows-ue5-architecture.md) — UE5 architecture for point and spot light shadows, dual-paraboloid and perspective projections
- [skylight-fog-parity.md](skylight-fog-parity.md) — Parity analysis between SkyLight ambient and height fog lighting contributions
- [ue5-environment-component-defaults.md](ue5-environment-component-defaults.md) — Default values for all environment components (lights, fog, sky, atmosphere)
- [ue5-reversed-z-shadow-maps.md](ue5-reversed-z-shadow-maps.md) — Reversed-Z depth testing for shadow maps, comparison with traditional Z
- [ue5-shadows/README.md](ue5-shadows/README.md) — Shadow systems detailed analysis entry point
- [ue5-shadows/cascade-shadow-flickering-fix.md](ue5-shadows/cascade-shadow-flickering-fix.md) — Techniques to reduce temporal artifacts in cascade transitions
- [ue5-shadows/shadow-comparison-semantics.md](ue5-shadows/shadow-comparison-semantics.md) — Depth comparison operators (less-than, greater-equal) and bias application
- [ue5-shadows/shadow-shader-templates.md](ue5-shadows/shadow-shader-templates.md) — HLSL templates for shadow sampling, PCF patterns, filtering
- [ue5-shadows/ue5-far-shadow-cascades-analysis.md](ue5-shadows/ue5-far-shadow-cascades-analysis.md) — Analysis of far-distance cascades for distant geometry
- [ue5-shadows/ue5-far-shadow-cascades-quick-ref.md](ue5-shadows/ue5-far-shadow-cascades-quick-ref.md) — Quick reference for far cascade parameters and bounds
- [ue5-shadows/ue5-far-shadows-code-snippets.md](ue5-shadows/ue5-far-shadows-code-snippets.md) — C++ and HLSL code snippets from UE5 far shadow implementation
- [ue5-shadows/ue5-far-shadows-implementation-guide.md](ue5-shadows/ue5-far-shadows-implementation-guide.md) — Step-by-step guide to implement far shadow cascades
- [ue5-shadows/ue5-pcss-implementation-analysis.md](ue5-shadows/ue5-pcss-implementation-analysis.md) — Percentage-closer soft shadows (PCSS): blocker search, penumbra estimation
- [ue5-shadows/ue5-pcss-quick-reference.md](ue5-shadows/ue5-pcss-quick-reference.md) — Quick reference for PCSS parameters and filter sizes
- [ue5-shadows/ue5-reversed-z-architecture.md](ue5-shadows/ue5-reversed-z-architecture.md) — How reversed-Z affects shadow depth testing and bias
- [ue5-shadows/ue5-reversed-z-implementation-patterns.md](ue5-shadows/ue5-reversed-z-implementation-patterns.md) — Implementation patterns for reversed-Z systems
- [ue5-shadows/ue5-reversed-z-quick-reference.md](ue5-shadows/ue5-reversed-z-quick-reference.md) — Quick reference for reversed-Z depth comparisons and bias
- [ue5-shadows/ue5-shadow-cache-technical-reference.md](ue5-shadows/ue5-shadow-cache-technical-reference.md) — Static shadow cache system: baking, runtime lookup, atlas management
- [ue5-shadows/ue5-shadow-sampling-exact-code.md](ue5-shadows/ue5-shadow-sampling-exact-code.md) — Exact shader code for shadow sampling from UE5
- [ue5-shadows/ue5-static-shadow-cache-system.md](ue5-shadows/ue5-static-shadow-cache-system.md) — Complete static shadow cache architecture and baking pipeline

## Quick Navigation

### Understanding Shadow Systems?
1. [ue5-shadows/ue5-shadow-cache-technical-reference.md](ue5-shadows/ue5-shadow-cache-technical-reference.md) — Overview of shadow caching
2. [ue5-shadows/shadow-comparison-semantics.md](ue5-shadows/shadow-comparison-semantics.md) — Depth testing basics
3. [ue5-shadows/ue5-reversed-z-architecture.md](ue5-shadows/ue5-reversed-z-architecture.md) — Reversed-Z specifics

### Implementing Cascaded Shadows?
- [ue5-shadows/ue5-far-shadow-cascades-analysis.md](ue5-shadows/ue5-far-shadow-cascades-analysis.md) — Cascade bounds and selection
- [ue5-shadows/ue5-far-shadows-implementation-guide.md](ue5-shadows/ue5-far-shadows-implementation-guide.md) — Implementation steps
- [ue5-shadows/ue5-far-shadow-cascades-quick-ref.md](ue5-shadows/ue5-far-shadow-cascades-quick-ref.md) — Parameter reference

### Implementing Soft Shadows (PCSS)?
- [ue5-shadows/ue5-pcss-implementation-analysis.md](ue5-shadows/ue5-pcss-implementation-analysis.md) — Algorithm details
- [ue5-shadows/ue5-pcss-quick-reference.md](ue5-shadows/ue5-pcss-quick-reference.md) — Parameters and constants
- [ue5-shadows/shadow-shader-templates.md](ue5-shadows/shadow-shader-templates.md) — HLSL templates

### Point/Spot Light Shadows?
- [local-light-shadows-ue5-architecture.md](local-light-shadows-ue5-architecture.md) — Architecture and projections

### Fixing Cascade Artifacts?
- [ue5-shadows/cascade-shadow-flickering-fix.md](ue5-shadows/cascade-shadow-flickering-fix.md) — Temporal stability improvements

## Key Concepts

### Shadow Caching
- **Static shadows**: Baked once, looked up in atlas at runtime O(1)
- **Atlas management**: Tile allocation, virtual pages, overflow handling
- **Receiver bias**: Per-light, per-cascade bias adjustment for artifact removal

### Cascaded Shadow Maps (CSM)
- **Multiple cascades**: Typical 4 cascades covering near to far distance
- **Cascade selection**: Based on receiver depth, smooth transitions
- **Per-cascade PCF**: Filter radius scales with cascade distance

### Soft Shadows (PCSS)
- **Blocker search**: Find average distance of occluders
- **Penumbra estimation**: Compute soft shadow filter size based on light/blocker/receiver geometry
- **Adaptive filtering**: Filter kernel size adjusts based on penumbra

### Reversed-Z Depth
- **Depth range**: [1, 0] instead of [0, 1] (far=1, near=0)
- **Comparison**: Greater-equal test instead of less-than
- **Bias**: Applies in opposite direction (subtract instead of add)

### Point/Spot Light Shadows
- **Dual-paraboloid**: Point lights use two hemispheric projections (front+back)
- **Perspective projection**: Spot lights use standard frustum projection
- **Atlas packing**: Multiple lights' shadows share atlas texture
