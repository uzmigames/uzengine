/// UzEngine DDGI Probe Update — DXR ray-traced octahedral atlas writer.
///
/// Updates the irradiance + depth atlases owned by `DDGISystem`. Each thread
/// updates one texel of one probe in either atlas (irradiance or depth — the
/// shader is dispatched twice per frame, once per atlas, with `g_atlasMode`
/// switching the write target). For each output texel:
///
///   1. Decode the (probe, octahedral xy) coordinates from the dispatch.
///   2. Decode the octahedral xy back into a unit direction.
///   3. Read the probe's `raysPerProbe` rays from the buffers `ddgi_probe_trace_cs.hlsl`
///      wrote this frame (block B.6; before it every texel traced the rays itself,
///      292 traces of every ray) and weight them by the basis dot product
///      (cone-blurred, Majercik 2019 §5). Without the buffers (sentinel SRV,
///      r.GI.DDGI.RayBuffers 0) the texel traces the rays as before.
///   4. Accumulate either irradiance (cosine-weighted radiance) or depth
///      statistics (mean distance, mean distance²).
///   5. EMA-blend with the previous frame for temporal stability.
///
/// Both atlases use a 1-texel border so bilinear lookups in deferred lighting
/// wrap around at octahedral seams.
///
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Probes" — atlas layout, Chebyshev visibility
/// @ref Cigolle et al. 2014 "A Survey of Efficient Representations for
///                           Independent Unit Vectors" — octahedral encoding
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

// ─── Root constants at b0 ──────────────────────────────────────────────────

SamplerState linearClamp : register(s2);

cbuffer RootConstants : register(b0) {
    uint g_irradAtlasUav;      ///< Bindless UAV index — irradiance atlas (RGBA16F).
    uint g_depthAtlasUav;      ///< Bindless UAV index — depth atlas (RG16F).
    uint g_tlasSrvIndex;       ///< Bindless SRV index of the scene TLAS.
    uint g_atlasMode;          ///< 0 = irradiance pass, 1 = depth pass.
    uint g_frameIndex;         ///< Frame counter for temporal jitter.
    uint g_probesPerAxis;      ///< Probes per axis (cube grid).
    uint g_probeOffset;        ///< Reserved for temporal probe spreading.
    uint g_skyRadianceSrv;     ///< Sky radiance cubemap, the same one the SDF radiance cache
                               ///< samples for escaping rays (kSentinelSrvIndex = flat sky colour).
    uint g_diagnosticUav;      ///< Irradiance pass only: bindless UAV of the transport
                               ///< diagnostic (float2 per atlas texel: mean ray radiance
                               ///< luminance, sky fraction); 0xffffffff = none. Mirrors the
                               ///< SDF writer's `traceStats` so both writers can be compared
                               ///< probe by probe on the same scene (test_gi_energy_gpu.ps1).
    uint g_knownRadianceBits;  ///< Diagnostic (followup-01 consumer stage): float bits of a constant
                               ///< radiance every ray returns instead of what it traced; 0 = off.
    uint g_knownKeepDepth;     ///< With the above: 1 = keep the real hit distance (Chebyshev acts),
                               ///< 0 = report the far distance (visibility 1).
    uint g_knownRadianceMode;  ///< 0 = constant; 1 = directional L * (0.5 + 0.5 dot(dir, d0)); 2 = per-probe parity.
    uint g_knownDistanceBits;  ///< Diagnostic: float bits of a known hit distance every ray reports; 0 = off.
    uint g_knownDistanceMode;  ///< 0 = constant D; 1 = D * (0.75 + 0.25 dot(dir, d0)).
    uint g_farDepth;           ///< Diagnostic: 1 = the DEPTH pass stores the far distance for every ray
                               ///< (visibility 1 at the consumer) while the radiance stays real.
};

// Trace constants (b2), helpers, sky, GDF surface and `TraceProbeRay` — shared with the
// trace pass so both derive the same ray i of probe p from the same arguments.
#include "ddgi_probe_common.hlsli"

