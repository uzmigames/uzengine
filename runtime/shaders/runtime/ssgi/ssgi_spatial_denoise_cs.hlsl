/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering", edge-stopping spatial filtering.
/// @ref Wright et al. 2022 "Lumen", interpolate radiance on the receiver plane before material modulation.
#include "ssgi_surface.hlsli"
cbuffer RootConstants : register(b0) {
    uint g_inputSrvIndex;
    uint g_aoInputSrvIndex;
    uint g_depthSrvIndex;
    uint g_normalSrvIndex;
    uint g_outputUavIndex;
    uint g_aoOutputUavIndex;
    uint g_texWidth;
    uint g_texHeight;
    uint g_sigmaDepthBits;
    uint g_sigmaNormalBits;
    uint g_sigmaLuminanceBits;
};
/// @constants 8x8 dispatch; 5x5 normalized spatial kernel with exp(-r^2/8) spatial falloff.
/// No color edge stop: the input is noisy E/pi, not albedo, and isolated bright samples must be averaged.
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_texWidth || id.y >= g_texHeight) return;
    int2 pixel = int2(id.xy);
    Texture2D<float4> inputTex = ResourceDescriptorHeap[g_inputSrvIndex];
    Texture2D<float> inputAO = ResourceDescriptorHeap[g_aoInputSrvIndex];
    Texture2D<float> depthTex = ResourceDescriptorHeap[g_depthSrvIndex];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[g_normalSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float> outputAO = ResourceDescriptorHeap[g_aoOutputUavIndex];
    uint2 fullSize;
    depthTex.GetDimensions(fullSize.x,fullSize.y);
    int2 fp = SurfacePixel(pixel,fullSize);
    float depth = depthTex.Load(int3(fp,0));
    if (IsSkyDepth(depth)) { outputTex[pixel] = 0.0; outputAO[pixel] = 1.0; return; }
    float3 position = SurfacePosition(fp,depth,fullSize);
    float3 normal = OctDecode(normalTex.Load(int3(fp,0)).rg);
    float3 colorSum = 0.0;
    float aoSum = 0.0, weightSum = 0.0;
    for (int y=-2; y<=2; ++y) for (int x=-2; x<=2; ++x) {
        int2 p = clamp(pixel+int2(x,y),int2(0,0),int2(g_texWidth,g_texHeight)-1);
        int2 sp = SurfacePixel(p,fullSize);
        float sd = depthTex.Load(int3(sp,0));
        if (IsSkyDepth(sd)) continue;
        float3 sn = OctDecode(normalTex.Load(int3(sp,0)).rg);
        float weight = exp(-float(x*x+y*y)/8.0) * SurfaceWeight(position,normal,SurfacePosition(sp,sd,fullSize),sn,asfloat(g_sigmaNormalBits));
        colorSum += inputTex.Load(int3(p,0)).rgb * weight;
        aoSum += inputAO.Load(int3(p,0)) * weight;
        weightSum += weight;
    }
    outputTex[pixel] = float4(colorSum / weightSum,1.0);
    outputAO[pixel] = aoSum / weightSum;
}
