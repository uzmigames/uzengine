# Proposal: Stochastic Screen-Space Reflections Refactor (Phase 2)

## Why

UzEngine's SSR pipeline produces visible noise and flickering artifacts that place it at
D-tier quality (comparable to Three.js, below every AAA engine). A 10-engine analysis
(docs/analysis/ssr/ssr-master-report.md) identified 11 issues — 1 CRITICAL, 4 HIGH, 4 MEDIUM,
2 LOW. The root causes are: (1) Hi-Z pyramid is built but never bound to SSR (trace reads
raw depth mip 0), (2) IGN jitter has no temporal frame offset so the temporal accumulation
converges to the same noisy pattern every frame, (3) no Karis inverse tonemap causes HDR
fireflies to dominate the spatial filter, (4) temporal clipping uses min/max AABB in RGB
instead of variance-based clipping in YCoCg. Every production engine (UE5, Frostbite, AMD
FidelityFX SSSR) solves these with well-documented techniques from published papers
(Stachowiak 2015, Heitz 2018, Karis 2014, Schied 2017, Wolfe 2022).

The refactor progresses through 4 phases: surgical bug fixes (Phase 1), denoise quality
(Phase 2), stochastic GGX sampling (Phase 3), and cascade architecture for future RT
integration (Phase 4). Phase 1 alone should eliminate the majority of visible noise.

## What Changes

### New Systems
1. **STBN Blue Noise Texture** — 128x128x64 pre-baked spatiotemporal blue noise volume
   (Wolfe 2022, NVIDIA open-source) for stochastic ray seed generation.
2. **GGX VNDF Importance Sampler** — Heitz 2018 visible normal distribution function sampling
   in the trace pass, replacing mirror-only reflection direction.
3. **Virtual Hit Reprojection** — store hit distance in SSR output, reconstruct world-space
   hit point, reproject through previous view-projection for correct temporal history.
4. **Tile Classification Pass** — compute pass that classifies 8x8 tiles by max roughness,
   skipping tiles above MaxRoughness threshold via indirect dispatch.
5. **Reflection Cascade Compositor** — confidence-weighted blending of SSR (Layer 1) with
   reflection captures (Layer 2, future) and IBL (Layer 3), designed for future DXR Layer 4.

### Refactoring
1. **ssr_trace_cs.hlsl** — bind Hi-Z mip chain, add temporal IGN, add VNDF sampling path,
   output hit distance, add bounds checks in binary refinement.
2. **ssr_resolve_cs.hlsl** — add Karis inverse tonemap, lower roughness passthrough to 0.04,
   replace Poisson disk with roughness-adaptive ellipse kernel (Phase 3).
3. **ssr_temporal_cs.hlsl** — replace min/max AABB with variance clipping in YCoCg space,
   adaptive blend weight based on accumulated sample count, increase velocity threshold.
4. **frame_gi_passes.cpp** — bind actual Hi-Z SRV instead of raw depth, fix hardcoded
   intensity, pass frame index to trace shader, wire STBN texture.
5. **deferred_lighting_ps.hlsl** — fix triple-weighting of SSR confidence (remove
   pre-multiply from trace, keep squared alpha in lighting).

## Impact
- Affected specs: docs/specs/screen-space-effects.md
- Affected code: runtime/shaders/runtime/postprocess/ssr_trace_cs.hlsl, ssr_resolve_cs.hlsl, ssr_temporal_cs.hlsl, runtime/engine/renderer/frame/frame_gi_passes.cpp, runtime/engine/renderer/scene/scene_renderer_impl.h, runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl
- Breaking change: NO — same 3-pass pipeline, same CBV layout, same render graph structure. CVars unchanged. Visual output improves.
- User benefit: Noise-free reflections on metallic/smooth surfaces, correct glossy reflections on rough surfaces, stable temporal accumulation under camera motion.
- Enables: Future DXR hybrid reflections (Phase 4), reflection capture probes, world-space SDF probe cache (AMD Brixelizer GI pattern).
