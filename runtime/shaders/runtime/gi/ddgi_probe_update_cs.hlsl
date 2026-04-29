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

    // ── VFX → DDGI emissive injection (phase16 hardware-validation) ──────
    // Mirrors the trailing fields of `uz::RadianceCacheTraceConstants` in
    // `engine/renderer/gi/radiance_cache.h`. When `vfxEmissiveSampleCount`
    // is 0 (CVar off / no alive emitters / radiance-cache shader path) the
    // accumulation loop below is skipped at runtime and DXC dead-code
    // strips the entire branch on shader-stage scheduling.
    uint  vfxEmissiveSrvIndex;
    uint  vfxEmissiveSampleCount;
    float _vfxEmissivePad0;
    float _vfxEmissivePad1;
};

// ─── VFX emissive sample (mirrors `uz::VfxEmissiveSample`) ───────────────
//
// Layout MUST match the CPU `struct VfxEmissiveSample` in
// `engine/vfx/lighting/vfx_ddgi_injection.h` byte-for-byte. The CPU side is:
//   Vec3  position;        // 12 bytes (3 floats, NOT a float4 — Vec3 is
//                          //          tightly packed with no trailing pad)
//   Vec3  emissive;        // 12 bytes
//   float influenceRadius; //  4 bytes
//   ───── total = 28 bytes ─────
//
// The structured buffer is created with `desc.stride = sizeof(VfxEmissiveSample)`
// = 28 in `vfx_ddgi_emissive_feed.cpp:53`. We must therefore declare the HLSL
// struct as scalars-only so DXC packs it tightly (28 bytes); a `float3 position`
// followed by `float3 emissive` would force `emissive` to offset 16 (HLSL
// disallows float3 straddling a 16-byte boundary), bloating the stride to 32
// and corrupting buffer reads.
struct VfxDDGIEmissiveSample {
    float positionX;        // offset 0
    float positionY;        // offset 4
    float positionZ;        // offset 8
    float emissiveX;        // offset 12
    float emissiveY;        // offset 16
    float emissiveZ;        // offset 20
    float influenceRadius;  // offset 24
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
