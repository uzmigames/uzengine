/// Camera test vertex shader — SM6.6 bindless vertex fetch with MVP.
///
/// Reads vertex data from StructuredBuffer, transforms by ViewProj from
/// per-frame constant buffer.

struct SimpleVertex {
    float3 position;
    float4 color;
    float2 texcoord;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint textureIndex;
    uint samplerIndex;
    uint reserved;
};

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
};

VSOutput main(uint vertexID : SV_VertexID) {
    StructuredBuffer<SimpleVertex> vertices = ResourceDescriptorHeap[vertexBufferIndex];

    SimpleVertex v = vertices[vertexID];

    VSOutput output;
    output.position = mul(float4(v.position, 1.0), viewProj);
    output.color    = v.color;
    output.texcoord = v.texcoord;
    return output;
}
