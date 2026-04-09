## GI + Shadows + Atmosphere Improvements (from Falcor analysis)
> Source: `docs/analysis/falcor/shader-improvement-table-shadows-gi-atmo.md`
> Estimate: 4 sessions

## Session 1: SSGI quality upgrade

- [x] 1.1 `ssgi/ssgi_trace_cs.hlsl` — implemented screen-space ray march with cosine-weighted hemisphere sampling, Hammersley sequence + IGN temporal jitter, OctDecode normals, ReconstructWorldPos, prev color lookup. Hi-Z slot reserved in root constants (g_hizSrvIndex, 0xFFFF=disabled). Linear march with depth thickness test. @ref McGuire & Mara 2014. Also implemented `prev_color_reduce_cs.hlsl` — Karis-weighted 2x downsample from full-res to half-res.
- [x] 1.2 `ssgi/ssgi_temporal_accum_cs.hlsl` — variance-guided clipping (μ ± γ·σ) replacing AABB min/max clamp. 3x3 neighborhood statistics, adaptive alpha based on clip distance, bilinear history sampling via SampleLevel, disocclusion detection via velocity reprojection bounds check. @ref Schied 2017, @ref Salvi 2016.
- [x] 1.3 `ssgi/ssgi_spatial_denoise_cs.hlsl` — 3x3 cross-bilateral filter with depth (exp falloff), normal (cosine^128 power), AND luminance variance edge-stopping (σ_L = k * local_std_dev). Pre-pass computes luminance mean/variance for adaptive edge stopping. @ref Schied 2017.
- [x] 1.4 Build passes (zig build clean). All 5 SSGI shaders (prev_color_reduce, trace, spatial_denoise, temporal_accum, upsample) compile. C++ SSGIRenderer root constants updated to match new packed layout. Visual verification pending runtime test with r.SSGI=true.

## Session 2: PCF + PCSS quality + shadow improvements

- [x] 2.1 `shadow.hlsli` — added per-pixel Poisson disk rotation via IGN (`RotatePoissonSample` + `InterleavedGradientNoise`). Each pixel rotates the 16-tap Poisson disk by a unique angle, eliminating visible banding on shadow edges. @ref Jimenez et al. 2016.
- [x] 2.2 `shadow.hlsli` — added `BlockerSearchPCSS()` with 16-tap rotated Poisson search + `EstimatePenumbraPCSS()` for physically-based variable penumbra from area light size. Activated when `filterMethod == 1`. Uses `tanLightSourceAngle` + `maxSoftKernelSize` from ShadowDataCB. @ref Fernando 2005.
- [x] 2.3 `shadow.hlsli` — added `SampleShadowESM()` exponential shadow map mode. `exp(-c * (receiver - occluder))` with c=80. Activated when `filterMethod == 2`. Single texture lookup + exponential falloff for soft edges without PCF. @ref Annen et al. 2008.
- [x] 2.4 Created `lighting/contact_shadows.hlsli` — screen-space ray march toward light direction for fine shadow detail. 16-step linear march with depth thickness test, UV bounds clipping, smooth fade-in. Hi-Z acceleration slot reserved (takes depthTex as bindless parameter). @ref Wihlidal 2017, @ref Uludag 2014.
- [x] 2.5 Build passes (zig build clean). Shadow system now supports 3 filter modes (PCF=0, PCSS=1, ESM=2) via `filterMethod` in ShadowDataCB. All existing shadow rendering unaffected (filterMethod defaults to 0 = standard PCF).

## Session 3: Atmosphere + clouds quality

- [x] 3.1 `cloud_raymarch_cs.hlsl` — added detail noise erosion in `SampleDensity()`. Samples detail noise 3D texture at 4x frequency, subtracts from shape density at edges (erosion strength modulated by density — preserves thick cores, erodes thin edges). Added `g_detailNoiseSrv` to CloudCB (offset 252, was padding). Added `m_detailNoiseSrvIndex` to CloudPass. @ref Schneider & Vos 2015.
- [x] 3.2 `cloud_temporal_cs.hlsl` — ALREADY IMPLEMENTED (178 lines). Full variance-clipped temporal reprojection with YCoCg color space, 3x3 neighborhood statistics (γ=1.25), prevViewProj reprojection, AABB clipping. @ref Karis 2014, @ref Salvi 2016.
- [x] 3.3 `god_rays_cs.hlsl` — implemented 16-sample radial light shaft blur from pixel toward projected sun position. Sky-masked sampling (only sky pixels contribute), exponential illumination decay (0.975/step), density/weight/decay/exposure parameters packed as float16 in root constants. @ref Mitchell 2007 GPU Gems 3 Ch. 13.
- [x] 3.4 `cloud_composite_ps.hlsl` — added simple aerial perspective tint: distance-based Rayleigh-like blue sky tint on distant cloud pixels (15% max blend at screen edges). @ref Hillaire 2020.
- [x] 3.5 Build passes (zig build clean). All atmosphere/cloud shaders compile.

## Session 4: SVGF denoiser (standalone pass — no SDK)

- [x] 4.1 Created `postprocess/svgf_reproject_cs.hlsl` — temporal reprojection with velocity-based reprojection, exponential moving average (α = max(1/N, 0.01)), moment accumulation (μ, σ²), disocclusion detection via UV bounds check. @ref Schied 2017.
- [x] 4.2 Created `postprocess/svgf_filter_moments_cs.hlsl` — 3x3 cross-bilateral spatial filter on moments with depth (exponential falloff) + normal (cosine^32 power) edge-stopping weights. Stabilizes variance estimate before à-trous. @ref Schied 2017.
- [x] 4.3 Created `postprocess/svgf_atrous_cs.hlsl` — 5x5 à-trous wavelet filter with configurable step size (1/2/4/8/16). Edge-stopping on depth + normal + luminance variance (σ_L = k·√σ²). B3 spline kernel weights. @ref Schied 2017, @ref Dammertz 2010.
- [x] 4.4 Created `SVGFPass` C++ class (`svgf_pass.h/.cpp`) — owns 6 GPU textures (color A/B ping-pong, moments A/B, history color, history moments), CreateResources/DestroyResources/OnResize lifecycle. Denoise() dispatch stub with documented pass sequence (reproject→filter_moments→5×à-trous). Pipeline registration pending (PipelineLibrary::GetSvgf*() methods).
- [x] 4.5 SVGF integration point documented in SVGFPass — designed as drop-in replacement for SSGIRenderer's spatial+temporal passes. SSGIRenderer can call SVGFPass::Denoise() instead of DispatchSpatialDenoise + DispatchTemporalAccum. Wiring requires PipelineLibrary registration of 3 new compute pipelines (deferred to pipeline registration task).
- [x] 4.6 Build passes (zig build clean). All 3 SVGF shaders compile. SVGFPass C++ compiles and links.
