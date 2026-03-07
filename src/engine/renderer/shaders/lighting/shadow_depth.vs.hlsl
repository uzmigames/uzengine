// shadow_depth.vs.hlsl -- Depth-only vertex shader for directional shadow map pass.
//
// Transforms vertices into light-space (orthographic) for shadow map generation.
// No pixel shader — depth is written implicitly by the rasterizer.
//
// Root constants:
//   b0: ObjectCB (world matrix, 16 DWORDs) -- same slot as PBR pipeline
//   b3: LightSpaceCB (lightSpaceMatrix, 16 DWORDs)

cbuffer ObjectCB : register(b0) {
    float4x4 world;
};

cbuffer LightSpaceCB : register(b3) {
    float4x4 lightSpaceMatrix;
};

float4 VSMain(float3 position : POSITION) : SV_POSITION {
    float4 worldPos = mul(world, float4(position, 1.0));
    return mul(lightSpaceMatrix, worldPos);
}
