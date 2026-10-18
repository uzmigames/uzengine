#include "pathtracer_shared.hlsli"

[shader("miss")]
void main(inout PathPayload payload) {
    if (nrdEnabled != 0u)
        NrdAddDiffuse(NrdFraction() * payload.throughput * PathEnvironment(WorldRayDirection()));
    payload.radiance += payload.throughput * PathEnvironment(WorldRayDirection());
    payload.terminated = true;
}

// The shadow ray starts with hit=true and skips closest-hit. Only a miss
// clears it, so its four-byte payload is never interpreted as PathPayload.
[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) {
    payload.hit = false;
}
