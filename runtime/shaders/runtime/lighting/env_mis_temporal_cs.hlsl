/// Environment MIS temporal accumulation compute shader.
///
/// @ref Veach 1997 "Robust Monte Carlo Methods for Light Transport Simulation" — multiple importance
///      sampling, balance heuristic
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" Chapter 14.6 — environment sampling
/// @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps" — the BRDF strategy
/// @ref Karis 2014 "High Quality Temporal Supersampling" — temporal accumulation
///
/// For the pixels it covers (metals below the roughness threshold, the coverage fading to 0 across the
/// thresholds) the shader estimates the specular environment term
///     int f(V, L) L_env(L) (N.L) dL
/// with two sampling strategies combined by the balance heuristic (block B.5b, 2026-10-08):
///   1. environment samples drawn through the alias table (the sampler's luminance pyramid), weighted
///      by p_env / (p_env + p_brdf); the sampler hands back world directions (its pyramid lives in the
///      cube's Y-up frame; block B.5b found the directions used unconverted, i.e. y and z swapped);
///   2. BRDF samples drawn from the visible GGX normals, weighted by p_brdf / (p_brdf + p_env).
/// Each strategy alone is an unbiased estimator; their weighted sum keeps the variance low where the
/// other strategy is hopeless (a mirror: the environment strategy hits the lobe with probability ~1e-6;
/// a broad sky: the BRDF strategy of a rough lobe is noisy). The earlier shader drew environment samples
/// only, weighted them with the balance heuristic as if the BRDF strategy existed, and so converged to
/// the environment half of the sum: near black on smooth metals, dark everywhere else.
///
/// The result is accumulated with an exponential moving average into a ping-pong history (no
/// reprojection). The output's alpha is the coverage: the deferred lighting blends the estimate over the
/// split-sum specular by it, so dielectrics and rough metals keep theirs.
///
/// Root constants at b0 (16): { albedoIdx, normalIdx, depthIdx, positionIdx, width, height,
///   historyReadSrvIdx, outputUavIdx, envCubemapSrvIdx, aliasSrvIdx, lumMip0SrvIdx, lumWidth, lumHeight,
///   frameIndex, totalLuminance, temporalAlpha }
/// PerFrameCB at b1 (the engine's layout; cameraPos and the sky light tint are read)
/// s0: linear sampler, s2: clamp sampler

// Env MIS requires 7+ bindless SRV/UAV bindings which exceeds SM5.0 limits.
// On SM5 this shader is a no-op; env MIS is only available with bindless (SM6.6).
#ifndef UZ_BINDLESS
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {}
#else

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"
#include "../common/math.hlsli"
#include "../common/sampling.hlsli"
#include "../material/brdf.hlsli"
#include "../include/envmap_sampling.hlsli"

// ─── Root Constants (b0) ──────────────────────────────────────────────────
cbuffer RootConstants : register(b0) {
    uint g_albedoIdx;
    uint g_normalIdx;
    uint g_depthIdx;
    uint g_positionIdx;
    uint g_width;
    uint g_height;
    uint g_historyReadSrvIdx;  ///< Previous frame's accumulated result (UINT32_MAX = first frame)
    uint g_outputUavIdx;       ///< Current frame output UAV
    uint g_envCubemapSrvIdx;   ///< Environment cubemap SRV for sampling
    uint g_aliasSrvIdx;        ///< Alias table StructuredBuffer SRV
    uint g_lumMip0SrvIdx;      ///< Luminance pyramid mip 0 SRV
    uint g_lumWidth;           ///< Luminance pyramid mip 0 width
    uint g_lumHeight;          ///< Luminance pyramid mip 0 height
    uint g_frameIndex;         ///< Frame counter for temporal jitter
    float g_totalLuminance;    ///< Total env luminance for PDF normalization
    float g_temporalAlpha;     ///< EMA blend factor (0.05 = 95% history, 5% new)
};

