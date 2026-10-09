/// UzEngine DDGI Probe Trace — one thread per probe ray (block B.6, 2026-10-07).
///
/// Traces every probe's `raysPerProbe` rays once per frame, shades the hit with the sun (and,
/// optionally, with the previous frame's irradiance at the hit: the second bounce the SDF
/// radiance cache has always carried) and writes the result to two structured buffers:
///
///   rayRadiance[probe * nRays + i] = (radiance.rgb, hitT)
///   rayInfo    [probe * nRays + i] = (lit, albedo luminance, front-facing, sun visibility)
///
/// `ddgi_probe_update_cs.hlsl` then integrates the buffers into the irradiance and depth
/// atlases, one thread per atlas texel. Before this pass existed the update shader traced the
/// same rays in every texel of the probe's tiles: 36 irradiance + 256 depth texels per probe,
/// each tracing the probe's 32 rays, 292 traces of every ray (RadianceCache_Trace 3.57 ms on
/// Sponza at 16^3 probes, measured 2026-10-07 before this change).
///
/// Dispatch X = ceil(totalProbes * nRays / 64), Y = Z = 1.
///
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes"
///      — sections 5.2-5.3, the indirect term from the previous frame's probes
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

SamplerState linearClamp : register(s2);

// ─── Root constants at b0 ──────────────────────────────────────────────────
//
// The first eight DWORDs are the update shader's, so the host fills one array for both.

cbuffer RootConstants : register(b0) {
    uint g_irradAtlasUav;      ///< Bindless UAV of the irradiance atlas: read (previous frame) for the indirect term.
    uint g_depthAtlasUav;      ///< Unused here (update shader slot).
    uint g_tlasSrvIndex;       ///< Bindless SRV index of the scene TLAS.
    uint g_atlasMode;          ///< Unused here (update shader slot).
    uint g_frameIndex;         ///< Frame counter for the per-frame ray rotation.
    uint g_probesPerAxis;      ///< Probes per axis (cube grid).
    uint g_probeOffset;        ///< Reserved for temporal probe spreading.
    uint g_skyRadianceSrv;     ///< Sky radiance cubemap (kSentinelSrvIndex = flat sky colour).
    uint g_rayRadianceUav;     ///< Bindless UAV of the (radiance, hitT) ray buffer.
    uint g_rayInfoUav;         ///< Bindless UAV of the hit-info ray buffer.
    uint g_indirectTerm;       ///< 1 = add albedo x previous irradiance at the hit (r.GI.DDGI.IndirectTerm).
    uint g_historyValid;       ///< 1 = the irradiance atlas holds a completed update (first frame: 0).
};

#include "ddgi_probe_common.hlsli"

// ─── Previous-frame irradiance at a hit point ────────────────────────────
//
// Mirrors `SamplePreviousDiffuse` in radiance_cache_trace_cs.hlsl: the eight probes around the
// point, trilinear weight times the squared normal weight (a probe behind the surface gets no
// vote), zero outside the grid. The atlas is read through its UAV (it is in that state while the
// writers run) at the texel the hit normal maps to in each probe's 6x6 octahedral tile; the
// atlas stores the previous frame's completed EMA (this pass runs before the update pass).
float3 SamplePreviousIrradiance(float3 position, float3 normal) {
    const float3 origin = float3(gridOriginX, gridOriginY, gridOriginZ);
    float3 cell = (position - origin) / probeSpacing;
    if (any(cell < 0.0) || any(cell > float(probesPerAxis - 1))) return float3(0.0, 0.0, 0.0);
    const uint3 lower = uint3(floor(cell));
    const uint3 upper = min(lower + 1, probesPerAxis - 1);
    const float3 fraction = frac(cell);

    RWTexture2D<float4> irradTex = ResourceDescriptorHeap[g_irradAtlasUav];
    // Texel of the normal's direction inside the tile (the lighting samples the same mapping
    // bilinearly; the writer's own history read uses the nearest texel).
    const float2 oct = OctEncodeDir(normal);
    const uint2 texel = min(uint2(oct * float(kIrradProbeSize)), uint2(kIrradProbeSize - 1u, kIrradProbeSize - 1u));

    float3 irradiance = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    [unroll]
    for (uint corner = 0; corner < 8; ++corner) {
        const uint3 index = uint3((corner & 1) ? upper.x : lower.x,
                                  (corner & 2) ? upper.y : lower.y,
                                  (corner & 4) ? upper.z : lower.z);
        float weight = ((corner & 1) ? fraction.x : 1.0 - fraction.x)
                     * ((corner & 2) ? fraction.y : 1.0 - fraction.y)
                     * ((corner & 4) ? fraction.z : 1.0 - fraction.z);
        const float3 probePosition = origin + float3(index) * probeSpacing;
        const float3 toProbe = probePosition - position;
        const float dist = length(toProbe);
        const float normalWeight = dist > 0.0 ? saturate(dot(normal, toProbe / dist)) : 1.0;
        weight *= normalWeight * normalWeight;
        if (weight <= 0.0) continue;
        const uint probeLinear = index.x + index.y * probesPerAxis + index.z * probesPerAxis * probesPerAxis;
        const float4 stored = irradTex[ProbeAtlasOrigin(probeLinear, kIrradProbeSize) + texel];
        if (stored.w <= 0.0) continue;       // never written (the writer stores alpha 1)
        irradiance += weight * stored.rgb;
        totalWeight += weight;
    }
    return totalWeight > 0.0 ? irradiance / totalWeight : float3(0.0, 0.0, 0.0);
}

// ─── Entry point — one thread per probe ray ──────────────────────────────

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    const uint nRays = min(raysPerProbe, kMaxRaysPerProbe);
    const uint tid   = DTid.x;
    if (tid >= totalProbes * nRays) return;
    const uint probeIdx = tid / nRays;
    const uint i        = tid - probeIdx * nRays;

    const float3 probePos = ProbePosition(probeIdx);
    const float3 rayDir   = ProbeRayDirection(i, nRays, probeIdx, g_frameIndex);

    float4 hitInfo;
    float3 hitP, hitN, hitAlbedo;
    float4 rt = TraceProbeRay(probePos, rayDir, hitInfo, hitP, hitN, hitAlbedo);

    // Second bounce from the previous frame's probes, as the SDF writer's
    // `radiance = albedo * (sunRad / kPI + indirect)`; the same 2 * kSurfaceEpsilon offset.
    if (g_indirectTerm != 0u && g_historyValid != 0u && rt.a < maxTraceDistance) {
        rt.rgb += hitAlbedo * SamplePreviousIrradiance(hitP + hitN * (2.0 * kSurfaceEpsilon), hitN);
    }

    RWStructuredBuffer<float4> rayRadiance = ResourceDescriptorHeap[g_rayRadianceUav];
    RWStructuredBuffer<float4> rayInfo     = ResourceDescriptorHeap[g_rayInfoUav];
    rayRadiance[tid] = rt;
    rayInfo[tid]     = hitInfo;
}
