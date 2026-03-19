================================================================================
UE5 AUTO-EXPOSURE HISTOGRAM-BASED IMPLEMENTATION ANALYSIS
Complete Research Summary — 2026-03-18
================================================================================

DELIVERABLES CREATED:
======================

1. ue5-auto-exposure-histogram.md (843 lines)
   ├─ Complete architectural breakdown of UE5's eye adaptation system
   ├─ Every constant, formula, and parameter WITH EXACT FILE:LINE citations
   ├─ 13 major sections covering histogram, adaptation, exposure math
   ├─ Local exposure bonus feature documented
   └─ UzEngine adaptation strategy provided

2. ue5-auto-exposure-quick-reference.md (209 lines)
   ├─ Constants checklists (DO NOT INVENT)
   ├─ Default parameters for both legacy and EV100 modes
   ├─ All formulas in one place for quick lookup
   ├─ Shader entry points and CVars
   └─ Implementation checklist

3. ue5-auto-exposure-implementation-guide.md (551 lines)
   ├─ Step-by-step algorithm pseudocode with source citations
   ├─ 3-phase architecture (Setup, Compute, Apply)
   ├─ Helper function implementations
   ├─ CPU parameter computation code
   ├─ Validation checklist
   ├─ Testing strategy
   ├─ Common pitfalls and fixes
   └─ Performance analysis

================================================================================
KEY FINDINGS
================================================================================

HISTOGRAM CONFIGURATION:
  • Bin count: 64 (fixed, DO NOT CHANGE)
  • Texture layout: 16 texels, 4 channels per texel (RGBA)
  • Log range: [-8, 4] (legacy) or [-10, 20] (EV100)
  • Conversion: position = scale * log2(lum) + bias

ADAPTATION ALGORITHM:
  • Percentile filtering: 10th to 90th percentile (EXACT)
  • Target exposure: average_luminance / 0.18
  • Clamping: min/max brightness * 0.18 (middle grey)
  • Speed up: 3.0 f-stops/sec (EXACT)
  • Speed down: 1.0 f-stop/sec (EXACT)
  • Transition: Linear vs Exponential at 1.5 f-stops (EXACT)

LUMINANCE CALCULATION:
  • Three standard methods supported:
    1. Uniform (1/3, 1/3, 1/3) — default
    2. NTSC (0.3, 0.59, 0.11) — legacy
    3. Rec.709 — working color space
  • Luminance epsilon: 0.0001 (to avoid log2(0) NaN)

EXPOSURE COMPENSATION:
  • Settings bias: 2^(AutoExposureBias) multiplicative
  • Curve bias: dynamic per EV100 (optional)
  • Combined: compensation = settings * curve
  • Applied as: exposure_scale = compensation / target_exposure

OUTPUT BUFFER (2 float4 values):
  [0].x = ExposureScale — Apply to scene color BEFORE tonemapping
  [0].y = TargetExposureScale
  [0].z = AverageSceneLuminance
  [0].w = MiddleGreyExposureCompensation
  [1].x = AverageLocalExposure (optional, for tone mapping)

PRE-EXPOSURE HANDLING:
  • CRITICAL: Remove pre-exposure BEFORE histogram capture
  • Formula: luminance = dot(color * (1.0 / pre_exposure), weights)
  • This ensures histogram captures real scene luminance, not pre-exposed

================================================================================
ALL CITATIONS (EXACT FILE:LINE)
================================================================================

C++ Files:
  PostProcessEyeAdaptation.cpp:
    - Line 458: AutoExposureMethod default
    - Line 459-460: AutoExposureLowPercent, HighPercent defaults
    - Line 467-477: MinBrightness, MaxBrightness, HistogramLogMin/Max defaults
    - Line 484-485: AutoExposureSpeedUp, SpeedDown defaults
    - Line 491: StartDistance (1.5 f-stops)
    - Line 503-505: M factor calculation
    - Line 547-562: Luminance method selection

  PostProcessEyeAdaptation.h:
    - Lines 51-79: FEyeAdaptationParameters struct

  Scene.h:
    - Lines 1883-1908: PostProcessSettings properties (defaults)

  Scene.cpp:
    - Lines 463-485: Parameter initialization

