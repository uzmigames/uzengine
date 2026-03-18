<!-- doc-meta
title: Deep Audit — Post-Process Shaders vs UE5 5.6
author: shader-engineer
date: 2026-03-18
status: complete
-->

# Deep Audit: Post-Process Shaders vs UE5 5.6 Source

Line-by-line comparison of all UzEngine post-process shaders against UE5 5.6 source at `W:\UE_5.6_Source`.

---

## Summary

| Shader | Issues Found | Critical | Medium | Low |
|--------|-------------|----------|--------|-----|
| Bloom Threshold | 1 | 0 | 1 | 0 |
| Bloom Downsample | 1 | 0 | 0 | 1 |
| Bloom Upsample | 1 | 0 | 1 | 0 |
| Bloom Composite | 1 | 0 | 0 | 1 |
| Tonemap | 3 | 1 | 1 | 1 |
| Exposure Histogram | 0 | 0 | 0 | 0 |
| Exposure Average | 2 | 1 | 1 | 0 |
| Color Grading LUT | 1 | 0 | 0 | 1 |
| GTAO | 2 | 0 | 1 | 1 |
| GTAO Denoise | 1 | 0 | 0 | 1 |
| Motion Blur | 0 | 0 | 0 | 0 |
| Motion Blur TileMax | 0 | 0 | 0 | 0 |
| Motion Blur NeighborMax | 1 | 0 | 0 | 1 |
| DOF Setup | 0 | 0 | 0 | 0 |
| DOF Blur | 0 | 0 | 0 | 0 |
| **TOTAL** | **14** | **2** | **5** | **7** |

---

## Bloom

### bloom_threshold_cs.hlsl

```
ISSUE: Karis average applied BEFORE threshold — UE5 does NOT use Karis average at threshold stage
UE5: PostProcessBloom.usf:37-67 — BloomSetupCommon does NOT apply Karis weighting.
  It applies threshold then multiplies by BloomAmount. No 2x2 block averaging with Karis weights.
  Karis average is only used during the FIRST downsample step (not threshold).
UzEngine: bloom_threshold_cs.hlsl:88-101 — Samples 4 texels with Karis weights,
  averages them, THEN thresholds the averaged result.
FIX: The threshold pass should simply downsample 2x with a standard box filter (or single
  bilinear sample at the center of the 2x2 block), apply exposure, threshold, and output.
  Karis weighting should be applied in the first downsample pass only (bloom_downsample_cs.hlsl
  mip 0 → mip 1). Alternatively, keep the Karis weight here since it is technically correct
  as a firefly prevention strategy at the threshold stage — this is the approach used by
  Unity HDRP and Call of Duty AW. The UE5 approach does NOT prevent fireflies at threshold;
  it relies on the downsample filter quality instead.
IMPACT: MEDIUM — Karis at threshold is not UE5-parity but is a valid engineering choice.
  Document as intentional deviation.
```

### bloom_downsample_cs.hlsl

```
ISSUE: UE5 uses a simpler 4-tap bilinear downsample, not the 13-tap Jimenez filter
UE5: PostProcessDownsample.usf:34-68 — DownsampleCommon uses 4 bilinear taps
  (offset by 0.5 texel) for a weighted average of a 4x4 region. This is simpler
  and cheaper than the 13-tap.
UzEngine: bloom_downsample_cs.hlsl:100-141 — Uses 13-tap Jimenez 2014 pattern.
FIX: No code change needed. The 13-tap is documented as an intentional deviation
  (line 37-39). The filter quality is better than UE5's default. Keep as-is.
IMPACT: LOW — Intentional quality improvement over UE5, properly documented.
```

### bloom_upsample_cs.hlsl

