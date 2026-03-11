/// Textured quad vertex shader — SM6.6 bindless vertex fetch.
///
/// Reads vertex data from a StructuredBuffer via ResourceDescriptorHeap[index].
/// Passes position, color, and texcoord to the pixel shader.

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
};

VSOutput main(uint vertexID : SV_VertexID) {
    StructuredBuffer<SimpleVertex> vertices = ResourceDescriptorHeap[vertexBufferIndex];

    SimpleVertex v = vertices[vertexID];

    VSOutput output;
    output.position = float4(v.position, 1.0);
    output.color    = v.color;
    output.texcoord = v.texcoord;
    return output;
}
