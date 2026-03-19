# Proposal: Advanced Post-Processing — SSAO, SSR, TAA, DoF, Motion Blur & Screen Effects (Phase 3A)

## Why
The 0.1 POC had a complete post-processing pipeline with 11 effects: GTAO, SSR, TAA, DoF, motion blur, auto-exposure, color grading, film grain, vignette, chromatic aberration, and CAS sharpening. Task 3.5.0 covers only basic post-processing (ACES tonemap, bloom, FXAA). This task fills the gap with all the advanced effects needed for 0.1 visual parity. These effects transform a technically correct render into a cinematic, polished image.

## What Changes

### New Systems
1. **GTAO (Ground Truth AO)** — XeGTAO-style horizon-angle AO with temporal filtering and bilateral blur, replaces flat ambient with screen-space occlusion
2. **SSR (Screen-Space Reflections)** — Hi-Z accelerated ray marching with roughness-based cone angle, Fresnel weighting, screen-edge fade
3. **TAA (Temporal Anti-Aliasing)** — Halton jitter + velocity buffer + YCoCg neighborhood clamping, replaces FXAA as primary AA
4. **Depth of Field** — Thin-lens CoC computation + separable bokeh blur, focus distance/aperture/focal length
5. **Motion Blur** — Per-pixel velocity-weighted directional blur from motion vectors
6. **Auto-Exposure** — Histogram-based eye adaptation with exponential convergence
7. **Color Grading** — Temperature, tint, saturation, contrast adjustments
8. **Screen Effects** — Film grain (blue-noise temporal), vignette (corner darkening), chromatic aberration (RGB edge shift), CAS sharpening
9. **Contact Shadows** — Short-range ray-march from surface along light direction for fine shadow detail
10. **PostProcessVolume** — Invisible AABB volumes that blend post-process settings by camera proximity

### Refactoring
- Post-process stack (3.5.0) expanded with ordered chain: SSAO → SSR → Lighting → Bloom → DoF → Motion Blur → TAA → Tonemap → Color Grading → Film Grain → Vignette
- FXAA (3.5.0) becomes fallback when TAA is disabled

## Impact
- Affected specs: `docs/engine/12-rendering/07-post-processing.md`
- Affected code: `runtime/shaders/post/`, `runtime/engine/renderer/passes/`
- Breaking change: NO — all effects are optional with per-effect toggles
- User benefit: Cinematic visual quality matching AAA games, configurable per-scene via PostProcessVolume
- Depends on: 3.5.0 (basic post-processing stack), 3.3.0 (deferred — for G-Buffer depth/normals)
