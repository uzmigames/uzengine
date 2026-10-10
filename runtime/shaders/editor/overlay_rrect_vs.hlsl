/// Editor overlay rounded-rect vertex shader (TextRenderer::AddRoundedRect).
///
/// Every vertex carries the full per-quad parameter set (OverlayRrectVertex,
/// 96 bytes, replicated on the 6 vertices of the quad). The shader converts
/// the screen-space pixel position to NDC and forwards the box-local position
/// (interpolated) plus the box parameters (constant per quad) to
/// overlay_rrect_ps.hlsl, which evaluates the rounded-box distance field.
///
/// SM6.6 (UZ_BINDLESS): vertices read from a StructuredBuffer through
/// ResourceDescriptorHeap[vertexBufferIndex] (root constants
/// {vertex buffer SRV, unused, sampler, vertex offset}).
/// SM5 (!UZ_BINDLESS): vertices from the Input Assembler, semantics as in
/// TextRenderer::InitPipelines (`rrectElements`).

struct VSOutput {
    float4 position     : SV_Position;
    float2 localPos     : TEXCOORD0;   ///< Pixel offset from the box centre.
    nointerpolation float2 halfExtent   : TEXCOORD1;
    nointerpolation float2 shadowOffset : TEXCOORD2;
    nointerpolation float3 params       : TEXCOORD3;   ///< x corner radius, y border thickness, z shadow blur (pixels).
    nointerpolation float4 fillColor    : COLOR0;
    nointerpolation float4 borderColor  : COLOR1;
    nointerpolation float4 shadowColor  : COLOR2;
};

cbuffer OverlayCB : register(b1) {
    float2 invScreenSize;  // 1.0 / screenSize
    float2 padding;
};

/// Screen pixels → NDC: x ∈ [0, W] → [-1, 1], y ∈ [0, H] → [1, -1] (Y flip),
/// the mapping overlay_vs.hlsl uses so the batches line up.
float4 ScreenToClip(float2 pixel) {
    return float4(pixel.x * invScreenSize.x * 2.0 - 1.0,
                  1.0 - pixel.y * invScreenSize.y * 2.0, 0.0, 1.0);
}

#ifdef UZ_BINDLESS
/// Mirrors OverlayRrectVertex (text_renderer.h). StructuredBuffer elements
/// are tightly packed, so the field order alone fixes the 96-byte layout.
struct OverlayRrectVertex {
    float2 position;
    float2 localPos;
    float2 halfExtent;
    float2 shadowOffset;
    float  cornerRadius;
    float  borderThickness;
    float  shadowBlur;
    float  _pad0;
    float4 fillColor;
    float4 borderColor;
    float4 shadowColor;
};

cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint unusedIndex;
    uint samplerIndex;
    uint vertexOffset;      ///< Added to SV_VertexID to index into StructuredBuffer.
};

VSOutput main(uint vertexID : SV_VertexID) {
    StructuredBuffer<OverlayRrectVertex> vertices = ResourceDescriptorHeap[vertexBufferIndex];
    const OverlayRrectVertex v = vertices[vertexID + vertexOffset];

    VSOutput output;
    output.position     = ScreenToClip(v.position);
    output.localPos     = v.localPos;
    output.halfExtent   = v.halfExtent;
    output.shadowOffset = v.shadowOffset;
    output.params       = float3(v.cornerRadius, v.borderThickness, v.shadowBlur);
    output.fillColor    = v.fillColor;
    output.borderColor  = v.borderColor;
    output.shadowColor  = v.shadowColor;
    return output;
}
#else
struct VSInput {
    float2 position        : POSITION;
    float2 localPos        : TEXCOORD0;
    float2 halfExtent      : TEXCOORD1;
    float2 shadowOffset    : TEXCOORD2;
    float  cornerRadius    : TEXCOORD3;
    float  borderThickness : TEXCOORD4;
    float  shadowBlur      : TEXCOORD5;
    float  pad0            : TEXCOORD6;
    float4 fillColor       : COLOR0;
    float4 borderColor     : COLOR1;
    float4 shadowColor     : COLOR2;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position     = ScreenToClip(input.position);
    output.localPos     = input.localPos;
    output.halfExtent   = input.halfExtent;
    output.shadowOffset = input.shadowOffset;
    output.params       = float3(input.cornerRadius, input.borderThickness, input.shadowBlur);
    output.fillColor    = input.fillColor;
    output.borderColor  = input.borderColor;
    output.shadowColor  = input.shadowColor;
    return output;
}
#endif
