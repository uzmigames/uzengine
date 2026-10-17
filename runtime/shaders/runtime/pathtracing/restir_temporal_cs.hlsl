#define RESTIR_COMPUTE
#include "pathtracer_shared.hlsli"
#include "../include/restir_di.hlsli"

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= restirWidth || id.y >= restirHeight) return;
    uint index = id.y * restirWidth + id.x;
    RWStructuredBuffer<RestirSurface> surfaces = ResourceDescriptorHeap[restirSurfaceUav];
    RWStructuredBuffer<Reservoir> initial = ResourceDescriptorHeap[restirInitialUav];
    RWStructuredBuffer<Reservoir> previous = ResourceDescriptorHeap[restirPreviousUav];
    RWStructuredBuffer<Reservoir> output = ResourceDescriptorHeap[restirCurrentUav];
    RestirSurface surface = surfaces[index];
    Reservoir r = initial[index];
    // Initial RIS represents 32 candidates. Temporal M measures independent
    // frame reservoirs, capped to 20 frames, not 20 of those 32 candidates.
    r.M = surface.valid != 0u && restirLightCount > 0u ? 1u : 0u;
    r.weightSum = RestirCombineWeight(RestirTarget(RestirContribution(surface, r.sample)), r.W, r.M);
    if (restirHistory != 0u && restirTemporal != 0u && r.M > 0u) {
        // The path-tracer gate invalidates on ANY camera/object change.
        // The same pixel owns the history. Subpixel jitter moves hits along
        // the surface, so validate point-to-plane separation, not hit depth.
        {
            Reservoir old = previous[index];
            float tolerance = max(0.01, length(surface.position - cameraPos.xyz) * 1e-4);
            if (old.M > 0u && dot(old.surfaceNormal, surface.normal) >= 0.9
                && abs(dot(old.surfacePosition - surface.position, surface.normal)) <= tolerance) {
                uint oldM = min(old.M, 19u);
                float target = RestirTarget(RestirContribution(surface, old.sample));
                float weight = RestirCappedSum(RestirCombineWeight(target, old.W, old.M), old.M, 19u);
                if (RestirStream(r.weightSum, weight, RestirRandom(index, sampleIndex, 137u)))
                    r.sample = old.sample;
                r.M += oldM;
            }
        }
    }
    r.W = RestirWeight(r.weightSum, r.M, RestirTarget(RestirContribution(surface, r.sample)));
    output[index] = r;
}
