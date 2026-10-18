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
#endif
