#ifndef UZ_REALTIME_COMMON
#define UZ_REALTIME_COMMON
#include "../common/encoding.hlsli"
#include "../include/depth_common.hlsli"
#include "realtime_denoise_math.hlsli"
cbuffer RealtimeFrameCB : register(b1) {
    row_major float4x4 viewProj : packoffset(c0);
    float4 cameraPos : packoffset(c4);
    row_major float4x4 invViewProj : packoffset(c9);
    float nearPlane : packoffset(c13.x);
    float farPlane : packoffset(c13.y);
};
// Class policy is separate from the receiver guides and signal descriptors.
cbuffer RealtimePolicyCB : register(b2) {
    float2 previousJitter;
    float2 currentJitter;
    float minCurrentWeight;
    float normalPower;
    float spatialRadiusScale;
    float mirrorRoughness;
};
cbuffer RealtimeRootCB : register(b0) {
    uint inputSrv, depthSrv, normalSrv, velocitySrv;
    uint historySrv, momentsSrv, geometrySrv, guideSrv;
    uint historyUav, momentsUav, geometryUav, guideUav;
    uint outputUav, width, height, historyValid;
    uint diagnosticsUav;
};
struct Receiver {
    float3 position;
    float3 normal;
    float roughness;
    float linearDepth;
    bool surface;
};
Receiver CurrentReceiver(int2 p) {
    Texture2D<float> depth = ResourceDescriptorHeap[depthSrv];
    Texture2D<float4> normal = ResourceDescriptorHeap[normalSrv];
    float z = depth.Load(int3(p,0));
    float4 n = normal.Load(int3(p,0));
    Receiver r;
    r.surface = !IsSkyDepth(z);
    r.position = ReconstructWorldPos((float2(p)+0.5)/float2(width,height), z, invViewProj);
    r.normal = OctDecode(n.rg);
    r.roughness = n.a;
    r.linearDepth = LinearizeDepthReversedZ(z,nearPlane,farPlane);
    return r;
}
float PackNormal(float3 n) {
    uint2 q = uint2(round(saturate(OctEncode(n))*1023.0));
    return float(q.x | (q.y<<10));
}
float3 UnpackNormal(float n) {
    uint q = uint(n);
    return OctDecode(float2(q&1023u,q>>10)/1023.0);
}
float PlaneTolerance(Receiver r) { return max(1.0,length(r.position-cameraPos.xyz)*0.001); }
float PlaneDistance(Receiver a, Receiver b) {
    float3 delta = a.position-b.position;
    return max(abs(dot(delta,a.normal)),abs(dot(delta,b.normal)));
}
bool AcceptReceiver(Receiver a, Receiver b) {
    return a.surface && b.surface &&
        Compatible(dot(a.normal,b.normal),PlaneDistance(a,b),PlaneTolerance(a),abs(a.roughness-b.roughness));
}
float ReceiverWeight(Receiver a, Receiver b) {
    return exp(-PlaneDistance(a,b)/PlaneTolerance(a)) *
        pow(saturate(dot(a.normal,b.normal)),normalPower) *
        exp(-abs(a.roughness-b.roughness)*32.0);
}
float Luminance(float3 rgb) { return dot(rgb,float3(0.2126,0.7152,0.0722)); }
bool InBounds(int2 p) { return all(p>=0) && all(p<int2(width,height)); }
#endif
