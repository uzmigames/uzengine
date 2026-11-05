#include "rough_specular_common.hlsli"
#include "rough_specular_math.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    if (!InBounds(p)) return;
    Texture2D<float4> normal = ResourceDescriptorHeap[normalSrv];
    Texture2D<float4> color = ResourceDescriptorHeap[historySrv];
    Texture2D<float4> guides = ResourceDescriptorHeap[guidesSrv];
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUav];
    // No mirror writes: its trace, temporal and spatial numerics remain legacy.
    if (!Reconstructs(normal.Load(int3(p, 0)).a)) return;
    float4 c = color.Load(int3(p, 0));
    float alpha = guides.Load(int3(p, 0)).a;
    if (c.a > 0.0 && alpha > 0.0) output[p] = float4(c.rgb, alpha);
}
