/// Editor overlay vertex shader — screen-space 2D quads.
///
/// Reads overlay vertices from StructuredBuffer via SM6.6 bindless.
/// Transforms screen-space pixel coordinates to NDC.

struct OverlayVertex {
    float2 position;
    float2 texcoord;
    float4 color;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint fontAtlasIndex;
    uint samplerIndex;
    uint vertexOffset;      ///< Added to SV_VertexID to index into StructuredBuffer.
};

cbuffer OverlayCB : register(b1) {
    float2 invScreenSize;  // 1.0 / screenSize
    float2 padding;
};

VSOutput main(uint vertexID : SV_VertexID) {
    StructuredBuffer<OverlayVertex> vertices = ResourceDescriptorHeap[vertexBufferIndex];

    OverlayVertex v = vertices[vertexID + vertexOffset];

    // Screen pixels → NDC: x∈[0,W] → [-1,1], y∈[0,H] → [1,-1] (flip Y)
    float2 ndc;
    ndc.x = v.position.x * invScreenSize.x * 2.0 - 1.0;
    ndc.y = 1.0 - v.position.y * invScreenSize.y * 2.0;

    VSOutput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.texcoord = v.texcoord;
    output.color    = v.color;
    return output;
}
