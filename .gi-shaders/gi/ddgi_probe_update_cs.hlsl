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

// ─── Trace constants at b2 (matches DDGI cb layout) ───────────────────────

cbuffer TraceConstants : register(b2) {
    float gridOriginX;
    float gridOriginY;
    float gridOriginZ;
    float probeSpacing;

    uint  probesPerAxis;
    uint  totalProbes;
    uint  raysPerProbe;
    uint  frameIndex;

    float maxTraceDistance;
    float temporalBlendAlpha;
    uint  shBufferUav;          // Unused by atlas writer.
    uint  _padTC;

    float _gdf0[4];
    float _gdf1[4];
    float _gdf2[4];
    float _gdf3[4];
    uint  _gdfSrv0;
    uint  _gdfSrv1;
    uint  _gdfSrv2;
    uint  _gdfSrv3;

    uint  numGdfClipmaps;
    float skyColorR;
    float skyColorG;
    float skyColorB;

    float lightDirX;
    float lightDirY;
    float lightDirZ;
    float lightIntensity;

    float lightColorR;
    float lightColorG;
    float lightColorB;
    float groundAlbedo;
};

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

// ─── Sample radiance toward a single direction ──────────────────────────

float4 TraceProbeRay(float3 origin, float3 dir) {
    if (g_tlasSrvIndex == kSentinelSrvIndex) {
        return float4(skyColorR, skyColorG, skyColorB, maxTraceDistance);
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
        // Single-bounce sun lighting at the (unknown-normal) hit point.
        // Use the negated ray direction as a conservative outward normal —
        // accurate per-vertex normals require an SBT/hit-group pipeline.
        float3 N    = -dir;
        float3 L    = normalize(float3(lightDirX, lightDirY, lightDirZ));
        float  NdL  = max(dot(N, L), 0.0);
        float3 sunRad = float3(lightColorR, lightColorG, lightColorB)
                      * lightIntensity * NdL;
        return float4(sunRad * groundAlbedo, q.CommittedRayT());
    }
    return float4(skyColorR, skyColorG, skyColorB, maxTraceDistance);
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
    uint frameJitter = (g_frameIndex * 1664525u + 1013904223u);

    float3 accumIrrad = float3(0, 0, 0);
    float  accumWeight = 0.0;

    float  accumDepth  = 0.0;
    float  accumDepth2 = 0.0;
    float  accumDepthW = 0.0;

    [loop]
    for (uint i = 0; i < nRays; ++i) {
        float2 xi = Hammersley(i + (frameJitter & 0xFFFFu),
                               nRays + (frameJitter & 0xFFu));
        xi = frac(xi);
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
