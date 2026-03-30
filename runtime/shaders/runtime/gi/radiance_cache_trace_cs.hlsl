/// UzEngine Radiance Cache Trace Compute Shader
///
/// Each thread handles one probe in the world-space probe grid.
/// Traces 32 rays from the probe center through the global distance field,
/// accumulates incoming radiance into L2 SH coefficients, and blends
/// with the previous frame's SH history via exponential moving average.
///
/// @ref Majercik et al. 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields"
/// @ref Wright 2021 "Radiance Caching for Real-Time Global Illumination"
/// @ref Hart 1996 "Sphere Tracing" — SDF-based ray marching for probe tracing
/// @constants kRaysPerProbe=32, kTemporalBlendAlpha=0.05, kSHFloat4PerProbe=7
/// @deviations Uses dense GDF clipmaps instead of the reference engine's paged GDF + mesh SDF hybrid.
///   Traces against GDF only (no near-field mesh SDF trace). Octahedral atlas replaced
///   with direct L2 SH accumulation for simpler integration with existing SH-based
///   deferred lighting path.

#include "../include/math_util.hlsli"
#include "../include/sdf_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (root param [0], 8x uint32 at b0)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer RootConstants : register(b0)
{
    uint g_shBufferUav;     // [0] UAV descriptor index for SH StructuredBuffer
    uint g_shBufferSrv;     // [1] SRV index (unused in trace, reserved for future)
    uint g_frameIndexRC;    // [2] Frame index via root constants fast path
    uint _rc_pad;           // [3]
    uint _rc_pad1;          // [4]
    uint _rc_pad2;          // [5]
    uint _rc_pad3;          // [6]
    uint _rc_pad4;          // [7]
};

// ─────────────────────────────────────────────────────────────────────────────
// Bulk Constants (root param [2], root CBV at b2 — supports up to 64KB)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer RadianceCacheTraceConstants : register(b2)
{
    float3  g_gridOrigin;        // World-space grid origin (minimum corner)
    float   g_probeSpacing;      // Distance between probes (cm)

    uint    g_probesPerAxis;     // Probes per axis (16)
    uint    g_totalProbes;       // Total probes (4096)
    uint    g_raysPerProbe;      // Rays per probe per frame (32)
    uint    g_frameIndex;        // Frame counter for temporal jitter

    float   g_maxTraceDistance;  // Maximum trace distance (cm)
    float   g_temporalBlendAlpha; // EMA blend factor (0.05)
    uint    _cb_reservedUav;     // Padding (UAV index now in root constants at b0)
    uint    _pad0;

    // GDF clipmap parameters (4 cascades)
    float4  g_gdfClipmap0;      // xyz = center, w = extent
    float4  g_gdfClipmap1;
    float4  g_gdfClipmap2;
    float4  g_gdfClipmap3;

    uint    g_gdfClipmapSrv0;   // SRV descriptor indices for GDF clipmap textures
    uint    g_gdfClipmapSrv1;
    uint    g_gdfClipmapSrv2;
    uint    g_gdfClipmapSrv3;

    uint    g_numGdfClipmaps;   // Number of active GDF clipmaps (1-4)
    float   g_skyColorR;        // Sky color for missed rays
    float   g_skyColorG;
    float   g_skyColorB;

    // Sun/light parameters for single-bounce GI
    float   g_lightDirX;        // Primary light direction (toward light)
    float   g_lightDirY;
    float   g_lightDirZ;
    float   g_lightIntensity;
    float   g_lightColorR;      // Primary light color
    float   g_lightColorG;
    float   g_lightColorB;
    float   g_groundAlbedo;     // Average scene albedo for bounce estimate
};

// ─────────────────────────────────────────────────────────────────────────────
// Sampler for GDF clipmap textures
// ─────────────────────────────────────────────────────────────────────────────

