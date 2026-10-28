/// @ref Wright et al. 2022 "Lumen", final gather depth and normal history rejection.
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering".
#include "ssgi_surface.hlsli"
cbuffer RootConstants : register(b0) {
    uint g_currentSrvIndex;
    uint g_currentAOSrvIndex;
    uint g_historySrvIndex;
    uint g_historyAOSrvIndex;
    uint g_velocitySrvIndex;
    uint g_outputUavIndex;
    uint g_aoOutputUavIndex;
    uint g_packedDimensions;
    uint g_temporalAlphaBits;
    uint g_varianceClipGammaBits;
    uint g_depthSrv;
    uint g_normalSrv;
    uint g_historyGeometrySrv;
    uint g_geometryUav;
    uint g_historyValid;
};
cbuffer HistoryCB : register(b2) {
    row_major float4x4 previousViewProj;
    uint   g_regionCount;      ///< World boxes where the distance field changed this frame or the last (block B.2)
    uint3  _regionPad;
    float4 g_regionLo[16];
    float4 g_regionHi[16];
};

/// True inside one of the change boxes: the history there was accumulated against a field that
/// no longer exists, so it is rejected (UZ_GATHER_FIELD_CHANGED).
bool InChangedRegion(float3 p) {
    [loop] for (uint r = 0; r < min(g_regionCount, 16u); ++r) {
        if (all(p >= g_regionLo[r].xyz) && all(p <= g_regionHi[r].xyz)) return true;
    }
    return false;
}

/// Why the history was not used. Recorded explicitly in the region dump because `count == 1` alone
/// cannot distinguish a rejected history from a freshly initialised one — both leave the pixel at
/// its current sample. Mirrors `GATHER_REJECT` in `scripts/rendering/flicker/regiondump.py`.
#define UZ_GATHER_ACCEPTED         0u
#define UZ_GATHER_SKY_DEPTH        1u
#define UZ_GATHER_HISTORY_INVALID  2u
#define UZ_GATHER_BEHIND_CAMERA    3u
#define UZ_GATHER_OFFSCREEN        4u
#define UZ_GATHER_ZERO_WEIGHT      5u
#define UZ_GATHER_FIELD_CHANGED    6u   ///< inside a box where the distance field changed (block B.2)

/// The history reprojection: the four bilinear taps that survive the receiver-plane test, weighted
/// and normalised. Returns the rejection code; the outputs are meaningful only when it is
/// `UZ_GATHER_ACCEPTED`, and on every other path the caller keeps the current sample, exactly as
/// before this returned a code instead of falling out of `main`.
uint ReprojectGatherHistory(float3 position, float3 normal, uint2 size, uint2 fullSize,
                            Texture2D<float4> historyGeometry, Texture2D<float4> historyTex,
                            Texture2D<float> historyAOTex,
                            out float3 historyColor, out float historyAOValue,
                            out float historyCount, out float weightSum) {
    historyColor = 0.0;
    historyAOValue = 0.0;
    historyCount = 0.0;
    weightSum = 0.0;
    if (g_historyValid == 0) return UZ_GATHER_HISTORY_INVALID;
    float4 previousClip = mul(float4(position,1.0), previousViewProj);
    if (previousClip.w <= 0.0) return UZ_GATHER_BEHIND_CAMERA;
    float2 previousUV = previousClip.xy / previousClip.w * float2(0.5,-0.5) + 0.5;
    if (any(previousUV < 0.0) || any(previousUV >= 1.0)) return UZ_GATHER_OFFSCREEN;
    float2 previousPixel = (previousUV * float2(fullSize) - 0.5) * 0.5;
    int2 base = int2(floor(previousPixel));
    float2 fraction = frac(previousPixel);
    float3 colorSum = 0.0;
    float aoSum = 0.0, countSum = 0.0;
    for (int y=0; y<2; ++y) for (int x=0; x<2; ++x) {
        int2 hp = base + int2(x,y);
        if (any(hp < 0) || any(hp >= int2(size))) continue;
        float4 geometry = historyGeometry.Load(int3(hp,0));
        if (geometry.w < 0.0) continue;
        float3 previousNormal = UnpackSurfaceNormal(geometry.w);
        if (!SurfaceCompatible(position,normal,geometry.xyz,previousNormal)) continue;
        float weight = (x == 0 ? 1.0-fraction.x : fraction.x) * (y == 0 ? 1.0-fraction.y : fraction.y);
        float4 history = historyTex.Load(int3(hp,0));
        weight = SsgiAvailableWeight(weight,history.a,RejectUnavailableGatherSamples());
        colorSum += history.rgb * weight;
        countSum += history.a * weight;
        aoSum += historyAOTex.Load(int3(hp,0)) * weight;
        weightSum += weight;
    }
    if (weightSum <= 0.0) return UZ_GATHER_ZERO_WEIGHT;
    historyColor = colorSum / weightSum;
    historyAOValue = aoSum / weightSum;
    historyCount = countSum / weightSum;
    return UZ_GATHER_ACCEPTED;
}

