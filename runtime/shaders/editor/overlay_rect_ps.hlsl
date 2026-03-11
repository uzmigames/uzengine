/// Editor overlay rect pixel shader — solid colored rectangles.
///
/// Simply outputs the vertex color directly (no texture sampling).

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

float4 main(PSInput input) : SV_Target {
    return input.color;
}
