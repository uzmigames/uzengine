#ifndef UZ_ROUGH_SPECULAR_COMMON
#define UZ_ROUGH_SPECULAR_COMMON
#include "../common/encoding.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RoughSpecularFrame : register(b1) {
    row_major float4x4 viewProj : packoffset(c0);
    float4 cameraPos : packoffset(c4);
    row_major float4x4 invViewProj : packoffset(c9);
    float nearPlane : packoffset(c13.x);
    float farPlane : packoffset(c13.y);
};
cbuffer RoughSpecularConfig : register(b2) {
    float2 previousJitter;
    float2 currentJitter;
};
cbuffer RoughSpecularRoot : register(b0) {
    uint inputSrv, depthSrv, normalSrv, velocitySrv;
    uint hitDistanceSrv, historySrv, momentsSrv, geometrySrv;
    uint guidesSrv, colorUav, momentsUav, geometryUav;
    uint guidesUav, width, height, historyValid;
    uint outputUav;
};
struct RoughReceiver {
    float3 position;
    float3 normal;
    float roughness;
    float hitDistance;
    float linearDepth;
    bool surface;
};
bool InBounds(int2 p) { return all(p >= 0) && all(p < int2(width, height)); }
float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float PackNormal(float3 n) {
    uint2 q = uint2(round(saturate(OctEncode(n)) * 1023.0));
    return float(q.x | (q.y << 10));
}
float3 UnpackNormal(float n) {
    uint q = uint(n);
    return OctDecode(float2(q & 1023u, q >> 10) / 1023.0);
}
RoughReceiver Receiver(int2 p) {
    Texture2D<float> depth = ResourceDescriptorHeap[depthSrv];
    Texture2D<float4> normal = ResourceDescriptorHeap[normalSrv];
    Texture2D<float> hit = ResourceDescriptorHeap[hitDistanceSrv];
    float z = depth.Load(int3(p, 0));
    float4 n = normal.Load(int3(p, 0));
    RoughReceiver r;
    r.position = ReconstructWorldPos((float2(p) + 0.5) / float2(width, height), z, invViewProj);
    r.normal = OctDecode(n.rg);
    r.roughness = n.a;
    r.hitDistance = hit.Load(int3(p, 0));
    r.linearDepth = LinearizeDepthReversedZ(z, nearPlane, farPlane);
    r.surface = !IsSkyDepth(z);
    return r;
}
float PlaneDistance(RoughReceiver a, RoughReceiver b) {
    float3 d = a.position - b.position;
    return max(abs(dot(d, a.normal)), abs(dot(d, b.normal)));
}
float PlaneTolerance(RoughReceiver r) {
    return max(0.5, length(r.position - cameraPos.xyz) * 0.001);
}
bool Compatible(RoughReceiver a, RoughReceiver b) {
    return a.surface && b.surface && b.roughness > 0.04
        && dot(a.normal, b.normal) > 0.9
        && abs(a.roughness - b.roughness) < 0.05
        && PlaneDistance(a, b) < PlaneTolerance(a);
}
#endif
