/// @ref Kopf et al. 2007 "Joint Bilateral Upsampling".
/// @ref Wright et al. 2022 "Lumen", interpolation restricted to the receiver plane.
#include "ssgi_world_gather.hlsli"
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
};
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
    float depth = depthTex.Load(int3(pixel,0));
    if (IsSkyDepth(depth)) { outputTex[pixel] = 0.0; outputAO[pixel] = 1.0; return; }
    float3 position = SurfacePosition(pixel,depth,fullSize);
    float3 normal = OctDecode(normalTex.Load(int3(pixel,0)).rg);
    uint2 halfSize;
    inputTex.GetDimensions(halfSize.x,halfSize.y);
    float2 coordinate = float2(pixel)*0.5;
    int2 base = int2(floor(coordinate));
    float2 fraction = frac(coordinate);
    float3 colorSum = 0.0;
    float aoSum = 0.0, weightSum = 0.0;
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
        colorSum += inputTex.Load(int3(hp,0)).rgb*weight;
        aoSum += inputAO.Load(int3(hp,0))*weight;
        weightSum += weight;
    }
    float3 result = weightSum > 0.0 ? colorSum/weightSum : 0.0;
    if (weightSum <= 0.0 && sdfDiffuseSamples != 0) {
        float3 geometricNormal = SurfaceGeometricNormal(depthTex,pixel,fullSize,depth,position);
        result = GatherSurfaceLighting(position,normal,geometricNormal,id.xy,uint(timeParams.z));
    }
    outputTex[pixel] = float4(result,1.0);
    outputAO[pixel] = weightSum > 0.0 ? aoSum/weightSum : 1.0;
}
