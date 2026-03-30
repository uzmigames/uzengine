# Proposal: Anti-Tiling Material System (Phase 2)

## Why

Large surfaces (terrain, floors, walls) using standard UV tiling show obvious repetition
patterns visible from medium/far distances. This breaks immersion and is one of the most
common visual artifacts in real-time rendering. The industry-standard solution is a
multi-octave blending approach: sample the base texture at two different scales/orientations,
blend via a procedural world-space noise mask, and overlay macro-frequency color/roughness
variation to break up uniformity.

This is a prerequisite for terrain rendering and any scene with large planar surfaces.
Without it, even high-quality PBR textures look tiled and artificial.

## What Changes

### New Systems

1. **`anti_tiling.hlsli`** — Reusable HLSL include with `SampleAntiTiled()` function that
   performs dual-sample blending, macro noise masking, dark spot breakup, and microdetail
   normal overlay. World-space UVs for macro variation to avoid UV seam artifacts.

2. **`AntiTilingParams` cbuffer/struct** — GPU-side parameter block with 9+ exposed
   parameters (baseTiling, variationTiling, variationStrength, macroNoiseScale,
   macroNoiseContrast, darkSpotsScale, darkSpotsStrength, bumpStrength, rotationAngle).

3. **`anti_tiling_math.h`** — C++ CPU-side mirror of the noise/blending math for parity
   tests (Tier 1 shader testing pipeline compliance).

4. **Material system integration** — Extend `PBRMaterial` or add a material feature flag
   so anti-tiling can be enabled per-material via the material system.

### Refactoring

- `gbuffer_ps.hlsl` / `mesh_ps.hlsl` will gain an `#ifdef ANTI_TILING` path that calls
  `SampleAntiTiled()` instead of the single-sample texture fetch.
- Shader variant system must support the `ANTI_TILING` permutation flag.

## Impact
- Affected specs: `docs/specs/rendering/shader-library.md`, `docs/specs/rendering/shader-variants.md`
- Affected code: `runtime/shaders/runtime/include/anti_tiling.hlsli` (NEW), `runtime/engine/renderer/material/material.h`, `runtime/shaders/runtime/geometry/gbuffer_ps.hlsl`, `runtime/shaders/runtime/geometry/mesh_ps.hlsl`
- Breaking change: NO — additive feature behind shader variant flag
- User benefit: Visually convincing large surfaces without manual texture atlasing or unique UVs
- Enables: Terrain system, large-scale environment rendering, architectural visualization
