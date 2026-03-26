/// Forward mesh pixel shader — Step 1: albedo only, no lighting.
/// Placeholder until deferred pipeline is complete.

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

#ifdef UZ_BINDLESS
struct PushConstants { uint vertexBufferIndex; };
ConstantBuffer<PushConstants> push : register(b0);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj  : packoffset(c0);
    float4   cameraPos           : packoffset(c4);
    float4   timeParams          : packoffset(c5);
    float4   lightDir            : packoffset(c6);
    float4   lightColor          : packoffset(c7);
    float4   ambientColor        : packoffset(c8);

    // Point lights (up to 3), packed at end of 768-byte CB
    uint     numPointLights              : packoffset(c33.w);
    float4   pointLight0_posRadius       : packoffset(c34);   // xyz=world position, w=radius (cm)
    float4   pointLight0_colorIntensity  : packoffset(c35);   // xyz=linear color, w=intensity
    float4   pointLight1_posRadius       : packoffset(c36);
    float4   pointLight1_colorIntensity  : packoffset(c37);
    float4   pointLight2_posRadius       : packoffset(c38);
    float4   pointLight2_colorIntensity  : packoffset(c39);
};

cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;
    uint     normalIndex;
    uint     roughnessIndex;
    uint     metallicIndex;
    uint     aoIndex;
    uint     emissiveIndex;
    float    alphaClipThreshold;
    uint     opacitySrvIndex;
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    float3   emissiveColor;
    float    emissiveIntensity;
    float3   baseColorTint;
    float    roughnessMultiplier;
    float    metallicMultiplier;
    float    normalIntensity;
    float2   uvScale;
    float2   uvOffset;
    float    aoIntensity;
    float    matOpacity;
    float    _pomPad[7];
    uint     materialFlags;
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture  : register(t0);
Texture2D<float4> opacityTexture : register(t1);
#endif
SamplerState linearSampler : register(s0);

float4 main(PSInput input) : SV_Target {
    float2 uv = input.texcoord * uvScale + uvOffset;

    float3 albedo = float3(1.0, 1.0, 1.0);
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo = tex.Sample(linearSampler, uv).rgb;
    }
#else
    albedo = albedoTexture.Sample(linearSampler, uv).rgb;
#endif
    albedo *= baseColorTint;

    // Simple NdotL for minimum visibility (directional light)
    float3 N = normalize(input.normalWS);
    float3 L = normalize(lightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    float3 color = albedo * (NdotL * lightColor.xyz + ambientColor.xyz);

    // ── Point light evaluation ───────────────────────────────────────────
    /// @ref Karis 2013 "Real Shading in Unreal Engine 4" — point light attenuation
    /// @constants inverse-square falloff with smooth radius windowing:
    ///   attenuation = saturate(1 - (d/r)^2)^2 / (d^2 + 1)
    ///   The +1 in the denominator prevents division-by-zero at distance=0.

    // Pack light data into arrays so we can loop without branch divergence
    float4 plPosRadius[3]       = { pointLight0_posRadius,      pointLight1_posRadius,      pointLight2_posRadius };
    float4 plColorIntensity[3]  = { pointLight0_colorIntensity,  pointLight1_colorIntensity,  pointLight2_colorIntensity };

    uint lightCount = min(numPointLights, 3u);
    for (uint i = 0; i < lightCount; ++i) {
        float3 lightPos   = plPosRadius[i].xyz;
        float  lightRadius = plPosRadius[i].w;
        float3 lightColor_i = plColorIntensity[i].xyz;
        float  intensity  = plColorIntensity[i].w;

        // Light vector from surface to light
        float3 Lp    = lightPos - input.positionWS;
        float  dist2 = dot(Lp, Lp);
        float  dist  = sqrt(dist2);
        float3 Ln    = Lp / max(dist, 1e-6); // normalized direction, guard against zero

        // Inverse-square attenuation with smooth radius falloff (Karis 2013, slide 12)
        // Window function: saturate(1 - (d/r)^2)^2  — smoothly goes to zero at radius
        // Physical falloff: 1 / (d^2 + 1)           — prevents singularity at d=0
        float distOverRadius = dist / max(lightRadius, 1e-6);
        float distOverRadius2 = distOverRadius * distOverRadius;
        float windowTerm = saturate(1.0 - distOverRadius2);
        float attenuation = (windowTerm * windowTerm) / (dist2 + 1.0);

        // Lambertian diffuse for this point light
        float NdotLp = max(dot(N, Ln), 0.0);

        color += albedo * NdotLp * lightColor_i * intensity * attenuation;
    }

    // ── Opacity texture modulation (forward/translucent path) ──────────
    float finalOpacity = matOpacity;
#ifdef UZ_BINDLESS
    if (opacitySrvIndex > 0) {
        Texture2D<float4> opacityTex = ResourceDescriptorHeap[opacitySrvIndex];
        finalOpacity *= opacityTex.Sample(linearSampler, uv).r;
    }
#else
    finalOpacity *= opacityTexture.Sample(linearSampler, uv).r;
#endif

    return float4(color, finalOpacity);
}
