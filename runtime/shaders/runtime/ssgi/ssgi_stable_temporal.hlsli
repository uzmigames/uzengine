// Shared GPU/CPU weight policy. Moments follow the noisy input, not the filtered output.
#ifndef UZ_SSGI_STABLE_TEMPORAL_HLSLI
#define UZ_SSGI_STABLE_TEMPORAL_HLSLI
#ifdef __cplusplus
namespace uz {
#endif
float SsgiStableConfidence(float mean, float secondMoment, float current,
                           float confidence, bool accepted, bool stationary) {
    float variance = secondMoment - mean * mean;
    variance = variance > 0.0f ? variance : 0.0f;
    float scale = mean * mean + 0.0001f;
    float delta = current - mean;
    // A coefficient of variation below 20% is a stable estimator. A 3-sigma
    // innovation (with a 10% relative allowance) loses confidence immediately.
    bool stable = accepted && stationary && variance < 0.04f * scale
        && delta * delta < 9.0f * variance + 0.01f * scale;
    return stable ? (confidence < 32.0f ? confidence + 1.0f : 32.0f) : 0.0f;
}
float SsgiStableCount(float historyCount, float confidence) {
    // No 64-frame cap once 16 consecutive compatible, quiet observations agree.
    float count = historyCount + 1.0f;
    return confidence >= 16.0f ? count : (count < 64.0f ? count : 64.0f);
}
float SsgiStableAlpha(float count, float confidence, float fastAlpha) {
    float reciprocal = 1.0f / count;
    return confidence >= 16.0f ? reciprocal
        : (reciprocal > fastAlpha ? reciprocal : fastAlpha);
}
#ifdef __cplusplus
}
#endif
#endif
