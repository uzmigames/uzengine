/// Auto-Exposure Average + Temporal Adaptation — Compute Shader
///
/// Eye Adaptation based on Reinhard et al. 2002 photographic tone reproduction.
///
/// Reads the 64-bin luminance histogram, applies percentile filtering,
/// computes weighted average log2 luminance, clamps to brightness bounds,
/// applies temporal adaptation, and outputs the final exposure multiplier.
///
/// # reference Exposure Formula
/// ```
/// TargetExposure = AverageSceneLuminance / 0.18
/// GlobalExposure = pow(2, AutoExposureBias) * 0.18 / AverageSceneLuminance
/// ```
/// Where 0.18 is middle grey (18% reflectance, Reinhard key value).

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants — 8 DWORDs
// ─────────────────────────────────────────────────────────────────────────────

cbuffer ExposureParams : register(b0)
{
    float g_HistLogMin;        // Log2 luminance range minimum (Equivalent: -10.0)
    float g_HistLogMax;        // Log2 luminance range maximum (Equivalent: +20.0)
    uint  g_HistogramSrvIdx;   // Bindless SRV index for the histogram buffer
    uint  g_Padding;
    float g_ExposureBias;      // EV offset — 0.0 = neutral (reference AutoExposureBias)
    float g_MinBrightness;     // Min avg luminance clamp (Equivalent: 2^-10 ≈ 0.001)
    float g_MaxBrightness;     // Max avg luminance clamp (Equivalent: 2^20 ≈ 1048576)
    float g_DeltaTime;         // Frame delta time in seconds
};

/// defaults — hardcoded to stay within root constant budget
/// @ref Reinhard et al. 2002 "Photographic Tone Reproduction" — auto-exposure defaults
/// AutoExposureSpeedUp = 3.0f, SpeedDown = 1.0f, LowPercent = 10.0f, HighPercent = 90.0f
static const float g_SpeedUp     = 3.0;   // f-stops/sec when LogDiff > 0 (target brighter than current)
static const float g_SpeedDown   = 1.0;   // f-stops/sec when LogDiff <= 0 (target darker than current)
static const float g_LowPercent  = 10.0;  // Equivalent: AutoExposureLowPercent = 10
static const float g_HighPercent = 90.0;  // Equivalent: AutoExposureHighPercent = 90

/// @ref Eye adaptation — ExponentialTransitionDistance default = 1.5 f-stops
/// Distance in f-stops at which we switch from exponential to linear adaptation.
/// Below this threshold: exponential (smooth settle near target).
/// Above this threshold: linear (constant speed for large jumps).
static const float g_StartDistance = 1.5;

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

RWStructuredBuffer<float> g_ExposureUAV : register(u0);

/// SM5 non-bindless: histogram SRV bound at t0.
#ifndef UZ_BINDLESS
StructuredBuffer<uint> g_HistogramSrvTex : register(t0);
#endif