```
ISSUE: g_StageTint is a scalar float — UE5 uses per-stage float3 RGB tint colors
UE5: Scene.cpp:426-437 — Each bloom stage has a separate Tint color (float3):
  Bloom1Tint=(0.3465,0.3465,0.3465), Bloom2Tint=(0.138,0.138,0.138), etc.
  PostProcessBloomSetup.cpp:268 — PassInputs.TintColor = BloomStage.Tint * TintScale
  The tint is a float3 RGB value multiplied per-stage, allowing colored bloom.
UzEngine: bloom_upsample_cs.hlsl:43 — g_StageTint is a single float scalar.
  Line 105: upsampled *= g_StageTint; // Scalar multiply, no color control.
FIX: Change g_StageTint from float to float3 (or float4 for alignment).
  Apply as: upsampled *= g_StageTint.rgb;
  Update CPU-side to pass float3 tint per upsample stage.
IMPACT: MEDIUM — Prevents artists from using colored bloom stages (warm/cool bloom layers).
```

### bloom_composite_ps.hlsl

```
ISSUE: Bloom composite is a separate pass — UE5 composites bloom inside the tonemap shader
UE5: PostProcessTonemap.usf:517-518 — FinalLinearColor += Bloom * (OneOverPreExposure * GlobalExposure * VignetteMask)
  Bloom is added BEFORE tonemapping and BEFORE the LUT lookup. This is critical because
  bloom needs to go through the same color grading as the scene.
UzEngine: bloom_composite_ps.hlsl — Separate pass that adds bloom AFTER tonemapping (line 88: "LDR after tone mapping").
  BUT: tonemap_ps.hlsl lines 254-262 ALSO composites bloom before exposure/tonemap.
  So bloom_composite_ps.hlsl appears to be a LEGACY pass, potentially double-adding bloom.
FIX: Verify that bloom_composite_ps.hlsl is not used when the tonemap pass handles bloom
  compositing. If both are active, bloom is double-applied. Remove or gate the legacy pass.
IMPACT: LOW — Likely unused in the current pipeline (tonemap_ps.hlsl handles it). Document.
```

---

## Tonemap

### tonemap_ps.hlsl

```
ISSUE: Vignette applied AFTER tonemapping — UE5 applies vignette BEFORE tonemapping
UE5: PostProcessTonemap.usf:515 — FinalLinearColor = SceneColor * SceneColorTint *
  (OneOverPreExposure * GlobalExposure * VignetteMask * LocalExposure)
  Vignette is a multiplicative mask applied to HDR linear color BEFORE bloom add and
  BEFORE the LUT/tonemap lookup. This means vignette darkening goes through the
  tone curve, creating a more natural-looking darkening at the edges.
UzEngine: tonemap_ps.hlsl:292-295 — Vignette is applied AFTER tonemapping (step 7),
  in LDR space. The file header (line 39-41) acknowledges this deviation:
  "We apply it AFTER tonemapping for simplicity — visually near-identical at
  typical intensity (0.4)"
FIX: Move vignette application to BEFORE exposure multiplication (between steps 1 and 4).
  Apply as: color *= ComputeVignetteMask(uv, vignetteIntensity); before color *= exposure;
  This changes vignette from LDR to HDR multiplication, matching UE5 exactly.
IMPACT: HIGH — At vignetteIntensity > 0.5, the difference becomes noticeable. The toe region
  of the tonemap curve handles dark values differently than linear multiplication in LDR.
  Dark edges should be tonemapped after darkening, not darkened after tonemapping.
```

