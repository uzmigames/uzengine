// fullscreen.vs.hlsl -- Shared fullscreen triangle vertex shader
//
// Generates a single triangle covering the entire viewport using SV_VertexID.
// No vertex buffer needed -- just cmd_draw(3, 1, 0, 0).
// Used by all post-process passes (SSAO, bloom, composite).

struct VSOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VSOutput VSMain(uint vid : SV_VertexID) {
    VSOutput o;
    // Fullscreen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    // UV: (0,1), (2,1), (0,-1) -- flipped Y for DX convention
    o.texcoord = float2((vid << 1) & 2, vid & 2);
    o.position = float4(o.texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
