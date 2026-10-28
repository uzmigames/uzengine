/// @ref Kopf et al. 2007 "Joint Bilateral Upsampling".
/// @ref Wright et al. 2022 "Lumen", interpolation restricted to the receiver plane.
cbuffer RootConstants : register(b0) {
    uint g_ssgiHalfSrvIndex;
    uint g_ssgiAOHalfSrvIndex;
    uint g_depthFullSrvIndex;
    uint g_normalFullSrvIndex;
    uint g_outputUavIndex;
    uint g_aoOutputUavIndex;
    uint g_fullWidth;
    uint g_fullHeight;
    uint g_depthSigmaBits;
    uint g_normalPowerBits;
#ifdef UZ_GATHER_DXR
    uint g_gatherTlasSrv;
    uint g_gatherMaterialSrv;
    uint g_gatherDxrMode;
#endif
};
#include "ssgi_world_gather.hlsli"

/// What the upsample did with the pixel, recorded because an all-zero weight vector is otherwise
/// ambiguous. Mirrors `GATHER_UPSAMPLE` in `scripts/rendering/flicker/regiondump.py`.
#define UZ_UPSAMPLE_BILATERAL 0u
#define UZ_UPSAMPLE_SKY       1u
#define UZ_UPSAMPLE_FALLBACK  2u
#define UZ_UPSAMPLE_EMPTY     3u

/// Region dump (diagnostics): the reconstruction's own mapping and the value the lighting reads.
///
/// The four weights, the base and the fractions are recorded rather than derived: the mapping from
/// a full-resolution pixel to the gather is not "halve the coordinate", it is a weighted
/// combination of four gather pixels whose weights depend on the receiver-plane test, and a
/// replay that assumed the simple mapping would silently disagree with the GPU.
void WriteRegionDumpGatherUpsample(uint2 fp, uint2 halfSize, uint2 fullSize,
                                   float4 weights, int2 base, float2 fraction,
                                   float3 result, uint state) {
    if (!UzGatherDumpActive()) return;
    StructuredBuffer<uint4>    rects = ResourceDescriptorHeap[_debugRegionSrv];
    RWStructuredBuffer<float4> dump  = ResourceDescriptorHeap[_debugRegionUav];
    for (uint r = 0u; r < _debugRegionCount; ++r) {
        const uint4 a = rects[r * 2u];
        const uint4 b = rects[r * 2u + 1u];
        if (!UzRegionContains(a, fp)) continue;
        const uint rec = UzRegionPixelBase(a, b, fp) + UzRegionGatherOffset(_debugRegionFields);
        dump[rec + 5] = weights;
        dump[rec + 6] = float4(float(base.x), float(base.y), fraction.x, fraction.y);
        dump[rec + 7] = float4(float(halfSize.x), float(halfSize.y),
                               float(fullSize.x), float(fullSize.y));
        dump[rec + 8] = float4(result, asfloat(state));
    }
}

/// @constants 8x8 threads; four bilinear taps on lattice fullPixel=2*halfPixel, including odd dimensions.
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 fullSize = uint2(g_fullWidth,g_fullHeight);
    if (any(id.xy >= fullSize)) return;
    int2 pixel = int2(id.xy);
    Texture2D<float4> inputTex = ResourceDescriptorHeap[g_ssgiHalfSrvIndex];
    Texture2D<float> inputAO = ResourceDescriptorHeap[g_ssgiAOHalfSrvIndex];
    Texture2D<float> depthTex = ResourceDescriptorHeap[g_depthFullSrvIndex];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[g_normalFullSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float> outputAO = ResourceDescriptorHeap[g_aoOutputUavIndex];
    uint2 halfSize;
    inputTex.GetDimensions(halfSize.x,halfSize.y);
    float2 coordinate = float2(pixel)*0.5;
    int2 base = int2(floor(coordinate));
    float2 fraction = frac(coordinate);
    float depth = depthTex.Load(int3(pixel,0));
    if (IsSkyDepth(depth)) {
        outputTex[pixel] = 0.0;
        outputAO[pixel] = 1.0;
        WriteRegionDumpGatherUpsample(id.xy, halfSize, fullSize, 0.0, base, fraction,
                                      float3(0,0,0), UZ_UPSAMPLE_SKY);
        return;
    }
    float3 position = SurfacePosition(pixel,depth,fullSize);
    float3 normal = OctDecode(normalTex.Load(int3(pixel,0)).rg);
    float3 colorSum = 0.0;
    float aoSum = 0.0, weightSum = 0.0;
    float4 tapWeights = 0.0;
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        int2 hp = clamp(base+int2(x,y),int2(0,0),int2(halfSize)-1);
        int2 sp = SurfacePixel(hp,fullSize);
        float sd = depthTex.Load(int3(sp,0));
        if (IsSkyDepth(sd)) continue;
        float3 sn = OctDecode(normalTex.Load(int3(sp,0)).rg);
        float3 samplePosition = SurfacePosition(sp,sd,fullSize);
        if (!SurfaceCompatible(position,normal,samplePosition,sn)) continue;
        float weight = (x == 0 ? 1.0-fraction.x : fraction.x) * (y == 0 ? 1.0-fraction.y : fraction.y);
        weight *= SurfaceWeight(position,normal,samplePosition,sn,asfloat(g_normalPowerBits));
        float4 sample = inputTex.Load(int3(hp,0));
        weight = SsgiAvailableWeight(weight,sample.a,RejectUnavailableGatherSamples());
        colorSum += sample.rgb*weight;
        aoSum += inputAO.Load(int3(hp,0))*weight;
        weightSum += weight;
        tapWeights[y*2+x] = weight;
    }
    float3 result = weightSum > 0.0 ? colorSum/weightSum : 0.0;
    uint state = weightSum > 0.0 ? UZ_UPSAMPLE_BILATERAL : UZ_UPSAMPLE_EMPTY;
    if (weightSum <= 0.0 && sdfDiffuseSamples != 0) {
        float3 geometricNormal = SurfaceGatherGeometricNormal(depthTex,pixel,fullSize,depth,position,normal);
        result = GatherSurfaceLighting(position,normal,geometricNormal,id.xy,uint(timeParams.z));
        state = UZ_UPSAMPLE_FALLBACK;
    }
    outputTex[pixel] = float4(result,1.0);
    outputAO[pixel] = weightSum > 0.0 ? aoSum/weightSum : 1.0;
    WriteRegionDumpGatherUpsample(id.xy, halfSize, fullSize, tapWeights, base, fraction,
                                  result, state);
}