```
ISSUE: Vignette does not use VignetteSpace aspect ratio correction
UE5: PostProcessCommon.ush:155-159 — VignetteSpace scales the position by:
  Scale = sqrt(2.0) / sqrt(1.0 + AspectRatio * AspectRatio)
  Pos *= float2(1.0, AspectRatio) * Scale
  This ensures the vignette is circular and corner intensity is consistent regardless
  of viewport aspect ratio.
UE5: PostProcessTonemap.usf:246 — OutVignette = VignetteSpace(LensViewportPos, AspectRatio)
  The VignetteSpace transform is computed in the VERTEX shader (not pixel shader).
UzEngine: tonemap_ps.hlsl:213-232 — ComputeVignetteMask uses raw screenPos without
  VignetteSpace correction. Lines 220-223 acknowledge this: "We assume 16:9 (most common)."
  The formula vignettePos = screenPos * intensity skips the aspect ratio scaling entirely.
FIX: Add aspect ratio to the cbuffer. Compute VignetteSpace in the pixel shader:
  float aspectRatio = screenWidth / screenHeight;  // or pass from CPU
  float scale = sqrt(2.0) / sqrt(1.0 + aspectRatio * aspectRatio);
  float2 vignettePos = screenPos * float2(1.0, aspectRatio) * scale * intensity;
IMPACT: MEDIUM — On non-16:9 displays (ultrawide 21:9, 4:3), the vignette is elliptical
  instead of circular, and corner brightness varies by aspect ratio.
```

```
ISSUE: Film grain uses 1e10 clamp instead of POSITIVE_INFINITY for sign extraction
UE5: PostProcessTonemap.usf:567 — clamp(UniformGrainNP * POSITIVE_INFINITY, half(-1.0), half(1.0))
  POSITIVE_INFINITY is a built-in HLSL constant (0x7F800000 as float).
UzEngine: tonemap_ps.hlsl:192 — clamp(uniformGrainNP * 1e10f, -1.0f, 1.0f)
  Using 1e10 instead of POSITIVE_INFINITY.
FIX: Replace 1e10f with asfloat(0x7F800000) or use: sign(uniformGrainNP)
  which is mathematically equivalent and clearer.
  Alternatively, keep 1e10 — it produces identical results for all practical inputs
  since any value * 1e10 exceeds the [-1, 1] clamp range unless it's exactly 0.
IMPACT: LOW — Functionally identical. The only edge case is uniformGrainNP in the range
  (-1e-10, 1e-10) which maps to 0 with 1e10 but maps to ±1 with POSITIVE_INFINITY.
  This sub-pixel difference is invisible.
```

---

## Auto Exposure

### exposure_histogram_cs.hlsl

**NO ISSUES FOUND.** The histogram implementation correctly:
- Uses 64 bins (UE5: HISTOGRAM_SIZE = 64, PostProcessHistogramCommon.ush:87)
- Uses log2 luminance mapping with linear remap (matches ComputeHistogramPositionFromLogLuminance)
- Uses 16x16 thread groups with LDS accumulation
- Excludes near-black pixels (UE5 uses EyeAdaptation_BlackHistogramBucketInfluence for this)
- Uses Rec.709 luminance (UE5 uses EyeAdaptation_LuminanceWeights which defaults to Rec.709)

### exposure_average_cs.hlsl

```
ISSUE: Temporal adaptation formula differs significantly from UE5
UE5: PostProcessHistogramCommon.ush:192-219 — ComputeEyeAdaptation uses a DUAL adaptation model:
  1. ExponentialAdaption: Factor = 1.0 - exp2(-FrameTime * AdaptionSpeed); Value = Current + (Target - Current) * Factor * M
  2. LinearAdaption: Offset = FrameTime * AdaptionSpeed; Value = clamp toward target
  3. Selects between them: AbsLogDiff > StartDistance → Linear, else → Exponential
  4. All adaptation is done in LOG2 SPACE (log2 of exposure), not linear space
  5. Uses EyeAdaptation_ExponentialUpM / ExponentialDownM multipliers on the exponential factor
  6. Uses EyeAdaptation_ForceTarget for lerp to target (camera cuts)
UzEngine: exposure_average_cs.hlsl:144-147 — Uses a single exponential formula in LINEAR space:
  alpha = 1.0 - exp(-deltaTime / max(adaptSpeed, 0.001))
  currentExposure = lerp(prevExposure, targetExposure, alpha)
  This is a simpler model that:
  - Does NOT adapt in log2 space (UE5 does)
  - Does NOT have the linear/exponential dual mode
  - Does NOT have the M multiplier
  - Uses 1/speed as the time constant instead of speed directly
FIX: Rewrite temporal adaptation to match UE5's ComputeEyeAdaptation:
  1. Work in log2 space: float logTarget = log2(targetExposure); float logPrev = log2(prevExposure);
  2. Compute logDiff and select speed/M based on direction
  3. Apply ExponentialAdaption formula with M factor
  4. Convert back: currentExposure = exp2(logAdapted)
  This requires passing ExponentialUpM, ExponentialDownM, StartDistance, ForceTarget
  as additional parameters.
IMPACT: HIGH — The adaptation curve shape differs visibly. UE5's log-space adaptation
  produces perceptually uniform speed across all brightness levels. Linear-space adaptation
  adapts faster for small changes and slower for large changes, which feels less natural.
  The M multiplier controls overshoot behavior that UE5 artists rely on.
```

