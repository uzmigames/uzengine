/// UzEngine HDRI skybox — renders cubemap as sky background.
/// Only fills pixels where depth == 0.0 (reversed-Z far plane = no geometry).

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct SkyboxIndices { uint depthIdx; uint cubemapIdx; };
ConstantBuffer<SkyboxIndices> skyboxIndices : register(b0);
#else
Texture2D<float>     sceneDepth  : register(t0);
TextureCube<float4>  hdriCubemap : register(t1);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
};

SamplerState linearSampler : register(s0);

/// Z-up world direction → Y-up DX cubemap convention
float3 WorldToCubemap(float3 dir) {
    return float3(dir.x, dir.z, dir.y);
}

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[skyboxIndices.depthIdx];
    float depth = depthTex.Sample(linearSampler, uv).r;
#else
    float depth = sceneDepth.Sample(linearSampler, uv).r;
#endif

    if (depth > 0.0001) discard;

    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 pt0 = mul(float4(ndc, 0.25, 1.0), invViewProj);
    float4 pt1 = mul(float4(ndc, 0.75, 1.0), invViewProj);
    float3 rayDir = normalize(pt0.xyz / pt0.w - pt1.xyz / pt1.w);

    float3 cubemapDir = WorldToCubemap(rayDir);

#ifdef UZ_BINDLESS
    TextureCube<float4> cubemap = ResourceDescriptorHeap[skyboxIndices.cubemapIdx];
    float3 color = cubemap.SampleLevel(linearSampler, cubemapDir, 0).rgb;
#else
    float3 color = hdriCubemap.SampleLevel(linearSampler, cubemapDir, 0).rgb;
#endif

    return float4(color, 1.0);
}
