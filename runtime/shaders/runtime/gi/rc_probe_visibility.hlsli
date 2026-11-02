/// UzEngine — radiance cache stage-2 reader helpers: near-field exclusion and Chebyshev visibility
/// (docs/analysis/lumen-radiance-cache-stability/01 §2.8, 03 §4 move 2, 04 §2.4). Included by every
/// reader of the probe SH buffer through `rc_probe_guard.hlsli`.
///
/// Both features are OFF unless their CVars are on (`r.GI.RadianceCache.NearFieldExclude`,
/// `r.GI.RadianceCache.Visibility`); their parameters travel in ONE float4 record appended to the SH
/// buffer after the per-probe state records (`ProbeParamsIndex`), written by
/// `RadianceCache::UpdateProbes` every frame and ZERO at the defaults, so every reader keeps its
/// arithmetic bit for bit: x = near-field TMin in cm (0 = off), y = end of the transition band in cm,
/// z = the cache's depth-moments atlas SRV + 1 (0 = none), w = that atlas writer's maxTraceDistance (cm).
///
/// Near field (Lumen): the world radiance cache is far-field only. A probe's rays start at
/// ProbeTMin = cell * sqrt(3) and a receiver's own rays must cover MinTraceDistanceBeforeInterpolation
/// = TMin + cell * sqrt(3) before the cache may answer, so near-field occlusion never comes from the
/// grid (`LumenRadianceCacheInterpolation.ush`). Here: a hit closer than TMin to the ray origin takes
/// none of the probe-interpolated indirect (its own trace already carries the occlusion the grid
/// cannot express below a cell), full weight from the band end, smoothstep between (no hard ring).
///
/// Visibility (Majercik 2019 §3.4, RTXGI): each probe carries the mean and mean-squared hit
/// distance of its own rays per octahedral direction (the DDGI depth atlas writer run on the cache's
/// rays); a receiver farther from a probe than the probe's mean hit distance toward it is weighted
/// by the Chebyshev upper bound var / (var + d^2), cubed, floored (0.05) so no probe leaves the set:
/// a probe behind a thin wall fades out continuously instead of leaking or flipping.
///
/// @ref Wright et al. 2022 "Lumen" — radiance cache coverage, MinTraceDistanceBeforeInterpolation
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields" §3.4
/// @ref NVIDIA RTXGI-DDGI `Irradiance.hlsl` — normal bias, Chebyshev floor

#ifndef UZ_RC_PROBE_VISIBILITY_HLSLI
#define UZ_RC_PROBE_VISIBILITY_HLSLI

/// Index of the stage-2 parameter record in the SH buffer: after the seven SH records, the guard
/// record and the state record of every probe (`RadianceCache::kParamsFloat4`).
uint ProbeParamsIndex(uint probesPerAxis) {
    return probesPerAxis * probesPerAxis * probesPerAxis * 9u;
}

/// Weight of the probe-interpolated indirect at a hit `hitT` from the ray origin: 0 up to TMin, 1 from
/// the band end, smooth in between; 1 when the near-field exclusion is off (record x = 0).
float RcNearFieldWeight(float hitT, float4 params) {
    if (params.x <= 0.0) return 1.0;
    return smoothstep(params.x, params.y, hitT);
}

// ─── Depth-moments atlas (the DDGI layout: 16x16 interior + 1-texel border per probe) ─────────────

static const uint  kRcDepthTile   = 16u;                 // DDGISystem::kDepthProbeSize / kDepthProbeSize
static const uint  kRcDepthStride = kRcDepthTile + 2u;
/// Query bias along the receiver normal, in probe spacings: a receiver ON a wall sits exactly at the
/// probe's mean hit distance toward it, where the test would flip on sub-centimetre noise (RTXGI
/// probeNormalBias 0.1 scene units at ~1 unit spacing).
static const float kRcVisibilityNormalBias = 0.1;
/// Floor of the visibility weight (RTXGI): a fully occluded probe is down-weighted, never removed.
static const float kRcVisibilityFloor = 0.05;

