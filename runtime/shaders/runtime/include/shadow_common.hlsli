/// Shadow sampling — stub. Will be implemented at Step 6.
#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

float UnpackCascadeFloat(uint i, float4 packed[2]) { return packed[i >> 2][i & 3]; }
float SampleShadowMap(float3 worldPos, float3 N, float viewDepth, float2 pixelPos) { return 1.0; }

#endif
