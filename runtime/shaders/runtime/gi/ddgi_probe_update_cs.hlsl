/// UzEngine DDGI Probe Update — DXR ray-traced octahedral atlas writer.
///
/// Updates the irradiance + depth atlases owned by `DDGISystem`. Each thread
/// updates one texel of one probe in either atlas (irradiance or depth — the
/// shader is dispatched twice per frame, once per atlas, with `g_atlasMode`
/// switching the write target). For each output texel:
///
///   1. Decode the (probe, octahedral xy) coordinates from the dispatch.
///   2. Decode the octahedral xy back into a unit direction.
///   3. Trace `raysPerProbe` rays at that direction (cone-blurred via the
///      basis dot weighting in Majercik 2019 §5).
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

#include "../common/constants.hlsli"
#include "gi_hit_lighting.hlsli"
#include "gi_trace_common.hlsli"

// ─── Root constants at b0 ──────────────────────────────────────────────────

cbuffer RootConstants : register(b0) {
    uint g_irradAtlasUav;      ///< Bindless UAV index — irradiance atlas (RGBA16F).
    uint g_depthAtlasUav;      ///< Bindless UAV index — depth atlas (RG16F).
    uint g_tlasSrvIndex;       ///< Bindless SRV index of the scene TLAS.
    uint g_atlasMode;          ///< 0 = irradiance pass, 1 = depth pass.
    uint g_frameIndex;         ///< Frame counter for temporal jitter.
    uint g_probesPerAxis;      ///< Probes per axis (cube grid).
    uint g_probeOffset;        ///< Reserved for temporal probe spreading.
    uint g_probeBudget;
};

// b2 TraceConstants + GDF/sky/albedo helpers live in gi_trace_common.hlsli
// (shared with the reference path tracer).

// ─── Constants ────────────────────────────────────────────────────────────

static const float kPI            = 3.14159265358979323846;
static const float kRayTMin       = 1.0;          // cm — avoid self-hit
static const uint  kIrradProbeSize = 6;           // matches DDGISystem::kIrradianceProbeSize
static const uint  kDepthProbeSize = 16;          // matches DDGISystem::kDepthProbeSize
static const uint  kMaxRaysPerProbe = 64;

// ─── Hammersley + spherical sample (shared with SDF shader) ──────────────

float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

