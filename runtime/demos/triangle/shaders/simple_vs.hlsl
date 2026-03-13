/// Simple vertex shader.
///
/// SM6.6 (UZ_BINDLESS): reads vertex data from StructuredBuffer via
/// ResourceDescriptorHeap[index]. No Input Assembler layout needed.
///
/// SM5 (!UZ_BINDLESS): receives vertex data via traditional Input Assembler.

#ifdef UZ_BINDLESS
struct SimpleVertex {
    float3 position;
    float4 color;
    float2 texcoord;
};
#endif

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

#ifdef UZ_BINDLESS
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
#else
struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.color    = input.color;
    output.texcoord = input.texcoord;
    return output;
}
#endif
