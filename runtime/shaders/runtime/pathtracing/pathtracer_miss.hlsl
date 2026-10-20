#include "pathtracer_shared.hlsli"

[shader("miss")]
void main(inout PathPayload payload) {
    if (nrdEnabled != 0u) {
        NrdAddDiffuse(NrdFraction() * payload.throughput * PathEnvironment(WorldRayDirection()));
        // A primary miss is background, not a surface specular signal.
        RWTexture2D<float4> virtualPosition = ResourceDescriptorHeap[nrdVirtualPositionUav];
        if (virtualPosition[DispatchRaysIndex().xy].w > 0.0) {
            NrdAddSpecular((1.0 - NrdFraction()) * payload.throughput
                * PathEnvironment(WorldRayDirection()));
            if (payload.bounceCount == 1u)
                virtualPosition[DispatchRaysIndex().xy] = float4(NrdVirtualPosition(payload.previousPosition,
                    WorldRayDirection(), payload.previousNormal, 10000.0), 1.0);
        }
    }
    payload.radiance += payload.throughput * PathEnvironment(WorldRayDirection());
    payload.terminated = true;
}

// The shadow ray starts with hit=true and skips closest-hit. Only a miss
// clears it, so its four-byte payload is never interpreted as PathPayload.
[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) {
    payload.hit = false;
}
