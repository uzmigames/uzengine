#ifndef UZ_ROUGH_SPECULAR_MATH
#define UZ_ROUGH_SPECULAR_MATH
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace uz::roughspecular {
using std::min;
using std::max;
using std::sqrt;
using std::atan;
using std::pow;
using std::exp2;
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
// Motion-aware reconstruction (UZ_ROUGH_MOTION_AWARE, docs/analysis/lumen-radiance-cache-stability/09).
// Reflected-direction spread of the GGX lobe at half maximum: 2 atan(alpha), alpha = the lighting's
// floored roughness^2 (rt_reflection_contract.hlsli ReflectionGlossyAlpha / kReflectionMinGGXAlpha).
UZ_ROUGH_INLINE float LobeHalfAngle(float roughness) {
    return 2.0f * atan(max(roughness * roughness, 0.0064f));
}
// Thin-lens / convex-mirror image distance behind the surface: 1 / (2 kappa + 1 / t). kappa is the
// receiver curvature (1 / radius, convex positive, concave clamped to a plane), inverseHitDistance 1 / t
// with 0 = a miss (infinity). Returns a negative sentinel when the image itself is at infinity (a planar
// receiver reflecting a miss): the caller reprojects the eye direction instead of a point.
UZ_ROUGH_INLINE float ThinLensDistance(float curvature, float inverseHitDistance) {
    float denominator = 2.0f * max(curvature, 0.0f) + max(inverseHitDistance, 0.0f);
    return denominator > 1e-6f ? 1.0f / denominator : -1.0f;
}
// History cap under camera motion with a trusted virtual reprojection: the static cap scaled by how far the
// lobe rotated since the previous frame relative to its own angular width, floored at today's motion cap.
UZ_ROUGH_INLINE float MotionCap(float roughness, float parallaxRadians, float power) {
    float keep = min(1.0f, max(0.0f, 1.0f - parallaxRadians / max(LobeHalfAngle(roughness), 1e-4f)));
    return max(4.0f, HistoryCap(roughness, 0.0f) * pow(keep, power));
}
// Short-history firefly limit: k x the reference level while the history count is below half the short
// history, relaxing 2x per frame after, off (a limit no sample reaches) from shortHistory on.
UZ_ROUGH_INLINE float FireflyLimit(float reference, float historyCount, float k, float shortHistory) {
    if (historyCount >= shortHistory) return 3.0e38f;
    return reference * k * exp2(max(0.0f, historyCount - 0.5f * shortHistory));
}
// Largest spatial tap radius (in pixels of world step worldStep) whose 25-tap support turns the reflected
// direction by less than the lobe's half angle on a receiver of curvature kappa: theta / (4 kappa s).
UZ_ROUGH_INLINE float CurvatureRadiusLimit(float roughness, float curvature, float worldStep) {
    float turnPerRadius = 4.0f * max(curvature, 0.0f) * max(worldStep, 0.0f);
    return turnPerRadius > 1e-6f ? LobeHalfAngle(roughness) / turnPerRadius : 1.0e4f;
}
#ifdef __cplusplus
}
#endif
#undef UZ_ROUGH_INLINE
#endif
