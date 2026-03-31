/// Fullscreen triangle vertex shader — no vertex buffer required.
/// @ref Standard fullscreen triangle technique (SV_VertexID 0,1,2)

struct FSOutput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

FSOutput main(uint vertexID : SV_VertexID) {
    FSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.positionCS = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.texcoord = float2(uv.x, 1.0f - uv.y);
    return output;
}
