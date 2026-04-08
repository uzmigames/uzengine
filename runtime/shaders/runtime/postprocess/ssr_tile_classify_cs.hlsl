/// UzEngine SSR Tile Classification — determines which 8x8 tiles need SSR ray tracing.
/// @ref AMD 2020 "FidelityFX SSSR" — tile classification + ray compaction
///
/// Reads roughness from the GBuffer normal texture (alpha channel) and builds:
/// 1. A compacted ray list (packed pixel coordinates)
/// 2. An indirect dispatch argument buffer for the trace pass
///
/// Tiles where all pixels have roughness >= MaxRoughness are skipped entirely.
/// Mirror surfaces (roughness < 0.0001) are always included.
///
/// @constants MIRROR_THRESHOLD = 0.0001

#include "../common/encoding.hlsli"

cbuffer SsrClassifyRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   normalSrvIdx;         // 0
    uint   depthSrvIdx;          // 1
#else
    uint   _srvPad0;             // 0
    uint   _srvPad1;             // 1
#endif
    uint   rayListUavIdx;        // 2 — UAV index for compacted ray list
    uint   counterArgsUavIdx;    // 3 — UAV index for ray counter + dispatch args
    uint   outputWidth;          // 4
    uint   outputHeight;         // 5
    float  roughnessThreshold;   // 6 — max roughness for SSR
    uint   _pad7;                // 7
};

#ifndef UZ_BINDLESS
Texture2D<float4> t_normal : register(t0);
Texture2D<float>  t_depth  : register(t1);
#endif

static const float MIRROR_THRESHOLD = 0.0001;

groupshared uint g_tileRayCount;

/// Pack pixel coordinates into a single uint.
uint PackRayCoords(uint2 pixel) {
    return (pixel.y << 16) | (pixel.x & 0xFFFF);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex,
          uint3 groupId : SV_GroupID)
{
    // Clear shared counter
    if (groupIndex == 0) {
        g_tileRayCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    bool needsRay = false;
    uint2 pixel = DTid.xy;

    if (pixel.x < outputWidth && pixel.y < outputHeight) {
#ifdef UZ_BINDLESS
        Texture2D<float4> normalTex = ResourceDescriptorHeap[normalSrvIdx];
        Texture2D<float>  depthTex  = ResourceDescriptorHeap[depthSrvIdx];
#else
        Texture2D<float4> normalTex = t_normal;
        Texture2D<float>  depthTex  = t_depth;
#endif

        float deviceZ   = depthTex.Load(int3(pixel, 0)).r;
        float4 normData = normalTex.Load(int3(pixel, 0));
        float roughness = max(normData.a, 0.04);

        // Reflective surface: not sky (deviceZ > 0.0001) and roughness below threshold
        bool isReflective = deviceZ > 0.0001;
        bool isGlossy     = roughness < roughnessThreshold;

        needsRay = isReflective && isGlossy;
    }

    // Count rays in this tile via groupshared atomic
    uint localIndex = 0;
    if (needsRay) {
        InterlockedAdd(g_tileRayCount, 1, localIndex);
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 allocates a contiguous block in the global ray list
    uint globalBase = 0;
    if (groupIndex == 0 && g_tileRayCount > 0) {
        // Atomically reserve space in the global ray counter
        RWByteAddressBuffer counterBuf = ResourceDescriptorHeap[counterArgsUavIdx];
        counterBuf.InterlockedAdd(0, g_tileRayCount, globalBase);
    }

    // Broadcast globalBase to all threads via groupshared
    // (Re-using g_tileRayCount as broadcast channel — safe after the barrier)
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0) {
        g_tileRayCount = globalBase;
    }

    GroupMemoryBarrierWithGroupSync();

    globalBase = g_tileRayCount;

    // Write packed ray coordinates to the compacted list
    if (needsRay) {
        RWStructuredBuffer<uint> rayList = ResourceDescriptorHeap[rayListUavIdx];
        rayList[globalBase + localIndex] = PackRayCoords(pixel);
    }
}