/// Region dump (diagnostics): the accumulator's stage records, see common/region_dump.hlsli.
///
/// This runs at half resolution and the rectangles are full-resolution pixels, so one gather pixel
/// `h` writes the four full pixels `2h + {0,1}^2` — which is exactly the set of pixels for which
/// `h` is tap (0,0) of the bilateral upsample. Every pixel of a rectangle therefore carries the
/// accumulator state of the gather pixel the reconstruction leans on, and an interior pixel's other
/// three taps are themselves in the rectangle, so the reconstruction can be replayed from the file.
void WriteRegionDumpGatherAccum(uint2 gatherPixel, uint2 gatherSize, uint2 fullSize,
                                float4 r0, float4 r1, float4 r2, float4 r3, float4 r4) {
    if (!UzGatherDumpActive()) return;
    UzGatherDumpHalf(gatherPixel, fullSize, 0u, r0);
    UzGatherDumpHalf(gatherPixel, fullSize, 1u, r1);
    UzGatherDumpHalf(gatherPixel, fullSize, 2u, r2);
    UzGatherDumpHalf(gatherPixel, fullSize, 3u, r3);
    UzGatherDumpHalf(gatherPixel, fullSize, 4u, r4);
    // Which resources this frame's accumulation actually bound. The history ping-pong index is not
    // a shader input, but it shows itself here: the SRV it reads and the UAV it writes swap.
    UzGatherDumpHalf(gatherPixel, fullSize, 17u,
                     float4(asfloat(g_historySrvIndex), asfloat(g_historyGeometrySrv),
                            asfloat(g_outputUavIndex), asfloat(g_historyValid)));
    // The frame stamp, written once per rectangle by the thread owning its origin pixel.
    StructuredBuffer<uint4>    rects = ResourceDescriptorHeap[_debugRegionSrv];
    RWStructuredBuffer<float4> dump  = ResourceDescriptorHeap[_debugRegionUav];
    for (uint r = 0u; r < _debugRegionCount; ++r) {
        const uint4 a = rects[r * 2u];
        const uint4 b = rects[r * 2u + 1u];
        for (uint q = 0u; q < 4u; ++q) {
            const uint2 fp = gatherPixel * 2u + uint2(q & 1u, q >> 1u);
            if (any(fp >= fullSize) || !UzRegionContains(a, fp)) continue;
            if (fp.x == a.x && fp.y == a.y) {
                dump[UzRegionFooterBase(a, b) + 2] = float4(asfloat(_debugRegionStamp),
                                                           float(gatherSize.x), float(gatherSize.y), 1.0);
            }
        }
    }
}

/// @constants 8x8 threads, four bilinear history taps, normal dot >=0.9, maximum 64 history frames.
/// Full-pixel 2*p+0.5 maps to half-lattice coordinate (fullUV*fullSize-0.5)/2.
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 size = uint2(g_packedDimensions & 0xffffu, g_packedDimensions >> 16);
    if (any(id.xy >= size)) return;
    int2 pixel = int2(id.xy);
    Texture2D<float4> currentTex = ResourceDescriptorHeap[g_currentSrvIndex];
    Texture2D<float> currentAO = ResourceDescriptorHeap[g_currentAOSrvIndex];
    Texture2D<float4> historyTex = ResourceDescriptorHeap[g_historySrvIndex];
    Texture2D<float> historyAO = ResourceDescriptorHeap[g_historyAOSrvIndex];
    Texture2D<float4> historyGeometry = ResourceDescriptorHeap[g_historyGeometrySrv];
    Texture2D<float> depthTex = ResourceDescriptorHeap[g_depthSrv];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[g_normalSrv];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float> outputAO = ResourceDescriptorHeap[g_aoOutputUavIndex];
    RWTexture2D<float4> outputGeometry = ResourceDescriptorHeap[g_geometryUav];
    uint2 fullSize;
    depthTex.GetDimensions(fullSize.x, fullSize.y);
    int2 fullPixel = SurfacePixel(pixel, fullSize);
    float depth = depthTex.Load(int3(fullPixel,0));
    float4 currentSample = currentTex.Load(int3(pixel,0));
    float3 current = currentSample.rgb;
    bool currentAvailable = !RejectUnavailableGatherSamples() || currentSample.a > 0.0;
    float ao = currentAO.Load(int3(pixel,0));

    float temporalAlpha = asfloat(g_temporalAlphaBits);
    float3 historyColor = 0.0;
    float historyAOValue = 0.0, historyCount = 0.0, weightSum = 0.0;
    uint reject = UZ_GATHER_SKY_DEPTH;

    outputGeometry[pixel] = float4(0,0,0,-1);
    if (!IsSkyDepth(depth)) {
        float3 position = SurfacePosition(fullPixel, depth, fullSize);
        float3 normal = OctDecode(normalTex.Load(int3(fullPixel,0)).rg);
        outputGeometry[pixel] = float4(position, PackSurfaceNormal(normal));
        if (InChangedRegion(position)) {
            reject = UZ_GATHER_FIELD_CHANGED;
        } else {
            reject = ReprojectGatherHistory(position, normal, size, fullSize,
                                            historyGeometry, historyTex, historyAO,
                                            historyColor, historyAOValue, historyCount, weightSum);
        }
    }

    float3 result = current;
    float resultAO = ao, count = currentAvailable ? 1.0 : 0.0, alpha = 1.0;
    if (reject == UZ_GATHER_ACCEPTED) {
        count = min(historyCount + (currentAvailable ? 1.0 : 0.0), 64.0);
        alpha = currentAvailable ? max(1.0/count, temporalAlpha) : 0.0;
        result = lerp(historyColor, current, alpha);
        resultAO = lerp(historyAOValue, ao, alpha);
    }
    outputTex[pixel] = float4(result, count);
    outputAO[pixel] = resultAO;

    WriteRegionDumpGatherAccum(uint2(pixel), size, fullSize,
                               float4(current, ao),
                               float4(historyColor, historyCount),
                               float4(result, alpha),
                               float4(asfloat(reject), float(pixel.x), float(pixel.y), count),
                               float4(weightSum, historyAOValue, resultAO, temporalAlpha));
}