SamplerState g_linearClamp : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
RWStructuredBuffer<float4> g_ShBuffer          : register(u0);
// SM5/Vulkan: dynamic clipmap indexing requires bindless. This single fallback
// texture allows compilation; the pass should be disabled on non-bindless backends.
Texture3D<float>           g_GdfClipmapFallback : register(t0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Quasi-random direction sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a quasi-random point on the unit sphere using Fibonacci spiral
/// with per-probe and per-frame jitter for temporal variation.
///
/// @ref Keinert et al. 2015 "Spherical Fibonacci Point Sets" — uniform sphere sampling
float3 SphericalFibonacci(uint sampleIndex, uint totalSamples, uint seed)
{
    // Golden ratio for Fibonacci spiral
    static const float kGoldenRatio = 1.6180339887f;

    float i = float(sampleIndex) + float(seed % 97) * 0.618f;
    float n = float(totalSamples);

    float phi = M_2PI * frac(i / kGoldenRatio);
    float cosTheta = 1.0f - (2.0f * i + 1.0f) / n;
    float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));

    return float3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// L2 Spherical Harmonics accumulation
// ─────────────────────────────────────────────────────────────────────────────

/// SH basis functions for L2 (9 coefficients).
/// @ref Ramamoorthi & Hanrahan 2001 — L2 spherical harmonic basis functions
void GetSHBasis(float3 dir, out float basis[9])
{
    // L0
    basis[0] = 0.282095f; // 1 / (2*sqrt(pi))

    // L1
    basis[1] = 0.488603f * dir.y;   // sqrt(3) / (2*sqrt(pi)) * y
    basis[2] = 0.488603f * dir.z;   // sqrt(3) / (2*sqrt(pi)) * z
    basis[3] = 0.488603f * dir.x;   // sqrt(3) / (2*sqrt(pi)) * x

    // L2
    basis[4] = 1.092548f * dir.x * dir.y;                    // sqrt(15) / (2*sqrt(pi)) * xy
    basis[5] = 1.092548f * dir.y * dir.z;                    // sqrt(15) / (2*sqrt(pi)) * yz
    basis[6] = 0.315392f * (3.0f * dir.z * dir.z - 1.0f);   // sqrt(5) / (4*sqrt(pi)) * (3z^2-1)
    basis[7] = 1.092548f * dir.x * dir.z;                    // sqrt(15) / (2*sqrt(pi)) * xz
    basis[8] = 0.546274f * (dir.x * dir.x - dir.y * dir.y); // sqrt(15) / (4*sqrt(pi)) * (x^2-y^2)
}

