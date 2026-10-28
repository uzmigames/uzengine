/// Screen-space ReSTIR GI, temporal reuse: the initial reservoir (M = N candidates) combined with
/// the previous frame's reservoir at the reprojected pixel, validated on the receiver plane and
/// capped at `g_maxHistory`, with the committed GRIS discipline of restir_gi_temporal_cs:
/// targets at BOTH domains, generalized balance MIS `m_i = M_i p_i / sum_j M_j p_j`.
/// @ref Lin et al. 2022 "Generalized Resampled Importance Sampling", Eqs. 4, 7, 22.
///
/// A sample is a direction: evaluating it at the other domain replays it from that surface with
/// the gather's own ray function, so the previous reservoir's direction adopted here carries the
/// radiance it returns from HERE. When the two surfaces are bit-identical (a static camera) the
/// replay is the cached trace, so the pass then costs no extra rays.
/// The canonical reservoir keeps M = N even when every candidate missed: a null sample still
/// owns its domain, so the history's MIS weight is never scaled by the hit rate (the negative
/// control of test_ssgi_restir_gi.cpp).
#include "ssgi_restir_gi.hlsli"

/// @constants 8x8 threads; nearest reprojected tap; dimension 301 of the hash for the stream.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 size = uint2(g_packedDimensions & 0xffffu, g_packedDimensions >> 16);
    if (any(id.xy >= size)) return;
    uint index = id.y * size.x + id.x;
    RWStructuredBuffer<SSGIReservoir> initial = ResourceDescriptorHeap[g_reservoirInUav];
    RWStructuredBuffer<SSGIReservoir> previous = ResourceDescriptorHeap[g_historyUav];
    RWStructuredBuffer<SSGIReservoir> output = ResourceDescriptorHeap[g_reservoirOutUav];
    Texture2D<float> depthTex = ResourceDescriptorHeap[g_depthSrv];
    uint2 fullSize;
    depthTex.GetDimensions(fullSize.x, fullSize.y);

    SSGIReservoir r = initial[index];
    SsgiRestirSurface surface = SsgiRestirSource(r);
    uint candidates = r.M;
    float selectedTarget = SsgiRestirOwnTarget(surface, r);
    // The initial reservoir enters the combine as one sample with contribution weight W:
    // w = p_hat W (MIS 1 when nothing is reused), so the finalize returns W unchanged.
    r.weightSum = selectedTarget * r.W;
    uint2 previousPixel;
    if ((g_flags & UZ_SSGI_RESTIR_HISTORY_VALID) != 0u && candidates > 0u
        && !SsgiRestirInChangedRegion(surface.position)
        && SsgiRestirReproject(surface.position, size, fullSize, previousPixel)) {
        SSGIReservoir old = previous[previousPixel.y * size.x + previousPixel.x];
        SsgiRestirSurface source = SsgiRestirSource(old);
        if (old.M > 0u && SsgiRestirCompatible(surface, source, 0.001)) {
            SdfReflectionScene scene = BuildSurfaceScene();
            uint oldM = min(old.M, max(g_maxHistory, 1u));
            float currentHere = selectedTarget;
            float oldThere = SsgiRestirOwnTarget(source, old);
            float currentThere, oldHere;
            float3 oldRadianceHere;
            float oldDistanceHere;
            if (SsgiRestirSameSurface(r, old)) {
                // The same rays from the same surface: the stored evaluations are the replays.
                currentThere = currentHere;
                oldHere = oldThere;
                oldRadianceHere = old.sampleRadiance;
                oldDistanceHere = old.sampleDistance;
            } else {
                float3 unused; float unusedDistance;
                currentThere = SsgiRestirReplayedTarget(scene, source, r.sampleDirection, unused, unusedDistance);
                oldHere = SsgiRestirReplayedTarget(scene, surface, old.sampleDirection, oldRadianceHere, oldDistanceHere);
            }
            // The canonical domain holds N candidates: its MIS share is N p_here against
            // N p_here + M_old p_there, and the history's is M_old p_there against
            // N p_here(old) + M_old p_there(old).
            r.weightSum = RestirReuseWeight(currentHere, r.W,
                RestirMIS(currentHere, candidates, float(candidates) * currentHere + float(oldM) * currentThere));
            float weight = RestirReuseWeight(oldHere, old.W,
                RestirMIS(oldThere, oldM, float(candidates) * oldHere + float(oldM) * oldThere));
            if (RestirStream(r.weightSum, weight, SsgiRestirRandom(index, g_frameIndex, 301u))) {
                r.sampleDirection = old.sampleDirection;
                r.flags = (r.flags & ~UZ_SSGI_SAMPLE_KIND_MASK) | SsgiRestirKindOfDistance(oldDistanceHere);
                r.sampleRadiance = oldRadianceHere;
                r.sampleDistance = oldDistanceHere;
                selectedTarget = oldHere;
            }
            r.M += oldM;
        }
    }
    r.W = RestirReuseFinalize(r.weightSum, selectedTarget);
    output[index] = r;
}
