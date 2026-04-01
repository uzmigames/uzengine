/// Environment MIS temporal accumulation compute shader.
///
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" Chapter 14.6 — MIS
/// @ref Veach 1997 "Robust Monte Carlo Methods for Light Transport Simulation"
/// @ref Karis 2014 "High Quality Temporal Supersampling" — temporal accumulation
///
/// This compute shader:
/// 1. Reads GBuffer (albedo, normal, roughness, metallic, depth, worldPos)
/// 2. For metallic pixels, samples the environment via alias table (4 spp)
/// 3. Evaluates GGX BRDF + MIS balance heuristic
/// 4. Blends with previous frame's result (exponential moving average)
/// 5. Outputs clean temporally-accumulated specular contribution
///
/// The deferred lighting pass reads the accumulated result as an additive
/// specular term, avoiding per-pixel noise.
///
/// Root constants at b0: { albedoIdx, normalIdx, depthIdx, positionIdx, width,
///                         height, historyReadSrvIdx, outputUavIdx }
/// PerFrameCB at b1 (standard 768-byte layout)
/// s0: linear sampler, s2: clamp sampler

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"
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

// ─── Hash function for per-pixel decorrelation ──────────────────────────
/// @ref Jarzynski & Olano 2020 "Hash Functions for GPU Rendering"
uint pcg_hash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
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

    // Sky pixels or no alias table → zero contribution
    if (z <= 0.0001 || g_aliasSrvIdx == kSentinelSrvIndex) {
        outputTex[px] = float4(0, 0, 0, 0);
        return;
    }

    // ── Decode GBuffer ───────────────────────────────────────────────────
    float3 baseColor = rt0.rgb;
    float  metallic  = rt1.b;
    float  roughness = max(rt1.a, 0.04);
    float  alpha     = roughness * roughness;
    float3 N         = OctDecode(rt1.rg);
    float3 F0        = ComputeF0(baseColor, metallic);
    float3 V         = normalize(cameraPos.xyz - worldPos);
    float  NoV       = max(dot(N, V), 1e-4);

    // Only compute MIS for metallic surfaces with low-to-medium roughness.
    // Rough/dielectric surfaces are well-served by split-sum IBL.
    float3 newSample = float3(0, 0, 0);

    if (metallic > 0.3 && roughness < 0.7) {
        StructuredBuffer<AliasEntry> aliasTable = ResourceDescriptorHeap[g_aliasSrvIdx];
        uint texelCount = g_lumWidth * g_lumHeight;

        // Per-pixel hash seed for decorrelation (PCG hash avoids row/column patterns)
        uint seed = pcg_hash(px.x + px.y * g_width + g_frameIndex * 1000003u);

        const uint kSamples = 4;
        for (uint i = 0; i < kSamples; ++i) {
            // Hammersley sequence decorrelated by PCG hash
            float2 u = Hammersley(i, kSamples);
            uint seed_i = pcg_hash(seed + i);
            u = frac(u + float2(
                float(seed_i & 0xFFFFu) / 65536.0,
                float(seed_i >> 16u) / 65536.0));

            EnvSample envS = SampleEnvironment(
                u, aliasTable, texelCount,
                g_lumMip0SrvIdx, g_totalLuminance,
                g_envCubemapSrvIdx,
                g_lumWidth, g_lumHeight,
                clampSampler);

            float3 L    = envS.direction;
            float  NoL  = max(dot(N, L), 0.0);

            if (NoL > 0.0 && envS.pdf > 0.0) {
                float3 H   = normalize(V + L);
                float  NoH = saturate(dot(N, H));
                float  VoH = saturate(dot(V, H));

                float  D   = D_GGX(alpha, NoH);
                float  Vis = V_SmithGGXCorrelated(alpha, NoV, NoL);
                float3 F   = F_Schlick(F0, float3(1,1,1), VoH);
                float3 brdf = D * Vis * F;

                // BRDF PDF for MIS weight: D(H) * NoH / (4 * VoH)
                float pdfBrdf = D * NoH / max(4.0 * VoH, 1e-6);
                float pdfEnv  = envS.pdf;

                // Balance heuristic: w_env = pdfEnv / (pdfEnv + pdfBrdf)
                float misWeight = pdfEnv / (pdfEnv + pdfBrdf);

                newSample += brdf * envS.color * NoL * misWeight / pdfEnv;
            }
        }
        newSample /= float(kSamples);

        // Apply sky tint
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

    outputTex[px] = float4(result, 1.0);
}
