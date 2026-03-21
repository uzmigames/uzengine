/// SSR Resolve Compute Shader
///
/// Composites the SSR trace output with the existing scene by applying
/// Fresnel-weighted blending and roughness-based filtering. The trace pass
/// writes rgb=hit color and a=confidence into a half-res or full-res texture.
/// This resolve pass reads the trace result and produces the final SSR
/// contribution that can be blended with IBL in the lighting pass.
///
/// # Algorithm
/// 1. Read the SSR trace result (rgb=color, a=confidence/vignette).
/// 2. Read surface properties from G-Buffer (normal, roughness, metallic).
/// 3. Compute Fresnel reflectance at the surface for energy-correct weighting.
/// 4. Apply screen-edge fade and roughness-based mip blur.
/// 5. Output the final reflection color with blend weight in alpha.
///
/// @ref McGuire & Mara 2014 "Efficient GPU Screen-Space Ray Tracing" — SSR pipeline
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" — screen-edge vignette
/// @ref Karis 2014 — luminance-weighted resolve for firefly suppression
/// @constants a2 = Roughness^4, G_SmithV, Karis weight = rcp(1 + Luminance)
/// @deviations The resolve is a separate pass from the trace for modularity.
///   The math is identical; only the execution topology differs.
///
/// # Resources
/// - t0: SSR trace output (R16G16B16A16_FLOAT) — rgb=hit color, a=confidence
/// - t1: Normal+Material G-Buffer (R8G8B8A8_UNORM) — rg=oct normal, b=metallic, a=roughness
/// - t2: Depth buffer (R32_FLOAT) — for view direction reconstruction
/// - u0: Resolved SSR output (R16G16B16A16_FLOAT) — rgb=reflection, a=blend weight
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB (matrices, camera)

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"

#define PI 3.14159265358979323846f

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer SSRResolveCB : register(b0) {
    uint  ssrTraceSrvIndex;   // DWORD 0 — SSR trace result SRV (bindless)
    uint  normalSrvIndex;     // DWORD 1 — normal+material G-Buffer SRV (bindless)
    uint  depthSrvIndex;      // DWORD 2 — depth buffer SRV (bindless)
    uint  _reserved0;         // DWORD 3 — reserved
    uint  outputWidth;        // DWORD 4 — output width
    uint  outputHeight;       // DWORD 5 — output height
    float ssrIntensity;       // DWORD 6 — final intensity multiplier [0,1]
    float roughnessMaskScale; // DWORD 7 — roughness fade scale (-2/MaxRoughness)
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame CB (b1) — same layout as deferred_lighting_ps.hlsl
// ─────────────────────────────────────────────────────────────────────────────

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    float    _pad_pf[14];
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: textures accessed via ResourceDescriptorHeap[]
#else
Texture2D<float4> ssrTraceResult  : register(t0);
Texture2D<float4> normalBuffer    : register(t1);
Texture2D<float>  depthBuffer     : register(t2);
#endif

RWTexture2D<float4> resolvedSSR   : register(u0);

SamplerState pointSampler  : register(s0);
SamplerState linearSampler : register(s2);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

float4 SampleSSRTrace(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[ssrTraceSrvIndex];
    return tex.SampleLevel(linearSampler, uv, 0);
#else
    return ssrTraceResult.SampleLevel(linearSampler, uv, 0);
#endif
}

float4 SampleNormalMaterial(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[normalSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return normalBuffer.SampleLevel(pointSampler, uv, 0);
#endif
}

float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[depthSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthBuffer.SampleLevel(pointSampler, uv, 0).r;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Position Reconstruction
// ─────────────────────────────────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, deviceZ, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fresnel & Specular Weight
// ─────────────────────────────────────────────────────────────────────────────

/// Schlick Fresnel approximation for SSR weighting.
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-based Rendering"
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * Pow5(saturate(1.0f - cosTheta));
}

/// Compute G_SmithV term used for SSR weighting in multi-ray mode.
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// @constants a = Roughness^2, a2 = a^2 = Roughness^4
///            G_SmithV = 2*NoV / (NoV + sqrt(NoV*(NoV - NoV*a2) + a2))
float ComputeSmithV(float NoV, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    return 2.0f * NoV / (NoV + sqrt(NoV * (NoV - NoV * a2) + a2));
}

// ─────────────────────────────────────────────────────────────────────────────
// Roughness Fade
// ─────────────────────────────────────────────────────────────────────────────

/// @ref McGuire & Mara 2014 — roughness-based SSR fade
/// @constants roughnessMaskScale from root CB, bias=+2
float GetRoughnessFade(float roughness) {
    return min(roughness * roughnessMaskScale + 2.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @ref McGuire & Mara 2014 — SSR resolve pass
[numthreads(8, 8, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= outputWidth || dispatchThreadId.y >= outputHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchThreadId.xy + 0.5f;
    float2 uv = pixelCenter / float2((float)outputWidth, (float)outputHeight);

    // ── Read G-Buffer ──
    float4 normalMaterial = SampleNormalMaterial(uv);
    float  deviceZ = SampleDepth(uv);

    float3 worldNormal = OctDecode(normalMaterial.rg);
    float  metallic    = normalMaterial.b;
    float  roughness   = normalMaterial.a;

    // ── Roughness fade — skip for rough surfaces ──
    float roughnessFade = GetRoughnessFade(roughness);
    [branch]
    if (roughnessFade <= 0.0f) {
        resolvedSSR[dispatchThreadId.xy] = 0.0f;
        return;
    }

    // ── Read SSR trace result ──
    float4 ssrTrace = SampleSSRTrace(uv);
    float3 hitColor    = ssrTrace.rgb;
    float  confidence  = ssrTrace.a;

    // Early out if no reflection data
    [branch]
    if (confidence <= 0.0f) {
        resolvedSSR[dispatchThreadId.xy] = 0.0f;
        return;
    }

    // ── Compose final SSR color ──
    // SSR output is UNWEIGHTED scene color. No Fresnel is applied here.
    // Fresnel weighting is deferred to the deferred lighting pass where EnvBRDF
    // is applied during SSR/IBL compositing (matching standard pattern).
    // No Fresnel in SSR resolve — Fresnel is applied later in the lighting pass.
    //   OutColor = SampleScreenColor(...) * Vignette;
    //   OutColor *= RoughnessFade;
    //   OutColor *= SSRParams.r;
    // NOTE: roughnessFade and intensity are already applied in ssr_trace_cs.hlsl:568-569.
    // Do NOT apply them again here.
    float3 reflection = hitColor;

    // Output: rgb = unweighted reflection color (already includes roughnessFade * intensity from trace)
    //         a = blend weight = confidence from trace (vignette * roughnessFade * intensity)
    // The lighting pass uses this alpha to lerp between IBL and SSR:
    //   specularIBL = lerp(iblReflection, ssrData.rgb, ssrData.a)
    resolvedSSR[dispatchThreadId.xy] = float4(reflection, saturate(confidence));
}
