#ifndef UZ_RESTIR_DI
#define UZ_RESTIR_DI
#include "restir_math.hlsli"

struct LightSample {
    uint lightIndex;
    float3 position; // Direction for an infinitely distant directional source.
    float3 normal;
    float3 emission;
    float pdf; // Discrete light-selection density; source disk is normalized.
};
struct Reservoir {
    LightSample sample;
    float weightSum;
    uint M;
    float W;
    float3 surfacePosition;
    float3 surfaceNormal;
};
struct RestirSurface {
    float3 position; uint valid;
    float3 normal; float roughness;
    float3 view; float metallic;
    float3 albedo; float padding;
};
void UpdateReservoir(inout Reservoir r, LightSample s, float weight, float u) {
    ++r.M;
    if (RestirStream(r.weightSum, weight, u)) r.sample = s;
}
float RestirTarget(float3 contribution) {
    return dot(contribution, float3(0.2126, 0.7152, 0.0722));
}
float3 RestirDirection(float3 P, LightSample sample, out float distance) {
    distance = 1e7;
    return sample.position;
}
float3 RestirContribution(RestirSurface s, LightSample light) {
    if (restirLightCount == 0u) return 0.0;
    float distance;
    float3 L = RestirDirection(s.position, light, distance);
    return EvaluatePBR(s.normal, s.view, L, s.albedo, s.roughness, s.metallic) * light.emission;
}
// Independent dimensions/frame hashes avoid reusing correlated Halton rotations
// when a reservoir survives many frames.
float RestirRandom(uint pixel, uint frame, uint dimension) {
    uint x = pixel * 1973u + frame * 9277u + dimension * 26699u + 911u;
    x = (x ^ (x >> 16u)) * 0x7feb352du;
    x = (x ^ (x >> 15u)) * 0x846ca68bu;
    x ^= x >> 16u;
    return float(x >> 8u) / 16777216.0;
}
#endif
