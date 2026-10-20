#ifndef UZ_RESTIR_GI
#define UZ_RESTIR_GI
#include "restir_di.hlsli"
#include "light_bvh.hlsli"

// Pass-private b4, so the 24-DWORD PathTracerRoot and every DI consumer stay unchanged.
cbuffer RestirGIRoot : register(b4) {
    uint giRestirEnabled;
    uint giRestirInitialUav;
    uint giRestirCurrentUav;
    uint giRestirPreviousUav;
    uint giRestirPartialUav;
    uint giRestirRadianceUav;
    uint giRestirTemporal;
    uint giRestirSpatial;
};

/// @ref Ouyang et al. 2021 "ReSTIR GI", Lin et al. 2022 "Generalized Resampled Importance Sampling"
// One-bounce sample: the secondary vertex plus everything needed to re-evaluate
// its exitant radiance toward ANY receiving surface. Nothing here depends on
// the surface that generated it, so the identity shift on scene points has
// Jacobian 1 in area measure and reuse stays exact for the base BRDF.
struct GISample {
    float3 position; uint lightIndex;    // x2; BVH emitter id, 0xffffffff when none.
    float3 normal; float roughness;      // Shading normal facing its generating vertex.
    float3 albedo; float metallic;
    float3 emission; float lightPdf;     // Unweighted emission; NEE solid-angle pdf (0: no balance).
    float3 lightDirection;               // From x2 toward its single NEE light sample.
    float3 lightValue;                   // Emission * geometry / source pdf * visibility; 0 when none.
};
struct GIReservoir {
    GISample sample;
    float weightSum;
    uint M;
    float W;
    float3 surfacePosition;              // The continuation ray origin, not the hit point.
    float3 surfaceNormal;
    float3 surfaceView; float surfaceRoughness;
    float3 surfaceAlbedo; float surfaceMetallic;
    float visibility;                    // Selected sample at THIS source surface only.
};
bool GISampleValid(GISample s) {
    return any(s.normal != 0.0);
}
void GISetSurface(inout GIReservoir r, RestirSurface s) {
    r.surfacePosition = s.position; r.surfaceNormal = s.normal;
    r.surfaceView = s.view; r.surfaceRoughness = s.roughness;
    r.surfaceAlbedo = s.albedo; r.surfaceMetallic = s.metallic;
}
RestirSurface GISource(GIReservoir r) {
    RestirSurface s = (RestirSurface)0;
    s.position = r.surfacePosition; s.normal = r.surfaceNormal;
    s.view = r.surfaceView; s.roughness = r.surfaceRoughness;
    s.albedo = r.surfaceAlbedo; s.metallic = r.surfaceMetallic;
    s.valid = r.M > 0u;
    return s;
}
// Exitant radiance of the secondary vertex toward the receiving surface:
// emission with the receiver's own BSDF/NEE balance, plus the stored NEE
// sample shaded with the vertex BSDF toward that receiver. Deterministic in
// (surface, sample); the NEE variates' law depends only on the vertex.
float3 GIRadiance(RestirSurface s, GISample y, float3 omega, float d2) {
    float3 toReceiver = -omega;
    float3 radiance = y.emission;
    if (y.lightIndex != 0xffffffffu && any(y.emission > 0.0)) {
        StructuredBuffer<PathDirectionalLight> directions = ResourceDescriptorHeap[directionalLightsSrv];
        uint configIndex = directions[0].padding;
        if (configIndex != 0u) {
            StructuredBuffer<LightBVHConfig> configurations = ResourceDescriptorHeap[configIndex - 1u];
            LightBVHConfig config = configurations[0];
            StructuredBuffer<LightBVHNode> nodes = ResourceDescriptorHeap[config.nodesSrv];
            StructuredBuffer<LightBVHLight> lights = ResourceDescriptorHeap[config.lightsSrv];
            LightBVHLight light = lights[y.lightIndex];
            // Same emitter-cosine convention as the closest-hit emitter MIS.
            float cosine = abs(dot(light.normal, toReceiver));
            if (cosine > 0.0) {
                float selection = EvaluateLightBVHPDF(s.position, s.normal, y.lightIndex, config, nodes, lights);
                float lightPdf = LightBVHSolidAnglePDF(selection, light.area, d2, cosine);
                radiance *= LightBVHBalanceWeight(RestirBRDFPDF(s, omega), float(neeSamples) * lightPdf);
            }
        }
    }
    if (any(y.lightValue > 0.0)) {
        RestirSurface vertex = (RestirSurface)0;
        vertex.position = y.position; vertex.normal = y.normal; vertex.view = toReceiver;
        vertex.roughness = y.roughness; vertex.metallic = y.metallic; vertex.albedo = y.albedo;
        float balance = y.lightPdf > 0.0
            ? LightBVHBalanceWeight(float(neeSamples) * y.lightPdf, RestirBRDFPDF(vertex, y.lightDirection))
            : 1.0;
        radiance += EvaluatePBR(y.normal, toReceiver, y.lightDirection, y.albedo, y.roughness, y.metallic)
                  * y.lightValue * balance;
    }
    return radiance;
}
// Area-measure one-bounce integrand at the receiving surface, without visibility.
// The canonical candidate reproduces the plain estimator exactly:
// GIContribution * (d^2 / (pdf cos2)) = f cos L_o / pdf.
float3 GIContribution(RestirSurface s, GISample y) {
    if (!GISampleValid(y)) return 0.0;
    float3 delta = y.position - s.position;
    float d2 = dot(delta, delta);
    if (d2 <= 0.01) return 0.0;
    float3 omega = delta * rsqrt(d2);
    float cos2 = dot(y.normal, -omega);
    if (cos2 <= 0.0) return 0.0;
    float3 f = EvaluatePBR(s.normal, s.view, omega, s.albedo, s.roughness, s.metallic);
    if (!any(f > 0.0)) return 0.0;
    return f * GIRadiance(s, y, omega, d2) * (cos2 / d2);
}
#ifdef RESTIR_COMPUTE
// Same origin, flags, mask and endpoint as the continuation ray that generated
// the vertex, so a source's own sample is always visible from that source.
float GIVisibility(RestirSurface surface, GISample y) {
    float3 delta = y.position - surface.position;
    float distance = length(delta);
    RayDesc ray;
    ray.Origin = surface.position;
    ray.Direction = delta / distance;
    ray.TMin = 0.01; ray.TMax = max(distance - 0.01, 0.01);
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(tRestirScene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed()) {}
    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
}
float GIVisibleTarget(RestirSurface surface, GISample y, out float visibility) {
    float target = RestirTarget(GIContribution(surface, y));
    visibility = target > 0.0 ? GIVisibility(surface, y) : 0.0;
    return RestirVisibilityTarget(target, visibility);
}
#endif
#endif