/// Packs 3-channel L2 SH (9 * 3 = 27 floats) into 7 float4s.
/// Follows industry-standard SkyIrradianceEnvironmentMap packing:
///   [0] = (R_L00, R_L11, R_L10, R_L1_1)
///   [1] = (G_L00, G_L11, G_L10, G_L1_1)
///   [2] = (B_L00, B_L11, B_L10, B_L1_1)
///   [3] = (R_L20, R_L21, R_L22, R_L2_1)
///   [4] = (G_L20, G_L21, G_L22, G_L2_1)
///   [5] = (B_L20, B_L21, B_L22, B_L2_1)
///   [6] = (R_L2_2, G_L2_2, B_L2_2, 0)
///
/// @ref Ramamoorthi 2001 — SH-packed sky irradiance evaluation
void PackSHToFloat4(float shR[9], float shG[9], float shB[9], out float4 packed[7])
{
    // L0 + L1 per channel
    packed[0] = float4(shR[0], shR[3], shR[2], shR[1]);
    packed[1] = float4(shG[0], shG[3], shG[2], shG[1]);
    packed[2] = float4(shB[0], shB[3], shB[2], shB[1]);

    // L2 per channel (4 of 5 coefficients)
    packed[3] = float4(shR[6], shR[7], shR[8], shR[5]);
    packed[4] = float4(shG[6], shG[7], shG[8], shG[5]);
    packed[5] = float4(shB[6], shB[7], shB[8], shB[5]);

    // Remaining L2 coefficient per channel
    packed[6] = float4(shR[4], shG[4], shB[4], 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main compute entry point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Majercik et al. 2019 "DDGI" — probe tracing entry point
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint probeIndex = dispatchThreadId.x;
    if (probeIndex >= g_totalProbes)
    {
        return;
    }

    // ── Bindless SH buffer access via root constants UAV index ───────
#ifdef UZ_BINDLESS
    RWStructuredBuffer<float4> shBuffer = ResourceDescriptorHeap[g_shBufferUav];
#else
    RWStructuredBuffer<float4> shBuffer = g_ShBuffer;
#endif

    // ── Bindless GDF clipmap texture SRV indices ─────────────────────
    uint gdfSrvIndices[4] = { g_gdfClipmapSrv0, g_gdfClipmapSrv1, g_gdfClipmapSrv2, g_gdfClipmapSrv3 };

    // ── Compute probe world position from its 3D grid index ────────────
    uint probeZ = probeIndex / (g_probesPerAxis * g_probesPerAxis);
    uint remain = probeIndex % (g_probesPerAxis * g_probesPerAxis);
    uint probeY = remain / g_probesPerAxis;
    uint probeX = remain % g_probesPerAxis;

    float3 probePos = g_gridOrigin + (float3(probeX, probeY, probeZ) + 0.5f) * g_probeSpacing;

    // ── Trace rays and accumulate SH ───────────────────────────────────
    float shR[9] = {0,0,0,0,0,0,0,0,0};
    float shG[9] = {0,0,0,0,0,0,0,0,0};
    float shB[9] = {0,0,0,0,0,0,0,0,0};

    // Per-probe seed: combine probe index with frame index for temporal variation
    uint seed = probeIndex * 17 + g_frameIndex * 7919;

    for (uint ray = 0; ray < g_raysPerProbe; ray++)
    {
        // Generate ray direction via Fibonacci spiral with temporal jitter
        float3 rayDir = SphericalFibonacci(ray, g_raysPerProbe, seed);

        // ── Sphere march through GDF clipmaps (finest cascade first) ────
        // Default radiance for rays that miss all geometry: full sky in all directions.
        // For probes in open space, the lower hemisphere also sees sky (no ground plane
        // in the SDF). When a ray hits geometry, the hit shading handles bounced light.
        float3 skyColor = float3(g_skyColorR, g_skyColorG, g_skyColorB);
        float3 lightDir = normalize(float3(g_lightDirX, g_lightDirY, g_lightDirZ));
        float3 lightCol = float3(g_lightColorR, g_lightColorG, g_lightColorB);
        // Lower hemisphere: Lambertian ground bounce = albedo * skyIrradiance.
        // Upper hemisphere irradiance integral = PI * skyColor, then BRDF = albedo/PI,
        // so reflected = albedo * skyColor. For missed downward rays, use this.
        float3 groundBounce = g_groundAlbedo * skyColor;
        float3 radiance = (rayDir.z >= 0.0f) ? skyColor : groundBounce;

        float4 clipmaps[4] = { g_gdfClipmap0, g_gdfClipmap1, g_gdfClipmap2, g_gdfClipmap3 };

        float t = 0.0f;
        static const uint kMaxMarchSteps = 32;
        // Minimum step size = voxel size of the finest cascade (assumes 64^3 resolution)
        float voxelSize0 = (clipmaps[0].w * 2.0f) / 64.0f;

        for (uint step = 0; step < kMaxMarchSteps; step++)
        {
            float3 samplePos = probePos + rayDir * t;

            // Find the finest cascade that contains this sample position
            // and sample its GDF texture via bindless descriptor heap access
            float dist = g_maxTraceDistance;

            for (uint c = 0; c < g_numGdfClipmaps; c++)
            {
                float3 center = clipmaps[c].xyz;
                float extent = clipmaps[c].w;

                // Check if sample position is inside this cascade's AABB
                float3 localPos = abs(samplePos - center);
                if (all(localPos < extent))
                {
                    // Compute normalized UVW coordinates within the cascade volume
                    // Maps world position to [0,1]^3 texture space
                    float3 uvPos = (samplePos - center + extent) / (extent * 2.0f);

                    // Bindless GDF texture sampling — each cascade has its own
                    // SRV index in the descriptor heap
#ifdef UZ_BINDLESS
                    Texture3D<float> gdfTex = ResourceDescriptorHeap[gdfSrvIndices[c]];
#else
                    Texture3D<float> gdfTex = g_GdfClipmapFallback;
#endif
                    dist = gdfTex.SampleLevel(g_linearClamp, uvPos, 0).r;
                    break;
                }
            }

            if (dist < voxelSize0)
            {
                // Surface hit: compute single-bounce diffuse GI.
                // Estimate surface normal from the GDF gradient via central differences,
                // then compute NdotL with the primary directional light.
                // Radiance = lightColor * albedo * max(NdotL, 0) + ambient
                float3 hitPos = samplePos;

                // Estimate normal via central differences on the GDF
                float eps = voxelSize0 * 2.0f;
                float3 hitNormal = float3(0, 0, 1);
                // Find the cascade that contains the hit point for gradient sampling
                for (uint nc = 0; nc < g_numGdfClipmaps; nc++)
                {
                    float3 ctr = clipmaps[nc].xyz;
                    float ext = clipmaps[nc].w;
                    if (all(abs(hitPos - ctr) < ext))
                    {
#ifdef UZ_BINDLESS
                        Texture3D<float> gTex = ResourceDescriptorHeap[gdfSrvIndices[nc]];
#else
                        Texture3D<float> gTex = g_GdfClipmapFallback;
#endif
                        float invExt2 = 1.0f / (ext * 2.0f);
                        float3 uvC = (hitPos - ctr + ext) * invExt2;
                        float3 epsUV = float3(eps * invExt2, 0, 0);
                        float dx = gTex.SampleLevel(g_linearClamp, uvC + epsUV.xyz, 0).r
                                 - gTex.SampleLevel(g_linearClamp, uvC - epsUV.xyz, 0).r;
                        float dy = gTex.SampleLevel(g_linearClamp, uvC + epsUV.yxz, 0).r
                                 - gTex.SampleLevel(g_linearClamp, uvC - epsUV.yxz, 0).r;
                        float dz = gTex.SampleLevel(g_linearClamp, uvC + epsUV.yzx, 0).r
                                 - gTex.SampleLevel(g_linearClamp, uvC - epsUV.yzx, 0).r;
                        hitNormal = normalize(float3(dx, dy, dz));
                        break;
                    }
                }

                float NdotL = max(dot(hitNormal, lightDir), 0.0f);
                // Single-bounce Lambertian reflection: BRDF = albedo/PI
                // Direct: (lightColor * NdotL) * (albedo/PI)
                // Ambient: sky irradiance (PI * skyColor for uniform sky) * (albedo/PI) = albedo * skyColor
                static const float kInvPI = 0.31830988f;
                radiance = lightCol * g_groundAlbedo * kInvPI * NdotL
                         + skyColor * g_groundAlbedo;
                break;
            }

            // Advance along the ray by the distance field value, clamped to
            // the minimum voxel size to avoid zero-step stalling
            t += max(dist, voxelSize0);
            if (t > g_maxTraceDistance)
            {
                break;
            }
        }

        // ── Accumulate into SH basis ──────────────────────────────────
        float basis[9];
        GetSHBasis(rayDir, basis);

        for (uint shIdx = 0; shIdx < 9; shIdx++)
        {
            shR[shIdx] += radiance.x * basis[shIdx];
            shG[shIdx] += radiance.y * basis[shIdx];
            shB[shIdx] += radiance.z * basis[shIdx];
        }
    }

    // ── Normalize SH by ray count ──────────────────────────────────────
    /// The SH projection integral over the sphere is approximated by
    /// (4 * pi / N) * sum(radiance * basis). The 4*pi factor accounts for
    /// the full sphere solid angle.
    float normFactor = (4.0f * PI) / float(g_raysPerProbe);
    for (uint shIdx = 0; shIdx < 9; shIdx++)
    {
        shR[shIdx] *= normFactor;
        shG[shIdx] *= normFactor;
        shB[shIdx] *= normFactor;
    }

    // ── Pack into 7 float4s ────────────────────────────────────────────
    float4 newSH[7];
    PackSHToFloat4(shR, shG, shB, newSH);

    // ── Temporal blend with previous frame's SH (EMA) ──────────────────
    /// @ref DDGI temporal blending — ProbeHistoryWeight = 0.95
    uint baseIdx = probeIndex * 7;

    // Detect uninitialized probes (L0 == 0 for all channels) and write
    // directly without blending. This avoids 90+ frames of slow convergence
    // from zero on first load or after probe grid scroll.
    float4 prevL0 = shBuffer[baseIdx];
    bool isUninitialized = (prevL0.x == 0.0f && prevL0.y == 0.0f && prevL0.z == 0.0f && prevL0.w == 0.0f);
    float alpha = isUninitialized ? 1.0f : g_temporalBlendAlpha;

    for (uint i = 0; i < 7; i++)
    {
        float4 prevSH = shBuffer[baseIdx + i];
        shBuffer[baseIdx + i] = lerp(prevSH, newSH[i], alpha);
    }
}