/// Octahedral direction encode — the deferred lighting's `DDGIOctEncode`, which mirrors `OctEncodeDir`
/// in ddgi_probe_common.hlsli so a consumer reads the texel the writer produced for the direction.
/// @ref Cigolle 2014 "A Survey of Efficient Representations for Independent Unit Vectors"
float2 ProbeOctEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.xy;
    if (n.z < 0.0) {
        e = (1.0 - abs(e.yx)) * float2(e.x >= 0.0 ? 1.0 : -1.0,
                                       e.y >= 0.0 ? 1.0 : -1.0);
    }
    return e * 0.5 + 0.5;
}

/// Chebyshev statistical visibility of a point from probe `probeLinear` (Majercik 2019 §3.4): the test
/// the DDGI block of deferred_lighting_ps.hlsl runs, factored here so the radiance cache's atlas
/// (written by the same ddgi_probe_update_cs.hlsl depth pass on the cache's rays) gets the identical
/// consumer. `toProbe` = probe position - query point, `distToProbe` its length, `maxTrace` the
/// writer's maxTraceDistance (both moments are stored normalised by it). Returns the cubed upper
/// bound; 1 when the probe is nearer than its mean hit distance in that direction.
float ProbeChebyshevVisibility(Texture2D<float2> depthTex, SamplerState depthSampler, uint probeLinear,
                               uint probesPerAxis, float maxTrace, float3 toProbe, float distToProbe) {
    const uint tileX = probeLinear % (probesPerAxis * probesPerAxis);
    const uint tileY = probeLinear / (probesPerAxis * probesPerAxis);
    const uint atlasDepthW = probesPerAxis * probesPerAxis * kRcDepthStride;
    const uint atlasDepthH = probesPerAxis * kRcDepthStride;
    float2 dirOct = ProbeOctEncode(normalize(-toProbe));
    float2 depthOrigin = float2(
        float(tileX * kRcDepthStride + 1u),
        float(tileY * kRcDepthStride + 1u));
    float2 depthUV = (depthOrigin + dirOct * float(kRcDepthTile))
                   / float2(float(atlasDepthW),
                            float(atlasDepthH));
    float2 momentT = depthTex.SampleLevel(depthSampler, depthUV, 0);
    float meanD  = momentT.x * maxTrace;
    float meanD2 = momentT.y * (maxTrace * maxTrace);
    float variance = max(meanD2 - meanD * meanD, 1e-4);
    // VSM-style Chebyshev — fully visible if the probe is closer than the surface, otherwise
    // upper-bound the visibility by p_max = variance / (variance + diff²), cubed against leakage.
    float visibility = 1.0;
    if (distToProbe > meanD) {
        float diff = distToProbe - meanD;
        visibility = variance / (variance + diff * diff);
        visibility = visibility * visibility * visibility;
    }
    return visibility;
}

/// The radiance cache's stage-2 weight factor of probe `probeLinear` (at its effective position
/// `probePosition`) for a receiver at `receiver` with normal `normal`: the Chebyshev visibility from
/// the cache's depth atlas, queried from the receiver lifted along its normal, floored. 1 when the
/// atlas is off (record z = 0), so the weights are untouched.
float RcProbeVisibilityWeight(float4 params, SamplerState depthSampler, uint probeLinear, uint probesPerAxis,
                              float spacing, float3 probePosition, float3 receiver, float3 normal) {
#ifdef UZ_BINDLESS
    if (params.z <= 0.0) return 1.0;
    Texture2D<float2> depthTex = ResourceDescriptorHeap[uint(params.z) - 1u];
    const float3 query   = receiver + normal * (kRcVisibilityNormalBias * spacing);
    const float3 toProbe = probePosition - query;
    const float  dist    = length(toProbe);
    if (dist <= 0.0) return 1.0;
    return max(kRcVisibilityFloor,
               ProbeChebyshevVisibility(depthTex, depthSampler, probeLinear, probesPerAxis, params.w, toProbe, dist));
#else
    // The atlas is reached through the SM 6.6 descriptor heap (DX12 bindless, every engine shader there);
    // the register-bound targets (SPIR-V, Metal, SM 5.0) have no radiance-cache visibility and keep the
    // weights untouched, as the deferred lighting's own radiance-cache block is UZ_BINDLESS-only.
    return 1.0;
#endif
}

#endif // UZ_RC_PROBE_VISIBILITY_HLSLI
