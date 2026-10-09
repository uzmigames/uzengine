/// UzEngine shadow depth vertex shader — depth-only, same mul order as mesh_vs.
/// @ref Standard shadow mapping
///
/// UZ_SHADOW_LINEAR=1 (local spot/point shadow tiles): also forwards the world position, which
/// `shadow_depth_linear_ps.hlsl` turns into distance / radius on SV_Depth. The clip-space position
/// still comes from the light's projection so rasterisation covers the tile correctly; only the
/// stored depth changes.

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
    float3 shadowLightPosition;   ///< Local lights only (UZ_SHADOW_LINEAR); zero for the CSM.
    float  shadowLightRadius;
};

#ifdef UZ_SHADOW_LINEAR
struct VSOutput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
};

#ifdef UZ_BINDLESS
VSOutput main(uint vertexID : SV_VertexID) {
    StructuredBuffer<Vertex> vb = ResourceDescriptorHeap[push.vertexBufferIndex];
    float4 worldPos = mul(float4(vb[vertexID].position, 1.0), model);
#else
VSOutput main(VSInput v) {
    float4 worldPos = mul(float4(v.position, 1.0), model);
#endif
    VSOutput o;
    o.position = mul(worldPos, shadowViewProj);
    o.worldPos = worldPos.xyz;
    return o;
}
#else
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
#endif
