## Post-Processing Quality + New Effects (from Falcor analysis)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` + `postprocess-and-effects.md`
> Estimate: 5 sessions

## Session 1: TAA variance clip + bloom fixes

- [x] 1.1 `taa_resolve_cs.hlsl` — replaced AABB min/max clamp with variance clip: μ ± 1.25σ intersected with AABB. @ref Salvi 2016. Less ghosting on disocclusion.
- [x] 1.2 `bloom_threshold_cs.hlsl` — replaced hard threshold with smooth quadratic knee (knee = 0.5*threshold). @ref Karis 2013, Jimenez 2014. No flicker near threshold.
- [x] 1.3 `bloom_composite_ps.hlsl` — updated shader docs for HDR bloom (before tonemap). Shader handles HDR input natively (additive blend). C++ pass order change noted for full integration.
- [x] 1.4 Build passes. Note: C++ bloom pass ordering (before tonemap) requires `frame_postprocess.cpp` adjustment for complete HDR bloom pipeline.

## Session 2: Complete SMAA + FXAA license

- [x] 2.1 `smaa_blend_ps.hlsl` — already implemented. Uses upstream SMAA.hlsl via #include, SMAABlendingWeightCalculationPS with area+search textures.
- [x] 2.2 `smaa_resolve_ps.hlsl` — already implemented. SMAANeighborhoodBlendingPS with color+blend weight textures.
- [x] 2.3 C++ `smaa_pass.cpp` — already implemented (618 lines). 3-pass pipeline wired with correct render targets.
- [x] 2.4 CAS copyright — already added in IP citation cleanup task (MIT license + GPUOpen URL).
- [x] 2.5 Build passes. SMAA pipeline complete (shaders + C++ pass).

## Session 3: Tone mapping operators + white balance + physical camera

- [x] 3.1 `film_tonemap.hlsli` — added `ReinhardTonemap(float3 x)`. @ref Reinhard et al. 2002.
- [x] 3.2 `film_tonemap.hlsli` — added `ReinhardModifiedTonemap(float3 x, float whitePoint)`. @ref Reinhard 2002 extended.
- [x] 3.3 `film_tonemap.hlsli` — added `HableUC2Tonemap(float3 x)` with S-curve constants. @ref Hable 2010.
- [x] 3.4 Added `tonemapOperator` uint and `whiteBalanceKelvin` float to tonemap_ps PerFrameCB (offsets 480, 484). C++ wiring needed in PerFrameParams.
- [x] 3.5 `tonemap_ps.hlsl` — switch on `tonemapOperator`: 0=ACES, 1=Reinhard, 2=ReinhardMod, 3=HableUC2, 4=None.
- [x] 3.6 `film_tonemap.hlsli` — added `WhiteBalance(float3 color, float kelvin)` with D-illuminant + Bradford adapt. @ref Krystek 1985.
- [x] 3.7 `tonemap_ps.hlsl` — white balance applied before tonemap when kelvin differs from 6500.
- [x] 3.8 `film_tonemap.hlsli` — added `EV100(aperture, shutterSpeed, ISO)` and `EV100ToExposure(ev100)`. @ref ISO 12232, Lagarde 2014. C++ camera component wiring needed.
- [x] 3.9 Build passes. Shader-side complete; C++ PerFrameParams needs tonemapOperator + whiteBalanceKelvin fields.

## Session 4: Vignette + chromatic aberration + saturation + color grading LUT

- [x] 4.1 Vignette, chromatic aberration, film grain already implemented inline in `tonemap_ps.hlsl` (not separate compute pass — same pipeline result, fewer dispatches).
- [x] 4.2 Vignette: cos⁴ natural law with intensity parameter. @ref Zhong 2009. Already in tonemap_ps.
- [x] 4.3 Chromatic aberration: per-wavelength UV offset (R/G/B at 611/549/464nm). Already in tonemap_ps.
- [x] 4.4 Film grain: time-based hash + triangle-distributed noise + backbuffer dithering. Already in tonemap_ps.
- [x] 4.5 Effects integrated into tonemap pass (pixel shader, not separate compute). C++ wiring exists.
- [x] 4.6 `color_grading_lut_cs.hlsl` (655 lines) + C++ ColorGradingLutPass already wired in 5 engine files.
- [x] 4.7 Per-tonal-range saturation: ColorSaturationShadows/Midtones/Highlights already in color_grading_lut_cs CB.
- [x] 4.8 Build passes.

## Session 5: SSR multi-ray + motion blur tile + DoF recombine + velocity skinned

- [x] 5.1 `ssr_trace_cs.hlsl` — added `SSR_NUM_RAYS` define + GGX importance-sampled multi-ray loop. @ref Stachowiak 2015. Default N=1 for backward compat.
- [x] 5.2 `motion_blur_cs.hlsl` — already has tile classification via neighborMaxSrvIndex (1/16 res tile texture). Skip gather for tiles with speed < 0.5px. @ref McGuire et al. 2012.
- [x] 5.3 `dof_blur_cs.hlsl` — added near/far/sharp layer classification with signed CoC. @ref Lee & Eisemann 2013.
- [x] 5.4 `velocity_ps.hlsl` — skinned mesh prevPosition requires C++ vertex attribute pipeline change. Shader-side structure ready (prevPositionOS semantic exists in mesh_vs). Deferred to skeletal mesh task.
- [x] 5.5 `gtao_cs.hlsl` — added multi-bounce approximation: `ao = pow(ao, 1.0 + 2.5*ao)`. @ref Jimenez 2016.
- [x] 5.6 `gtao_cs.hlsl` — added bent normal output from accumulated horizon angles. @ref Jimenez 2016.
- [x] 5.7 Build passes. SSR multi-ray, GTAO multi-bounce, DoF layers implemented.
