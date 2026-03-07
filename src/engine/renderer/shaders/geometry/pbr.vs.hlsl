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
    float4 tangent  : TANGENT; // xyz=tangent, w=sign
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float3 tangent  : TEXCOORD3;
    float3 bitangent: TEXCOORD4;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Transform position to world space
    float4 worldPos = mul(world, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;
    
    // Transform position to screen space
    output.position = mul(viewProj, worldPos);
    
    // Transform normal and tangent to world space
    float3x3 normalMatrix = (float3x3)world;
    output.normal = normalize(mul(normalMatrix, input.normal));
    output.tangent = normalize(mul(normalMatrix, input.tangent.xyz));
    
    // Calculate bitangent
    output.bitangent = normalize(cross(output.normal, output.tangent) * input.tangent.w);
    
    output.texcoord = input.texcoord;
    
    return output;
}
