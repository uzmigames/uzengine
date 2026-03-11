/// Camera test pixel shader — passthrough vertex color.

struct PSInput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    return input.color;
}