// ─── Camera / scene data from PerFrameCB (b1) ────────────────────────────
// The prefix of the engine's PerFrameCB (constant_buffers.h); the pass binds the frame's allocation.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
    uint   irradianceSrvIndex, prefilteredSrvIndex;
    uint   brdfLutSrvIndex, prefilteredMipLevels;
    float  skyLightIntensity, skyLightColorR, skyLightColorG, skyLightColorB;
};

SamplerState linearSampler : register(s0);
SamplerState clampSampler  : register(s2);

/// Coverage thresholds: the estimate replaces the split-sum specular on metals below the roughness
/// threshold; it fades to 0 across the bands so the hand-over leaves no seam.
static const float kCoverageMetallicLo  = 0.3;
static const float kCoverageMetallicHi  = 0.5;
static const float kCoverageRoughnessLo = 0.55;
static const float kCoverageRoughnessHi = 0.7;
static const uint  kEnvSamples  = 4;
static const uint  kBrdfSamples = 4;
/// The lighting pass clamps the roughness to sqrt(r.Material.MinGGXAlpha) = 0.04 by default; the same
/// floor keeps the lobe's density finite on a mirror.
static const float kMinRoughness = 0.04;

// ─── Hash function for per-pixel decorrelation ──────────────────────────
/// @ref Jarzynski & Olano 2020 "Hash Functions for GPU Rendering"
uint pcg_hash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// Two uniform numbers from the running seed (the per-pixel, per-frame decorrelation of the sequences).
/// One hash per number: the alias table's bin selection needs the full float precision (131072 bins), a
/// 16-bit uniform reaches only every other bin.
float2 Rand2(inout uint seed) {
    seed = pcg_hash(seed);
    float a = float(seed) * 2.3283064365386963e-10;   // 1 / 2^32
    seed = pcg_hash(seed);
    float b = float(seed) * 2.3283064365386963e-10;
    return float2(a, b);
}

float MisCoverage(float metallic, float roughness) {
    return smoothstep(kCoverageMetallicLo, kCoverageMetallicHi, metallic)
         * (1.0 - smoothstep(kCoverageRoughnessLo, kCoverageRoughnessHi, roughness));
}

