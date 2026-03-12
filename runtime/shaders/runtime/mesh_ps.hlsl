/// Standard mesh pixel shader — textured with directional lighting.
///
/// SM6.6 (UZ_BINDLESS): samples albedo via ResourceDescriptorHeap[materialIndex].
/// SM5 (!UZ_BINDLESS): samples albedo via register-bound texture t0.
///
/// Applies basic N·L directional lighting with ambient. Outputs to a single
/// render target (R8G8B8A8_UNORM swapchain backbuffer).

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

#ifdef UZ_BINDLESS
// Root constants at b0 (only needed in VS for bindless vertex fetch)
struct PushConstants {
    uint vertexBufferIndex;
};
ConstantBuffer<PushConstants> push : register(b0);
#endif

// Per-frame CB at b1
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
};

// Per-object CB at b2
cbuffer PerObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    uint     materialIndex;
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture : register(t0);
#endif
SamplerState linearSampler : register(s0);

float4 main(PSInput input) : SV_Target {
    // Directional light (fixed sun direction, Z-up)
    float3 lightDir = normalize(float3(0.5, 0.3, 0.8));
    float3 lightColor = float3(1.0, 0.95, 0.9);
    float3 ambient = float3(0.25, 0.25, 0.3);

    // N·L diffuse
    float3 N = normalize(input.normalWS);
    float NdotL = max(dot(N, lightDir), 0.0);
    float3 diffuse = lightColor * NdotL;

    // Base color
    float3 albedo = float3(1.0, 1.0, 1.0);

    if (materialIndex > 0) {
#ifdef UZ_BINDLESS
        Texture2D<float4> albedoTex = ResourceDescriptorHeap[materialIndex];
        albedo = albedoTex.Sample(linearSampler, input.texcoord).rgb;
#else
        albedo = albedoTexture.Sample(linearSampler, input.texcoord).rgb;
#endif
    }

    float3 finalColor = albedo * (ambient + diffuse);

    return float4(finalColor, 1.0);
}
