// overlay_vs.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kOverlayVS[] = R"hlsl(
/// Editor overlay vertex shader — screen-space 2D quads.
///
/// SM6.6 (UZ_BINDLESS): reads overlay vertices from StructuredBuffer via
/// ResourceDescriptorHeap[index].
///
/// SM5 (!UZ_BINDLESS): receives overlay vertices via Input Assembler.
///
/// Transforms screen-space pixel coordinates to NDC.

#ifdef UZ_BINDLESS
struct OverlayVertex {
    float2 position;
    float2 texcoord;
    float4 color;
};
#endif

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint fontAtlasIndex;
    uint samplerIndex;
    uint vertexOffset;      ///< Added to SV_VertexID to index into StructuredBuffer.
};
#endif

cbuffer OverlayCB : register(b1) {
    float2 invScreenSize;  // 1.0 / screenSize
    float2 padding;
};

#ifdef UZ_BINDLESS
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
#else
struct VSInput {
    float2 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

VSOutput main(VSInput input) {
    float2 ndc;
    ndc.x = input.position.x * invScreenSize.x * 2.0 - 1.0;
    ndc.y = 1.0 - input.position.y * invScreenSize.y * 2.0;

    VSOutput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.texcoord = input.texcoord;
    output.color    = input.color;
    return output;
}
#endif
)hlsl";
} // namespace uz::generated
