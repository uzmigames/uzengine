// basic.ps.hlsl -- Basic pixel shader for UzEngine editor geometry.
//
// Applies ambient + N·L directional diffuse lighting to vertex-interpolated
// color.  The directional light parameters come from cbuffer LightCB.

cbuffer LightCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float3 normal   : NORMAL;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Ambient + directional N·L diffuse.
    float ambient = 0.35;
    float ndl     = max(dot(normalize(input.normal), -normalize(lightDir)), 0.0);
    float3 lit    = input.color * (ambient + (1.0 - ambient) * ndl * lightIntensity * lightColor);
    return float4(lit, 1.0f);
}
