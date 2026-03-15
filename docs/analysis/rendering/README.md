<!-- doc-meta
title: Rendering Analysis Documentation Index
description: Reference engine analysis documents organized by subsystem (sky atmosphere, RHI, render graph, shaders, etc.).
date: 2026-03-15
status: active
-->

# Rendering Analysis — UE5 Architecture Research

This directory contains deep-dive analysis of Unreal Engine 5.6 rendering systems, with concrete adaptation patterns for UzEngine.

## Documents

### Sky Atmosphere System (Complete)

1. **[Executive Summary](ue5-sky-atmosphere-executive-summary.md)** — Quick reference
   - Core architecture overview
   - Key numbers and LUT dimensions
   - Render graph integration points
   - Performance metrics
   - **Start here** for high-level understanding

2. **[Full System Analysis](ue5-sky-atmosphere-system.md)** — Detailed breakdown
   - Render pass architecture (4-tier LUT hierarchy)
   - Shader structure & Bruneton model implementation
   - Component data structures (CPU & GPU)
   - Height fog integration
   - Sky-to-IBL capture mechanism
   - Performance optimizations & cvars
   - **Read this** for complete technical understanding

3. **[UzEngine Adaptation Patterns](ue5-sky-atmosphere-uzengine-adaptation.md)** — Implementation guide
   - Scene proxy pattern (component → renderer decoupling)
   - LUT architecture (compute-based generation)
   - Aerial perspective volume (3D froxel fog)
   - Rendering integration points
   - Height fog integration (code examples)
   - Real-time IBL capture (helper patterns)
   - CPU-side parameter hashing (change detection)
   - Implementation checklist
   - **Read this** to implement in UzEngine

## Key Insights

### Why UE5's Approach Works

- **Precomputation**: 4 lookup tables computed once per atmosphere change, not per-frame
- **Multi-scale**: LUTs at different resolutions for different purposes (transmittance precise, multi-scatter approximate)
- **Per-view**: Aerial perspective volume regenerated per-view per-frame (captures view-relative fog)
- **Scalability**: 5+ quality tiers via cvars (FastSky, sample counts, resolution)
- **Integration**: Unified fog system (sky atmosphere as light source)

### Applicable to UzEngine

✅ **Copy the pattern, not the code**:
- Component-proxy decoupling (UzEngine already has SceneEnvironment)
- 4-tier LUT structure (compute shaders instead of UE5's mix of compute/pixel)
- Aerial perspective volume (3D froxel grid, non-linear depth distribution)
- Height fog modulation (blend sky atmosphere into fog parameters)
- Real-time IBL capture (for reflection probes)

❌ **Don't copy**:
- UE5's C++ reflection system (too heavy for UzEngine)
- FRHICommandList (UzEngine uses render graph only)
- Complex material system integration

### Volumetric Rendering System (Frostbite)

1. **[Frostbite Volumetric Rendering Analysis](frostbite-volumetric-rendering.md)** — Complete technical breakdown
   - Single-scattering rendering equation with all terms
   - Froxel grid architecture (8×8×64 frustum-aligned)
   - 3-step pipeline: material voxelization → light scattering → final accumulation
   - Participating media material model (σ_a, σ_s, σ_t, ρ, g, σ_e)
   - Critical: Energy-conservative integration formula with HLSL code
   - Temporal integration with Halton jitter (5% blend)
   - Volumetric shadow maps (32³ clip-map per light)
   - Particle voxelization with extinction normalization
   - Performance: 2.95 ms on PS4 900p (8×8 tiles) / 0.83 ms (16×16 tiles)
   - Validation vs Mitsuba path tracer
   - UzEngine adaptation strategy (phases 3.5a-f, ~10 weeks)
   - **Read this** for production volumetric rendering implementation

## Related Analysis Documents

- [RHI Abstraction](rhi-abstraction-pattern.md) — How UE5 abstracts GPU APIs
- [Render Proxy Pattern](render-proxy-pattern.md) — CPU scene mirroring to GPU (also see FPrimitiveSceneProxy)
- [Component System](component-architecture.md) — UE5's actor-component model

## File Structure

```
docs/analysis/rendering/
├── README.md (this file)
├── ue5-sky-atmosphere-executive-summary.md
├── ue5-sky-atmosphere-system.md
├── ue5-sky-atmosphere-uzengine-adaptation.md
└── [Other subsystems as analyzed]
```

## Integration with UzEngine

**Use this analysis to**:
1. Design `SkyAtmosphere` resource class (persistent, hash-based invalidation)
2. Write compute shaders for Transmittance, MultiScatter, SkyView, AerialPerspective LUTs
3. Integrate aerial perspective volume into deferred lighting pass
4. Modulate height fog with sky atmosphere parameters
5. Implement real-time reflection capture for sky
6. Add console variables for scalability

**Estimated implementation effort**: ~2-3 weeks for a solid implementation (full Bruneton model + all optimizations).

## References

**UE5 Source**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp` (1793 lines)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.h`
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf` (main shader)
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush` (shader math)

**Papers** (referenced in UE5 code):
- Bruneton et al. "Precomputed Atmospheric Scattering" (EGSR 2008)
- Hillaire et al. "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (SIGGRAPH 2020)

