/// GBuffer pixel shader — Step 1: albedo only.
///
/// Writes base color to RT0. No normal mapping, no PBR params yet.
/// Each step will add one feature and be tested visually.
///
/// RT0 (R8G8B8A8_UNORM): rgb = albedo, a = 1.0

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
    float    displacementScale;
    uint     heightmapSrvIndex;
    float    pomMinLayers;
    float    pomMaxLayers;
    float    pomFadeStart;
    float    pomFadeEnd;
    float    pomSelfShadow;
    uint     materialFlags;
};

#ifdef UZ_BINDLESS
// Bindless textures
#else
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float4> emissiveTexture  : register(t5);
#endif
SamplerState linearSampler : register(s0);
SamplerState clampSampler  : register(s2);

GBufferOutput main(PSInput input) {
    float2 uv = input.texcoord * uvScale + uvOffset;

    // ── Albedo ────────────────────────────────────────────────────────────
    float3 albedo = float3(1.0, 1.0, 1.0);
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo = tex.SampleBias(linearSampler, uv, mipBias).rgb;
    }
#else
    albedo = albedoTexture.SampleBias(linearSampler, uv, mipBias).rgb;
#endif
    albedo *= baseColorTint;

    // ── Normal (world-space geometric normal for now) ─────────────────────
    float3 N = normalize(input.normalWS);

    // ── PBR params (defaults) ─────────────────────────────────────────────
    float metallic  = 0.0;
    float roughness = 0.5;
    float ao        = 1.0;

    // ── Sample roughness/metallic/AO textures ─────────────────────────────
    bool isORM = (materialFlags & 0x4u) != 0;
#ifdef UZ_BINDLESS
    if (isORM && aoIndex > 0) {
        Texture2D<float4> ormTex = ResourceDescriptorHeap[aoIndex];
        float3 orm = ormTex.SampleBias(linearSampler, uv, mipBias).rgb;
        ao        = orm.r;
        roughness = orm.g;
        metallic  = orm.b;
    } else {
        if (roughnessIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[roughnessIndex];
            roughness = tex.SampleBias(linearSampler, uv, mipBias).r;
        }
        if (metallicIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[metallicIndex];
            metallic = tex.SampleBias(linearSampler, uv, mipBias).r;
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
        metallic  = metallicTexture.SampleLevel(clampSampler, uv, 0).r;
        ao        = aoTexture.SampleBias(linearSampler, uv, mipBias).r;
    }
#endif

    roughness *= roughnessMultiplier;
    metallic  *= metallicMultiplier;
    ao = lerp(1.0, ao, aoIntensity);

    // ── Normal mapping ────────────────────────────────────────────────────
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

    // ── Octahedral encode normal ──────────────────────────────────────────
    // Inline to avoid include dependency. Will extract to include later.
    float3 na = abs(N);
    float  nt = na.x + na.y + na.z;
    float2 octN = N.xy / nt;
    if (N.z < 0.0) {
        float2 octSign = float2(octN.x >= 0.0 ? 1.0 : -1.0, octN.y >= 0.0 ? 1.0 : -1.0);
        octN = (1.0 - abs(octN.yx)) * octSign;
    }
    octN = octN * 0.5 + 0.5;

    // ── Pack specular + shadingModelID into RT3.a ─────────────────────────
    float specularVal = float((materialFlags >> 8u) & 0xFFu) / 255.0;
    uint  smID        = (materialFlags >> 24u) & 0xFu;
    float packedAlpha = specularVal + float(smID) * 16.0;

    // ── Emissive ──────────────────────────────────────────────────────────
    float3 emissive = float3(0, 0, 0);
#ifdef UZ_BINDLESS
    if (emissiveIndex > 0) {
        Texture2D<float4> eTex = ResourceDescriptorHeap[emissiveIndex];
        emissive = eTex.SampleBias(linearSampler, uv, mipBias).rgb;
    }
#else
    emissive = emissiveTexture.SampleBias(linearSampler, uv, mipBias).rgb;
#endif
    emissive *= emissiveColor * emissiveIntensity;

    // ── Alpha test (Masked blend mode) ────────────────────────────────────
    if ((materialFlags & 0x3u) == 1) {
        float alphaValue = matOpacity;
        if (opacitySrvIndex > 0) {
            Texture2D<float4> opacityTex = ResourceDescriptorHeap[opacitySrvIndex];
            alphaValue *= opacityTex.Sample(linearSampler, uv).r;
        }
        if (alphaValue < alphaClipThreshold) discard;
    }

    // ── Per-shading-model custom data in RT2.w ──────────────────────────
    // RT2.w is available (RGBA32Float) for per-model parameters.
    // smID==4 (ClearCoat): clearCoatRoughness from materialFlags bits[28:31]
    float customW = 1.0;
    if (smID == 4) {
        customW = float((materialFlags >> 28u) & 0xFu) / 15.0;
    }

    // ── Write GBuffer ─────────────────────────────────────────────────────
    GBufferOutput output;
    output.albedoAO     = float4(albedo, ao);
    output.normalMatPbr = float4(octN, metallic, roughness);
    output.positionWS   = float4(input.positionWS, customW);
    output.emissiveHDR  = float4(emissive, packedAlpha);
    return output;
}
