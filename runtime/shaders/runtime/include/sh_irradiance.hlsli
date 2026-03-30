/// L2 SH irradiance. @ref Ramamoorthi 2001 eq.13, Sloan 2008
#ifndef SH_IRRADIANCE_HLSLI
#define SH_IRRADIANCE_HLSLI

#include "math_util.hlsli"

float3 GetSkySHDiffuse(float3 N, float4 SH[7]) {
    static const float kA0 = PI;  // pi
    static const float kA1 = 2.09439510;  // 2pi/3
    static const float c1  = 0.42904276;
    static const float c3  = 0.24770795;

    float4 nDir = float4(kA1 * N.x, kA1 * N.y, kA1 * N.z, kA0);

    float3 irr;
    irr.r = dot(float4(SH[0].xyz, SH[0].w), nDir);
    irr.g = dot(float4(SH[1].xyz, SH[1].w), nDir);
    irr.b = dot(float4(SH[2].xyz, SH[2].w), nDir);

    float x = N.x, y = N.y, z = N.z;
    float4 l2 = float4(x*y, y*z, 3.0*z*z - 1.0, x*z);
    float  l2m2 = x*x - y*y;
    float4 w = float4(2.0*c1, 2.0*c1, c3, 2.0*c1);

    irr.r += dot(SH[3], l2 * w) + c1 * l2m2 * SH[6].x;
    irr.g += dot(SH[4], l2 * w) + c1 * l2m2 * SH[6].y;
    irr.b += dot(SH[5], l2 * w) + c1 * l2m2 * SH[6].z;

    return max(irr, 0.0);
}

#endif
