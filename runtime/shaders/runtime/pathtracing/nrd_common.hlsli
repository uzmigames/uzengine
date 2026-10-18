#include "../common/encoding.hlsli"
#include "nrd_math.hlsli"

cbuffer NrdRoot : register(b0) {
    uint rawSrv, diffuseSrv, albedoSrv, depthSrv;
    uint normalSrv, velocitySrv, historySrv, guideSrv;
    uint outputUav, guideUav, width, height;
    uint historyValid, inputSrv, stepWidth, remodulate;
};
cbuffer NrdStage2Root : register(b1) {
    uint specularSrv, modulationSrv, virtualPositionSrv, channel;
    uint filteredDiffuseSrv, flags, pathGuideSrv, debugSignal;
    row_major float4x4 previousViewProj;
};
// Existing PerFrameCB prefix, bound read-only; no shared buffer layout growth.
cbuffer NrdCamera : register(b2) {
    row_major float4x4 currentViewProj;
    float4 cameraPosition, cameraTime, cameraLightDir, cameraLightColor, cameraAmbient;
    row_major float4x4 inverseViewProj;
};
// flags: 1 = stage 2, 2 = history clamp, 4 = disocclusion fix.
float3 NrdCurrent(int2 p) {
    if (channel != 0u) {
        Texture2D<float4> specular = ResourceDescriptorHeap[specularSrv];
        Texture2D<float4> modulation = ResourceDescriptorHeap[modulationSrv];
        return specular.Load(int3(p, 0)).rgb / max(modulation.Load(int3(p, 0)).rgb, 1e-3);
    }
    Texture2D<float4> diffuse = ResourceDescriptorHeap[diffuseSrv];
    Texture2D<float4> albedo = ResourceDescriptorHeap[albedoSrv];
    return diffuse.Load(int3(p, 0)).rgb / max(albedo.Load(int3(p, 0)).rgb, 1e-3);
}

// Raw reversed-Z is sufficient for relative depth tests: temporal reuse is
// reset on camera cuts, and normal/depth history is sampled with motion.
float4 NrdGuide(int2 p) {
    // DXR sees both sides of opaque geometry, unlike culled raster guides.
    // Use the actual sampled primary hit for stage-2 edge/disocclusion tests.
    if ((flags & 1u) != 0u) {
        Texture2D<float4> guide = ResourceDescriptorHeap[pathGuideSrv];
        return guide.Load(int3(p, 0));
    }
    Texture2D<float> depth = ResourceDescriptorHeap[depthSrv];
    Texture2D<float4> normal = ResourceDescriptorHeap[normalSrv];
    float4 n = normal.Load(int3(p, 0));
    return float4(n.xy, depth.Load(int3(p, 0)), n.w);
}
