/// UzEngine GBuffer pixel shader.
/// @ref Deferred shading — writes albedo+AO, normal+metallic+roughness, worldPos, emissive
/// Based on old working shader structure.

#include "../common/encoding.hlsli"

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

struct GBufferOutput {
    float4 albedoAO     : SV_Target0;
    float4 normalMatPbr : SV_Target1;
    float4 positionWS   : SV_Target2;
    float4 emissiveHDR  : SV_Target3;
};

#ifdef UZ_BINDLESS
// no push constants needed by PS
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj  : packoffset(c0);
    float4   cameraPos           : packoffset(c4);
    float4   timeParams          : packoffset(c5);
    float4   lightDir            : packoffset(c6);
    float4   lightColor          : packoffset(c7);
    float4   ambientColor        : packoffset(c8);
    float    mipBias             : packoffset(c30);
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
    row_major float4x4 obj_model;
    row_major float4x4 obj_normalMatrix;
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
    float    displacementScale;
    uint     heightmapSrvIndex;
    float    pomMinLayers;
    float    pomMaxLayers;
    float    pomFadeStart;
    float    pomFadeEnd;
    float    pomSelfShadow;
    uint     materialFlags;
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float4> emissiveTexture  : register(t5);
Texture2D<float4> opacityTexture   : register(t6);
#endif

SamplerState linearSampler : register(s0);
SamplerState clampSampler  : register(s2);

// ── Material Graph Support ──────────────────────────────────────────────
// When UZ_MATERIAL_GRAPH is defined, the fixed texture-sampling block is
// replaced by a call to the generated ComputeMaterialOutputs() function.
// The MaterialPipeline injects the generated HLSL at the marker below.
// Material graph support: materialParams[0..3] = texture SRV indices (as asuint),
// materialParams[4..15] = user parameters. Both packed into b3.
#ifdef UZ_MATERIAL_GRAPH
cbuffer MaterialParamsCB : register(b3) {
    float4 materialParams[16];
};
// texIndex helper: extracts texture SRV index from materialParams.
// The first 4 float4 (16 float slots) store uint SRV indices as raw bits.
// asuint() reinterprets the float bits as uint (no numeric conversion).
uint getTexIndex(uint slot) {
    uint vecIdx = slot / 4;
    uint compIdx = slot % 4;
    float4 v = materialParams[vecIdx];
    if (compIdx == 0) return asuint(v.x);
    if (compIdx == 1) return asuint(v.y);
    if (compIdx == 2) return asuint(v.z);
    return asuint(v.w);
}
#include "material_body.hlsli"
#endif

GBufferOutput main(PSInput input) {
    float2 uv = input.texcoord * uvScale + uvOffset;

#ifdef UZ_MATERIAL_GRAPH
    // ── Material Graph path — generated ComputeMaterialOutputs() ─────────
    float3 albedo; float metallic; float roughness;
    float3 graphNormal; float3 emissive;
    float opacity; float ao;

    ComputeMaterialOutputs(
        uv, input.positionWS, normalize(input.normalWS),
        float4(1,1,1,1), // vertexColor (not yet wired)
        float3(0,0,0),   // cameraPosition (TODO: wire from PerFrameCB)
        float3(0,0,0),   // objectPosition
        input.positionCS.xy / float2(1920, 1080), // screenUV approximate
        0.0,             // totalTime (TODO: wire from PerFrameCB)
        materialParams,
        albedo, metallic, roughness,
        graphNormal, emissive, opacity, ao);

    // Normal mapping: the graph outputs a tangent-space normal from the
    // normal map texture (raw RGB sample). Transform it to world space
    // using the interpolated TBN matrix, same as the fixed-PBR path.
    float3 N = normalize(input.normalWS);
    {
        float3 T = normalize(input.tangentWS.xyz);
        float3 B = cross(N, T) * input.tangentWS.w;
        float3x3 TBN = float3x3(T, B, N);
        // graphNormal is the raw RGBA sample from the normal map texture.
        // Decode from [0,1] → [-1,1] tangent space.
        float3 normalTS;
        normalTS.xy = graphNormal.xy * 2.0 - 1.0;
        normalTS.z = sqrt(max(1.0 - dot(normalTS.xy, normalTS.xy), 0.0));
        N = normalize(mul(normalTS, TBN));
    }

#else
    // ── Fixed PBR path — texture-sampled material properties ─────────────
    float3 albedo = float3(1, 1, 1);
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo = tex.SampleBias(linearSampler, uv, mipBias).rgb;
    }
#else
    albedo = albedoTexture.SampleBias(linearSampler, uv, mipBias).rgb;
#endif
    albedo *= baseColorTint;

    // ── PBR params ────────────────────────────────────────────────────────
    // When no texture is bound (index == 0 in bindless, or fallback in SM5),
    // use the multiplier as the direct value instead of sampling.
    // This allows scenes to set roughness/metallic via scalar params alone.
    float metallic  = 0.0;
    float roughness = 1.0;
    float ao        = 1.0;

    bool isORM = (materialFlags & 0x4u) != 0;