Shader Files:
  PostProcessEyeAdaptation.usf:
    - Lines 170-198: EyeAdaptationCommon (target exposure, adaptation)
    - Lines 231-247: EyeAdaptationCS (main compute shader entry)
    - Lines 250-263: BasicEyeAdaptationSetupPS (luminance to alpha)
    - Lines 305-365: BasicEyeAdaptationCS (fallback method)

  PostProcessHistogramCommon.ush:
    - Lines 49-52: CalculateEyeAdaptationLuminance
    - Lines 73-84: ComputeLogLuminanceFromHistogramPosition, inverse
    - Lines 86-88: HISTOGRAM_SIZE = 64
    - Lines 101-114: GetHistogramBucket (unpacking RGBA)
    - Lines 116-126: ComputeHistogramSum
    - Lines 130-156: ComputeAverageLuminanceWithoutOutlier (percentile filtering)
    - Lines 171-174: AdaptationWeightTexture (meter mask)
    - Lines 176-181: ExponentialAdaption formula
    - Lines 183-190: LinearAdaption formula
    - Lines 192-219: ComputeEyeAdaptation (main adaptation blending)

================================================================================
WHAT NOT TO INVENT
================================================================================

FORBIDDEN CREATIVE CHANGES:
  X Change histogram bin count from 64
  X Use different percentile thresholds than 10%-90%
  X Invent new luminance weighting methods
  X Use different middle grey constant than 0.18
  X Change adaptation speeds from 3 up / 1 down f-stops/sec
  X Use different transition distance than 1.5 f-stops
  X Skip pre-exposure removal before histogram
  X Create alternative exposure formulas
  X Assume how the API works — ALWAYS read UE5 source first
  X Skip the log-space adaptation (log2 is critical)

ALLOWED OPTIMIZATIONS:
  OK Multi-frame filtering on exposure output (post-processing only)
  OK Adaptive histogram binning (if UE5 does it, or justified by data)
  OK Additional metering modes (add new ones, don't replace existing)
  OK Local exposure enhancements (build on UE5's, don't replace)
  OK Extended dynamic range support (follow EV100 mode pattern)

================================================================================
INTEGRATION CHECKLIST FOR UZENGINE
================================================================================

Phase 1 - Histogram Setup:
  [ ] Create HistogramSetupPS shader
  [ ] Implement luminance calculation with 3 selectable methods
  [ ] Pre-exposure removal: color *= (1.0 / pre_exposure)
  [ ] Histogram position conversion: pos = scale * log2(lum) + bias
  [ ] Write histogram to texture (4 RGBA channels per 4 buckets)
  [ ] Bind meter mask texture (optional, default white)

Phase 2 - Adaptation Compute:
  [ ] Create EyeAdaptationCS compute shader [1,1,1] threads
  [ ] Implement GetHistogramBucket RGBA unpacking
  [ ] Implement ComputeHistogramSum (all 64 buckets)
  [ ] Implement percentile filtering (10%-90%)
  [ ] Implement weighted average luminance calculation
  [ ] Implement target exposure = avg_lum / 0.18
  [ ] Implement ComputeEyeAdaptation (log-space, linear+exponential)
  [ ] Compute M factors on CPU (smooth transition)
  [ ] Apply exposure compensation (settings * curve)
  [ ] Output ExposureScale, TargetExposureScale, AvgLum, Compensation

Phase 3 - Application:
  [ ] Multiply scene color by exposure_scale before tonemapping
  [ ] Test with RenderDoc texture inspection
  [ ] Verify histogram values independent of pre_exposure
  [ ] Compare output with UE5 reference

Testing:
  [ ] Unit tests for all parameter conversions
  [ ] Visual tests with known luminance scenes
  [ ] RenderDoc histogram inspection
  [ ] Frame-by-frame adaptation curve validation
  [ ] Comparison with UE5 output (pixel-perfect or close)

================================================================================
REFERENCE LINKS
================================================================================

Main Analysis Document:
  E:/UzEngine/docs/analysis/rendering/ue5-auto-exposure-histogram.md

Quick Reference (Constants & Formulas):
  E:/UzEngine/docs/analysis/rendering/ue5-auto-exposure-quick-reference.md

Implementation Guide (Pseudocode & Integration):
  E:/UzEngine/docs/analysis/rendering/ue5-auto-exposure-implementation-guide.md

UE5 Source Code Root:
  W:/UE_5.6_Source

Related UzEngine Documentation:
  docs/analysis/rendering/ue5-uniform-buffer-shader-binding-system.md
  docs/analysis/rendering/ue5-reversed-z-shadow-maps.md
  docs/analysis/rendering/ue5-sky-atmosphere-analysis.md

================================================================================
END OF SUMMARY
================================================================================
