// Reuse the rendering density and CloudCB verbatim. The unused entry is removed
// by DXC when compiling this shader's main.
// Shadow generation must never sample its own BSM through the rendering hooks.
#undef UZ_CLOUD_BSM
#define main CloudUnusedRaymarchMain
#include "cloud_raymarch_cs.hlsl"
#undef main

// Shared with cloud_shadow_resolve_cs.hlsl: exactly 256 bytes at b1.
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

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= resolution)) return;
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUav];
    float2 uv = (float2(DTid.xy) + 0.5f) / float(resolution);
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float4 nearH = mul(float4(ndc, 0.0f, 1.0f), shadowInvViewProj);
    float4 farH = mul(float4(ndc, 1.0f, 1.0f), shadowInvViewProj);
    float3 nearWorld = nearH.xyz / nearH.w;
    float3 direction = normalize(farH.xyz / farH.w - nearWorld);
    float3 origin = nearWorld + float3(0.0f, 0.0f, PLANET_RADIUS_KM);
    if (g_explicitLayers != 0) {
        float2 intervals[4];
        float support = 0.0f;
        uint count = min(g_layerCount, 4u);
        for (uint layer = 0; layer < count; ++layer) {
            float inner = PLANET_RADIUS_KM + g_layerGeometry[layer].x;
            float nearT, farT;
            bool hit = GetRayNearFar(origin, direction, inner,
                inner + g_layerGeometry[layer].y, nearT, farT);
            farT = min(farT, depthRangeKm);
            intervals[layer] = hit ? float2(nearT, max(nearT, farT)) : 0.0f;
            support += intervals[layer].y - intervals[layer].x;
        }
        uint steps = clamp(uint(ceil(float(max(g_primarySteps, 1u))
            * max(g_shadowViewSampleCountScale, 0.0f))), 1u, 256u);
        float stepLength = support / float(steps);
        float opticalDepth = 0.0f, front = depthRangeKm, back = 0.0f;
        for (uint step = 0; step < steps && support > 0.0f; ++step) {
            float packedT = (float(step) + 0.5f) * stepLength;
            for (uint layer = 0; layer < count; ++layer) {
                float length = intervals[layer].y - intervals[layer].x;
                if (packedT < length) {
                    float t = intervals[layer].x + packedT;
                    float mip = LayerMip(layer, direction, stepLength, 0.0f);
                    float extinction = LayerDensity(origin + direction * t, layer, mip)
                        * g_extinction * DENSITY_SCALE;
                    opticalDepth += extinction * stepLength;
                    if (extinction > 0.0f) {
                        front = min(front, max(intervals[layer].x, t - stepLength * 0.5f));
                        back = max(back, min(intervals[layer].y, t + stepLength * 0.5f));
                    }
                    break;
                }
                packedT -= length;
            }
        }
        opticalDepth = min(max(opticalDepth, 0.0f), 65504.0f);
        float meanExtinction = opticalDepth / max(back - front, EXT_EPS);
        output[DTid.xy] = opticalDepth > 0.0f
            ? float4(front, min(meanExtinction, 65504.0f), opticalDepth, exp(-opticalDepth))
            : float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    float innerRadius = PLANET_RADIUS_KM + g_layerBottomKm;
    float2 outerHit = RaySphereIntersect(origin, direction, 0.0f, innerRadius + g_layerHeightKm);
    float start = max(outerHit.x, 0.0f);
    float end = min(outerHit.y, depthRangeKm);
    float2 groundHit = RaySphereIntersect(origin, direction, 0.0f, PLANET_RADIUS_KM);
    if (groundHit.x >= 0.0f) end = min(end, groundHit.x);
    if (end <= start || g_layerHeightKm <= 0.0f) {
        output[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // Remove the empty inner sphere from the integration budget. Rays can
    // cross two cloud-shell intervals when they miss the solid planet.
    float2 innerHit = RaySphereIntersect(origin, direction, 0.0f, innerRadius);
    float gapStart = end;
    float gapEnd = end;
    if (innerHit.y > start && innerHit.x < end) {
        gapStart = clamp(innerHit.x, start, end);
        gapEnd = clamp(innerHit.y, start, end);
    }
    float firstLength = gapStart - start;
    float secondLength = end - gapEnd;
    float supportLength = firstLength + secondLength;
    if (supportLength <= 0.0f) {
        output[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    uint steps = clamp(uint(ceil(float(max(g_primarySteps, 1u))
        * max(g_shadowViewSampleCountScale, 0.0f))), 1u, 256u);
    float stepLength = supportLength / float(steps);
    float3 texelsPerKm = 128.0f * float3(g_noiseXYScale, g_noiseXYScale,
                                       g_noiseZScale / g_layerHeightKm);
    float3 footprint = abs(direction * texelsPerKm) * stepLength;
    float mipLevel = log2(max(1.0f, max(footprint.x, max(footprint.y, footprint.z))));
    float opticalDepth = 0.0f;
    float frontDepth = end;
    float backDepth = start;
    [loop]
    for (uint step = 0; step < steps; ++step) {
        float packedT = (float(step) + 0.5f) * stepLength;
        bool firstSegment = packedT < firstLength;
        float t = firstSegment ? start + packedT : gapEnd + packedT - firstLength;
        float density = SampleDensity(origin + direction * t, innerRadius, g_layerHeightKm, mipLevel);
        float extinction = density * g_extinction * DENSITY_SCALE;
        if (extinction <= 0.0f) continue;
        opticalDepth += extinction * stepLength;
        frontDepth = min(frontDepth, max(firstSegment ? start : gapEnd, t - stepLength * 0.5f));
        backDepth = max(backDepth, min(firstSegment ? gapStart : end, t + stepLength * 0.5f));
    }
    opticalDepth = min(max(opticalDepth, 0.0f), 65504.0f);
    float meanExtinction = opticalDepth / max(backDepth - frontDepth, EXT_EPS);
    output[DTid.xy] = opticalDepth > 0.0f
        ? float4(frontDepth, min(meanExtinction, 65504.0f), opticalDepth, exp(-opticalDepth))
        : float4(0.0f, 0.0f, 0.0f, 1.0f);
}
