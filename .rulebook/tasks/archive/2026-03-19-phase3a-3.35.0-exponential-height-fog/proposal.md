# Exponential Height Fog

## Why

Scenes without atmospheric fog look flat — distant objects are fully visible with no depth cue. Every UE5 default scene includes ExponentialHeightFog providing distance-based density and altitude falloff, creating a sense of scale. P1 gap from the 2026-03-14 gap analysis. Without it, outdoor scenes cannot match UE5 Basic Scene quality. No dependencies — deferred lighting pass exists.

## What Changes

### New Systems
1. **FogParameters struct** — density, falloff, height, start distance, inscattering color, directional inscattering exponent, max opacity
2. **Fog computation in deferred lighting** — per-pixel exponential height fog blended at end of lighting composition
3. **Fog CB** — constant buffer with fog parameters uploaded per-frame

### Refactoring
- Deferred lighting shader extended with fog integration after light accumulation
- PerFrameCB extended with fog parameters or separate fog CB slot

## Impact
- Affected specs: `docs/engine/12-rendering/06-lighting.md`
- Affected code: `runtime/engine/renderer/passes/lighting_pass.*`, `runtime/shaders/deferred_lighting_ps.hlsl`
- Breaking change: NO
- User benefit: Outdoor scenes gain atmospheric depth matching UE5 default quality
- Enables: Volumetric clouds (3.40.0), sky atmosphere (3.8.0) can feed fog color
