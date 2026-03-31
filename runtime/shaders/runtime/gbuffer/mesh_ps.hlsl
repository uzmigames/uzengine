/// UzEngine forward shading pixel shader (translucent/additive).
/// Simple NdotL + ambient. Input matches mesh_vs output.

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 _viewProj : packoffset(c0);
    float4 cameraPos             : packoffset(c4);
    float4 _time                 : packoffset(c5);
    float4 lightDir              : packoffset(c6);
    float4 lightColor            : packoffset(c7);
    float4 ambientColor          : packoffset(c8);
};

#define UZ_LIGHT_TYPES_NO_CB
cbuffer PerObjectCB : register(b2) {
    uint  albedoIndex;
    uint  _n, _r, _m, _a;
    float3 _pad;
    row_major float4x4 _model;
    row_major float4x4 _nmat;
    float3 _ec; float _eInt;
    float3 baseColorTint; float _rMul;
    float _mMul, _nInt;
    float2 _us, _uo;
    float _aoI, matOpacity;
};

SamplerState linearSampler : register(s0);

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target {
    float3 albedo = baseColorTint;
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo *= tex.Sample(linearSampler, input.texcoord).rgb;
    }
#endif
    float3 N = normalize(input.normalWS);
    float3 L = normalize(lightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    float3 color = albedo * (lightColor.xyz * NdotL + ambientColor.xyz);
    return float4(color, matOpacity);
}
