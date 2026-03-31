/// UzEngine mesh vertex shader.
/// @ref Standard vertex transform — row-vector * matrix convention (row_major)

struct VSOutput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

#ifdef UZ_BINDLESS
struct Vertex { float3 position; float3 normal; float2 texcoord; float4 tangent; };
struct PushConstants { uint vertexBufferIndex; };
ConstantBuffer<PushConstants> push : register(b0);
#else
struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
};

cbuffer PerObjectCB : register(b2) {
    uint  _t0, _t1, _t2, _t3, _t4;
    float3 _pad;
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
};

#ifdef UZ_BINDLESS
VSOutput main(uint vertexID : SV_VertexID) {
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
    Vertex v = vertices[vertexID];
    float4x4 instModel  = model;
    float4x4 instNormal = normalMatrix;
#else
VSOutput main(VSInput v) {
    float4x4 instModel  = model;
    float4x4 instNormal = normalMatrix;
#endif

    VSOutput output;
    float4 worldPos = mul(float4(v.position, 1.0), instModel);
    output.positionWS = worldPos.xyz;
    output.positionCS = mul(worldPos, viewProj);
    output.normalWS   = normalize(mul(float4(v.normal, 0.0), instNormal).xyz);
    output.tangentWS  = float4(normalize(mul(float4(v.tangent.xyz, 0.0), instNormal).xyz), v.tangent.w);
    output.texcoord   = v.texcoord;
    return output;
}
