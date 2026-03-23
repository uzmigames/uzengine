/// Contact shadows — stub. Will be implemented at Step 6.
#ifndef CONTACT_SHADOWS_HLSLI
#define CONTACT_SHADOWS_HLSLI

float ComputeContactShadow(float3 wp, float3 ld, float rl, float vd,
    float2 pp, float fc, float4x4 vp, Texture2D<float> dt, SamplerState s) { return 1.0; }

#endif
