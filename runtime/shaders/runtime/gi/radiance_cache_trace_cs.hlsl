/// UzEngine Radiance Cache Trace — SDF-based probe update.
///
/// Updates one L2 SH probe per thread. For every probe in the grid we shoot
/// `raysPerProbe` uniformly-distributed sphere rays, sphere-trace them through
/// the global distance field clipmap hierarchy, and accumulate the resulting
/// radiance into the probe's L2 SH coefficients (compact 7-float4 packing —
/// see gi_hit_lighting.hlsli for the format contract).
///
/// Hit points use the shared surface-cache-lite model (GIHit_Radiance):
///   - Real surface normal from the GDF central-difference gradient.
///   - Sun visibility from a second GDF sphere-trace toward the light.
///   - Scene-average albedo (CPU-computed from the draw list materials).
///   - Multi-bounce feedback: previous-frame probe irradiance at the hit.
/// Missed rays sample the atmosphere sky cubemap in the ray direction
/// (falling back to the flat skylight color when the cubemap is absent).
/// The new SH is EMA-blended with the prior frame to suppress noise.
///
/// @ref Sloan 2008 "Stupid Spherical Harmonics (SH) Tricks" — L2 projection
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Probes" — hysteresis + infinite bounces
/// @ref Wright/Crassin 2018 "Distance Field Ray Tracing" — clipmap stepping
/// @ref Ramamoorthi & Hanrahan 2001 — SH irradiance environment maps

#include "../common/constants.hlsli"
#include "gi_hit_lighting.hlsli"

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
    uint  _padTC;

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
    float groundAlbedo;          // Legacy scalar fallback (kept for layout).

    float sceneAlbedoR;          // CPU-computed average scene albedo (linear).
    float sceneAlbedoG;
    float sceneAlbedoB;
    uint  skyCubemapSrv;         // Atmosphere sky cubemap SRV (sentinel = flat sky).
};

// ─── Constants ────────────────────────────────────────────────────────────

static const float kPI            = 3.14159265358979323846;
static const float kSurfaceEpsilon = 5.0;     // cm — accept hits below this distance
static const float kMinStep        = 2.0;     // cm — never advance less than this
static const uint  kMaxStepsPerRay = 96;
static const uint  kMaxRaysPerProbe = 64;

// ─── L2 SH accumulation/packing ─────────────────────────────────────────────
// Raw basis (GISH_EvaluateBasis) + compact pack (GISH_PackCompact) come from
// gi_hit_lighting.hlsli and are exact ports of the CPU projector — see the
// format contract there. The previous local SHEvaluateL2/PackL2 pair stored
// raw bands with mismatched sign/order into the compact layout, which
// EvalSH_L2 then read as if convolution had been folded in (L0 ~4× hot,
// L1/L2 lobes swapped).

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

// ─── Hit-point queries (surface-cache-lite inputs) ─────────────────────────

/// Outward surface normal from the GDF gradient (central differences, 6 taps).
/// Flipped to face the incoming ray so backface/inside hits still shade sanely.
/// @ref Hart 1996 "Sphere Tracing" — SDF gradient as surface normal
float3 GdfHitNormal(float3 p, float3 rayDir) {
    const float e = kSurfaceEpsilon;
    float3 g = float3(
        SampleGdfDistance(p + float3(e, 0, 0)) - SampleGdfDistance(p - float3(e, 0, 0)),
        SampleGdfDistance(p + float3(0, e, 0)) - SampleGdfDistance(p - float3(0, e, 0)),
        SampleGdfDistance(p + float3(0, 0, e)) - SampleGdfDistance(p - float3(0, 0, e)));
    float len = length(g);
    float3 n = (len > 1e-5) ? g / len : -rayDir;
    // A surface visible along `rayDir` must face against it.
    return (dot(n, rayDir) > 0.0) ? -n : n;
}

/// Sun visibility at a hit point: 1 when a GDF sphere-trace toward the light
/// escapes, 0 when it re-hits geometry. This is what stops enclosed interiors
/// from receiving direct sun injection through walls.
static const uint kShadowMaxSteps = 32;
float GdfSunVisibility(float3 p, float3 n, float3 toLight, float maxT) {
    // Push off along BOTH the normal and the light direction so the trace
    // starts outside the kSurfaceEpsilon acceptance band of the surface.
    float3 origin = p + n * (2.0 * kSurfaceEpsilon);
    float t = kMinStep;
    [loop]
    for (uint i = 0; i < kShadowMaxSteps; ++i) {
        float3 q = origin + toLight * t;
        float d = SampleGdfDistance(q);
        if (d < kSurfaceEpsilon) {
            return 0.0;
        }
        t += max(d, kMinStep);
        if (t >= maxT) {
            break;
        }
    }
    return 1.0;
}

