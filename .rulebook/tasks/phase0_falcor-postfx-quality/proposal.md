# Proposal: phase0_falcor-postfx-quality

## Why
Falcor comparison revealed significant post-processing gaps:
1. **TAA uses AABB clamp** instead of variance clipping — causes visible ghosting on motion (Salvi 2016 is standard)
2. **SMAA is incomplete** — only edge detection pass exists, passes 2-3 (blend weight + neighborhood blend) missing. SMAA produces zero anti-aliasing effect.
3. **Bloom threshold is hard** — causes flicker on pixels near threshold. Falcor uses smooth quadratic knee.
4. **Bloom applied post-tonemap** — physically incorrect. Should be pre-tonemap in HDR.
5. **Only ACES tone mapping** — Falcor has selectable operators (Reinhard, Hable UC2, etc.)
6. **No white balance** — standard camera pipeline feature (Kelvin-based)
7. **No physical camera exposure** — ISO/f-number/shutter speed → EV100
8. **No vignette, chromatic aberration, film grain** — standard cinematic effects
9. **SSR is single-ray mirror only** — Falcor uses stochastic multi-ray for rough reflections
10. **Motion blur has no tile classification** — every pixel evaluates gather even in static regions
11. **GTAO missing multi-bounce** and bent normal output

## What Changes
- TAA: variance clipping (μ ± γ·σ) replacing AABB min/max
- SMAA: implement passes 2-3 (blend weight + neighborhood blend) — complete the pipeline
- Bloom: smooth quadratic knee threshold + move to pre-tonemap HDR
- Tone mapping: add Reinhard, Modified Reinhard, Hable UC2 operators with runtime selection
- White balance: Kelvin → RGB via D-illuminant chromaticity + Bradford adapt
- Physical camera: EV100 from ISO/aperture/shutter speed
- Create `simple_postfx_cs.hlsl`: vignette + chromatic aberration + film grain in one pass
- Wire `color_grading_lut_cs.hlsl` C++ pass (shader exists, needs `ColorGradingLutPass`)
- SSR: add stochastic GGX multi-ray mode for rough reflections
- Motion blur: add tile classification pre-pass (skip static tiles)
- DoF: add near/far/sharp layer recombine pass
- GTAO: add multi-bounce approximation + bent normal output
- Velocity: add per-vertex motion vectors for skinned meshes

## Impact
- Affected specs: `docs/specs/rendering/screen-space-effects.md`, `docs/specs/rendering/renderer.md`
- Affected code: 20+ shader files in `postprocess/`, new `SimplePostFxPass` C++ class, `ColorGradingLutPass` C++ class
- Breaking change: NO (all additive or quality improvements; existing defaults preserved)
- User benefit: Less ghosting (TAA), working SMAA, cinematic color grading pipeline, rough surface reflections, faster motion blur on static scenes, artist-selectable tone mapping