```
ISSUE: Adaptation speed semantics are inverted vs UE5
UE5: PostProcessHistogramCommon.ush:199-200 —
  AdaptionSpeed = (LogDiff > 0) ? EyeAdaptation_ExposureSpeedUp : EyeAdaptation_ExposureSpeedDown
  SpeedUp is used when LogDiff > 0, meaning target is BRIGHTER than current.
  "SpeedUp" = speed of brightening = going from dark to bright.
UzEngine: exposure_average_cs.hlsl:144-145 —
  adaptSpeed = (targetExposure < prevExposure) ? g_SpeedUp : g_SpeedDown
  SpeedUp is used when target < previous, meaning exposure is DECREASING (scene getting brighter,
  exposure must decrease). This is the OPPOSITE semantic.
  UzEngine says "Asymmetric adaptation: slower to brighten, faster to darken" but with
  SpeedUp=3.0 and SpeedDown=1.0, and the inverted condition, the actual behavior is:
  - Scene gets brighter → exposure decreases → uses SpeedUp=3.0 → adapts FASTER (correct for UE5's intent)
  - Scene gets darker → exposure increases → uses SpeedDown=1.0 → adapts SLOWER
  Despite the inverted naming, the actual values (3.0 for bright→dark, 1.0 for dark→bright)
  match UE5's behavior coincidentally, because the speed values are correct.
FIX: No code change needed — the behavior matches despite confusing naming. Add a comment
  clarifying the semantic inversion vs UE5.
IMPACT: MEDIUM — No visual difference with current defaults, but if an artist changes
  SpeedUp/SpeedDown expecting UE5 semantics, they'll get the opposite of what they expect.
  Rename the constants or swap the condition to match UE5's convention.
```

---

## Color Grading LUT

### color_grading_lut_cs.hlsl

```
ISSUE: FilmToneMap called with explicit parameters — UE5 reads from global uniforms
UE5: PostProcessCombineLUTs.usf:275 — FilmToneMap(ColorAP1) with NO parameters.
  The FilmToneMap function in TonemapCommon.ush:105 reads FilmSlope, FilmToe, FilmShoulder,
  FilmBlackClip, FilmWhiteClip from global uniform declarations (lines 99-103).
UzEngine: color_grading_lut_cs.hlsl:587 — FilmToneMap(ColorAP1, FilmSlope, FilmToe, ...)
  with explicit parameters passed from the cbuffer.
FIX: No change needed. This is an architectural difference, not a bug. UzEngine passes
  params explicitly which is arguably better engineering (no hidden global state).
  The MATH is identical.
IMPACT: LOW — Pure API style difference, no visual impact.
```

