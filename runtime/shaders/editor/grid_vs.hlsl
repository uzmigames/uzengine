/// Editor grid vertex shader — fullscreen triangle.
///
/// Generates a fullscreen triangle from SV_VertexID (0,1,2).
/// Passes clip-space position and NDC coordinates to the pixel shader
/// for world-space ray reconstruction.

struct VSOutput {
    float4 position : SV_Position;
    float2 ndc      : TEXCOORD0;
};

/// Generates fullscreen coverage with a single triangle.
///
/// Vertex 0: (-1, -1)  Vertex 1: (3, -1)  Vertex 2: (-1, 3)
/// This triangle covers the entire [-1,1]² NDC quad.
VSOutput main(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Fullscreen triangle trick: oversized tri that covers the viewport
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);

    // NDC for world reconstruction (flip Y for DirectX convention)
    output.ndc = float2(output.position.x, output.position.y);

    return output;
}
