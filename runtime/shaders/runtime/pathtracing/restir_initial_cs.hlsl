#define RESTIR_COMPUTE
#include "pathtracer_shared.hlsli"
#include "../include/restir_di.hlsli"

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= restirWidth || id.y >= restirHeight) return;
    uint index = id.y * restirWidth + id.x;
    RWStructuredBuffer<RestirSurface> surfaces = ResourceDescriptorHeap[restirSurfaceUav];
    RWStructuredBuffer<Reservoir> output = ResourceDescriptorHeap[restirInitialUav];
    StructuredBuffer<PathDirectionalLight> lights = ResourceDescriptorHeap[directionalLightsSrv];
    RestirSurface surface = surfaces[index];
    Reservoir r = (Reservoir)0;
    r.surfacePosition = surface.position;
    r.surfaceNormal = surface.normal;
    if (surface.valid != 0u && restirLightCount > 0u) {
        for (uint i = 0u; i < 32u; ++i) {
            uint d = i * 5u;
            LightSample s = (LightSample)0;
            s.lightIndex = min(uint(RestirRandom(index, sampleIndex, d)
                               * restirLightCount), restirLightCount - 1u);
            PathDirectionalLight light = lights[s.lightIndex];
            s.emission = light.radiance;
            s.pdf = 1.0 / float(restirLightCount);
            float3 L = normalize(light.direction), T, B;
            PathBasis(L, T, B);
            float radius = sqrt(RestirRandom(index, sampleIndex, d + 1u)) * tan(light.angularRadius);
            float phi = 2.0 * PI * RestirRandom(index, sampleIndex, d + 2u);
            s.position = normalize(L + radius * (cos(phi) * T + sin(phi) * B));
            float target = RestirTarget(RestirContribution(surface, s));
            UpdateReservoir(r, s, target / s.pdf, RestirRandom(index, sampleIndex, d + 4u));
        }
        r.W = RestirWeight(r.weightSum, r.M, RestirTarget(RestirContribution(surface, r.sample)));
    }
    output[index] = r;
}
