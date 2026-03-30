## Post-Processing Quality + New Effects (from Falcor analysis)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` + `postprocess-and-effects.md`
> Estimate: 5 sessions

## Session 1: TAA variance clip + bloom fixes

- [ ] 1.1 `postprocess/taa_resolve_cs.hlsl` — replace AABB min/max neighborhood clamp with variance clipping: `μ ± γ·σ` (γ=1.25). @ref Salvi 2016 "An Excursion in Temporal Supersampling".
- [ ] 1.2 `postprocess/bloom_threshold_cs.hlsl` — replace hard threshold with smooth quadratic knee: `soft = max(0, brightness - threshold + knee); result = soft*soft / (4*knee + 1e-5)`. @ref Karis 2013.
- [ ] 1.3 `postprocess/bloom_composite_ps.hlsl` — move bloom application before tone mapping (HDR bloom). Restructure pass order if needed.
- [ ] 1.4 Build + capture screenshots. TAA should show less ghosting on motion. Bloom should have no flicker near threshold.

## Session 2: Complete SMAA + FXAA license

- [ ] 2.1 `postprocess/smaa_blend_ps.hlsl` — implement SMAA pass 2: blend weight calculation. Sample edge texture + search for patterns + compute blend weights. @ref Jimenez 2012 "SMAA".
- [ ] 2.2 `postprocess/smaa_resolve_ps.hlsl` — implement SMAA pass 3: neighborhood blending. Sample color + blend weights → anti-aliased output. @ref Jimenez 2012.
- [ ] 2.3 C++ `SmaaPass` — wire passes 2+3 with correct render targets (edge RT → blend weight RT → final output).
- [ ] 2.4 Add AMD FSR CAS copyright notice to `cas_sharpen_ps.hlsl` (MIT license attribution).
- [ ] 2.5 Build + verify: SMAA should produce visible edge smoothing. Compare SMAA vs FXAA quality via `--screenshot-stdout`.

## Session 3: Tone mapping operators + white balance + physical camera

- [ ] 3.1 `include/film_tonemap.hlsli` — add `ReinhardTonemap(float3 x)`: `x / (1 + x)`. @ref Reinhard et al. 2002.
- [ ] 3.2 `include/film_tonemap.hlsli` — add `ReinhardModifiedTonemap(float3 x, float whitePoint)`: `x * (1 + x/whitePoint²) / (1 + x)`.
- [ ] 3.3 `include/film_tonemap.hlsli` — add `HableUC2Tonemap(float3 x)`: Uncharted 2 filmic S-curve (A=0.15, B=0.50, C=0.10, D=0.20, E=0.02, F=0.30). @ref Hable 2010.
- [ ] 3.4 Add `tonemapOperator` uint to `PerFrameCB` (0=ACES, 1=Reinhard, 2=ReinhardMod, 3=HableUC2, 4=None). Wire from C++ component.
- [ ] 3.5 `postprocess/tonemap_ps.hlsl` — switch on `tonemapOperator` to select curve. Default ACES.
- [ ] 3.6 `include/film_tonemap.hlsli` — add `WhiteBalance(float3 color, float kelvin)` → `float3`. D-illuminant chromaticity + Bradford adapt. @ref Krystek 1985.
- [ ] 3.7 Add `whiteBalanceKelvin` float to `PerFrameCB` (default 6500). Wire from component.
- [ ] 3.8 Implement physical camera exposure: `EV100(float aperture, float shutterSpeed, float ISO)` → `float exposure`. Add `cameraISO`, `cameraAperture`, `cameraShutter` to exposure CB. @ref standard photography.
- [ ] 3.9 Build + verify: switch between tone mapping operators via CLI cvar.

## Session 4: Vignette + chromatic aberration + saturation + color grading LUT

- [ ] 4.1 Create `postprocess/simple_postfx_cs.hlsl` — single compute pass for: vignette, chromatic aberration, film grain. @ref Falcor `SimplePostFX`.
- [ ] 4.2 Vignette: `vignetteAmount * smoothstep(vignetteRadius, vignetteRadius - vignetteSoftness, dist_from_center)`.
- [ ] 4.3 Chromatic aberration: sample R/G/B at slightly offset UVs (radial distortion from center). `float2 offset = (uv - 0.5) * caIntensity`.
- [ ] 4.4 Film grain: `color += grainIntensity * (pcgHash(pixel + frame) / 4294967295.0 - 0.5)`.
- [ ] 4.5 C++ `SimplePostFxPass` — compute pass, bind parameters, register in render graph after tone mapping.
- [ ] 4.6 `postprocess/color_grading_lut_cs.hlsl` — wire C++ pass (shader exists, needs `ColorGradingLutPass` class). Support external artist LUT texture blending.
- [ ] 4.7 Per-tonal-range saturation controls: shadows/midtones/highlights saturation multipliers in `color_grading_lut_cs.hlsl`.
- [ ] 4.8 Build + verify all new effects via screenshots.

## Session 5: SSR multi-ray + motion blur tile + DoF recombine + velocity skinned

- [ ] 5.1 `postprocess/ssr_trace_cs.hlsl` — add stochastic multi-ray mode: GGX importance sample N rays per pixel (N=1 for smooth, N=4 for rough). @ref Stachowiak 2015.
- [ ] 5.2 `postprocess/motion_blur_cs.hlsl` — add tile classification pre-pass: skip gather for static tiles (max velocity < threshold). @ref McGuire et al. 2012.
- [ ] 5.3 `postprocess/dof_blur_cs.hlsl` — implement near/far/sharp layer separation recombine pass. @ref Lee & Eisemann 2013.
- [ ] 5.4 `postprocess/velocity_ps.hlsl` — add per-vertex motion vectors for skinned meshes (requires `prevPosition` vertex attribute).
- [ ] 5.5 `postprocess/gtao_cs.hlsl` — add multi-bounce approximation: `ao = pow(ao, 1.0 + 2.5 * ao)`. @ref Jimenez 2016.
- [ ] 5.6 `postprocess/gtao_cs.hlsl` — add bent normal output (float3) for directional GI occlusion. @ref Jimenez 2016.
- [ ] 5.7 Build + verify: rough reflections should show blurred SSR. Motion blur should be cheaper on static scenes.
