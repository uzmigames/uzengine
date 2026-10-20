#define RESTIR_COMPUTE
#include "pathtracer_shared.hlsli"
#include "../include/restir_gi.hlsli"

// Adds the resampled one-bounce term to the path's remaining radiance and
// performs the same fused Kahan accumulation as pathtracer_raygen. A selected
// sample is visible from this surface by construction of its target, so no
// further shadow ray is traced here.
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= restirWidth || id.y >= restirHeight) return;
    uint index = id.y * restirWidth + id.x;
    RWStructuredBuffer<GIReservoir> reservoirs = ResourceDescriptorHeap[giRestirCurrentUav];
    RWTexture2D<float4> partial = ResourceDescriptorHeap[giRestirPartialUav];
    GIReservoir r = reservoirs[index];
    float3 radiance = partial[id.xy].rgb;
    if (r.M > 0u && r.W > 0.0)
        radiance += GIContribution(GISource(r), r.sample) * r.W;
    // @ref Kahan 1965 "Pracniques: Further Remarks on Reducing Truncation Errors"
    RWTexture2D<float4> accumulated = ResourceDescriptorHeap[accumulationUav];
    RWTexture2D<float4> compensation = ResourceDescriptorHeap[compensationUav];
    precise float3 mean = radiance;
    float3 correction = 0.0;
    if (accumulatedFrames != 0u) {
        float3 previous = accumulated[id.xy].rgb;
        correction = compensation[id.xy].rgb;
        mean.x = PathAccumulate(radiance.x, previous.x, correction.x, accumulatedFrames);
        mean.y = PathAccumulate(radiance.y, previous.y, correction.y, accumulatedFrames);
        mean.z = PathAccumulate(radiance.z, previous.z, correction.z, accumulatedFrames);
    }
    accumulated[id.xy] = float4(mean, 1.0);
    compensation[id.xy] = float4(correction, 0.0);
    RWTexture2D<float4> output = ResourceDescriptorHeap[giRestirRadianceUav];
    output[id.xy] = float4(mean, 1.0);
}
