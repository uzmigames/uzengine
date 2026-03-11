/// Simple vertex shader — SM6.6 bindless vertex fetch.
///
/// Reads vertex data from a StructuredBuffer via ResourceDescriptorHeap[index].
/// No Input Assembler vertex layout is used.

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

// Root constant at b0: descriptor index of the vertex structured buffer
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
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
