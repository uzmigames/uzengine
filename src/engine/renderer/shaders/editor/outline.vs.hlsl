// outline.vs.hlsl -- Selection outline vertex shader
// Inflates mesh vertices along normals to create an outline fringe.
//
// Root constants match PBR layout:
//   b0: float4x4 world matrix (16 DWORDs)
//   b1: MaterialCB (8 DWORDs) — [4]=outline width
//   b3: FrameCB (viewProj matrix)

cbuffer ObjectCB : register(b0) {
    float4x4 world;
};

cbuffer MaterialCB : register(b1) {
    float4 _unused0;
    float4 outlineParams;    // x=outlineWidth
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
    
    // Inflate along normal for outline thickness
    float outlineWidth = outlineParams.x;
    float3 inflated = input.position + input.normal * outlineWidth;
    
    float4 worldPos = mul(world, float4(inflated, 1.0));
    output.position = mul(viewProj, worldPos);
    
    return output;
}
