// outline.ps.hlsl -- Selection outline pixel shader
// Outputs a solid color for the outline fringe.

cbuffer MaterialCB : register(b1) {
    float4 outlineColor;     // rgb=outline color, a=unused
    float4 _unused1;
};

float4 PSMain() : SV_TARGET {
    return float4(outlineColor.rgb, 1.0);
}
