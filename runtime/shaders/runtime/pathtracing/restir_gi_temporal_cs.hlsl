#define RESTIR_COMPUTE
#include "pathtracer_shared.hlsli"
#include "../include/restir_gi.hlsli"

/// @ref Lin et al. 2022 "Generalized Resampled Importance Sampling", Eqs. 4, 7, 22.
// Same discipline as restir_temporal_cs: the canonical candidate (M = 1, W =
// 1/p_A, or a null outcome that still owns its domain) is combined with the
// validated previous-frame reservoir using visible targets at BOTH surfaces.
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= restirWidth || id.y >= restirHeight) return;
    uint index = id.y * restirWidth + id.x;
    RWStructuredBuffer<GIReservoir> initial = ResourceDescriptorHeap[giRestirInitialUav];
    RWStructuredBuffer<GIReservoir> previous = ResourceDescriptorHeap[giRestirPreviousUav];
    RWStructuredBuffer<GIReservoir> output = ResourceDescriptorHeap[giRestirCurrentUav];
    GIReservoir r = initial[index];
    RestirSurface surface = GISource(r);
    // M = 1 whenever the primary surface receives: a missed or unsupported
    // continuation is a null sample, never an absent domain.
    r.M = surface.valid != 0u ? 1u : 0u;
    float selectedTarget = RestirVisibilityTarget(RestirTarget(GIContribution(surface, r.sample)), r.visibility);
    r.weightSum = selectedTarget * r.W;
    if (restirHistory != 0u && giRestirTemporal != 0u && r.M > 0u) {
        GIReservoir old = previous[index];
        float tolerance = max(0.01, length(surface.position - cameraPos.xyz) * 1e-4);
        if (old.M > 0u && dot(old.surfaceNormal, surface.normal) >= 0.9
            && abs(dot(old.surfacePosition - surface.position, surface.normal)) <= tolerance) {
            uint oldM = min(old.M, 19u);
            RestirSurface source = GISource(old);
            float currentHere = selectedTarget;
            float unusedVisibility, oldVisibilityHere;
            float currentThere = GIVisibleTarget(source, r.sample, unusedVisibility);
            float oldHere = GIVisibleTarget(surface, old.sample, oldVisibilityHere);
            float oldThere = RestirVisibilityTarget(RestirTarget(GIContribution(source, old.sample)), old.visibility);
            r.weightSum = RestirReuseWeight(currentHere, r.W,
                RestirMIS(currentHere, 1u, currentHere + float(oldM) * currentThere));
            float weight = RestirReuseWeight(oldHere, old.W,
                RestirMIS(oldThere, oldM, oldHere + float(oldM) * oldThere));
            if (RestirStream(r.weightSum, weight, RestirRandom(index, sampleIndex, 301u))) {
                r.sample = old.sample;
                r.visibility = oldVisibilityHere;
                selectedTarget = oldHere;
            }
            r.M += oldM;
        }
    }
    r.W = RestirReuseFinalize(r.weightSum, selectedTarget);
    output[index] = r;
}
