#ifndef UZ_NRD_SDK_MATH
#define UZ_NRD_SDK_MATH

// Scalar front-end math replicated from NRD.hlsli v4.17.3 so the engine-side
// pack shader compiles without the (optional, gitignored) SDK tree and so the
// formulas are unit-testable in C++ (test_nrd_sdk.cpp includes this file).
/// @ref NRD.hlsli _NRD_GetSpecMagicCurve: lobe-spread curve, 0 at mirror, 1 at fully rough.
/// @constants 200 = NRD curve steepness; power 0.5 for hit-distance normalization.
float NrdSdkSpecMagicCurve(float roughness, float power) {
    float r = roughness < 0.0f ? 0.0f : (roughness > 1.0f ? 1.0f : roughness);
    float f = 1.0f - exp2(-200.0f * roughness * roughness);
    return f * pow(r, power);
}
/// @ref NRD.hlsli _REBLUR_GetHitDistanceNormalization: f = (A + |viewZ| B) lerp(C, 1, curve)
float NrdSdkHitDistNormalization(float viewZ, float a, float b, float c, float roughness) {
    float curve = NrdSdkSpecMagicCurve(roughness, 0.5f);
    return (a + abs(viewZ) * b) * (c + (1.0f - c) * curve);
}
/// @ref NRD.hlsli REBLUR_FrontEnd_GetNormHitDist: saturate(hitDist / f), never 0 ("no data").
/// @constants 1e-6 = NRD_EPS
float NrdSdkNormHitDist(float hitDist, float normalization) {
    float n = hitDist / normalization;
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return max(n, 1e-6f);
}
/// @ref NRD.hlsli _NRD_LinearToYCoCg / _NRD_YCoCgToLinear, one channel at a time.
float NrdSdkLumaY(float r, float g, float b) { return 0.25f * r + 0.5f * g + 0.25f * b; }
float NrdSdkChromaCo(float r, float g, float b) { return 0.5f * r - 0.5f * b + 0.0f * g; }
float NrdSdkChromaCg(float r, float g, float b) { return -0.25f * r + 0.5f * g - 0.25f * b; }
float NrdSdkLinearR(float y, float co, float cg) { return max(y - cg + co, 0.0f); }
float NrdSdkLinearG(float y, float co, float cg) { return max(y + cg + 0.0f * co, 0.0f); }
float NrdSdkLinearB(float y, float co, float cg) { return max(y - cg - co, 0.0f); }
#endif
