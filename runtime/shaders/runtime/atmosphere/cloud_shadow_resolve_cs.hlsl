#include "cloud_shadow.hlsli"

// Exactly 256 bytes; field order matches generation and the C++ upload.
cbuffer ShadowCB : register(b1) {
    row_major float4x4 shadowInvViewProj;
    row_major float4x4 previousViewProj;
    uint outputUav;
    uint currentSrv;
    uint historySrv;
    uint resolution;
    uint historyValid;
    float depthRangeKm;
    float previousDepthRangeKm;
    float varianceGamma;
    float4 reserved[6];
};

float4 CloudShadowConsistentBSM(float4 bsm) {
    bsm.rgb = clamp(bsm.rgb, 0.0f, 65504.0f);
    bsm.r = min(bsm.r, depthRangeKm);
    if (bsm.g <= 0.0f || bsm.b <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    // The implied last support depth must stay within this map's depth range.
    bsm.b = min(bsm.b, bsm.g * max(depthRangeKm - bsm.r, 0.0f));
    bsm.a = exp(-bsm.b);
    return bsm;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= resolution)) return;
    Texture2D<float4> current = ResourceDescriptorHeap[currentSrv];
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUav];
    float4 raw = current.Load(int3(DTid.xy, 0));
    float4 result = CloudShadowConsistentBSM(raw);
    // Do not access the history descriptor until history has been initialized.
    if (historyValid == 0) {
        output[DTid.xy] = result;
        return;
    }

    float2 uv = (float2(DTid.xy) + 0.5f) / float(resolution);
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float4 nearH = mul(float4(ndc, 0.0f, 1.0f), shadowInvViewProj);
    float4 farH = mul(float4(ndc, 1.0f, 1.0f), shadowInvViewProj);
    float3 nearWorld = nearH.xyz / nearH.w;
    float3 direction = normalize(farH.xyz / farH.w - nearWorld);
    float3 currentFrontWorld = nearWorld + direction * raw.r;
    float4 previousH = mul(float4(currentFrontWorld, 1.0f), previousViewProj);
    float3 previousProjected = previousH.xyz / previousH.w;
    float2 previousUv = previousProjected.xy * float2(0.5f, -0.5f) + 0.5f;
    if (any(previousUv < 0.0f) || any(previousUv > 1.0f)
        || previousProjected.z < 0.0f || previousProjected.z > 1.0f) {
        output[DTid.xy] = result;
        return;
    }

    Texture2D<float4> history = ResourceDescriptorHeap[historySrv];
    float4 old = history.SampleLevel(cloudShadowSampler, previousUv, 0);
    // For an orthographic matrix the world-space gradient of projected Z
    // points away from the sun. Recover old front world position on that ray
    // before measuring its depth relative to the new sun-side near plane.
    float3 previousDirection = normalize(float3(previousViewProj[0][2],
        previousViewProj[1][2], previousViewProj[2][2]));
    float3 previousFrontWorld = currentFrontWorld + previousDirection
        * (old.r - previousProjected.z * previousDepthRangeKm);
    old.r = dot(previousFrontWorld - nearWorld, direction);
    float directionProjection = dot(previousDirection, direction);
    if (directionProjection <= 0.0f || old.r < 0.0f || old.r > depthRangeKm) {
        output[DTid.xy] = result;
        return;
    }
    // Preserve integrated extinction when rotating the old support interval.
    old.g /= directionProjection;

    float4 sum = 0.0f;
    float4 sumSquared = 0.0f;
    float4 neighborhoodMin = raw;
    float4 neighborhoodMax = raw;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            int2 pixel = clamp(int2(DTid.xy) + int2(x, y), int2(0, 0),
                               int2(resolution - 1, resolution - 1));
            float4 neighbor = current.Load(int3(pixel, 0));
            sum += neighbor;
            sumSquared += neighbor * neighbor;
            neighborhoodMin = min(neighborhoodMin, neighbor);
            neighborhoodMax = max(neighborhoodMax, neighbor);
        }
    }
    float4 mean = sum / 9.0f;
    float4 deviation = sqrt(max(sumSquared / 9.0f - mean * mean, 0.0f));
    float4 lower = max(neighborhoodMin, mean - max(varianceGamma, 0.0f) * deviation);
    float4 upper = min(neighborhoodMax, mean + max(varianceGamma, 0.0f) * deviation);
    old = clamp(old, lower, upper);
    // Alpha participates in clipping/blending; derive optical depth from the
    // averaged transmittance to keep the BSM's redundant channels consistent.
    float4 blended = lerp(raw, old, 0.9f);
    blended.b = min(max(blended.b, 0.0f), -log(max(saturate(blended.a), 1e-30f)));
    output[DTid.xy] = CloudShadowConsistentBSM(blended);
}
