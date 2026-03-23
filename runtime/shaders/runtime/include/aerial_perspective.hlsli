/// Aerial perspective — stub. Will be implemented at Step 8.
#ifndef AERIAL_PERSPECTIVE_HLSLI
#define AERIAL_PERSPECTIVE_HLSLI

float4 ComputeAerialPerspective(float3 wp, float3 cam, float3 sd, float si,
    float br, float tr, float3 rb, float rsh, float mb, float msh, float ma,
    float rs, float ms, Texture2D<float4> lut, SamplerState s) {
    return float4(0, 0, 0, 1);
}

#endif
