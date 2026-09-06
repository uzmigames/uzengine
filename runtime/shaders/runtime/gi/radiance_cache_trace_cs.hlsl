/// UzEngine Radiance Cache Trace — SDF-based probe update.
///
/// Updates one L2 SH probe per thread. For every probe in the grid we shoot
/// `raysPerProbe` cosine-weighted hemisphere rays, ray-march them through the
/// global distance field clipmap hierarchy, and accumulate the resulting
/// radiance into the probe's L2 SH coefficients (7 float4 packing). Hit points
/// receive single-bounce sun lighting modulated by the average ground albedo.
/// Missed rays sample the sky color. The new SH is EMA-blended with the prior
/// frame to suppress single-frame noise.
///
/// @ref Sloan 2008 "Stupid Spherical Harmonics (SH) Tricks" — L2 projection
/// @ref Wright 2015 "Dynamic Diffuse Global Illumination" — surfel/probe GI
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Probes" — temporal hysteresis blending
/// @ref Wright/Crassin 2018 "Distance Field Ray Tracing" — clipmap stepping

#include "../common/constants.hlsli"

// ─── Root constants at b0 ──────────────────────────────────────────────────

cbuffer RootConstants : register(b0) {
    uint g_shBufferUavIndex;   // Bindless UAV index of the probe SH buffer.
    uint g_shBufferSrvIndex;   // (unused) Bindless SRV — kept for future blends.
    uint g_frameIndex;         // Frame counter for temporal jitter.
    uint g_probeOffset;        // First probe index updated this frame.
    uint g_probeBudget;        // Number of probes to update this frame (0 = all).
    uint _padRC[3];
};

// ─── Trace constants at b2 (matches RadianceCacheTraceConstants) ──────────

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
    uint  shBufferUav;          // Same as g_shBufferUavIndex (cb mirror).
    uint  firstWrite;           // 1 = buffer never written: replace instead of blend.

    float gdfCenter0X, gdfCenter0Y, gdfCenter0Z, gdfExtent0;
    float gdfCenter1X, gdfCenter1Y, gdfCenter1Z, gdfExtent1;
    float gdfCenter2X, gdfCenter2Y, gdfCenter2Z, gdfExtent2;
    float gdfCenter3X, gdfCenter3Y, gdfCenter3Z, gdfExtent3;

    uint  gdfClipmapSrv0;
    uint  gdfClipmapSrv1;
    uint  gdfClipmapSrv2;
    uint  gdfClipmapSrv3;

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
static const float kSurfaceEpsilon = 5.0;     // cm — accept hits below this distance
static const float kMinStep        = 2.0;     // cm — never advance less than this
static const uint  kMaxStepsPerRay = 96;
static const uint  kMaxRaysPerProbe = 64;

// ─── L2 SH basis (Sloan 2008 packing — pre-cosine-convolved on accumulate) ──
//
// We store 9 raw L2 coefficients per channel. They are written into the 7
// float4 packing consumed by `EvalSH_L2` in deferred lighting:
//   coeffs[0] = (sh1.r, sh2.r, sh3.r, sh0.r)   ← UE5/Three.js L0+L1 packing
//   coeffs[1] = (sh1.g, sh2.g, sh3.g, sh0.g)
//   coeffs[2] = (sh1.b, sh2.b, sh3.b, sh0.b)
//   coeffs[3] = (sh4.r, sh5.r, sh6.r, sh7.r)
//   coeffs[4] = (sh4.g, sh5.g, sh6.g, sh7.g)
//   coeffs[5] = (sh4.b, sh5.b, sh6.b, sh7.b)
//   coeffs[6] = (sh8.r, sh8.g, sh8.b, 1)
//
// Basis values (Ramamoorthi & Hanrahan 2001, Sloan 2008):
//   Y0   =  0.282095
//   Y1.x = -0.488603 * x
//   Y1.y =  0.488603 * y
//   Y1.z = -0.488603 * z
//   Y2.xy =  1.092548 * x * y
//   Y2.yz = -1.092548 * y * z
//   Y2.zz =  0.315392 * (3*z*z - 1)
//   Y2.xz = -1.092548 * x * z
//   Y2.xx =  0.546274 * (x*x - y*y)

void SHEvaluateL2(float3 dir, out float basis[9]) {
    basis[0] =  0.282095;
    basis[1] = -0.488603 * dir.x;
    basis[2] =  0.488603 * dir.y;
    basis[3] = -0.488603 * dir.z;
    basis[4] =  1.092548 * dir.x * dir.y;
    basis[5] = -1.092548 * dir.y * dir.z;
    basis[6] =  0.315392 * (3.0 * dir.z * dir.z - 1.0);
    basis[7] = -1.092548 * dir.x * dir.z;
    basis[8] =  0.546274 * (dir.x * dir.x - dir.y * dir.y);
}

