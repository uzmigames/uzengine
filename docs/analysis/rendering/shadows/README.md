<!-- doc-meta
type: index
category: rendering
theme: shadows
title: Shadow Mapping Analysis — CSM, PCF, PCSS, Cache
created: 2026-03-16
updated: 2026-03-16
status: complete
author: Claude
-->

# Shadow Mapping Analysis — CSM, PCF, PCSS, Cache

Complete analysis of UE5 shadow rendering systems including cascade shadow maps (CSM), PCF filtering, PCSS soft shadows, and static shadow caching.

## Files

- [cascade-shadow-flickering-fix.md](cascade-shadow-flickering-fix.md) — Root causes and solutions for CSM cascade transition artifacts and flickering
- [shadow-comparison-semantics.md](shadow-comparison-semantics.md) — The pattern: how UE5 solves reversed-Z vs. linear-depth in shadow comparison (three-phase conversion)
- [shadow-shader-templates.md](shadow-shader-templates.md) — Ready-to-use HLSL/C++ templates for shadow depth output and PCF filtering
- [ue5-far-shadow-cascades-analysis.md](ue5-far-shadow-cascades-analysis.md) — Detailed analysis of UE5's far cascade system (beyond 4 near cascades)
- [ue5-far-shadow-cascades-quick-ref.md](ue5-far-shadow-cascades-quick-ref.md) — Quick reference for far cascade implementation parameters and formulas
- [ue5-far-shadows-code-snippets.md](ue5-far-shadows-code-snippets.md) — Exact code from UE5 source for far shadow cascade setup
- [ue5-far-shadows-implementation-guide.md](ue5-far-shadows-implementation-guide.md) — Step-by-step guide to implementing far shadow cascades in UzEngine
- [ue5-pcss-implementation-analysis.md](ue5-pcss-implementation-analysis.md) — Percentage-Closer Soft Shadows (PCSS): filter size calculation, blocker search, algorithm
- [ue5-pcss-quick-reference.md](ue5-pcss-quick-reference.md) — Quick reference for PCSS parameters, light parameterization, filter kernel sizes
- [ue5-reversed-z-architecture.md](ue5-reversed-z-architecture.md) — How UE5 uses reversed-Z (1=near, 0=far) and why it matters for shadow depth
- [ue5-reversed-z-implementation-patterns.md](ue5-reversed-z-implementation-patterns.md) — Implementation patterns for reversed-Z: depth output, readback, comparison
- [ue5-reversed-z-quick-reference.md](ue5-reversed-z-quick-reference.md) — Cheat sheet for reversed-Z depth conversion formulas and constants
- [ue5-shadow-cache-technical-reference.md](ue5-shadow-cache-technical-reference.md) — Technical deep-dive into static shadow caching system: primitive flagging, invalidation, atlas management
- [ue5-shadow-sampling-exact-code.md](ue5-shadow-sampling-exact-code.md) — Exact code snippets from UE5 with file paths and line numbers for all shadow operations
- [ue5-static-shadow-cache-system.md](ue5-static-shadow-cache-system.md) — Overview of UE5's static shadow cache: when to use, how to configure, performance benefits

## Quick Navigation

### New to Shadow Mapping?
1. Read [shadow-comparison-semantics.md](shadow-comparison-semantics.md) for the conceptual pattern
2. Copy templates from [shadow-shader-templates.md](shadow-shader-templates.md)
3. Refer to [ue5-shadow-sampling-exact-code.md](ue5-shadow-sampling-exact-code.md) for exact implementations

### Implementing CSM?
- [ue5-far-shadow-cascades-analysis.md](ue5-far-shadow-cascades-analysis.md) — Full cascade system
- [ue5-reversed-z-architecture.md](ue5-reversed-z-architecture.md) — Depth space handling
- [cascade-shadow-flickering-fix.md](cascade-shadow-flickering-fix.md) — Fixing artifacts

### Implementing PCSS (Soft Shadows)?
- [ue5-pcss-implementation-analysis.md](ue5-pcss-implementation-analysis.md) — Algorithm details
- [ue5-pcss-quick-reference.md](ue5-pcss-quick-reference.md) — Parameters and formulas
- [ue5-shadow-sampling-exact-code.md](ue5-shadow-sampling-exact-code.md) — Code implementation

### Optimizing with Static Shadow Cache?
- [ue5-static-shadow-cache-system.md](ue5-static-shadow-cache-system.md) — Overview and configuration
- [ue5-shadow-cache-technical-reference.md](ue5-shadow-cache-technical-reference.md) — Technical internals

## Key Concepts

### Three-Phase Shadow Comparison (from UE5)
1. **Store Linear Depth** — Shadow map stores linear [0=near, 1=far]
2. **Read and Convert** — Scene depth converted to match shadow space
3. **Compare** — Simple: ShadowmapDepth > SceneDepth = unshadowed

See [shadow-comparison-semantics.md](shadow-comparison-semantics.md) for complete explanation.

### Reversed-Z Handling
- UE5 uses reversed-Z for main view: 1=near plane, 0=far plane
- Shadow maps store standard linear depth (not reversed)
- Conversion formula: `LinearDepth = 1 - ReversedZDepth`

See [ue5-reversed-z-architecture.md](ue5-reversed-z-architecture.md) for details.

### Cascade Shadow Maps (CSM)
- Multiple shadow maps at different scales for different distances
- Near cascades for high detail, far cascades for distant geometry
- Transition between cascades must be smooth to avoid visual artifacts

See [ue5-far-shadow-cascades-analysis.md](ue5-far-shadow-cascades-analysis.md) for UE5's cascade system.

## Related Documentation

- **General Rendering**: `docs/analysis/rendering/general/`
- **Post-Processing**: `docs/analysis/rendering/post-process/`
- **Atmosphere & Sky**: `docs/analysis/rendering/atmosphere/`
- **UzEngine Specs**: `docs/specs/rendering/`