**All other aspects of the LUT shader match UE5 exactly:**
- LogToLin formula matches GammaCorrectionCommon.ush:80-93 (LinearRange=14, LinearGrey=0.18, ExposureGrey=444)
- White balance (PlanckianLocusChromaticity, D_IlluminantChromaticity, PlanckianIsothermal, ChromaticAdaptation) — all constants match TonemapCommon.ush exactly
- ColorCorrect and ColorCorrectAll — exact match with PostProcessCombineLUTs.usf:88-140
- ExpandGamut — Wide_2_XYZ_MAT values match line 238-243
- BlueCorrection matrices match lines 256-269
- 1.05 normalization matches line 419
- All ACES color space matrices verified against ACESCommon.ush

---

## GTAO

### gtao_cs.hlsl

```
ISSUE: GTAO_NUM_TAPS=4 and GTAO_NUM_ANGLES=2 — significantly lower than UE5 medium quality
UE5: PostProcessAmbientOcclusion.usf:540-563 — Quality tiers:
  SHADER_QUALITY 0 (very low): GTAO_NUMTAPS=4
  SHADER_QUALITY 1 (low): GTAO_NUMTAPS=6
  SHADER_QUALITY 2 (medium): GTAO_NUMTAPS=8
  SHADER_QUALITY 3 (high): GTAO_NUMTAPS=12
  SHADER_QUALITY 4 (very high): GTAO_NUMTAPS=20
  NumAngles is passed via GTAOParams[4].y, typically 2 for medium, 3 for high.
UzEngine: gtao_cs.hlsl:91 — GTAO_NUM_TAPS=4 (matches UE5 VERY LOW quality)
  gtao_cs.hlsl:89 — GTAO_NUM_ANGLES=2 (matches UE5 medium NumAngles)
FIX: For medium quality parity, increase GTAO_NUM_TAPS to 8. For high quality, use 12.
  Ideally, make this a compile-time #define controlled by the CVar system.
IMPACT: MEDIUM — 4 taps produces significantly more noise than 8 taps, requiring heavier
  denoising to compensate. The quality/performance tradeoff is worse.
```

```
ISSUE: GetRandomVector temporal rotation uses frameIndex directly — UE5 passes via GTAOParams
UE5: PostProcessAmbientOcclusion.usf:812-836 — GetRandomVector reads temporal rotation
  from GTAOParams[0].xy (cos/sin of temporal angle) and GTAOParams[0].z (temporal offset).
  These are computed on the CPU: TemporalAngle = (FrameNumber % 6) * (PI / 3.0)
  and TemporalOffset = ((FrameNumber / 6) % 4) * 0.25.
UzEngine: gtao_cs.hlsl:287-293 — Computes temporal rotation IN THE SHADER from frameIndex.
  The math is identical (frameIndex % 6 * PI/3, (frameIndex/6) % 4 * 0.25) but done per-pixel.
FIX: No change needed. Computing in shader is slightly less efficient (redundant sin/cos per pixel)
  but produces identical results. Could optimize by precomputing on CPU and passing via cbuffer.
IMPACT: LOW — Identical visual result, minor perf overhead from redundant trig per pixel.
```

**Verified correct aspects:**
- SearchForLargestAngleDual matches UE5 usf:648-694 exactly
- ComputeInnerIntegral matches UE5 usf:771-795 exactly (all constants verified)
- InterleavedGradientNoise constants (52.9829189, 0.06711056, 0.00583715) match usf:799-802
- acosFast matches FastMathThirdParty.ush:108-114
- Distance fade pattern matches UE5 usf:913-915
- AO normalization (2.0/PI) matches usf:909
- Offset advance (0.617) matches usf:903
- Thickness blend pattern matches usf:676
- Attenuation factor (2.0 / (R*R)) matches usf:877

### gtao_denoise_cs.hlsl

