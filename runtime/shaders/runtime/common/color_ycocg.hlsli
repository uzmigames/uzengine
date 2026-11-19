#ifndef UZ_COMMON_COLOR_YCOCG_HLSLI
#define UZ_COMMON_COLOR_YCOCG_HLSLI

/// RGB <-> YCoCg, the unscaled form the SSR temporal pass (postprocess/ssr_temporal_cs.hlsl)
/// clips its history in: Y = R/4 + G/2 + B/4, Co = R/2 - B/2, Cg = -R/4 + G/2 - B/4, with the
/// exact inverse R = Y + Co - Cg, G = Y + Cg, B = Y - Co - Cg (every coefficient is a dyadic
/// rational, so the round trip is exact in reals and exact in float on dyadic inputs).
/// @ref Malvar & Sullivan 2003 "YCoCg-R: A Color Space with RGB Reversibility and Low Dynamic Range"
/// @ref Karis 2014 "High Quality Temporal Supersampling" — history clipping in YCoCg
/// @constants luma = {0.25, 0.5, 0.25}, Co = {0.5, 0, -0.5}, Cg = {-0.25, 0.5, -0.25}
///
/// Scalar, one channel at a time, so tests/engine/renderer/test_color_math_parity.cpp compiles
/// this same text as C++ (the nrd_sdk_math.hlsli pattern) and the shader wraps it into float3
/// helpers. Not the 4x-scaled variant of postprocess/taa_resolve_cs.hlsl, which folds the 1/4
/// into its inverse.
float SsrYCoCgLumaY(float r, float g, float b)    { return 0.25f * r + 0.5f * g + 0.25f * b; }
float SsrYCoCgChromaCo(float r, float g, float b) { return 0.5f * r - 0.5f * b; }
float SsrYCoCgChromaCg(float r, float g, float b) { return -0.25f * r + 0.5f * g - 0.25f * b; }
float SsrYCoCgToR(float y, float co, float cg)    { return y + co - cg; }
float SsrYCoCgToG(float y, float co, float cg)    { return y + cg; }
float SsrYCoCgToB(float y, float co, float cg)    { return y - co - cg; }

#endif // UZ_COMMON_COLOR_YCOCG_HLSLI
