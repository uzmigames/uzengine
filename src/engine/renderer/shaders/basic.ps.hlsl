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

struct PSInput {
    float4 position  : SV_POSITION;
    float3 color     : COLOR;
    float3 normal    : NORMAL;
    float2 worldXZ   : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Simple hemisphere lighting (up = bright, down = dark).
    // No external LightCB needed — editor geometry is self-lit.
    float hemi  = 0.5 + 0.5 * normalize(input.normal).y;
    float3 lit  = input.color * (0.5 + 0.5 * hemi);

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
