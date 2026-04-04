/// UzEngine Debug View Pixel Shader
/// Visualizes G-Buffer channels based on the debug mode root constant.

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) {
    uint g_mode;
    uint g_albedoSrv;
    uint g_normalSrv;
    uint g_depthSrv;
    uint g_positionSrv;
};
#else
cbuffer RootConstants : register(b0) {
    uint g_mode;
};
Texture2D<float4> t_albedo   : register(t0);
Texture2D<float4> t_normal   : register(t1);
Texture2D<float>  t_depth    : register(t2);
Texture2D<float4> t_position : register(t3);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float nearPlane, farPlane;
};

SamplerState pointSampler : register(s0);

float LinearizeDepth(float d, float n, float f) {
    // Reversed-Z: near=1, far=0
    return n / max(d, 1e-7);
}

/// Decode octahedral normal from RG16F
float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    int2 px = int2(pos.xy);

#ifdef UZ_BINDLESS
    Texture2D<float4> albedoTex   = ResourceDescriptorHeap[g_albedoSrv];
    Texture2D<float4> normalTex   = ResourceDescriptorHeap[g_normalSrv];
    Texture2D<float>  depthTex    = ResourceDescriptorHeap[g_depthSrv];
    Texture2D<float4> positionTex = ResourceDescriptorHeap[g_positionSrv];
#else
    #define albedoTex   t_albedo
    #define normalTex   t_normal
    #define depthTex    t_depth
    #define positionTex t_position
#endif

    float4 albedoData = albedoTex.Load(int3(px, 0));
    float4 normalData = normalTex.Load(int3(px, 0));
    float  depthRaw   = depthTex.Load(int3(px, 0));
    float4 posData    = positionTex.Load(int3(px, 0));

    float3 N = OctDecode(normalData.xy);

    switch (g_mode) {
    case 1: // Albedo
        return float4(albedoData.rgb, 1);

    case 2: // WorldNormal
        return float4(N * 0.5 + 0.5, 1);

    case 3: // Metallic
        return float4(normalData.bbb, 1);

    case 4: // Roughness
        return float4(normalData.aaa, 1);

    case 5: // AO
        return float4(albedoData.aaa, 1);

    case 6: // Depth
    {
        float linDepth = LinearizeDepth(depthRaw, nearPlane, farPlane);
        float d = saturate(linDepth / 5000.0); // Normalize to 50m range
        return float4(d, d, d, 1);
    }

    case 7: // WorldPosition
        return float4(frac(posData.xyz * 0.01), 1); // Color grid at 1m intervals

    case 11: // Unlit (same as Albedo)
        return float4(albedoData.rgb, 1);

    case 13: // Tangent (derived from normal — approximate)
    {
        // Derive tangent direction from normal using cross with least-aligned axis
        float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
        float3 T = normalize(cross(up, N));
        return float4(T * 0.5 + 0.5, 1);
    }

    case 14: // Faceted (detect normal discontinuities via screen-space derivatives)
    {
        // Use ddx/ddy of world normal to detect hard edges
        float3 dNdx = ddx(N);
        float3 dNdy = ddy(N);
        float edgeStrength = length(dNdx) + length(dNdy);
        // Threshold: values > 0.1 indicate a hard edge
        float edge = smoothstep(0.05, 0.2, edgeStrength);
        // Show mesh with red highlights on hard edges
        float3 baseColor = albedoData.rgb * 0.5;
        float3 edgeColor = float3(1, 0, 0); // Red for hard edges
        return float4(lerp(baseColor, edgeColor, edge), 1);
    }

    default: // Lit or unknown — should not reach here
        return float4(1, 0, 1, 1); // Magenta = error
    }
}
