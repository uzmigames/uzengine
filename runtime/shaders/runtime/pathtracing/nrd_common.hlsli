#include "../common/encoding.hlsli"
#include "nrd_math.hlsli"

cbuffer NrdRoot : register(b0) {
    uint rawSrv, diffuseSrv, albedoSrv, depthSrv;
    uint normalSrv, velocitySrv, historySrv, guideSrv;
    uint outputUav, guideUav, width, height;
    uint historyValid, inputSrv, stepWidth, remodulate;
};

// Raw reversed-Z is sufficient for relative depth tests: temporal reuse is
// reset on camera cuts, and normal/depth history is sampled with motion.
float4 NrdGuide(int2 p) {
    Texture2D<float> depth = ResourceDescriptorHeap[depthSrv];
    Texture2D<float4> normal = ResourceDescriptorHeap[normalSrv];
    float4 n = normal.Load(int3(p, 0));
    return float4(n.xy, depth.Load(int3(p, 0)), n.w);
}