// Pack 9 raw L2 coefficients per channel into the 7-float4 layout.
void PackL2(float shR[9], float shG[9], float shB[9], out float4 packed[7]) {
    packed[0] = float4(shR[1], shR[2], shR[3], shR[0]);
    packed[1] = float4(shG[1], shG[2], shG[3], shG[0]);
    packed[2] = float4(shB[1], shB[2], shB[3], shB[0]);
    packed[3] = float4(shR[4], shR[5], shR[6], shR[7]);
    packed[4] = float4(shG[4], shG[5], shG[6], shG[7]);
    packed[5] = float4(shB[4], shB[5], shB[6], shB[7]);
    packed[6] = float4(shR[8], shG[8], shB[8], 1.0);
}

void UnpackL2(float4 packed[7], out float shR[9], out float shG[9], out float shB[9]) {
    shR[0] = packed[0].w; shG[0] = packed[1].w; shB[0] = packed[2].w;
    shR[1] = packed[0].x; shG[1] = packed[1].x; shB[1] = packed[2].x;
    shR[2] = packed[0].y; shG[2] = packed[1].y; shB[2] = packed[2].y;
    shR[3] = packed[0].z; shG[3] = packed[1].z; shB[3] = packed[2].z;
    shR[4] = packed[3].x; shG[4] = packed[4].x; shB[4] = packed[5].x;
    shR[5] = packed[3].y; shG[5] = packed[4].y; shB[5] = packed[5].y;
    shR[6] = packed[3].z; shG[6] = packed[4].z; shB[6] = packed[5].z;
    shR[7] = packed[3].w; shG[7] = packed[4].w; shB[7] = packed[5].w;
    shR[8] = packed[6].x; shG[8] = packed[6].y; shB[8] = packed[6].z;
}

// ─── GDF clipmap sampling ─────────────────────────────────────────────────
//
// Each clipmap is a 64^3 R16F volume centered on a world position with a
// half-extent in cm. Sampling returns the signed distance to the nearest
// surface in cm; values outside the clipmap return a large positive number
// (the next cascade is tried automatically).

float SampleGdfClipmap(uint srvIdx, float3 worldPos, float3 center, float extent) {
    if (srvIdx == kSentinelSrvIndex || extent <= 0.0) {
        return 1e9;
    }
    float3 local = (worldPos - center) / extent;       // [-1,1] inside the clipmap
    if (any(abs(local) > 1.0)) {
        return 1e9;
    }
    float3 uvw = local * 0.5 + 0.5;                    // [0,1]
    Texture3D<float> gdfTex = ResourceDescriptorHeap[srvIdx];
    SamplerState samp = SamplerDescriptorHeap[1];      // linear clamp (matches engine s2)
    return gdfTex.SampleLevel(samp, uvw, 0).r;
}

float SampleGdfDistance(float3 worldPos) {
    // Try cascades from finest to coarsest. Each cascade is 4x larger so the
    // first one that contains the point gives the most accurate distance.
    float d = SampleGdfClipmap(gdfClipmapSrv0,
                               worldPos,
                               float3(gdfCenter0X, gdfCenter0Y, gdfCenter0Z),
                               gdfExtent0);
    if (d < 1e8) return d;
    d = SampleGdfClipmap(gdfClipmapSrv1,
                         worldPos,
                         float3(gdfCenter1X, gdfCenter1Y, gdfCenter1Z),
                         gdfExtent1);
    if (d < 1e8) return d;
    d = SampleGdfClipmap(gdfClipmapSrv2,
                         worldPos,
                         float3(gdfCenter2X, gdfCenter2Y, gdfCenter2Z),
                         gdfExtent2);
    if (d < 1e8) return d;
    return SampleGdfClipmap(gdfClipmapSrv3,
                            worldPos,
                            float3(gdfCenter3X, gdfCenter3Y, gdfCenter3Z),
                            gdfExtent3);
}

// Sphere-tracing through the GDF. Returns hit distance (>=0) or -1 on miss.
float TraceGDF(float3 origin, float3 dir, float maxT) {
    float t = kMinStep;
    [loop]
    for (uint i = 0; i < kMaxStepsPerRay; ++i) {
        float3 p = origin + dir * t;
        float d = SampleGdfDistance(p);
        if (d < kSurfaceEpsilon) {
            return t;
        }
        t += max(d, kMinStep);
        if (t >= maxT) {
            return -1.0;
        }
    }
    return -1.0;
}