/// Previous-frame probe irradiance at a world position (nearest probe, one
/// 7-float4 fetch). Feeds the multi-bounce term — reading the same buffer the
/// dispatch writes is intentional: probes outside this frame's update window
/// hold last frame's converged values, and the EMA keeps any same-frame
/// mixing stable. @ref Majercik 2019 §"infinite bounces"
float3 ProbeIrradianceAt(float3 worldPos, float3 n) {
    RWStructuredBuffer<float4> shBuf = ResourceDescriptorHeap[g_shBufferUavIndex];
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

/// Sky radiance for a miss ray: atmosphere cubemap when available (directional
/// sky — bright toward the sun horizon, dark opposite), else the flat
/// skylight color the old path used.
/// Z-up world direction → Y-up DX cubemap convention (engine-wide swizzle).
float3 SkyRadiance(float3 dir) {
    if (skyCubemapSrv != kSentinelSrvIndex) {
        TextureCube<float4> sky = ResourceDescriptorHeap[skyCubemapSrv];
        SamplerState samp = SamplerDescriptorHeap[1];
        return sky.SampleLevel(samp, float3(dir.x, dir.z, dir.y), 0).rgb;
    }
    return float3(skyColorR, skyColorG, skyColorB);
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

    float3 L = normalize(float3(lightDirX, lightDirY, lightDirZ));
    float3 sunColorIntensity = float3(lightColorR, lightColorG, lightColorB)
                             * lightIntensity;
    float3 sceneAlbedo = float3(sceneAlbedoR, sceneAlbedoG, sceneAlbedoB);

    [loop]
    for (uint i = 0; i < nRays; ++i) {
        float2 xi = Hammersley(i + (frameJitter & 0xFFFFu), nRays + (frameJitter & 0xFFu));
        xi = frac(xi); // keep in [0,1)
        float3 dir = SphericalSample(xi);

        float t = TraceGDF(probePos, dir, maxTraceDistance);

        float3 radiance;
        if (t < 0.0) {
            // Miss → directional sky (atmosphere cubemap or flat fallback).
            radiance = SkyRadiance(dir);
        } else {
            // Hit → surface-cache-lite shading (gi_hit_lighting.hlsli):
            // real GDF-gradient normal, sun occlusion trace, scene-average
            // albedo, and previous-frame probe irradiance as the
            // multi-bounce feedback term.
            float3 hitPos = probePos + dir * t;
            float3 n      = GdfHitNormal(hitPos, dir);
            float  sunVis = GdfSunVisibility(hitPos, n, L, maxTraceDistance);
            float3 bounce = ProbeIrradianceAt(hitPos + n * probeSpacing * 0.25, n);
            radiance = GIHit_Radiance(sceneAlbedo, float3(0, 0, 0), n,
                                      L, sunColorIntensity, sunVis, bounce);
        }

        // Accumulate raw radiance-projection coefficients with the SAME
        // basis the CPU projector uses; convolution constants are folded at
        // pack time by GISH_PackCompact (see gi_hit_lighting.hlsli).
        float basis[9];
        GISH_EvaluateBasis(dir, basis);

        [unroll]
        for (uint b = 0; b < 9; ++b) {
            // Monte-Carlo: 4π / N (full-sphere uniform distribution).
            float w = basis[b] * 4.0 * kPI * invN;
            shR[b] += radiance.r * w;
            shG[b] += radiance.g * w;
            shB[b] += radiance.b * w;
        }
    }

    // Pack into the engine-wide compact 7-float4 layout (EvalSH_L2-ready).
    float4 newPacked[7];
    GISH_PackCompact(shR, shG, shB, newPacked);

    // ── Hysteresis: temporal blend with previous SH ────────────────────
    // @ref Majercik 2019 "Dynamic Diffuse Global Illumination" — alpha 0.05–0.15
    RWStructuredBuffer<float4> shBuf = ResourceDescriptorHeap[g_shBufferUavIndex];
    uint base = probeIdx * 7u;

    float a = saturate(temporalBlendAlpha);
    [unroll]
    for (uint k = 0; k < 7; ++k) {
        float4 prev = shBuf[base + k];
        float4 blended = lerp(prev, newPacked[k], a);
        shBuf[base + k] = blended;
    }
}
