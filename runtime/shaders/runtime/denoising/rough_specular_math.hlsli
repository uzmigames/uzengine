#ifndef UZ_ROUGH_SPECULAR_MATH
#define UZ_ROUGH_SPECULAR_MATH
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace uz::roughspecular {
using std::min;
using std::max;
using std::sqrt;
#define UZ_ROUGH_INLINE inline
#else
#define UZ_ROUGH_INLINE
#endif
UZ_ROUGH_INLINE bool Reconstructs(float roughness) { return roughness > 0.04f; }
UZ_ROUGH_INLINE float NormalPower(float roughness) {
    return min(512.0f, 8.0f / max(roughness * roughness, 0.001f));
}
UZ_ROUGH_INLINE float SpatialRadius(float roughness, float hitDistance, float depth) {
    // Misses have an infinite footprint; finite hits limit the lobe footprint.
    float footprint = hitDistance > 0.0f ? hitDistance / max(hitDistance + depth, 0.001f) : 1.0f;
    return 1.0f + min(2.0f, 8.0f * roughness * roughness * footprint);
}
UZ_ROUGH_INLINE float HistoryCap(float roughness, float motionPixels) {
    return motionPixels > 0.5f ? 4.0f : min(128.0f, 16.0f + 384.0f * roughness * roughness);
}
UZ_ROUGH_INLINE float ClampHistory(float history, float mean, float estimatorVariance,
                                  float motionPixels) {
    float extent = (motionPixels > 0.5f ? 1.0f : 3.0f) * sqrt(max(estimatorVariance, 0.0f));
    return max(0.0f, min(max(history, mean - extent), mean + extent));
}
#ifdef __cplusplus
}
#endif
#undef UZ_ROUGH_INLINE
#endif
