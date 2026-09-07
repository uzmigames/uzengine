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
cbuffer HistoryCB : register(b2) { row_major float4x4 previousViewProj; };
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
    float3 current = currentTex.Load(int3(pixel,0)).rgb;
    float ao = currentAO.Load(int3(pixel,0));
    outputTex[pixel] = float4(current,1.0);
    outputAO[pixel] = ao;
    outputGeometry[pixel] = float4(0,0,0,-1);
    if (IsSkyDepth(depth)) return;
    float3 position = SurfacePosition(fullPixel, depth, fullSize);
    float3 normal = OctDecode(normalTex.Load(int3(fullPixel,0)).rg);
    outputGeometry[pixel] = float4(position, PackSurfaceNormal(normal));
    if (g_historyValid == 0) return;
    float4 previousClip = mul(float4(position,1.0), previousViewProj);
    if (previousClip.w <= 0.0) return;
    float2 previousUV = previousClip.xy / previousClip.w * float2(0.5,-0.5) + 0.5;
    if (any(previousUV < 0.0) || any(previousUV >= 1.0)) return;
    float2 previousPixel = (previousUV * float2(fullSize) - 0.5) * 0.5;
    int2 base = int2(floor(previousPixel));
    float2 fraction = frac(previousPixel);
    float3 colorSum = 0.0;
    float aoSum = 0.0, countSum = 0.0, weightSum = 0.0;
    for (int y=0; y<2; ++y) for (int x=0; x<2; ++x) {
        int2 hp = base + int2(x,y);
        if (any(hp < 0) || any(hp >= int2(size))) continue;
        float4 geometry = historyGeometry.Load(int3(hp,0));
        if (geometry.w < 0.0) continue;
        float3 previousNormal = UnpackSurfaceNormal(geometry.w);
        if (!SurfaceCompatible(position,normal,geometry.xyz,previousNormal)) continue;
        float weight = (x == 0 ? 1.0-fraction.x : fraction.x) * (y == 0 ? 1.0-fraction.y : fraction.y);
        float4 history = historyTex.Load(int3(hp,0));
        colorSum += history.rgb * weight;
        countSum += history.a * weight;
        aoSum += historyAO.Load(int3(hp,0)) * weight;
        weightSum += weight;
    }
    if (weightSum <= 0.0) return;
    float count = min(countSum / weightSum + 1.0, 64.0);
    float alpha = max(1.0/count, asfloat(g_temporalAlphaBits));
    outputTex[pixel] = float4(lerp(colorSum/weightSum,current,alpha),count);
    outputAO[pixel] = lerp(aoSum/weightSum,ao,alpha);
}
