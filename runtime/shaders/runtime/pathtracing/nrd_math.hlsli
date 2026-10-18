#ifndef UZ_NRD_MATH
#define UZ_NRD_MATH

/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
/// @constants 1e-6 prevents division by zero; weights are normalized by their sum.
float NrdDepthWeight(float difference, float scale) {
    return exp(-abs(difference) / max(scale, 1e-6f));
}
float NrdNormalWeight(float cosine) {
    return pow(max(cosine, 0.0f), 32.0f);
}
/// @ref Karis 2014 "High Quality Temporal Supersampling"
/// @constants 0.5 converts D3D clip coordinates to top-left UV.
float NrdClipToU(float x, float w) { return 0.5f + 0.5f * x / w; }
float NrdClipToV(float y, float w) { return 0.5f - 0.5f * y / w; }
float NrdHistoryUV(float uv, float velocity, float extent) {
    return uv - velocity / extent;
}
// Mirror-like surfaces use a narrow footprint and short history. Rough lobes
// tolerate both spatial and temporal integration (perceptual roughness).
float NrdSpecularRadius(float roughness) { return 1.0f + 3.0f * roughness * roughness; }
float NrdSpecularHistory(float roughness) { return 2.0f + 30.0f * roughness * roughness; }
float NrdSpecularNormalWeight(float cosine, float roughness) {
    // HLSL pow(0,0) is undefined; fully rough surfaces have exponent zero.
    return pow(max(cosine, 1e-6f), 128.0f * (1.0f - roughness));
}
float NrdClampHistory(float value, float mean, float sigma, float k) {
    float low = mean - k * sigma, high = mean + k * sigma;
    return value < low ? low : (value > high ? high : value);
}
float NrdVirtualComponent(float position, float direction, float normal,
                         float directionDotNormal, float distance) {
    return position + (direction - 2.0f * directionDotNormal * normal) * distance;
}
#endif
