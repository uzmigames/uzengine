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
    RestirSetSurface(r, surface);
    if (surface.valid != 0u && restirLightCount > 0u) {
        for (uint i = 0u; i < 32u; ++i) {
            uint d = i * 7u;
            LightSample s = (LightSample)0;
            uint configIndex = lights[0].padding;
            if (configIndex != 0u) {
                StructuredBuffer<LightBVHConfig> configurations = ResourceDescriptorHeap[configIndex - 1u];
                LightBVHConfig config = configurations[0];
                StructuredBuffer<LightBVHNode> nodes = ResourceDescriptorHeap[config.nodesSrv];
                StructuredBuffer<LightBVHLight> emitters = ResourceDescriptorHeap[config.lightsSrv];
                LightBVHSample chosen = SampleLightBVH(surface.position, surface.normal,
                    float3(RestirRandom(index, sampleIndex, d),
                           RestirRandom(index, sampleIndex, d + 1u),
                           RestirRandom(index, sampleIndex, d + 2u)), config, nodes, emitters);
                LightBVHLight light = emitters[chosen.lightId];
                s.lightIndex = chosen.lightId;
                s.emission = light.emission;
                if (light.type == 0u) {
                    // Fixed emitter-area coordinates: identity shift, Jacobian 1.
                    s.position = chosen.samplePos; s.normal = chosen.sampleNormal;
                    s.pdf = chosen.pdf;
                    if (light.emissiveSrv != 0xffffffffu) {
                        Texture2D<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(light.emissiveSrv)];
                        s.emission *= tex.SampleLevel(linearWrap, chosen.uv, 0).rgb;
                    }
                } else {
                    float3 L = normalize(light.v0), T, B;
                    PathBasis(L, T, B);
                    float radius = sqrt(RestirRandom(index, sampleIndex, d + 3u)) * tan(light.angularRadius);
                    float phi = 2.0 * PI * RestirRandom(index, sampleIndex, d + 4u);
                    s.position = normalize(L + radius * (cos(phi) * T + sin(phi) * B));
                    s.pdf = chosen.selectionPdf;
                }
            } else {
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
            }
            float target = RestirTarget(RestirContribution(surface, s));
            UpdateReservoir(r, s, target / s.pdf, RestirRandom(index, sampleIndex, d + 6u));
        }
        r.W = RestirWeight(r.weightSum, r.M, RestirTarget(RestirContribution(surface, r.sample)));
    }
    output[index] = r;
}
