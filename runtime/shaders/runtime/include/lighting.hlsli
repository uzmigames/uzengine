/// Forward lighting — stub. Will be implemented at Step 7.
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI
#include "pbr_common.hlsli"
#include "light_types.hlsli"

float3 AccumulateDirectLighting(float3 wp, float3 N, float3 V, float NdotV,
    float3 alb, float met, float alpha, float3 F0,
    uint count, LightData lights[MAX_LIGHTS],
    float3 cam, uint cascCount, float2 pp) {
    // Stub: just first light NdotL * color
    if (count > 0) {
        float3 L = normalize(lights[0].direction.xyz);
        return EvaluateBRDF(N, V, L, lights[0].color.xyz, alb, met, alpha, F0);
    }
    return float3(0,0,0);
}

#endif
