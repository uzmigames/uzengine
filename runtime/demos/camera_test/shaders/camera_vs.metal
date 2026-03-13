/// Camera test vertex shader (Metal / MSL).
///
/// SM5-equivalent path: receives vertex data via staged vertex buffer.
/// Multiplies position by viewProj matrix from PerFrameCB at buffer(1).
///
/// NOTE: The engine stores matrices in row-major order (DirectXMath convention).
/// The HLSL shader uses `row_major float4x4` with `mul(row_vector, matrix)`.
/// In MSL, float4x4 is column-major, so we transpose before multiplying:
///   clip_pos = transpose(viewProj) * float4(position, 1.0)

#include <metal_stdlib>
using namespace metal;

/// Matches uz::SimpleVertex layout (36 bytes):
///   float3 position  (offset 0,  12 bytes)
///   float4 color     (offset 12, 16 bytes)
///   float2 texcoord  (offset 28,  8 bytes)
struct VertexIn {
    float3 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
};

/// Per-frame constant buffer — bound at buffer(1), matching HLSL register(b1).
///
/// The engine uploads this as a 256-byte PerFrameCB struct. Only the first
/// 96 bytes (viewProj + cameraPos + timeParams) carry data; the rest is padding.
/// The viewProj matrix is stored row-major (DirectXMath convention).
struct PerFrameCB {
    float4x4 viewProj;     // row-major in memory — must transpose for MSL
    float4   cameraPos;
    float4   timeParams;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant PerFrameCB& perFrame [[buffer(1)]]) {
    // Transpose because the engine stores row-major but MSL float4x4 is column-major.
    float4x4 vp = transpose(perFrame.viewProj);

    VertexOut out;
    out.position = vp * float4(in.position, 1.0);
    out.color    = in.color;
    out.texcoord = in.texcoord;
    return out;
}