/// Density of the reflected direction under visible-normal sampling: p(L) = G1(V) D(H) / (4 N.V).
float PdfGGXVisible(float alpha, float NoV, float NoH) {
    return G1_SmithGGX(alpha * alpha, NoV) * D_GGX(alpha, NoH) / max(4.0 * NoV, 1e-6);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_width || DTid.y >= g_height) return;

    int2 px = int2(DTid.xy);

    // ── Read GBuffer ─────────────────────────────────────────────────────
    Texture2D<float4> gbAlbedo = ResourceDescriptorHeap[g_albedoIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[g_normalIdx];
    Texture2D<float>  gbDepth  = ResourceDescriptorHeap[g_depthIdx];
    Texture2D<float4> gbPos    = ResourceDescriptorHeap[g_positionIdx];

    float4 rt0 = gbAlbedo.Load(int3(px, 0));
    float4 rt1 = gbNormal.Load(int3(px, 0));
    float  z   = gbDepth.Load(int3(px, 0)).r;
    float3 worldPos = gbPos.Load(int3(px, 0)).xyz;

    // Output target
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_outputUavIdx];

    // Sky pixels or no alias table → no coverage
    if (z <= 0.0001 || g_aliasSrvIdx == kSentinelSrvIndex) {
        outputTex[px] = float4(0, 0, 0, 0);
        return;
    }

    // ── Decode GBuffer ───────────────────────────────────────────────────
    float3 baseColor = rt0.rgb;
    float  metallic  = rt1.b;
    float  roughness = max(rt1.a, kMinRoughness);
    float  alpha     = roughness * roughness;
    float3 N         = OctDecode(rt1.rg);
    float3 F0        = ComputeF0(baseColor, metallic);
    float3 V         = normalize(cameraPos.xyz - worldPos);
    float  NoV       = max(dot(N, V), 1e-4);
    float  coverage  = MisCoverage(metallic, roughness);

    float3 newSample = float3(0, 0, 0);

    if (coverage > 0.0) {
        StructuredBuffer<AliasEntry> aliasTable = ResourceDescriptorHeap[g_aliasSrvIdx];
        TextureCube<float4> envMap = ResourceDescriptorHeap[g_envCubemapSrvIdx];
        uint texelCount = g_lumWidth * g_lumHeight;

        float3 T, B;
        BuildOrthonormalBasis(N, T, B);
        float3 Vt = normalize(float3(dot(V, T), dot(V, B), dot(V, N)));

        // Per-pixel, per-frame seed (PCG hash avoids row/column patterns)
        uint seed = pcg_hash(px.x + px.y * g_width + g_frameIndex * 1000003u);

        // ── Strategy 1: environment samples through the alias table ──────
        for (uint i = 0; i < kEnvSamples; ++i) {
            float2 u = frac(Hammersley(i, kEnvSamples) + Rand2(seed));
            EnvSample envS = SampleEnvironment(
                u, aliasTable, texelCount,
                g_lumMip0SrvIdx, g_totalLuminance,
                g_envCubemapSrvIdx,
                g_lumWidth, g_lumHeight,
                clampSampler);

            float3 L   = envS.direction;
            float  NoL = dot(N, L);
            if (NoL > 0.0 && envS.pdf > 0.0) {
                float3 H   = normalize(V + L);
                float  NoH = saturate(dot(N, H));
                float  VoH = saturate(dot(V, H));

                float  D    = D_GGX(alpha, NoH);
                float  Vis  = V_SmithGGXCorrelated(alpha, NoV, NoL);
                float3 F    = F_Schlick(F0, float3(1, 1, 1), VoH);
                float3 brdf = D * Vis * F;

                float pdfBrdf = PdfGGXVisible(alpha, NoV, NoH);
                float w       = envS.pdf / (envS.pdf + pdfBrdf);
                newSample += brdf * envS.color * NoL * w / envS.pdf / float(kEnvSamples);
            }
        }

        // ── Strategy 2: BRDF samples from the visible GGX normals ─────────
        for (uint j = 0; j < kBrdfSamples; ++j) {
            float2 u  = frac(Hammersley(j, kBrdfSamples) + Rand2(seed));
            float3 Ht = SampleGGX_VNDF(alpha, Vt, u);
            float3 H  = normalize(T * Ht.x + B * Ht.y + N * Ht.z);
            float3 L  = 2.0 * dot(V, H) * H - V;
            float  NoL = dot(N, L);
            if (NoL > 0.0) {
                float NoH = saturate(dot(N, H));
                float VoH = saturate(dot(V, H));
                float pdfBrdf = PdfGGXVisible(alpha, NoV, NoH);
                if (pdfBrdf > 0.0) {
                    float  pdfEnv = EvaluateEnvironmentPDF(L, g_lumMip0SrvIdx, g_totalLuminance, texelCount);
                    float  D    = D_GGX(alpha, NoH);
                    float  Vis  = V_SmithGGXCorrelated(alpha, NoV, NoL);
                    float3 F    = F_Schlick(F0, float3(1, 1, 1), VoH);
                    float3 brdf = D * Vis * F;
                    float3 Lenv = envMap.SampleLevel(clampSampler, EnvWorldToCubemap(L), 0).rgb;
                    float  w    = pdfBrdf / (pdfBrdf + pdfEnv);
                    newSample += brdf * Lenv * NoL * w / pdfBrdf / float(kBrdfSamples);
                }
            }
        }

        // The sky light's tint and intensity, as the split-sum specular applies them.
        float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;
        newSample *= skyTint;
    }

    // ── Temporal accumulation (exponential moving average) ────────────────
    // alpha = 0.05 means 95% history + 5% new → converges in ~20 frames
    float3 result = newSample;

    if (g_historyReadSrvIdx != kSentinelSrvIndex) {
        Texture2D<float4> historyTex = ResourceDescriptorHeap[g_historyReadSrvIdx];
        float3 history = historyTex.Load(int3(px, 0)).rgb;
        result = lerp(history, newSample, g_temporalAlpha);
    }

    outputTex[px] = float4(result, coverage);
}

#endif // UZ_BINDLESS
