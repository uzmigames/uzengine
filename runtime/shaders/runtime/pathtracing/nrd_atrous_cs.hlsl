#include "nrd_common.hlsli"

/// @ref Dammertz et al. 2010 "Edge-Avoiding A-Trous Wavelet Transform for fast Global Illumination Filtering"
/// @constants 8 = tile; [1,4,6,4,1]/16 = separable B3 spline; .02 = relative
/// depth scale; 32 = normal exponent (nrd_math); .1 = roughness scale.
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= width || tid.y >= height) return;
    int2 p = tid.xy;
    Texture2D<float4> input = ResourceDescriptorHeap[inputSrv];
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUav];
    float4 center = NrdGuide(p);
    float3 sum = 0.0;
    float total = 0.0;
    const float kernel[5] = {1.0, 4.0, 6.0, 4.0, 1.0};
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            int2 q = p + int2(x, y) * int(stepWidth);
            if (any(q < 0) || q.x >= int(width) || q.y >= int(height)) continue;
            float4 guide = NrdGuide(q);
            if ((guide.z == 0.0) != (center.z == 0.0)) continue;
            float weight = kernel[x + 2] * kernel[y + 2];
            weight *= NrdDepthWeight(guide.z - center.z, .02 * max(center.z, guide.z));
            weight *= NrdNormalWeight(dot(OctDecode(center.xy), OctDecode(guide.xy)));
            weight *= exp(-abs(center.w - guide.w) / .1);
            sum += input.Load(int3(q, 0)).rgb * weight;
            total += weight;
        }
    }
    float3 result = sum / max(total, 1e-6);
    if (remodulate != 0u) {
        Texture2D<float4> raw = ResourceDescriptorHeap[rawSrv];
        Texture2D<float4> diffuse = ResourceDescriptorHeap[diffuseSrv];
        Texture2D<float4> albedo = ResourceDescriptorHeap[albedoSrv];
        // Preserve the untouched primary-specular/emission/miss residual.
        result = result * max(albedo.Load(int3(p, 0)).rgb, 1e-3)
               + raw.Load(int3(p, 0)).rgb - diffuse.Load(int3(p, 0)).rgb;
    }
    output[p] = float4(max(result, 0.0), 1.0);
}
