/// Screen-space ReSTIR GI, initial candidates: the N cosine rays of the surface final gather
/// streamed into one reservoir per half-resolution pixel (Talbot et al. 2005, Talbot's RIS).
/// @ref Ouyang et al. 2021 "ReSTIR GI", initial sampling; Lin et al. 2022 "GRIS", Eq. 4.
///
/// Rays, tangent frame, rotation and per-ray radiance are the gather's own (sdf_diffuse_gather.hlsli:
/// GatherSdfDiffuseIdentified), so every candidate is a ray the plain path would have averaged.
/// Candidate weight: target over source density, `lum(L) cos/pi / (cos/pi) = lum(L)` on the
/// hemisphere and 0 off it; `W = sum / (N p_hat)`. The canonical estimate `f W` therefore has
/// exactly the plain mean's luminance, `sum_i lum(L_i) / N`, whatever candidate is selected.
#include "ssgi_restir_gi.hlsli"

/// @constants 8x8 threads; up to 32 candidates (the gather's clamp); dimensions 100.. of the hash.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 size = uint2(g_packedDimensions & 0xffffu, g_packedDimensions >> 16);
    if (any(id.xy >= size)) return;
    int2 pixel = int2(id.xy);
    uint index = id.y * size.x + id.x;
    Texture2D<float> depthTex = ResourceDescriptorHeap[g_depthSrv];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[g_normalSrv];
    RWStructuredBuffer<SSGIReservoir> reservoirs = ResourceDescriptorHeap[g_reservoirOutUav];

    uint2 fullSize;
    depthTex.GetDimensions(fullSize.x, fullSize.y);
    int2 fullPx = SurfacePixel(pixel, fullSize);
    float depth = depthTex.Load(int3(fullPx, 0));
    SSGIReservoir r = (SSGIReservoir)0;
    r.sampleDistance = -2.0;
    if (IsSkyDepth(depth)) { reservoirs[index] = r; return; }

    float3 N = OctDecode(normalTex.Load(int3(fullPx, 0)).rg);
    float3 worldPos = SurfacePosition(fullPx, depth, fullSize);
    float3 geometricNormal = SurfaceGeometricNormal(depthTex, fullPx, fullSize, depth, worldPos);
    SdfReflectionScene scene = BuildSurfaceScene();
    float3 origin;
    bool resolved = ResolveSdfRayOrigin(scene, worldPos, geometricNormal, origin);
    SsgiRestirSurface surface;
    surface.position = origin;
    surface.normal = N;
    surface.geometricNormal = geometricNormal;
    SsgiRestirSetSurface(r, surface);
    // An unsupported receiver (the gather returns 0 there): no domain, nothing for neighbours to
    // reuse into, resolved as 0 like the plain path.
    if (!resolved) { reservoirs[index] = r; return; }
    // Targets are evaluated on the stored (octahedral 2x16) surface, the one every later pass
    // unpacks, so a reservoir's W cancels its own target exactly across passes. The rays
    // themselves keep the gather's exact frame and geometric normal below.
    surface = SsgiRestirSource(r);

    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    uint count = clamp(g_candidates, 1u, 32u);
    float2 rotation = SdfDiffuseRotation(uint2(fullPx), g_frameIndex, sdfSampleRotation);
    float selectedTarget = 0.0;
    [loop] for (uint i = 0u; i < count; ++i) {
        float pdf;
        float3 local = SampleCosineHemisphere(frac(Hammersley(i, count) + rotation), pdf);
        float3 direction = tangent * local.x + bitangent * local.y + N * local.z;
        float distance;
        float3 radiance = TraceSdfSurfaceRadianceIdentified(scene, origin, direction, 0u, geometricNormal, distance);
        float cosine = SsgiRestirCosine(surface, direction);
        float target = RestirTarget(radiance) * cosine;
        // target / source density, the cosine term being common to both (restir_math:
        // RestirCandidateWeight without the division that would only add rounding).
        float weight = cosine > 0.0 ? RestirTarget(radiance) : 0.0;
        if (RestirStream(r.weightSum, weight, SsgiRestirRandom(index, g_frameIndex, 100u + i))) {
            r.sampleDirection = direction;
            r.flags = (r.flags & ~UZ_SSGI_SAMPLE_KIND_MASK) | SsgiRestirKindOfDistance(distance);
            r.sampleRadiance = radiance;
            r.sampleDistance = distance;
            selectedTarget = target;
        }
    }
    r.M = count;
    r.W = RestirWeight(r.weightSum, count, selectedTarget);
    reservoirs[index] = r;
}
