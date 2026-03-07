// basic.vs.hlsl -- Basic vertex shader for UzEngine editor geometry.
//
// Transforms vertices by worldViewProj, applies per-object color multiplier,
// and passes a proxy normal derived from object-space position for simple
// N·L diffuse lighting in the pixel shader.

cbuffer TransformCB : register(b0) {
    float4x4 worldViewProj;
};
cbuffer ColorCB : register(b1) {
    float3 colorMul;
    float  alphaMult;       // overall alpha multiplier (1.0 = fully opaque)
    float3 cameraWorldPos;  // camera world position for distance fade
    float  gridFadeEnd;     // XZ distance where lines are fully transparent (0 = no fade)
};

struct VSInput {
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct PSInput {
    float4 position  : SV_POSITION;
    float3 color     : COLOR;
    float3 normal    : NORMAL;
    float2 worldXZ   : TEXCOORD0;  // world-space XZ for distance fade
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = mul(worldViewProj, float4(input.position, 1.0f));
    output.color    = input.color * colorMul;
    // Use object-space position as a proxy normal for wireframe/cube shading.
    output.normal   = normalize(input.position);
    // Pass world-space XZ for per-pixel distance fade in PS.
    // Grid vertices are in world space (grid world matrix = identity).
    output.worldXZ  = input.position.xz;
    return output;
}