#ifdef UZ_BINDLESS
    if (isORM && aoIndex > 0) {
        Texture2D<float4> ormTex = ResourceDescriptorHeap[aoIndex];
        float3 orm = ormTex.SampleBias(linearSampler, uv, mipBias).rgb;
        ao = orm.r; roughness = orm.g; metallic = orm.b;
    } else {
        if (roughnessIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[roughnessIndex];
            roughness = tex.SampleBias(linearSampler, uv, mipBias).r;
            roughness *= roughnessMultiplier;
        } else {
            roughness = roughnessMultiplier; // no texture: use multiplier directly
        }
        if (metallicIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[metallicIndex];
            metallic = tex.SampleBias(linearSampler, uv, mipBias).r;
            metallic *= metallicMultiplier;
        } else {
            metallic = metallicMultiplier; // no texture: use multiplier directly
        }
        if (aoIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[aoIndex];
            ao = tex.SampleBias(linearSampler, uv, mipBias).r;
        }
    }
#else
    if (isORM) {
        float3 orm = aoTexture.SampleBias(linearSampler, uv, mipBias).rgb;
        ao = orm.r; roughness = orm.g; metallic = orm.b;
    } else {
        roughness = roughnessTexture.SampleBias(linearSampler, uv, mipBias).r;
        roughness *= roughnessMultiplier;
        metallic  = metallicTexture.SampleBias(linearSampler, uv, mipBias).r;
        metallic  *= metallicMultiplier;
        ao        = aoTexture.SampleBias(linearSampler, uv, mipBias).r;
    }
#endif
    ao = lerp(1.0, ao, aoIntensity);

    // ── Normal mapping ────────────────────────────────────────────────────
    float3 N = normalize(input.normalWS);
#ifdef UZ_BINDLESS
    if (normalIndex > 0) {
        float3 T = normalize(input.tangentWS.xyz);
        float3 B = cross(N, T) * input.tangentWS.w;
        float3x3 TBN = float3x3(T, B, N);
        Texture2D<float4> nrmTex = ResourceDescriptorHeap[normalIndex];
        float3 nSample = nrmTex.SampleBias(linearSampler, uv, mipBias).rgb;
        float3 normalTS;
        normalTS.xy = nSample.rg * 2.0 - 1.0;
        normalTS.xy *= normalIntensity;
        normalTS.z = sqrt(max(1.0 - dot(normalTS.xy, normalTS.xy), 0.0));
        N = normalize(mul(normalTS, TBN));
    }
#else
    {
        float3 T = normalize(input.tangentWS.xyz);
        float3 B = cross(N, T) * input.tangentWS.w;
        float3x3 TBN = float3x3(T, B, N);
        float3 nSample = normalTexture.SampleBias(linearSampler, uv, mipBias).rgb;
        float3 normalTS;
        normalTS.xy = nSample.rg * 2.0 - 1.0;
        normalTS.xy *= normalIntensity;
        normalTS.z = sqrt(max(1.0 - dot(normalTS.xy, normalTS.xy), 0.0));
        N = normalize(mul(normalTS, TBN));
    }
#endif

#endif // !UZ_MATERIAL_GRAPH (end of fixed-PBR texture sampling block)

    // ── Geometric specular anti-aliasing ────────────────────────────────
    // @ref Kaplanyan 2016 "Stable Geometric Specular Antialiasing with
    //      Projected-Space NDF Filtering"
    // Increases roughness when normal varies rapidly across the pixel,
    // preventing specular highlight aliasing at grazing angles.
    {
        float3 dNdx = ddx(N);
        float3 dNdy = ddy(N);
        float variance = max(dot(dNdx, dNdx), dot(dNdy, dNdy));
        float kernelRoughness = min(variance * 2.0, 0.18); // clamped to prevent over-blur
        roughness = saturate(roughness + kernelRoughness);
    }

    // ── Pack specular + shadingModelID into RT3.a ─────────────────────────
    float specularVal = float((materialFlags >> 8u) & 0xFFu) / 255.0;
    uint  smID        = (materialFlags >> 24u) & 0xFu;
    float packedAlpha = specularVal + float(smID) * 16.0;

    // ── Emissive ──────────────────────────────────────────────────────────
#ifndef UZ_MATERIAL_GRAPH
    float3 emissive = float3(0, 0, 0);
#ifdef UZ_BINDLESS
    if (emissiveIndex > 0) {
        Texture2D<float4> eTex = ResourceDescriptorHeap[emissiveIndex];
        emissive = eTex.SampleBias(linearSampler, uv, mipBias).rgb;
    }
#else
    emissive = emissiveTexture.SampleBias(linearSampler, uv, mipBias).rgb;
#endif
#endif // !UZ_MATERIAL_GRAPH
    // Apply per-object emissive tint and intensity (both fixed PBR and material graph paths).
    emissive *= emissiveColor * emissiveIntensity;

    // ── Alpha test ────────────────────────────────────────────────────────
    if ((materialFlags & 0x3u) == 1) {
        float alphaValue = matOpacity;
#ifdef UZ_BINDLESS
        if (opacitySrvIndex > 0) {
            Texture2D<float4> opTex = ResourceDescriptorHeap[opacitySrvIndex];
            alphaValue *= opTex.Sample(linearSampler, uv).r;
        }
#else
        alphaValue *= opacityTexture.Sample(linearSampler, uv).r;
#endif
        if (alphaValue < alphaClipThreshold) discard;
    }

    // ── Custom data in RT2.w ──────────────────────────────────────────────
    float customW = 1.0;
    if (smID == 4) {
        customW = float((materialFlags >> 28u) & 0xFu) / 15.0;
    }

    // ── Write GBuffer ─────────────────────────────────────────────────────
    GBufferOutput output;
    output.albedoAO     = float4(albedo, ao);
    output.normalMatPbr = float4(OctEncode(N), metallic, roughness);
    output.positionWS   = float4(input.positionWS, customW);
    output.emissiveHDR  = float4(emissive, packedAlpha);
    return output;
}
