/// Gizmo pixel shader — unlit flat colored output with alpha.
///
/// Simply passes through the interpolated vertex color and alpha from the
/// vertex shader. No lighting, no textures. The alpha channel is used to
/// dim non-active axes when one axis is being dragged (alpha = 0.5 for
/// inactive, 1.0 for active/hovered).

struct PSInput {
    float4 position : SV_Position;
    float3 color    : COLOR;
    float  alpha    : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    return float4(input.color, input.alpha);
}
