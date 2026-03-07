// basic.ps.hlsl -- Basic pixel shader for UzEngine editor geometry.
//
// Applies ambient + N·L directional diffuse lighting to vertex-interpolated
// color, with distance-based alpha fade for grid lines (UE5-style grid).

cbuffer ColorCB : register(b1) {
    float3 colorMul;
    float  alphaMult;       // overall alpha multiplier
    float3 cameraWorldPos;  // camera world position for distance fade
    float  gridFadeEnd;     // XZ distance where lines are fully transparent (0 = no fade)
};
cbuffer LightCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
};

struct PSInput {
    float4 position  : SV_POSITION;
    float3 color     : COLOR;
    float3 normal    : NORMAL;
    float2 worldXZ   : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Ambient + directional N·L diffuse.
    float ambient = 0.35;
    float ndl     = max(dot(normalize(input.normal), -normalize(lightDir)), 0.0);
    float3 lit    = input.color * (ambient + (1.0 - ambient) * ndl * lightIntensity * lightColor);

    // Distance-based alpha fade (UE5-style grid): lines fade out gradually
    // starting at 30% of gridFadeEnd and reaching 0 at gridFadeEnd.
    // gridFadeEnd <= 0 disables fade (gizmos, non-grid geometry).
    float alpha = alphaMult;
    if (gridFadeEnd > 0.001) {
        float2 xzDelta = input.worldXZ - cameraWorldPos.xz;
        float  dist    = length(xzDelta);
        float  fadeStart = gridFadeEnd * 0.30;
        alpha *= 1.0 - smoothstep(fadeStart, gridFadeEnd, dist);
    }

    return float4(lit, alpha);
}