// ─── Entry point — one thread per atlas texel of one probe ───────────────
//
// Dispatch X = probesPerAxis * probesPerAxis * tileSize
// Dispatch Y = probesPerAxis * tileSize
// Dispatch Z = 1
//
// We hand-decode the probe index from the dispatch coordinates so the same
// shader works for both atlas resolutions (6 vs 16) without needing a
// permutation define — `g_atlasMode` selects the active path.

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint tileSize = (g_atlasMode == 0u) ? kIrradProbeSize : kDepthProbeSize;
    uint stride   = tileSize + 2u;

    // Which probe tile does this texel belong to?
    uint tileX = DTid.x / stride;
    uint tileY = DTid.y / stride;
    if (tileX >= g_probesPerAxis * g_probesPerAxis) return;
    if (tileY >= g_probesPerAxis) return;

    // Position inside the tile (skip the 1-texel border).
    uint localX = DTid.x % stride;
    uint localY = DTid.y % stride;
    if (localX == 0u || localX > tileSize) return;
    if (localY == 0u || localY > tileSize) return;

    // Linear probe index (matches `ProbeAtlasOrigin` packing).
    uint probeIdx = tileY * (g_probesPerAxis * g_probesPerAxis) + tileX;
    if (probeIdx >= totalProbes) return;

    // Octahedral coordinate inside the tile, [0,1).
    float2 octUV = (float2(float(localX - 1u) + 0.5,
                           float(localY - 1u) + 0.5)) / float(tileSize);
    float3 sampleDir = OctDecodeDir(octUV);

    float3 probePos = ProbePosition(probeIdx);

    // Trace `raysPerProbe` directions, weight by basis dot product so the
    // texel only integrates radiance from rays near `sampleDir`. This is
    // Majercik 2019 §5 — the per-texel cosine integration that gives DDGI
    // its smooth probe filtering without resampling at lookup time.
    uint nRays = min(raysPerProbe, kMaxRaysPerProbe);

    // Block B.6: the rays were traced once per probe by ddgi_probe_trace_cs.hlsl into the two
    // ray buffers (index probe * nRays + i). The sentinel SRV (r.GI.DDGI.RayBuffers 0, or no
    // buffers) keeps the per-texel trace of the previous design for an A/B on one binary.
    const bool useRayBuffers = ddgiRayRadianceSrv != kSentinelSrvIndex
                            && ddgiRayInfoSrv != kSentinelSrvIndex;
    const uint rayBase = probeIdx * nRays;

    float3 accumIrrad = float3(0, 0, 0);
    float  accumWeight = 0.0;

    // Depth moments are stored normalised — see the accumulation below.
    const float invMaxTraceDistance = 1.0 / max(maxTraceDistance, 1e-4);

    float  accumDepth  = 0.0;
    float  accumDepth2 = 0.0;
    float  accumDepthW = 0.0;

    // Transport diagnostic: unweighted mean of the ray radiance luminance and the
    // fraction of rays that escaped to the sky — the same two statistics the SDF
    // writer records in `traceStats` (.w and .x), so the two writers can be compared
    // in linear radiance rather than through the tonemapped frame.
    float diagLum = 0.0;
    float diagSky = 0.0;
    float diagSkyLum = 0.0;   // luminance carried by the sky rays alone
    // Hit side (followup-01 stage 3): what the rays that did NOT escape found.
    float diagHitCount  = 0.0;
    float diagHitDist   = 0.0;
    float diagHitLit    = 0.0;
    float diagHitAlbedo = 0.0;
    float diagHitBack   = 0.0;   // hit rays whose committed triangle was back-facing

    [loop]
    for (uint i = 0; i < nRays; ++i) {
        // A full stratified set, rotated per frame and per probe (Cranley-Patterson), the
        // same construction as radiance_cache_trace_cs.hlsl (`ProbeRayDirection`; the trace
        // pass derives ray i of this probe from the same arguments). The previous
        // `Hammersley(i + j, nRays + (jitter & 0xFF))` was a contiguous window into a longer
        // sequence: its polar coordinate spanned ~32/N of [0,1), so each frame's rays covered a
        // band of the sphere (followup-01, H-57: mean 0.373 of a field whose mean is 0.5).
        float3 rayDir = ProbeRayDirection(i, nRays, probeIdx, g_frameIndex);

        float4 hitInfo;
        float4 rt;
        if (useRayBuffers) {
            StructuredBuffer<float4> rayRadiance = ResourceDescriptorHeap[ddgiRayRadianceSrv];
            StructuredBuffer<float4> rayInfoBuf  = ResourceDescriptorHeap[ddgiRayInfoSrv];
            rt      = rayRadiance[rayBase + i];
            hitInfo = rayInfoBuf[rayBase + i];
        } else {
            float3 hitP, hitN, hitAlbedo;
            uint   unusedMeshId, unusedPrimIndex;
            float2 unusedBary;
            rt = TraceProbeRay(probePos, rayDir, kGITraceSentinelSrv, hitInfo, hitP, hitN, hitAlbedo,
                               unusedMeshId, unusedPrimIndex, unusedBary);
        }
        float3 radiance = rt.rgb;
        float  hitT     = rt.a;
        // Diagnostic (followup-01, H-60 follow-on): real radiance, far depth — separates the visibility
        // test from the writer-side difference under real lighting. Depth pass only.
        if (g_farDepth != 0u && g_atlasMode == 1u) hitT = maxTraceDistance;
        {
            const float knownL = asfloat(g_knownRadianceBits);
            if (knownL > 0.0) {
                float knownLeff = knownL;
                if (g_knownRadianceMode == 2u) {
                    // Per-probe parity: odd cells L, even cells L/4 (followup-01 step 4).
                    const uint3 cell = ProbeIndexToCell(probeIdx);
                    knownLeff = ((cell.x + cell.y + cell.z) & 1u) != 0u ? knownL : 0.25 * knownL;
                }
                radiance = KnownRadiance(knownLeff, rayDir, g_knownRadianceMode);
                if (g_knownKeepDepth == 0u) hitT = maxTraceDistance;
                const float knownD = asfloat(g_knownDistanceBits);
                if (knownD > 0.0) {
                    // Known hit distance for the depth atlas: constant, or varying with direction.
                    hitT = (g_knownDistanceMode == 1u) ? knownD * (0.75 + 0.25 * dot(rayDir, kKnownDir)) : knownD;
                }
            }
        }
        if (hitT < maxTraceDistance) {
            diagHitCount  += 1.0;
            diagHitDist   += hitT;
            diagHitLit    += hitInfo.x;
            diagHitAlbedo += hitInfo.y;
            diagHitBack   += 1.0 - hitInfo.z;
        }
        diagLum += dot(radiance, float3(0.2126, 0.7152, 0.0722));
        diagSky += (hitT >= maxTraceDistance) ? 1.0 : 0.0;
        diagSkyLum += (hitT >= maxTraceDistance) ? dot(radiance, float3(0.2126, 0.7152, 0.0722)) : 0.0;

        // Weight by max(0, dot(sampleDir, rayDir)) — cosine of the angle
        // between the texel direction and the traced ray. This is the
        // per-texel integral that produces a smooth octahedral atlas.
        float w = max(dot(sampleDir, rayDir), 0.0);
        if (g_atlasMode == 0u) {
            accumIrrad  += radiance * w;
            accumWeight += w;
        } else {
            // Depth atlas uses a sharper cone (square the weight) to
            // preserve depth discontinuities for the Chebyshev test.
            //
            // Store the distance NORMALISED by maxTraceDistance. The atlas is
            // RG16F and the second moment is a squared distance: at Sponza's
            // scale (cm, maxTraceDistance = probeSpacing*16*2 ~ 4000) d^2 is
            // ~1.6e7 and saturates FP16's 65504 limit. A saturated second
            // moment makes `variance = max(meanD2 - meanD*meanD, 1e-4)`
            // collapse to 1e-4 and, because `distToProbe < meanD` then holds
            // everywhere, Chebyshev returned visibility = 1 for every probe —
            // DDGI had no occlusion at all and lit through walls.
            // Measured before this change: gi.ddgi_depth max = 65504 (FP16 max).
            float dw = w * w;
            float hitNorm = hitT * invMaxTraceDistance;
            accumDepth  += hitNorm * dw;
            accumDepth2 += hitNorm * hitNorm * dw;
            accumDepthW += dw;
        }
    }

    if (g_atlasMode == 0u) {
        if (g_diagnosticUav != 0xffffffffu) {
            RWStructuredBuffer<float4> diagnostic = ResourceDescriptorHeap[g_diagnosticUav];
            const uint texel = (localY - 1u) * tileSize + (localX - 1u);
            const float invRays = 1.0 / float(max(nRays, 1u));
            const float invHits = 1.0 / max(diagHitCount, 1.0);
            const uint  rec = (probeIdx * (tileSize * tileSize) + texel) * 2u;
            // w: back-face fraction over the HIT rays (H-63 follow-on); zero without hits.
            diagnostic[rec] = float4(diagLum * invRays, diagSky * invRays,
                                     diagSkyLum * invRays, diagHitBack * invHits);
            // Hit side: mean distance, lit fraction and mean albedo over the HIT rays only
            // (zero when the probe has none), and the hit count itself.
            diagnostic[rec + 1] = float4(diagHitDist * invHits, diagHitLit * invHits,
                                         diagHitAlbedo * invHits, diagHitCount);
        }
        float3 irrad = (accumWeight > 1e-6) ? (accumIrrad / accumWeight)
                                            : float3(0, 0, 0);

        // ── VFX emissive injection (band-0 / omnidirectional) ───────────────
        //
        // For each VFX emissive sample, compute the Karis attenuation between
        // probe centre and sample position and add the resulting radiance
        // uniformly to every octahedral texel of this probe. The contribution
        // is direction-independent (band-0 / SH[0]) — a fireball located near
        // a probe lights every direction equally, modulo the smooth radius
        // cutoff. This matches the proposal's "omnidirectional irradiance"
        // semantics for the atlas variant of the DDGI update.
        //
        // CPU mirror — `VfxDDGIAttenuation` /  `VfxDDGIProbeContribution` in
        //   runtime/engine/vfx/lighting/vfx_ddgi_injection.cpp:21-47
        //
        //   float dOverR = d / r;
        //   float s      = max(0, 1 - dOverR^4);
        //   float s2     = s * s;
        //   float invSq  = 1 / (d*d + 1);
        //   return s2 * invSq;
        //
        // The HLSL below reproduces this byte-for-byte (same operands, same
        // ordering, same denominator constant). Verified item 1.4.
        //
        // The gate is doubly-conservative: the runtime sample count must be
        // non-zero AND the SRV index must not be sentinel. When the
        // `r.VFX.DDGIInjection` CVar is off, `GatherAndUploadVfxEmissiveSamples`
        // calls `m_vfxDDGIEmissiveFeed.Reset()` which forces `sampleCount == 0`,
        // skipping the loop entirely.
        if (vfxEmissiveSampleCount > 0u && vfxEmissiveSrvIndex != 0xFFFFFFFFu) {
            StructuredBuffer<VfxDDGIEmissiveSample> emissiveSamples =
                ResourceDescriptorHeap[NonUniformResourceIndex(vfxEmissiveSrvIndex)];

            [loop]
            for (uint vi = 0u; vi < vfxEmissiveSampleCount; ++vi) {
                VfxDDGIEmissiveSample s = emissiveSamples[vi];

                if (s.influenceRadius <= 0.0f) continue;

                // probePos - samplePos: distance vector from sample to probe.
                // CPU helper uses `sample.position - probePosition` then takes
                // its magnitude — only `d` is used so sign doesn't matter.
                float dx = probePos.x - s.positionX;
                float dy = probePos.y - s.positionY;
                float dz = probePos.z - s.positionZ;
                float distSq = dx * dx + dy * dy + dz * dz;
                float d      = sqrt(distSq);

                // Smooth radius cutoff: matches CPU early-out `d >= radius`.
                if (d >= s.influenceRadius) continue;

                float dOverR = d / s.influenceRadius;
                float dOverR2 = dOverR * dOverR;
                float dOverR4 = dOverR2 * dOverR2;
                float sCutoff = max(0.0f, 1.0f - dOverR4);
                float s2 = sCutoff * sCutoff;
                float invSq = 1.0f / (distSq + 1.0f);
                float falloff = s2 * invSq;

                irrad.x += s.emissiveX * falloff;
                irrad.y += s.emissiveY * falloff;
                irrad.z += s.emissiveZ * falloff;
            }
        }

        // EMA hysteresis blend with the previous frame.
        RWTexture2D<float4> irradTex = ResourceDescriptorHeap[g_irradAtlasUav];
        uint2 atlasOrigin = ProbeAtlasOrigin(probeIdx, kIrradProbeSize);
        uint2 atlasXY     = atlasOrigin + uint2(localX - 1u, localY - 1u);

        float4 prev = irradTex[atlasXY];
        float a = saturate(temporalBlendAlpha);
        float4 blended = lerp(prev, float4(irrad, 1.0), a);
        irradTex[atlasXY] = blended;
    } else {
        // Both moments are in normalised units [0,1]; the consumer rescales
        // by maxTraceDistance (derived from probe spacing) before the
        // Chebyshev comparison.
        float meanD  = (accumDepthW > 1e-6) ? (accumDepth  / accumDepthW) : 1.0;
        float meanD2 = (accumDepthW > 1e-6) ? (accumDepth2 / accumDepthW) : 1.0;

        RWTexture2D<float2> depthTex = ResourceDescriptorHeap[g_depthAtlasUav];
        uint2 atlasOrigin = ProbeAtlasOrigin(probeIdx, kDepthProbeSize);
        uint2 atlasXY     = atlasOrigin + uint2(localX - 1u, localY - 1u);

        float2 prev = depthTex[atlasXY];
        float a = saturate(temporalBlendAlpha);
        float2 blended = lerp(prev, float2(meanD, meanD2), a);
        depthTex[atlasXY] = blended;
    }
}
