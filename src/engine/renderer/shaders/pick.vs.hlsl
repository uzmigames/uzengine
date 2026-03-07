// pick.vs.hlsl -- Pick buffer vertex shader
// Transforms vertices to clip space. Same vertex layout as PBR.
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
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 position : SV_POSITION;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(world, float4(input.position, 1.0));
    output.position = mul(viewProj, worldPos);
    return output;
}
