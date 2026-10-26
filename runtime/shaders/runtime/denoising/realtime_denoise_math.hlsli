#ifndef UZ_REALTIME_DENOISE_MATH
#define UZ_REALTIME_DENOISE_MATH
#ifdef __cplusplus
#include <algorithm>
namespace uz::denoise {
using std::min;
using std::max;
#define UZ_DENOISE_INLINE inline
#else
#define UZ_DENOISE_INLINE
#endif
UZ_DENOISE_INLINE bool Eligible(float producerWeight) { return producerWeight > 0.0f; }
UZ_DENOISE_INLINE float PreviousCoordinate(float uv, float velocityPixels, float size,
                                          float previousJitter, float currentJitter) {
    return uv + (previousJitter - currentJitter - velocityPixels) / size;
}
UZ_DENOISE_INLINE bool Compatible(float normalCosine, float planeDistance,
                                 float tolerance, float roughnessDifference) {
    return normalCosine >= 0.9f && planeDistance <= tolerance && roughnessDifference < 0.1f;
}
UZ_DENOISE_INLINE float HistoryCap(float roughness, float motionPixels) {
    return motionPixels > 1.0f ? 2.0f : 4.0f + 12.0f * roughness * roughness;
}
UZ_DENOISE_INLINE float ClampLuminance(float history, float fast, float sigma, float motionPixels) {
    float extent = (motionPixels > 1.0f ? 0.5f : 3.0f) * sigma;
    return max(0.0f, min(max(history, fast - extent), fast + extent));
}
#ifdef __cplusplus
}
#endif
#undef UZ_DENOISE_INLINE
#endif
