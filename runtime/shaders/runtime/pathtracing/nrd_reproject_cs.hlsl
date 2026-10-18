#include "nrd_common.hlsli"

/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
/// @constants 8 = thread tile; 32 = history cap; .02 = relative depth rejection;
/// .9 = normal rejection cosine; 1e-3 = demodulation floor; .1 = roughness rejection.
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= width || tid.y >= height) return;
    int2 p = tid.xy;
    Texture2D<float4> diffuse = ResourceDescriptorHeap[diffuseSrv];
    Texture2D<float4> albedo = ResourceDescriptorHeap[albedoSrv];
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUav];
    RWTexture2D<float4> guides = ResourceDescriptorHeap[guideUav];
    float4 guide = NrdGuide(p);
    guides[p] = guide;
    float3 value = diffuse.Load(int3(p, 0)).rgb / max(albedo.Load(int3(p, 0)).rgb, 1e-3);
    float count = 1.0;
    if (historyValid != 0u && guide.z > 0.0) {
        Texture2D<float2> velocity = ResourceDescriptorHeap[velocitySrv];
        float2 uv = (float2(p) + .5) / float2(width, height);
        float2 motion = velocity.Load(int3(p, 0));
        float2 previousUV = float2(NrdHistoryUV(uv.x, motion.x, width),
                                  NrdHistoryUV(uv.y, motion.y, height));
        int2 q = int2(floor(previousUV * float2(width, height)));
        if (all(q >= 0) && q.x < int(width) && q.y < int(height)) {
            Texture2D<float4> previousGuides = ResourceDescriptorHeap[guideSrv];
            float4 oldGuide = previousGuides.Load(int3(q, 0));
            bool accepted = oldGuide.z > 0.0
                && abs(oldGuide.z - guide.z) <= .02 * max(oldGuide.z, guide.z)
                && dot(OctDecode(oldGuide.xy), OctDecode(guide.xy)) > .9
                && abs(oldGuide.w - guide.w) < .1;
            if (accepted) {
                Texture2D<float4> history = ResourceDescriptorHeap[historySrv];
                float4 previous = history.Load(int3(q, 0));
                count = min(previous.a + 1.0, 32.0);
                value = lerp(previous.rgb, value, 1.0 / count);
            }
        }
    }
    output[p] = float4(value, count);
}
