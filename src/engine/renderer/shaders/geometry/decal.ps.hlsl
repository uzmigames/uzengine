// decal.ps.hlsl -- Screen-space decal projection
//
// Projects a decal texture onto scene geometry using depth buffer reconstruction.
// Reads scene depth, reconstructs world position, tests against decal OBB volume,
// and outputs albedo/normal/roughness modifications to G-Buffer.
//
// References:
//   [UE5] DeferredDecal.usf
//   [Rendering Decals in Screen Space] — Wronski 2014

cbuffer DecalCB : register(b0) {
    float4x4 decalWorldToLocal;  // inverse decal transform (world -> decal local)
    float4   decalAlbedo;        // RGBA albedo tint
    float    decalNormalStrength; // normal map blending strength
    float    decalRoughness;     // roughness override
    float    decalMetallic;      // metallic override
    float    decalOpacity;       // overall opacity
};

cbuffer InvVPCB : register(b3) {
    float4x4 invViewProj;
};

Texture2D    depthTex     : register(t0);
Texture2D    decalAlbedoTex : register(t1);
Texture2D    decalNormalTex : register(t2);
SamplerState linearClamp   : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput {
    float4 albedo : SV_TARGET0;   // Albedo modification
    float4 normal : SV_TARGET1;   // Normal modification (octahedral)
    float4 material : SV_TARGET2; // Metallic/Roughness modification
};

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y;
    float4 wp = mul(invViewProj, ndc);
    return wp.xyz / wp.w;
}

PSOutput PSMain(PSInput input) {
    PSOutput output;
    output.albedo = float4(0, 0, 0, 0);
    output.normal = float4(0, 0, 0, 0);
    output.material = float4(0, 0, 0, 0);

    float depth = depthTex.Load(int3(input.position.xy, 0)).r;
    if (depth >= 0.999) return output;

    float3 worldPos = ReconstructWorldPos(input.texcoord, depth);

    // Transform to decal local space
    float4 localPos = mul(decalWorldToLocal, float4(worldPos, 1.0));

    // OBB test: discard if outside [-0.5, 0.5] in all axes
    if (any(abs(localPos.xyz) > 0.5)) return output;

    // UV from decal local XZ (top-down projection)
    float2 decalUV = localPos.xz + 0.5;

    // Sample decal textures
    float4 albedo = decalAlbedoTex.Sample(linearClamp, decalUV) * decalAlbedo;
    albedo.a *= decalOpacity;

    output.albedo = albedo;
    output.material = float4(decalMetallic, decalRoughness, 0, albedo.a);

    return output;
}
