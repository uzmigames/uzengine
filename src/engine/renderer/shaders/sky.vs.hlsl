// sky.vs.hlsl -- Fullscreen sky triangle (no vertex buffer -- SV_VertexID only)
//
// Produces a single triangle covering the entire viewport at z=0.9999.
// The pixel shader reconstructs world-space ray direction from NDC + camera vectors.
//
// Root constants (b3 = SkyCB):
//   float3 camForward, float tanHalfFovY
//   float3 camRight,   float aspect
//   float3 camUp,      float _pad

cbuffer SkyCB : register(b3) {
    float3 camForward;  float tanHalfFovY;
    float3 camRight;    float aspect;
    float3 camUp;       float _pad;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 ndc      : TEXCOORD0;
};

VSOutput VSMain(uint vid : SV_VertexID) {
    // Fullscreen triangle: 3 vertices covering [-1,1]x[-1,1] in NDC
    float2 ndcPos[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    VSOutput o;
    o.ndc      = ndcPos[vid];
    // z=0.9999 so sky sits just in front of the far plane; depth_write=false
    o.position = float4(ndcPos[vid], 0.9999, 1.0);
    return o;
}
