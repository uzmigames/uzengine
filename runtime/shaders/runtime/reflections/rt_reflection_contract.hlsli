// Shared CPU/shader contract for the Stage 1 mirror ray and fallback mask.
#ifndef UZ_RT_REFLECTION_CONTRACT
#define UZ_RT_REFLECTION_CONTRACT

#ifdef __cplusplus
#include "vector3.h"
namespace uz::rtreflection {
using float3 = Vec3;
#endif

static const float kReflectionRayTMin = 1.0f;
static const float kReflectionSurfaceBias = 2.0f;

// incident and normal are unit vectors; incident points toward the surface.
float3 ReflectionDirection(float3 incident, float3 normal) {
    float d = incident.x * normal.x + incident.y * normal.y + incident.z * normal.z;
    return incident - normal * (2.0f * d);
}

bool ReflectionEligible(float deviceZ, float roughness, float maxRoughness) {
    return deviceZ > 0.0001f && roughness <= maxRoughness;
}

// Use the current screen trace, not temporally reprojected history, to avoid
// retaining a reflection after its source has left the screen.
bool ReflectionNeedsRT(float deviceZ, float roughness, float maxRoughness,
                       float screenConfidence) {
    return ReflectionEligible(deviceZ, roughness, maxRoughness)
        && screenConfidence <= 0.001f;
}

bool ReflectionHitUsable(bool triangleHit, float surfaceDistance, float normalLengthSquared) {
    return triangleHit && surfaceDistance < 1e8f && normalLengthSquared > 0.5f;
}

#ifdef __cplusplus
} // namespace uz::rtreflection
#endif
#endif