#define NUM_BINS 64

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(1, 1, 1)]
void main()
{
    // ── Read histogram ──────────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    StructuredBuffer<uint> histogramSRV = ResourceDescriptorHeap[g_HistogramSrvIdx];
#else
    StructuredBuffer<uint> histogramSRV = g_HistogramSrvTex;
#endif

    uint bins[NUM_BINS];
    uint totalPixels = 0;

    [unroll]
    for (uint i = 0; i < NUM_BINS; ++i)
    {
        bins[i] = histogramSRV[i];
        totalPixels += bins[i];
    }

    if (totalPixels == 0)
        return;

    // ── Percentile filtering (Equivalent: skip lowest 10%, highest 10%) ────────
    float minFractionSum = float(totalPixels) * g_LowPercent  / 100.0;
    float maxFractionSum = float(totalPixels) * g_HighPercent / 100.0;

    float weightedLogLum = 0.0;
    float weightSum      = 0.0;

    // standard pattern: ComputeAverageLuminanceWithoutOutlier
    for (uint b = 0; b < NUM_BINS; ++b)
    {
        float bucket = float(bins[b]);

        // Remove bottom percentile pixels
        float sub = min(bucket, minFractionSum);
        bucket -= sub;
        minFractionSum -= sub;
        maxFractionSum -= sub;

        // Remove top percentile pixels
        bucket = min(bucket, maxFractionSum);
        maxFractionSum -= bucket;

        // Log2 luminance at bin center
        float binCenter = g_HistLogMin
            + (float(b) + 0.5) / float(NUM_BINS)
            * (g_HistLogMax - g_HistLogMin);

        weightedLogLum += binCenter * bucket;
        weightSum      += bucket;
    }

    if (weightSum < 0.0001)
        return;

    // ── Average luminance (geometric mean via log2 space) ───────────────
    float avgLogLum = weightedLogLum / weightSum;
    float avgLum    = pow(2.0, avgLogLum);

    // Clamp to artist-defined brightness range
    avgLum = clamp(avgLum, g_MinBrightness, g_MaxBrightness);

    // ── reference Exposure Formula ────────────────────────────────────────────
    // TargetExposure = avgLum / 0.18 (the scene luminance that should map to 18% grey)
    // GlobalExposure = ExposureBiasLinear * 0.18 / avgLum
    //
    // ExposureBiasLinear = pow(2, AutoExposureBias)
    // With AutoExposureBias=0: ExposureBiasLinear=1.0
    //
    // This is the classic Reinhard key-value formula:
    //   exposure = key / avgLuminance
    // where key = 0.18 * ExposureBias

    float exposureBiasLinear = pow(2.0, g_ExposureBias);  // EV offset → linear multiplier
    float targetExposure     = exposureBiasLinear * 0.18 / avgLum;

    // ── Temporal adaptation (reference dual exponential/linear model in log2 space) ──
    /// @ref Reinhard 2002 — exposure adaptation:192-219 — ComputeEyeAdaptation
    /// @ref Eye adaptation —491-505 — ExponentialUpM/DownM computation
    /// @constants g_StartDistance=1.5 (f-stop transition threshold for exponential-to-linear switch)
    /// @constants g_SpeedUp=3.0 (f-stops/sec upward adaptation rate), g_SpeedDown=1.0 (f-stops/sec downward)
    /// @constants kFrameTimeEps=1/60 (minimum frame time epsilon for adaptation stability)
    /// @deviations NONE
    float prevExposure = g_ExposureUAV[0];

    // First-frame init: snap to target (no adaptation on first frame)
    if (prevExposure < 1e-10)
        prevExposure = targetExposure;

    // Work entirely in log2 space for perceptually uniform adaptation
    // @ref Reinhard 2002 — exposure adaptation:194-197
    float logTarget = log2(targetExposure);
    float logPrev   = log2(prevExposure);
    float logDiff   = logTarget - logPrev;

    // Select speed based on direction of change
    // LogDiff > 0: target is brighter than current → use SpeedUp
    // LogDiff <= 0: target is darker than current → use SpeedDown
    // @ref Reinhard 2002 — exposure adaptation:199
    float adaptSpeed = (logDiff > 0) ? g_SpeedUp : g_SpeedDown;

    // ── Compute M multiplier for exponential curve derivative continuity ──
    // M ensures the exponential curve's slope matches the linear curve's slope
    // at the transition distance, preventing visual jumps between modes.
    // @ref Eye adaptation —492-505
    float startTime = g_StartDistance / max(adaptSpeed, 0.001);
    float kFrameTimeEps = 1.0 / 60.0;  // @ref Eye adaptation —503
    float exponentialM = kFrameTimeEps / ((1.0 - exp2(-kFrameTimeEps * adaptSpeed)) * startTime);

    // ── Exponential adaptation (for small changes, smooth settle near target) ──
    // @ref Reinhard 2002 — exposure adaptation:176-181 — ExponentialAdaption
    float expFactor = 1.0 - exp2(-g_DeltaTime * adaptSpeed);
    float logAdaptedExp = logPrev + (logTarget - logPrev) * expFactor * exponentialM;

    // ── Linear adaptation (for large changes, constant perceptual speed) ──
    // @ref Reinhard 2002 — exposure adaptation:183-190 — LinearAdaption
    float linOffset = g_DeltaTime * adaptSpeed;
    float logAdaptedLin = (logPrev < logTarget)
        ? min(logTarget, logPrev + linOffset)
        : max(logTarget, logPrev - linOffset);

    // ── Select model based on f-stop distance ──
    // Large jumps (> 1.5 f-stops): linear for constant speed
    // Small adjustments (<= 1.5 f-stops): exponential for smooth convergence
    // @ref Reinhard 2002 — exposure adaptation:208
    float absLogDiff = abs(logDiff);
    float logAdapted = (absLogDiff > g_StartDistance) ? logAdaptedLin : logAdaptedExp;

    // Convert back from log2 to linear exposure
    // @ref Reinhard 2002 — exposure adaptation:213
    float currentExposure = exp2(logAdapted);

    // Clamp to prevent extreme values (reference note: target is pre-clamped, but
    // we clamp the result too as a safety net since we don't have postFX volumes)
    float minExposure = exposureBiasLinear * 0.18 / max(g_MaxBrightness, 0.001);
    float maxExposure = exposureBiasLinear * 0.18 / max(g_MinBrightness, 0.00001);
    currentExposure = clamp(currentExposure, minExposure, maxExposure);

    // ── Write persistent state ──────────────────────────────────────────
    g_ExposureUAV[0] = currentExposure;  // For next frame's adaptation
    g_ExposureUAV[1] = currentExposure;  // For tonemap shader (same value)
}
