#define RESTIR_COMPUTE
#include "pathtracer_shared.hlsli"
#include "../include/restir_gi.hlsli"

/// @ref Lin et al. 2022 "Generalized Resampled Importance Sampling", Eqs. 4, 7, 22.
/// @constants 4 neighbors in a 30-pixel disk; 0.9 normal gate; 1% depth tolerance.
// Mirrors restir_spatial_cs with the one-bounce reservoir: the identity shift
// on scene points has Jacobian 1 in area measure, targets include exact
// visibility at EVERY domain, cached visibility applies only to a sample's own
// source, and the canonical pixel covers the receiving support.
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= restirWidth || id.y >= restirHeight) return;
    uint index = id.y * restirWidth + id.x;
    RWStructuredBuffer<GIReservoir> input = ResourceDescriptorHeap[giRestirCurrentUav];
    RWStructuredBuffer<GIReservoir> output = ResourceDescriptorHeap[giRestirInitialUav];
    GIReservoir candidates[5];
    candidates[0] = input[index];
    RestirSurface surface = GISource(candidates[0]);
    uint count = 1u;
    if (surface.valid != 0u) {
        for (uint i = 0u; i < 4u; ++i) {
            float radius = 30.0 * sqrt(RestirRandom(index, sampleIndex, 311u + i * 2u));
            float phi = 2.0 * PI * RestirRandom(index, sampleIndex, 312u + i * 2u);
            int2 pixel = int2(id.xy) + int2(round(radius * float2(cos(phi), sin(phi))));
            if (any(pixel < 0) || pixel.x >= int(restirWidth) || pixel.y >= int(restirHeight)) continue;
            GIReservoir neighbor = input[uint(pixel.y) * restirWidth + uint(pixel.x)];
            float tolerance = max(0.01, length(surface.position - cameraPos.xyz) * 0.01);
            if (neighbor.M == 0u || dot(neighbor.surfaceNormal, surface.normal) < 0.9
                || abs(dot(neighbor.surfacePosition - surface.position, surface.normal)) > tolerance) continue;
            candidates[count++] = neighbor;
        }
    }
    GIReservoir result = candidates[0];
    result.weightSum = 0.0;
    result.M = 0u;
    float selectedTarget = 0.0;
    for (uint i = 0u; i < count; ++i) {
        float targetSum = 0.0;
        float sourceTarget = 0.0;
        float here = 0.0, visibilityHere = 0.0;
        for (uint j = 0u; j < count; ++j) {
            float visibility = candidates[i].visibility;
            RestirSurface source = GISource(candidates[j]);
            float target;
            if (j == i)
                target = RestirVisibilityTarget(RestirTarget(GIContribution(source, candidates[i].sample)), visibility);
            else
                target = GIVisibleTarget(source, candidates[i].sample, visibility);
            targetSum += float(candidates[j].M) * target;
            if (j == i) sourceTarget = target;
            if (j == 0u) { here = target; visibilityHere = visibility; }
        }
        float weight = RestirReuseWeight(here, candidates[i].W,
            RestirMIS(sourceTarget, candidates[i].M, targetSum));
        if (RestirStream(result.weightSum, weight, RestirRandom(index, sampleIndex, 331u + i))) {
            result.sample = candidates[i].sample;
            result.visibility = visibilityHere;
            selectedTarget = here;
        }
        result.M += candidates[i].M;
    }
    result.W = RestirReuseFinalize(result.weightSum, selectedTarget);
    // Spatial output never becomes temporal history.
    output[index] = result;
}
