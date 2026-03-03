// {{NAME}} -- Minimal unlit vertex + pixel shader.
//
// This shader passes vertex positions through a world-view-projection matrix
// and outputs a flat color without any lighting calculations.

cbuffer PerFrame : register(b0) {
    float4x4 viewProjection;
};

cbuffer PerObject : register(b1) {
    float4x4 world;
    float4   color;
};

struct VSInput {
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPos = mul(world, float4(input.position, 1.0));
    output.position = mul(viewProjection, worldPos);
    output.uv       = input.uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return color;
}
