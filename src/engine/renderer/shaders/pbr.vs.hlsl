// pbr.vs.hlsl -- PBR vertex shader
// Transforms vertices with normals and UVs for lit rendering.
//
// Root constants:
//   b0: float4x4 world matrix (16 DWORDs)
//   b3: FrameCB (viewProj matrix)

cbuffer ObjectCB : register(b0) {
    float4x4 world;
};

cbuffer FrameCB : register(b3) {
    float4x4 viewProj;
};

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Transform position to world space
    float4 worldPos = mul(world, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;
    
    // Transform position to screen space
    output.position = mul(viewProj, worldPos);
    
    // Transform normal to world space (assumes no non-uniform scale)
    output.normal = normalize(mul((float3x3)world, input.normal));
    
    output.texcoord = input.texcoord;
    
    return output;
}