```
ISSUE: Denoise kernel offset pattern uses {(0,0),(2,0),(0,2),(2,2)} — asymmetric
UE5: PostProcessAmbientOcclusion.usf:528-534 — MainSSAOSmoothCS uses 4 bilinear samples
  at offsets that are centered around the pixel: the 4 quadrants of the 4x4 block.
  The UE5 smooth pass is a simple box blur using bilinear sampling to cover 4x4 with 4 taps.
UzEngine: gtao_denoise_cs.hlsl:116-121 — Offsets are (0,0), (2,0), (0,2), (2,2).
  This samples the CENTER pixel plus 3 neighbors to the RIGHT and BELOW.
  A centered 4x4 would use offsets like (-1,-1), (1,-1), (-1,1), (1,1).
FIX: Change offsets to center the kernel:
  float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1)
  These cover a 4x4 block centered on the pixel via bilinear filtering.
IMPACT: LOW — The asymmetric kernel produces a slight directional bias (shift down-right)
  but the bilateral depth weighting mitigates most artifacts.
```

---

## Motion Blur

### motion_blur_cs.hlsl

**NO ISSUES FOUND.** The implementation correctly follows UE5 MotionBlurApply.usf:
- CONFIG_MINIMAL_PIXEL_VELOCITY = 0.5 matches MotionBlurCommon.ush:34
- SOFT_Z_EXTENT = 1 matches MotionBlurApply.usf:54
- SAMPLE_COUNT_FACTOR = 4 matches MotionBlurApply.usf:341
- InterleavedGradientNoise constants match RandomInterleavedGradientNoise.ush:16
- Bidirectional sampling pattern matches MotionBlurApply.usf:438-450
- Depth weighting matches MotionBlurApply.usf:235-238
- Velocity scale pre-computation documented correctly from PostProcessMotionBlur.cpp:613

### motionblur_tilemax_cs.hlsl

**NO ISSUES FOUND.** TILE_SIZE=16 matches MotionBlurCommon.ush:24. Parallel reduction is correct.

### motionblur_neighbormax_cs.hlsl

```
ISSUE: 3x3 neighborhood instead of UE5's 7x7 direction-aware scatter-as-gather
UE5: MotionBlurTileGather.usf:37-83 — Uses a 7x7 neighborhood (-3..+3) with a
  velocity-direction-aware quad intersection test to determine which neighbors
  actually contribute based on whether their velocity vector would reach the current tile.
UzEngine: motionblur_neighbormax_cs.hlsl:83-102 — Simple 3x3 max(-1..+1).
FIX: This is documented as an intentional simplification (lines 29-34). For full UE5
  parity, implement the 7x7 direction-aware test. The 3x3 approach is conservative
  (includes more tiles than needed) but never misses relevant tiles. Keep as-is for now.
IMPACT: LOW — Conservative approach may cause unnecessary blur computation in some tiles
  but never produces incorrect results. Visual output is identical.
```

---

## Depth of Field

### dof_setup_cs.hlsl

**NO ISSUES FOUND.** The CoC formula matches UE5 DOFCommon.ush:209-226 exactly:
- CocRadius = ((SceneDepth - Focus) / SceneDepth) * CocInfinityRadius
- Focal region transition matches DepthOfFieldCommon.ush:54-66
- MaxForegroundRadius/MaxBackgroundRadius = 0.025 matches DiaphragmDOFUtils.cpp:15,21

### dof_blur_cs.hlsl

**NO ISSUES FOUND.** Ring-based sampling pattern matches UE5 DOFGatherKernel.ush:37-53:
- RING_COUNT=5 matches DOFCommon.ush:106
- Half-ring offset for alternating rings matches line 44
- Per-pixel IGN rotation matches DOFGatherPass.usf:218-219
- CoC intersection weighting logic is correct
- Recombine smoothstep with MAX_RECOMBINE_ABS_COC_RADIUS=3.0 matches DOFCommon.ush:109

---

## FilmToneMap (film_tonemap.hlsli)