// ─── Hammersley + cosine-hemisphere sample ───────────────────────────────

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

// Maps a [0,1)^2 sample onto a unit sphere — full-sphere distribution so the
// L2 SH bands are excited symmetrically (we don't have a single normal at the
// probe location).
float3 SphericalSample(float2 xi) {
    float z = 1.0 - 2.0 * xi.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * kPI * xi.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

// ─── Entry point — one thread per probe ──────────────────────────────────

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // Temporal probe spreading: each frame updates `g_probeBudget` probes
    // starting at `g_probeOffset`, wrapping around the grid. Skipping probes
    // outside the budget keeps GPU cost bounded for large grids.
    uint localIdx = DTid.x;
    uint budget = (g_probeBudget == 0) ? totalProbes : g_probeBudget;
    if (localIdx >= budget) return;
    uint probeIdx = (g_probeOffset + localIdx) % totalProbes;
    if (probeIdx >= totalProbes) return;

    // Probe coordinate inside the cube grid.
    uint gx = probeIdx % probesPerAxis;
    uint gy = (probeIdx / probesPerAxis) % probesPerAxis;
    uint gz = probeIdx / (probesPerAxis * probesPerAxis);

    float3 probePos = float3(gridOriginX, gridOriginY, gridOriginZ)
                    + float3(float(gx), float(gy), float(gz)) * probeSpacing;

    float shR[9] = {0,0,0,0,0,0,0,0,0};
    float shG[9] = {0,0,0,0,0,0,0,0,0};
    float shB[9] = {0,0,0,0,0,0,0,0,0};

    uint nRays = min(raysPerProbe, kMaxRaysPerProbe);
    float invN = 1.0 / float(nRays);

    // Per-frame jitter — rotate the Hammersley sequence so different frames
    // sample different directions, then EMA-blend in time.
    uint frameJitter = (frameIndex * 1664525u + 1013904223u);

    [loop]
    for (uint i = 0; i < nRays; ++i) {
        float2 xi = Hammersley(i + (frameJitter & 0xFFFFu), nRays + (frameJitter & 0xFFu));
        xi = frac(xi); // keep in [0,1)
        float3 dir = SphericalSample(xi);

        float t = TraceGDF(probePos, dir, maxTraceDistance);

        float3 radiance;
        if (t < 0.0) {
            // Miss → sky.
            radiance = float3(skyColorR, skyColorG, skyColorB);
        } else {
            // Hit → single-bounce sun lighting on a Lambertian surface of
            // average ground albedo. We don't know the actual normal at the
            // hit point (no second SDF gradient sample), so use the negated
            // ray direction as a conservative outward normal.
            float3 N    = -dir;
            float3 L    = normalize(float3(lightDirX, lightDirY, lightDirZ));
            float  NdL  = max(dot(N, L), 0.0);
            float3 sunRad = float3(lightColorR, lightColorG, lightColorB)
                          * lightIntensity * NdL;
            radiance = sunRad * groundAlbedo;
        }

        // Accumulate into raw L2 SH (Sloan 2008). Cosine convolution is
        // applied at evaluation time by EvalSH_L2 — store raw bands here.
        float basis[9];
        SHEvaluateL2(dir, basis);

        [unroll]
        for (uint b = 0; b < 9; ++b) {
            // Monte-Carlo: 4π / N (full-sphere uniform distribution).
            float w = basis[b] * 4.0 * kPI * invN;
            shR[b] += radiance.r * w;
            shG[b] += radiance.g * w;
            shB[b] += radiance.b * w;
        }
    }

    // Pack into 7 float4s (matches deferred lighting `EvalSH_L2`).
    float4 newPacked[7];
    PackL2(shR, shG, shB, newPacked);

    // ── Hysteresis: temporal blend with previous SH ────────────────────
    // @ref Majercik 2019 "Dynamic Diffuse Global Illumination" — alpha 0.05–0.15
    RWStructuredBuffer<float4> shBuf = ResourceDescriptorHeap[g_shBufferUavIndex];
    uint base = probeIdx * 7u;

    float a = (firstWrite != 0) ? 1.0 : saturate(temporalBlendAlpha);
    [unroll]
    for (uint k = 0; k < 7; ++k) {
        float4 prev = shBuf[base + k];
        float4 blended = lerp(prev, newPacked[k], a);
        shBuf[base + k] = blended;
    }
}
