#include "pathtracer_shared.hlsli"

[shader("raygeneration")]
void main() {
    uint2 pixel = DispatchRaysIndex().xy;
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
    if (hasGeometry != 0u) {
        TraceRay(tScene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
    } else {
        payload.radiance = PathEnvironment(ray.Direction);
        payload.terminated = true;
    }
    uRadiance[pixel] = float4(payload.radiance, 1.0);
}
