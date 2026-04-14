/// Flipbook debug VS — generates a fixed-size screen-space quad at top-right corner.
///
/// Draws a quad at roughly the top-right corner of the screen using a single
/// Draw(6,1,0,0) call. No vertex buffer needed — positions and UVs derived
/// from SV_VertexID.
///
/// Entry point: main (mandatory).

struct VSOut {
    float4 positionCS : SV_Position;
    float2 uv         : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID) {
    // 6 vertices = 2 triangles forming a quad.
    // Quad corners in NDC: top-right of screen, 40% wide × 40% tall.
    // NDC range is [-1, +1]. Top-right quadrant: x in [0.2, 0.98], y in [0.98, 0.2].
    static const float2 kCorners[6] = {
        float2(0.20f,  0.98f), // top-left of quad
        float2(0.98f,  0.98f), // top-right
        float2(0.20f,  0.20f), // bottom-left
        float2(0.98f,  0.98f), // top-right (2nd tri)
        float2(0.98f,  0.20f), // bottom-right
        float2(0.20f,  0.20f), // bottom-left (2nd tri)
    };
    static const float2 kUVs[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1),
    };

    VSOut o;
    o.positionCS = float4(kCorners[vid], 0.0f, 1.0f);
    o.uv         = kUVs[vid];
    return o;
}
