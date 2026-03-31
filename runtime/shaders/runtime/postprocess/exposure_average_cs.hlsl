/// UzEngine auto-exposure average + temporal adaptation.
/// @ref Reinhard 2002 "Photographic Tone Reproduction" — key-value exposure
/// @ref Falcor ToneMapping.ps.slang — exposure = key / avgLuminance (key=0.042)
///
/// Reads 64-bin histogram, computes percentile-filtered average luminance,
/// applies temporal adaptation (dual exponential/linear model), outputs exposure.
///
/// Root constants b0: 8 DWORDs
/// UAV u0: RWStructuredBuffer<float> exposure[2]

cbuffer ExposureParams : register(b0) {
    float g_HistLogMin;
    float g_HistLogMax;
    uint  g_HistogramSrvIdx;
    uint  g_Padding;
    float g_ExposureBias;
    float g_MinBrightness;
    float g_MaxBrightness;
    float g_DeltaTime;
};

RWStructuredBuffer<float> g_ExposureUAV : register(u0);

#ifndef UZ_BINDLESS
StructuredBuffer<uint> g_HistogramSrvTex : register(t0);
#endif

#define NUM_BINS 64

/// @ref Reinhard 2002 — adaptation speeds (f-stops/sec)
static const float g_SpeedUp     = 3.0;
static const float g_SpeedDown   = 1.0;
static const float g_LowPercent  = 10.0;
static const float g_HighPercent = 90.0;
static const float g_StartDistance = 1.5;

[numthreads(1, 1, 1)]
void main() {
#ifdef UZ_BINDLESS
    StructuredBuffer<uint> hist = ResourceDescriptorHeap[g_HistogramSrvIdx];
#else
    StructuredBuffer<uint> hist = g_HistogramSrvTex;
#endif

    uint bins[NUM_BINS];
    uint totalPixels = 0;
    [unroll] for (uint i = 0; i < NUM_BINS; ++i) {
        bins[i] = hist[i];
        totalPixels += bins[i];
    }
    if (totalPixels == 0) return;

    // Percentile filtering — skip darkest 10% and brightest 10%
    float minFrac = float(totalPixels) * g_LowPercent / 100.0;
    float maxFrac = float(totalPixels) * g_HighPercent / 100.0;
    float weightedLogLum = 0.0, weightSum = 0.0;

    for (uint b = 0; b < NUM_BINS; ++b) {
        float bucket = float(bins[b]);
        float sub = min(bucket, minFrac);
        bucket -= sub; minFrac -= sub; maxFrac -= sub;
        bucket = min(bucket, maxFrac);
        maxFrac -= bucket;

        float binCenter = g_HistLogMin + (float(b) + 0.5) / float(NUM_BINS) * (g_HistLogMax - g_HistLogMin);
        weightedLogLum += binCenter * bucket;
        weightSum += bucket;
    }
    if (weightSum < 0.0001) return;

    float avgLogLum = weightedLogLum / weightSum;
    float avgLum = pow(2.0, avgLogLum);
    avgLum = clamp(avgLum, g_MinBrightness, g_MaxBrightness);

    // Exposure = key / avgLuminance (Reinhard key=0.18)
    float exposureBiasLinear = pow(2.0, g_ExposureBias);
    float targetExposure = exposureBiasLinear * 0.18 / avgLum;

    // Temporal adaptation
    float prevExposure = g_ExposureUAV[0];
    if (prevExposure < 1e-10) prevExposure = targetExposure;

    float logTarget = log2(targetExposure);
    float logPrev = log2(prevExposure);
    float logDiff = logTarget - logPrev;
    float adaptSpeed = (logDiff > 0) ? g_SpeedUp : g_SpeedDown;

    // Exponential + linear blend
    float startTime = g_StartDistance / max(adaptSpeed, 0.001);
    float kEps = 1.0 / 60.0;
    float expM = kEps / ((1.0 - exp2(-kEps * adaptSpeed)) * startTime);

    float expFactor = 1.0 - exp2(-g_DeltaTime * adaptSpeed);
    float logExp = logPrev + logDiff * expFactor * expM;

    float linOff = g_DeltaTime * adaptSpeed;
    float logLin = (logPrev < logTarget)
        ? min(logTarget, logPrev + linOff)
        : max(logTarget, logPrev - linOff);

    float logAdapted = (abs(logDiff) > g_StartDistance) ? logLin : logExp;
    float currentExposure = exp2(logAdapted);

    float minExp = exposureBiasLinear * 0.18 / max(g_MaxBrightness, 0.001);
    float maxExp = exposureBiasLinear * 0.18 / max(g_MinBrightness, 0.00001);
    currentExposure = clamp(currentExposure, minExp, maxExp);

    g_ExposureUAV[0] = currentExposure;
    g_ExposureUAV[1] = currentExposure;
}
