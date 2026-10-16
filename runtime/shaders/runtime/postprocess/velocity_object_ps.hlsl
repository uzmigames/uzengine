#include "../common/velocity_common.hlsli"

cbuffer VelocityObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 prevModel;
    row_major float4x4 unjitteredViewProj;
    row_major float4x4 prevViewProj;
    float2 screenSize;
    float2 _velocityObjectPad;
};

float2 main(float4 positionCS : SV_Position,
            float4 clipCurr : TEXCOORD0, float4 clipPrev : TEXCOORD1) : SV_Target0 {
    if (clipPrev.w <= 1e-6) return 0;
    return NdcToRenderPixels(clipCurr.xy / clipCurr.w, screenSize)
         - NdcToRenderPixels(clipPrev.xy / clipPrev.w, screenSize);
}
