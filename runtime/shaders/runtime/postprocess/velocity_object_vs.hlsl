struct VSOutput {
    float4 positionCS : SV_Position;
    float4 clipCurr : TEXCOORD0;
    float4 clipPrev : TEXCOORD1;
};

#ifdef UZ_BINDLESS
struct Vertex { float3 position; float3 normal; float2 texcoord; float4 tangent; };
struct VertexPacked { float3 position; int4 packedNormal; float2 texcoord; int4 packedTangent; uint _pad; };
// Matches mesh_vs and all mesh draw callers; 4 of the shared 24 DWORDs.
struct PushConstants { uint vertexBufferIndex; uint usePackedNormals; uint colorBufferIndex; uint previousVertexBufferIndex; };
ConstantBuffer<PushConstants> push : register(b0);
#else
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent : TANGENT;
};
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
};

cbuffer VelocityObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 prevModel;
    row_major float4x4 unjitteredViewProj;
    row_major float4x4 prevViewProj;
    float2 screenSize;
    float2 _velocityObjectPad;
};

#ifdef UZ_BINDLESS
VSOutput main(uint vertexID : SV_VertexID) {
    float3 vPosition, prevPosition;
    uint previousIndex = push.previousVertexBufferIndex != 0
        ? push.previousVertexBufferIndex : push.vertexBufferIndex;
    if (push.usePackedNormals != 0) {
        StructuredBuffer<VertexPacked> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
        StructuredBuffer<VertexPacked> previous = ResourceDescriptorHeap[previousIndex];
        vPosition = vertices[vertexID].position;
        prevPosition = previous[vertexID].position;
    } else {
        StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
        StructuredBuffer<Vertex> previous = ResourceDescriptorHeap[previousIndex];
        vPosition = vertices[vertexID].position;
        prevPosition = previous[vertexID].position;
    }
#else
VSOutput main(VSInput v) {
    float3 vPosition = v.position;
    float3 prevPosition = vPosition;
#endif
    VSOutput output;
    // Identical expression order and b1 viewProj to mesh_vs, for Equal depth.
    float4x4 instModel = model;
    float4 worldPos = mul(float4(vPosition, 1.0), instModel);
    output.positionCS = mul(worldPos, viewProj);
    output.clipCurr = mul(worldPos, unjitteredViewProj);
    output.clipPrev = mul(mul(float4(prevPosition, 1.0), prevModel), prevViewProj);
    return output;
}
