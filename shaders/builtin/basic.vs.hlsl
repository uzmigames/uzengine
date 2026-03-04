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
    float _pad;
};
cbuffer LightCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
};

struct VSInput {
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float3 normal   : NORMAL;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = mul(worldViewProj, float4(input.position, 1.0f));
    output.color    = input.color * colorMul;
    // Use object-space position as a proxy normal for wireframe/cube shading.
    output.normal   = normalize(input.position);
    return output;
}
