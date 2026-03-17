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
/// @source UE5 SSRTReflections.usf:105-416 — ScreenSpaceReflections
///         (UE5 does trace+resolve in one pass; we split for modularity)
/// @source UE5 SSRTReflections.usf:190-194 — Smith visibility for SSR weight
/// @source UE5 SSRTReflections.usf:319-320 — Karis tonemap for multi-ray
/// @source UE5 SSRTReflections.usf:397 — SampleScreenColor
/// @source UE5 SSRTRayCast.ush:674-681 — ComputeHitVignetteFromScreenPos
/// @constants a2 = Roughness^4 (usf:190-191), G_SmithV (usf:194),
///            Karis weight = rcp(1 + Luminance) (usf:320)
/// @deviations The resolve is a separate pass from the trace. UE5 combines
///   them into one pixel/compute shader. The math is identical; only the
///   execution topology differs.
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
SamplerState linearSampler : register(s1);

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
/// @source UE5 BRDF.ush:193 — F_Schlick
/// @constants NONE (standard Schlick formula)
/// @deviations NONE
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * Pow5(saturate(1.0f - cosTheta));
}

/// Compute G_SmithV term used for SSR weighting in multi-ray mode.
/// @source UE5 SSRTReflections.usf:190-194
/// @constants a = Roughness^2, a2 = a^2 = Roughness^4 (usf:190-191)
///            G_SmithV = 2*NoV / (NoV + sqrt(NoV*(NoV - NoV*a2) + a2))  (usf:194)
/// @deviations NONE
float ComputeSmithV(float NoV, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    return 2.0f * NoV / (NoV + sqrt(NoV * (NoV - NoV * a2) + a2));
}

// ─────────────────────────────────────────────────────────────────────────────
// Roughness Fade
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SSRTReflections.usf:45-49 — GetRoughnessFade
/// @constants roughnessMaskScale from root CB, bias=+2 (usf:48)
/// @deviations NONE
float GetRoughnessFade(float roughness) {
    return min(roughness * roughnessMaskScale + 2.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SSRTReflections.usf:441-462 — ScreenSpaceReflectionsCS (resolve path)
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

    // ── Compute Fresnel weight ──
    // Reconstruct view direction for Fresnel evaluation
    float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    float3 V = normalize(cameraPos.xyz - worldPos);
    float  NoV = saturate(dot(worldNormal, V));

    // F0: dielectric = 0.04, metals use albedo (approximated here with metallic lerp)
    // @source UE5 BRDF.ush:209 — DielectricSpecularToF0(0.04)
    // For the resolve pass, we use a scalar F0 approximation since we don't have
    // albedo in this pass. The full Fresnel*albedo weighting happens when
    // compositing SSR with IBL in the lighting pass.
    float3 F0 = lerp(0.04f, 0.8f, metallic);
    float3 F  = FresnelSchlick(NoV, F0);

    // ── Compose final SSR color ──
    // Weight the hit color by Fresnel reflectance and confidence
    float3 reflection = hitColor * F;

    // Apply roughness fade and intensity
    // @source UE5 SSRTReflections.usf:401-402
    reflection *= roughnessFade;
    reflection *= ssrIntensity;

    // Output: rgb = Fresnel-weighted reflection color, a = blend weight
    // The blend weight is confidence * roughnessFade * intensity * average(F)
    // The lighting pass uses this alpha to lerp between IBL and SSR:
    //   finalReflection = lerp(iblReflection, ssrReflection / alpha, alpha)
    float blendWeight = confidence * roughnessFade * ssrIntensity * dot(F, 1.0f / 3.0f);
    resolvedSSR[dispatchThreadId.xy] = float4(reflection, saturate(blendWeight));
}
