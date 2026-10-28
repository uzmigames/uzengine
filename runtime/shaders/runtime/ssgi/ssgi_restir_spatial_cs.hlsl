/// Screen-space ReSTIR GI, spatial reuse and resolve: the pixel's reservoir (the temporal output)
/// combined with up to `g_spatialSamples` neighbours in a disk. Every neighbour's direction is
/// replayed from THIS surface (the gather's own ray function: its radiance here is what the
/// resolve uses) and this pixel's direction is replayed from every neighbour's surface, which is
/// all the pairwise MIS needs: 2K rays instead of the K(K+1) of the full balance heuristic.
/// @ref Bitterli et al. 2020 "Spatiotemporal Reservoir Resampling", pairwise MIS; Lin et al. 2022.
/// The resolved estimate `f W * rcIntensity` is written where the plain trace writes, so the
/// spatial denoise, temporal accumulation, upsample and deferred lighting consume it unchanged.
/// The spatial output never becomes temporal history.
///
/// Pairwise MIS with M weighting, K neighbours: for neighbour k's direction
///   m_k = M_k p_k / (K M_k p_k + M_c p_c),   for the canonical direction
///   m_c = (1/K) sum_k M_c p_c / (K M_k p_k + M_c p_c),
/// which partitions unity over the K+1 candidates and vanishes outside each source's support.
#include "ssgi_restir_gi.hlsli"

float SsgiRestirPairwiseMis(float numerator, float denominator) {
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

/// @constants 8x8 threads; at most 4 neighbours; 1% depth tolerance, 0.9 normal gate for a
/// neighbour; dimensions 311.. (disk) and 331.. (stream) of the hash.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 size = uint2(g_packedDimensions & 0xffffu, g_packedDimensions >> 16);
    if (any(id.xy >= size)) return;
    uint index = id.y * size.x + id.x;
    RWStructuredBuffer<SSGIReservoir> input = ResourceDescriptorHeap[g_reservoirInUav];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_outputUav];
    RWTexture2D<float> aoOutputTex = ResourceDescriptorHeap[g_aoOutputUav];

    SSGIReservoir canonical = input[index];
    SsgiRestirSurface surface = SsgiRestirSource(canonical);
    if (canonical.M == 0u) {
        // Sky (alpha 0), or an unsupported receiver (the gather's 0 with alpha 1): the plain
        // path's exact texels, AO 1 in world mode.
        const bool unsupported = (canonical.flags & UZ_SSGI_SURFACE_NO_GEOMETRIC_NORMAL) != 0u;
        outputTex[id.xy] = float4(0, 0, 0, unsupported ? 1.0 : 0.0);
        aoOutputTex[id.xy] = 1.0;
        return;
    }

    SSGIReservoir neighbours[4];
    float radius = asfloat(g_spatialRadiusBits);
    uint wanted = min(g_spatialSamples, 4u);
    uint K = 0u;
    for (uint i = 0u; i < wanted; ++i) {
        float r = radius * sqrt(SsgiRestirRandom(index, g_frameIndex, 311u + i * 2u));
        float phi = 2.0 * PI * SsgiRestirRandom(index, g_frameIndex, 312u + i * 2u);
        int2 pixel = int2(id.xy) + int2(round(r * float2(cos(phi), sin(phi))));
        if (any(pixel < 0) || any(pixel >= int2(size)) || all(pixel == int2(id.xy))) continue;
        SSGIReservoir neighbour = input[uint(pixel.y) * size.x + uint(pixel.x)];
        if (neighbour.M == 0u || !SsgiRestirCompatible(surface, SsgiRestirSource(neighbour), 0.01)) continue;
        neighbours[K++] = neighbour;
    }

    float canonicalHere = SsgiRestirOwnTarget(surface, canonical);
    float3 selectedRadiance = canonical.sampleRadiance;
    float3 selectedDirection = canonical.sampleDirection;
    float selectedTarget = canonicalHere;
    float weightSum = 0.0;
    float W;
    if (K == 0u) {
        W = canonical.W;
    } else {
        SdfReflectionScene scene = BuildSurfaceScene();
        const float Mc = float(canonical.M);
        // The canonical direction replayed at every neighbour: p_k(omega_c).
        float canonicalThere[4];
        for (uint k = 0u; k < K; ++k) {
            float3 unused; float unusedDistance;
            canonicalThere[k] = SsgiRestirReplayedTarget(scene, SsgiRestirSource(neighbours[k]),
                                                         canonical.sampleDirection, unused, unusedDistance);
        }
        float misCanonical = 0.0;
        for (uint k = 0u; k < K; ++k) {
            misCanonical += SsgiRestirPairwiseMis(Mc * canonicalHere,
                                                  float(K) * float(neighbours[k].M) * canonicalThere[k] + Mc * canonicalHere);
        }
        misCanonical /= float(K);
        weightSum = RestirReuseWeight(canonicalHere, canonical.W, misCanonical);
        // Each neighbour's direction replayed here: p_c(omega_k), and the radiance it returns from
        // this surface, which is what the resolve uses if it is selected.
        for (uint k = 0u; k < K; ++k) {
            float3 radianceHere; float distanceHere;
            float here = SsgiRestirReplayedTarget(scene, surface, neighbours[k].sampleDirection, radianceHere, distanceHere);
            float there = SsgiRestirOwnTarget(SsgiRestirSource(neighbours[k]), neighbours[k]);
            float Mk = float(neighbours[k].M);
            float mis = SsgiRestirPairwiseMis(Mk * there, float(K) * Mk * there + Mc * here);
            float weight = RestirReuseWeight(here, neighbours[k].W, mis);
            if (RestirStream(weightSum, weight, SsgiRestirRandom(index, g_frameIndex, 331u + k))) {
                selectedRadiance = radianceHere;
                selectedDirection = neighbours[k].sampleDirection;
                selectedTarget = here;
            }
        }
        W = RestirReuseFinalize(weightSum, selectedTarget);
    }
    float3 resolved = W > 0.0 ? selectedRadiance * SsgiRestirCosine(surface, selectedDirection) * W : 0.0;
    outputTex[id.xy] = float4(resolved * rcIntensity, 1.0);
    aoOutputTex[id.xy] = 1.0;
}
