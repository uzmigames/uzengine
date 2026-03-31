/// UzEngine shadow depth vertex shader — depth-only, same mul order as mesh_vs.
/// @ref Standard shadow mapping

#ifdef UZ_BINDLESS
struct Vertex { float3 position; float3 normal; float2 texcoord; float4 tangent; };
struct PushConstants { uint vertexBufferIndex; };
ConstantBuffer<PushConstants> push : register(b0);
#else
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float2 texcoord : TEXCOORD0; float4 tangent : TANGENT; };
#endif

cbuffer PerObjectCB : register(b2) {
    uint _t0, _t1, _t2, _t3, _t4;
    float3 _pad;
    row_major float4x4 model;
};

cbuffer ShadowCB : register(b4) {
    row_major float4x4 shadowViewProj;
};

#ifdef UZ_BINDLESS
float4 main(uint vertexID : SV_VertexID) : SV_Position {
    StructuredBuffer<Vertex> vb = ResourceDescriptorHeap[push.vertexBufferIndex];
    float4 worldPos = mul(float4(vb[vertexID].position, 1.0), model);
    return mul(worldPos, shadowViewProj);
}
#else
float4 main(VSInput v) : SV_Position {
    float4 worldPos = mul(float4(v.position, 1.0), model);
    return mul(worldPos, shadowViewProj);
}
#endif
