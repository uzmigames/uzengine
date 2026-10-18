#include "pathtracer_shared.hlsli"
#include "../include/restir_di.hlsli"

[shader("raygeneration")]
void main() {
    uint2 pixel = DispatchRaysIndex().xy;
    if (restirMode != 0u) {
        RWStructuredBuffer<RestirSurface> surfaces = ResourceDescriptorHeap[restirSurfaceUav];
        surfaces[pixel.y * restirWidth + pixel.x] = (RestirSurface)0;
    }
    float2 jitter = float2(Halton(sampleIndex + 1u, 2u),
                           Halton(sampleIndex + 1u, 3u)) - 0.5;
    float2 uv = (float2(pixel) + 0.5 + jitter) / float2(DispatchRaysDimensions().xy);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 target = mul(float4(ndc, 0.5, 1.0), invViewProj);
    RayDesc ray;
    ray.Origin = cameraPos.xyz;
    ray.Direction = normalize(target.xyz / target.w - ray.Origin);
    ray.TMin = 0.01;
    ray.TMax = 1e7;
    PathPayload payload;
    payload.radiance = 0.0;
    payload.throughput = 1.0;
    payload.bounceCount = 0u;
    payload.terminated = false;
    payload.previousPosition = 0.0;
    payload.previousNormal = 0.0;
    payload.previousBsdfPdf = 0.0;
    if (hasGeometry != 0u) {
        // Iteration keeps the DXR stack at primary + nested shadow regardless of N.
        // @ref Kajiya 1986 "The Rendering Equation"
        // @constants maxBounces is the configured number of surface vertices.
        for (uint bounce = 0u; bounce < maxBounces; ++bounce) {
            TraceRay(tScene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
            if (restirMode != 0u) return;
            if (payload.terminated) break;
            ray.Origin = payload.nextOrigin;
            ray.Direction = payload.nextDirection;
        }
    } else {
        payload.radiance = PathEnvironment(ray.Direction);
        payload.terminated = true;
    }
    if (restirMode != 0u) return;
    // Fused resolve: no extra dispatch, half-float history, or sample intermediate.
    // @ref Kahan 1965 "Pracniques: Further Remarks on Reducing Truncation Errors"
    // @constants 1 = one new sample; zero history is overwritten without reading it.
    RWTexture2D<float4> accumulated = ResourceDescriptorHeap[accumulationUav];
    RWTexture2D<float4> compensation = ResourceDescriptorHeap[compensationUav];
    precise float3 mean = payload.radiance;
    float3 correction = 0.0;
    if (accumulatedFrames != 0u) {
        float3 previous = accumulated[pixel].rgb;
        correction = compensation[pixel].rgb;
        mean.x = PathAccumulate(payload.radiance.x, previous.x, correction.x, accumulatedFrames);
        mean.y = PathAccumulate(payload.radiance.y, previous.y, correction.y, accumulatedFrames);
        mean.z = PathAccumulate(payload.radiance.z, previous.z, correction.z, accumulatedFrames);
    }
    accumulated[pixel] = float4(mean, 1.0);
    compensation[pixel] = float4(correction, 0.0);
    uRadiance[pixel] = float4(mean, 1.0);
}