float3 SphericalSample(float2 xi) {
    float z = 1.0 - 2.0 * xi.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * kPI * xi.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

// ─── Octahedral encode/decode (Cigolle 2014) ─────────────────────────────
//
// Maps a unit direction <-> [-1,1]^2. The decode used by the atlas reader
// must exactly mirror this so the per-texel direction is consistent.

float2 OctWrap(float2 v) {
    return (1.0 - abs(v.yx)) * float2(v.x >= 0.0 ? 1.0 : -1.0,
                                      v.y >= 0.0 ? 1.0 : -1.0);
}

float2 OctEncodeDir(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = (n.z >= 0.0) ? n.xy : OctWrap(n.xy);
    return e * 0.5 + 0.5; // map to [0,1]
}

float3 OctDecodeDir(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// ─── Atlas tile layout helpers ───────────────────────────────────────────

uint3 ProbeIndexToCell(uint linearIdx) {
    uint x = linearIdx % g_probesPerAxis;
    uint y = (linearIdx / g_probesPerAxis) % g_probesPerAxis;
    uint z = linearIdx / (g_probesPerAxis * g_probesPerAxis);
    return uint3(x, y, z);
}

float3 ProbePosition(uint linearIdx) {
    uint3 c = ProbeIndexToCell(linearIdx);
    return float3(gridOriginX, gridOriginY, gridOriginZ)
         + float3(float(c.x), float(c.y), float(c.z)) * probeSpacing;
}

// Atlas tile = (probesPerAxis * probesPerAxis) tiles laid out across X,
// stacked probesPerAxis tiles along Y. Each tile is `(size+2)` to include
// the 1-texel border. Returns the texel coordinate of the probe's (0,0)
// corner inside the atlas, EXCLUDING the border (i.e. the first interior
// texel).
uint2 ProbeAtlasOrigin(uint linearIdx, uint tileSize) {
    uint stride = tileSize + 2u;
    uint tileX  = linearIdx % (g_probesPerAxis * g_probesPerAxis);
    uint tileY  = linearIdx / (g_probesPerAxis * g_probesPerAxis);
    return uint2(tileX * stride + 1u,
                 tileY * stride + 1u);
}

// ─── Hit-point queries (surface-cache-lite inputs) ───────────────────────

/// Previous-frame radiance-cache irradiance at a world position (nearest
/// probe). Shares the probe grid with the SDF path — frame_constants keeps
/// both grids aligned. @ref Majercik 2019 §"infinite bounces"
float3 DdgiBounceIrradiance(float3 worldPos, float3 n) {
    if (rcProbeShSrv == kSentinelSrvIndex) return float3(0, 0, 0);
    StructuredBuffer<float4> shBuf = ResourceDescriptorHeap[rcProbeShSrv];
    float3 cell = (worldPos - float3(gridOriginX, gridOriginY, gridOriginZ)) / probeSpacing;
    int3 idx3 = clamp(int3(round(cell)), int3(0, 0, 0),
                      int3(int(probesPerAxis) - 1,
                           int(probesPerAxis) - 1,
                           int(probesPerAxis) - 1));
    uint idx = uint(idx3.x)
             + uint(idx3.y) * probesPerAxis
             + uint(idx3.z) * probesPerAxis * probesPerAxis;
    uint base = idx * 7u;
    float4 packed[7];
    [unroll]
    for (uint k = 0; k < 7; ++k) packed[k] = shBuf[base + k];
    return GISH_EvalIrradiance(packed, n);
}

// ─── Sample radiance toward a single direction ──────────────────────────

float4 TraceProbeRay(float3 origin, float3 dir) {
    if (g_tlasSrvIndex == kSentinelSrvIndex) {
        return float4(GITrace_SkyRadiance(dir), maxTraceDistance);
    }
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_tlasSrvIndex];

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = kRayTMin;
    ray.TMax      = maxTraceDistance;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float  hitT   = q.CommittedRayT();
        float3 hitPos = origin + dir * hitT;
        uint   meshId = q.CommittedInstanceID();

        // Geometric normal without vertex fetch: the GDF gradient — the SDF
        // chain runs alongside DDGI (frame_gi_passes always dispatches the
        // radiance cache), so the clipmaps hold real distances. Falls back
        // to the negated ray direction when the GDF has no data here.
        float3 n = GITrace_GdfNormal(hitPos, dir);

        // Sun visibility: precise second RayQuery toward the light.
        float3 L = normalize(float3(lightDirX, lightDirY, lightDirZ));
        float sunVis = 1.0;
        {
            RayDesc sray;
            sray.Origin    = hitPos + n * (2.0 * kRayTMin);
            sray.Direction = L;
            sray.TMin      = kRayTMin;
            sray.TMax      = maxTraceDistance;
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
            sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, sray);
            sq.Proceed();
            if (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) sunVis = 0.0;
        }

        float3 sunColorIntensity = float3(lightColorR, lightColorG, lightColorB)
                                 * lightIntensity;
        float3 albedo = GITrace_HitAlbedo(meshId);
        float3 bounce = DdgiBounceIrradiance(hitPos + n * probeSpacing * 0.25, n);

        float3 radiance = GIHit_Radiance(albedo, float3(0, 0, 0), n,
                                         L, sunColorIntensity, sunVis, bounce);
        return float4(radiance, hitT);
    }
    return float4(GITrace_SkyRadiance(dir), maxTraceDistance);
}

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

    // Per-frame ray-set rotation with a SHORT deterministic cycle.
    // A white-noise jitter (LCG on frameIndex) makes each frame's estimate
    // statistically independent, so the EMA-blended probe value executes a
    // random walk that never stations — visible as low-frequency lighting
    // wander. With a cycle of kJitterCycle distinct Cranley-Patterson
    // offsets the input sequence is periodic, so the EMA converges to a
    // stationary orbit of amplitude ~alpha * sigma (imperceptible) while
    // still integrating kJitterCycle * nRays effective directions.
    // @ref Cranley & Patterson 1976 "Randomization of Number Theoretic
    //      Methods for Multiple Integration" — toroidal shift of a fixed set
    // @ref Roberts 2018 "The Unreasonable Effectiveness of Quasirandom
    //      Sequences" — R2 constants 0.75487767, 0.56984029
    const uint kJitterCycle = 16u;
    float2 cpOffset = frac(float2(0.75487767, 0.56984029)
                           * float(g_frameIndex % kJitterCycle));

    float3 accumIrrad = float3(0, 0, 0);
    float  accumWeight = 0.0;

    float  accumDepth  = 0.0;
    float  accumDepth2 = 0.0;
    float  accumDepthW = 0.0;

    [loop]
    for (uint i = 0; i < nRays; ++i) {
        float2 xi = frac(Hammersley(i, nRays) + cpOffset);
        float3 rayDir = SphericalSample(xi);

        float4 rt = TraceProbeRay(probePos, rayDir);
        float3 radiance = rt.rgb;
        float  hitT     = rt.a;

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
            float dw = w * w;
            accumDepth  += hitT * dw;
            accumDepth2 += hitT * hitT * dw;
            accumDepthW += dw;
        }
    }

    if (g_atlasMode == 0u) {
        float3 irrad = (accumWeight > 1e-6) ? (accumIrrad / accumWeight)
                                            : float3(0, 0, 0);

        // EMA hysteresis blend with the previous frame.
        RWTexture2D<float4> irradTex = ResourceDescriptorHeap[g_irradAtlasUav];
        uint2 atlasOrigin = ProbeAtlasOrigin(probeIdx, kIrradProbeSize);
        uint2 atlasXY     = atlasOrigin + uint2(localX - 1u, localY - 1u);

        float4 prev = irradTex[atlasXY];
        float a = saturate(temporalBlendAlpha);
        float4 blended = lerp(prev, float4(irrad, 1.0), a);
        irradTex[atlasXY] = blended;
    } else {
        float meanD  = (accumDepthW > 1e-6) ? (accumDepth  / accumDepthW)
                                            : maxTraceDistance;
        float meanD2 = (accumDepthW > 1e-6) ? (accumDepth2 / accumDepthW)
                                            : (maxTraceDistance * maxTraceDistance);

        RWTexture2D<float2> depthTex = ResourceDescriptorHeap[g_depthAtlasUav];
        uint2 atlasOrigin = ProbeAtlasOrigin(probeIdx, kDepthProbeSize);
        uint2 atlasXY     = atlasOrigin + uint2(localX - 1u, localY - 1u);

        float2 prev = depthTex[atlasXY];
        float a = saturate(temporalBlendAlpha);
        float2 blended = lerp(prev, float2(meanD, meanD2), a);
        depthTex[atlasXY] = blended;
    }
}
