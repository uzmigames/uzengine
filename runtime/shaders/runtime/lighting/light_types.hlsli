/// UzEngine light data structures and attenuation.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — windowed inverse-square attenuation

#ifndef UZ_LIGHTING_LIGHT_TYPES_HLSLI
#define UZ_LIGHTING_LIGHT_TYPES_HLSLI

static const uint kMaxPointLights = 64;
static const uint kMaxSpotLights  = 16;
static const uint kLightFlag_InverseSquared = 1u;

struct PointLightData   // 64 bytes — matches C++ exactly
{
    float3 position;     float radius;
    float3 color;        float sourceRadius;
    float falloffExponent; float specularScale; float diffuseScale; uint flags;
    uint shadowIndex;    float3 _pad;
};

struct SpotLightData    // 80 bytes — matches C++ exactly
{
    float3 position;     float radius;
    float3 color;        float cosOuterAngle;
    float3 direction;    float invCosConeDifference;
    float falloffExponent; float specularScale; float diffuseScale; uint flags;
    uint shadowIndex;    float3 _spotPad;
};

// LightingCB at b2 — guarded so mesh_ps can include this without b2 conflict
#ifndef UZ_LIGHT_TYPES_NO_CB
cbuffer LightingCB : register(b2)
{
    PointLightData pointLights[kMaxPointLights]; // 4096 bytes
    uint pointLightCount;
    uint spotLightCount;
    uint2 _lightPad;
    SpotLightData spotLights[kMaxSpotLights];    // 1280 bytes
    float _lightPad2[60];                         // pad to 5632
};
#endif

/// @ref Karis 2013 — windowed inverse-square falloff
float DistanceAttenuation(float distSq, float invRadiusSq)
{
    float f = distSq * invRadiusSq;
    float s = saturate(1.0 - f * f);
    return s * s / max(distSq, 1e-4);
}

float SpotAngleAttenuation(float cosAngle, float cosOuterAngle, float invCosConeDiff)
{
    float t = saturate((cosAngle - cosOuterAngle) * invCosConeDiff);
    return t * t;
}

#endif
