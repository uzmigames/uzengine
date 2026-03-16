/// Auto-Exposure Average + Temporal Adaptation — Compute Shader
///
/// UE5 Eye Adaptation parity (PostProcessEyeAdaptation.usf).
///
/// Reads the 64-bin luminance histogram, applies percentile filtering,
/// computes weighted average log2 luminance, clamps to brightness bounds,
/// applies temporal adaptation, and outputs the final exposure multiplier.
///
/// # UE5 Exposure Formula
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
    float g_HistLogMin;        // Log2 luminance range minimum (UE5: -10.0)
    float g_HistLogMax;        // Log2 luminance range maximum (UE5: +20.0)
    uint  g_HistogramSrvIdx;   // Bindless SRV index for the histogram buffer
    uint  g_Padding;
    float g_ExposureBias;      // EV offset — 0.0 = neutral (UE5 AutoExposureBias)
    float g_MinBrightness;     // Min avg luminance clamp (UE5: 2^-10 ≈ 0.001)
    float g_MaxBrightness;     // Max avg luminance clamp (UE5: 2^20 ≈ 1048576)
    float g_DeltaTime;         // Frame delta time in seconds
};

/// UE5 defaults — hardcoded to stay within root constant budget
static const float g_SpeedUp     = 3.0;   // Adaptation speed when brightening (seconds)
static const float g_SpeedDown   = 1.0;   // Adaptation speed when darkening (seconds)
static const float g_LowPercent  = 10.0;  // UE5: AutoExposureLowPercent = 10
static const float g_HighPercent = 90.0;  // UE5: AutoExposureHighPercent = 90

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

RWByteAddressBuffer g_ExposureUAV : register(u0);

/// SM5 non-bindless: histogram SRV bound at t0.
#ifndef UZ_BINDLESS
ByteAddressBuffer g_HistogramSrvTex : register(t0);
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
    ByteAddressBuffer histogramSRV = ResourceDescriptorHeap[g_HistogramSrvIdx];
#else
    ByteAddressBuffer histogramSRV = g_HistogramSrvTex;
#endif

    uint bins[NUM_BINS];
    uint totalPixels = 0;

    [unroll]
    for (uint i = 0; i < NUM_BINS; ++i)
    {
        bins[i] = histogramSRV.Load(i * 4);
        totalPixels += bins[i];
    }

    if (totalPixels == 0)
        return;

    // ── Percentile filtering (UE5: skip lowest 10%, highest 10%) ────────
    float minFractionSum = float(totalPixels) * g_LowPercent  / 100.0;
    float maxFractionSum = float(totalPixels) * g_HighPercent / 100.0;

    float weightedLogLum = 0.0;
    float weightSum      = 0.0;

    // UE5 pattern: ComputeAverageLuminanceWithoutOutlier
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

    // ── UE5 Exposure Formula ────────────────────────────────────────────
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

    // ── Temporal adaptation ─────────────────────────────────────────────
    float prevExposure = asfloat(g_ExposureUAV.Load(0));

    // First-frame init: snap to target
    if (prevExposure < 1e-10)
        prevExposure = targetExposure;

    // Asymmetric adaptation: slower to brighten, faster to darken
    float adaptSpeed = (targetExposure < prevExposure) ? g_SpeedUp : g_SpeedDown;
    float alpha = 1.0 - exp(-g_DeltaTime / max(adaptSpeed, 0.001));
    float currentExposure = lerp(prevExposure, targetExposure, alpha);

    // Clamp to prevent extreme values
    float minExposure = exposureBiasLinear * 0.18 / max(g_MaxBrightness, 0.001);
    float maxExposure = exposureBiasLinear * 0.18 / max(g_MinBrightness, 0.00001);
    currentExposure = clamp(currentExposure, minExposure, maxExposure);

    // ── Write persistent state ──────────────────────────────────────────
    g_ExposureUAV.Store(0, asuint(currentExposure));  // For next frame's adaptation
    g_ExposureUAV.Store(4, asuint(currentExposure));  // For tonemap shader (same value)
}
