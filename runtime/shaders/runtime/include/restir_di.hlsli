#ifndef UZ_RESTIR_DI
#define UZ_RESTIR_DI
#include "restir_math.hlsli"
#include "light_bvh.hlsli"

struct LightSample {
    uint lightIndex;
    float3 position; // Direction for an infinitely distant directional source.
    float3 normal;
    float3 emission;
    float pdf; // Source joint area density, or discrete normalized-disk density.
};
struct Reservoir {
    LightSample sample;
    float weightSum;
    uint M;
    float W;
    float3 surfacePosition;
    float3 surfaceNormal;
    float3 surfaceView; float surfaceRoughness;
    float3 surfaceAlbedo; float surfaceMetallic;
    float visibility; // Selected sample at THIS source surface, never a neighbor's visibility.
};
struct RestirSurface {
    float3 position; uint valid;
    float3 normal; float roughness;
    float3 view; float metallic;
    float3 albedo; float padding;
};
void RestirSetSurface(inout Reservoir r, RestirSurface s) {
    r.surfacePosition = s.position; r.surfaceNormal = s.normal;
    r.surfaceView = s.view; r.surfaceRoughness = s.roughness;
    r.surfaceAlbedo = s.albedo; r.surfaceMetallic = s.metallic;
}
RestirSurface RestirSource(Reservoir r) {
    RestirSurface s = (RestirSurface)0;
    s.position = r.surfacePosition; s.normal = r.surfaceNormal;
    s.view = r.surfaceView; s.roughness = r.surfaceRoughness;
    s.albedo = r.surfaceAlbedo; s.metallic = r.surfaceMetallic;
    s.valid = r.M > 0u;
    return s;
}
void UpdateReservoir(inout Reservoir r, LightSample s, float weight, float u) {
    ++r.M;
    if (RestirStream(r.weightSum, weight, u)) r.sample = s;
}
float RestirTarget(float3 contribution) {
    return dot(contribution, float3(0.2126, 0.7152, 0.0722));
}
float3 RestirDirection(float3 P, LightSample sample, out float distance) {
    distance = 1e7;
    if (any(sample.normal != 0.0)) {
        float3 delta = sample.position - P;
        distance = length(delta);
        return delta / max(distance, 1e-10);
    }
    return sample.position;
}
// Same base-material density as PathBRDFPDF; compute shaders cannot include
// the DXR-only sampling functions. This does not alter the base BRDF.
float RestirBRDFPDF(RestirSurface s, float3 L) {
    float NoL = saturate(dot(s.normal, L));
    if (NoL <= 0.0) return 0.0;
    float3 H = normalize(s.view + L);
    float alpha = RoughnessToAlpha(s.roughness);
    float NoV = max(dot(s.normal, s.view), 1e-6);
    float specular = D_GGX(alpha, saturate(dot(s.normal, H)))
                   * G1_SmithGGX(alpha * alpha, NoV) / (4.0 * NoV);
    float probability = lerp(0.5, 1.0, s.metallic);
    return probability * specular + (1.0 - probability) * NoL / PI;
}
float3 RestirContribution(RestirSurface s, LightSample light) {
    if (restirLightCount == 0u) return 0.0;
    float distance;
    float3 L = RestirDirection(s.position, light, distance);
    float3 value = light.emission;
    if (any(light.normal != 0.0)) {
        if (!PathCanContinue(1u, maxBounces) || distance <= 0.1) return 0.0;
        float cosine = abs(dot(light.normal, -L));
        if (cosine <= 0.0) return 0.0;
        value *= cosine / (distance * distance);
        StructuredBuffer<PathDirectionalLight> directions = ResourceDescriptorHeap[directionalLightsSrv];
        StructuredBuffer<LightBVHConfig> configurations = ResourceDescriptorHeap[directions[0].padding - 1u];
        LightBVHConfig config = configurations[0];
        StructuredBuffer<LightBVHNode> nodes = ResourceDescriptorHeap[config.nodesSrv];
        StructuredBuffer<LightBVHLight> lights = ResourceDescriptorHeap[config.lightsSrv];
        float selection = EvaluateLightBVHPDF(s.position, s.normal, light.lightIndex, config, nodes, lights);
        float pdf = LightBVHSolidAnglePDF(selection, lights[light.lightIndex].area,
                                          distance * distance, cosine);
        // Preserve the existing NEE/BSDF partition at every receiving surface.
        // The emitter-hit path retains its complementary BSDF MIS term.
        value *= LightBVHBalanceWeight(float(neeSamples) * pdf, RestirBRDFPDF(s, L));
    }
    return EvaluatePBR(s.normal, s.view, L, s.albedo, s.roughness, s.metallic) * value;
}
#ifdef RESTIR_COMPUTE
RaytracingAccelerationStructure tRestirScene : register(t0);
float RestirVisibility(RestirSurface surface, LightSample sample) {
    StructuredBuffer<PathDirectionalLight> lights = ResourceDescriptorHeap[directionalLightsSrv];
    // Preserve the BVH-off stage-2 estimator and its exact random sequence.
    if (lights[0].padding == 0u) return 1.0;
    RayDesc ray;
    ray.Origin = surface.position + surface.normal * 0.05;
    float distance;
    ray.Direction = RestirDirection(surface.position, sample, distance);
    ray.TMin = 0.01; ray.TMax = max(distance - 0.05, 0.01);
    if (any(sample.normal != 0.0)) {
        float3 delta = sample.position - ray.Origin;
        distance = length(delta);
        ray.Direction = delta / distance;
        ray.TMax = max(distance - 0.01, 0.01);
    }
    // Match final DXR shading, including the offset-origin emitter endpoint.
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(tRestirScene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed()) {}
    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
}
float RestirVisibleTarget(RestirSurface surface, LightSample sample, out float visibility) {
    float target = RestirTarget(RestirContribution(surface, sample));
    visibility = target > 0.0 ? RestirVisibility(surface, sample) : 0.0;
    return RestirVisibilityTarget(target, visibility);
}
#endif
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