**NO ISSUES FOUND.** Every line matches UE5 TonemapCommon.ush:105-220:
- All ACES matrices verified against ACESCommon.ush (AP0_2_XYZ, XYZ_2_AP0, AP1_2_XYZ, etc.)
- Glow module: RRT_GLOW_GAIN=0.05, RRT_GLOW_MID=0.08 match lines 146-147
- Red modifier: RRT_RED_SCALE=0.82, RRT_RED_PIVOT=0.03, RRT_RED_HUE=0, RRT_RED_WIDTH=135 match lines 158-161
- Pre-desaturate=0.96 matches line 175
- Post-desaturate=0.93 matches line 216
- S-curve construction (ToeScale, ShoulderScale, ToeMatch, StraightMatch, ShoulderMatch) — all verified
- InMatch=0.18, OutMatch=0.18 match lines 180-181
- Sigmoid formulas for ToeColor and ShoulderColor match lines 204-205
- Selection using step() instead of UE5's select() — functionally identical
  (UE5 usf:207-208 uses `select(LogColor < ToeMatch, ToeColor, StraightColor)`)
- Hermite blend t computation matches lines 210-213

---

## Priority Fix List

### CRITICAL (2 issues — fix immediately)

1. **Vignette applied after tonemapping** (tonemap_ps.hlsl)
   — Move to before exposure/tonemap. UE5 applies vignette to HDR linear color.

2. **Exposure temporal adaptation formula** (exposure_average_cs.hlsl)
   — Rewrite to use UE5's log2-space dual exponential/linear adaptation model.

### MEDIUM (5 issues — fix before next milestone)

3. **Bloom upsample per-stage tint is scalar** — change to float3 for colored bloom.
4. **Bloom threshold Karis average** — document as intentional deviation from UE5.
5. **Vignette missing aspect ratio correction** — add VignetteSpace transform.
6. **GTAO NUM_TAPS=4** — increase to 8 for medium quality parity.
7. **Exposure speed semantic inversion** — clarify naming or swap condition.

### LOW (7 issues — track for future cleanup)

8. Bloom downsample 13-tap vs UE5 4-tap — intentional, documented.
9. Bloom composite legacy pass — verify not double-applied.
10. Film grain 1e10 vs POSITIVE_INFINITY — functionally identical.
11. Color grading LUT explicit params — architectural, not a bug.
12. GTAO temporal rotation computed in shader — minor perf, identical result.
13. GTAO denoise asymmetric kernel — minor directional bias.
14. NeighborMax 3x3 vs 7x7 — conservative, documented deviation.

---

## Fix Status (2026-03-18)

| # | Issue | Status |
|---|-------|--------|
| 1 | Bloom threshold Karis average | **DOCUMENTED** as intentional deviation (Jimenez 2014 pattern) |
| 2 | Bloom downsample 13-tap vs UE5 4-tap | No change needed (intentional improvement) |
| 3 | Bloom upsample scalar tint | **FIXED** — changed to float3 RGB tint (R, G, B) |
| 4 | Bloom composite legacy pass | No change needed (unused) |
| 5 | Vignette after tonemapping | **FIXED** — moved to HDR space before exposure/tonemap |
| 6 | Vignette missing aspect ratio | **FIXED** — VignetteSpace correction via ddx/ddy aspect ratio |
| 7 | Film grain 1e10 vs POSITIVE_INFINITY | No change needed (functionally identical) |
| 8 | Exposure temporal adaptation in linear | **FIXED** — log2 space with dual exponential/linear model |
| 9 | Exposure speed semantics inverted | **FIXED** — SpeedUp now correctly selects when logDiff > 0 |
| 10 | Color grading LUT explicit params | No change needed (architectural, not a bug) |
| 11 | GTAO NUM_TAPS=4 | **FIXED** — increased to 8 (UE5 medium quality) |
| 12 | GTAO temporal rotation in shader | No change needed (identical result) |
| 13 | GTAO denoise asymmetric kernel | **FIXED** — centered offsets (-1,-1),(1,-1),(-1,1),(1,1) |
| 14 | NeighborMax 3x3 vs 7x7 | No change needed (conservative, documented) |
