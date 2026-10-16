#ifndef UZ_CLOUD_SHADOW_HLSLI
#define UZ_CLOUD_SHADOW_HLSLI

// Structured-buffer ABI: 160 bytes. All matrices and distances use world km.
struct CloudShadowCascade {
    row_major float4x4 viewProj;
    row_major float4x4 invViewProj;
    float4 params; // splitNearKm, splitFarKm, depthRangeKm, texelWorldKm
    uint srvIndex;
    uint resolution;
    uint _pad0;
    uint _pad1;
};

#ifndef UZ_CLOUD_SHADOW_USE_EXISTING_SAMPLER
SamplerState cloudShadowSampler : register(s2); // Linear, clamp.
#else
#define cloudShadowSampler clampSampler
#endif

bool CloudShadowProject(float3 worldPosKm, CloudShadowCascade cascade,
                        out float3 projected) {
    float4 clip = mul(float4(worldPosKm, 1.0f), cascade.viewProj);
    projected = clip.xyz / clip.w;
    projected.xy = projected.xy * float2(0.5f, -0.5f) + 0.5f;
    return all(projected.xy >= 0.0f) && all(projected.xy <= 1.0f)
        && projected.z >= 0.0f;
}

float CloudShadowCascadeTransmittance(float3 projected, float3 sunDir,
                                     CloudShadowCascade cascade) {
    Texture2D<float4> shadowMap = ResourceDescriptorHeap[NonUniformResourceIndex(cascade.srvIndex)];
    float receiverDepth = projected.z * cascade.params.z;
    float4 center = shadowMap.SampleLevel(cloudShadowSampler, projected.xy, 0);
    float sunElevation = max(abs(normalize(sunDir).z), 0.05f);
    float radiusKm = max(cascade.params.w,
        max(receiverDepth - center.r, 0.0f) * 0.00465f / sunElevation);
    float radiusUv = radiusKm / max(cascade.params.w * float(cascade.resolution), 1e-7f);
    float transmittance = 0.0f;
    [unroll]
    for (uint tap = 0; tap < 16; ++tap) {
        float angle = float(tap) * 2.39996323f;
        float2 offset = float2(cos(angle), sin(angle))
            * sqrt((float(tap) + 0.5f) / 16.0f) * radiusUv;
        float4 bsm = shadowMap.SampleLevel(cloudShadowSampler, projected.xy + offset, 0);
        float opticalDepth = min(max((receiverDepth - bsm.r) * max(bsm.g, 0.0f), 0.0f),
                                 max(bsm.b, 0.0f));
        transmittance += exp(-opticalDepth);
    }
    return transmittance / 16.0f;
}

float CloudShadowOpticalDepth(float3 worldPosKm, float3 sunDir,
                             uint cascadeBufferSrv, uint cascadeCount) {
    if (cascadeCount == 0 || cascadeBufferSrv == 0xFFFFFFFFu) return 0.0f;
    StructuredBuffer<CloudShadowCascade> cascades = ResourceDescriptorHeap[cascadeBufferSrv];
    [loop]
    for (uint index = 0; index < cascadeCount; ++index) {
        CloudShadowCascade cascade = cascades[index];
        float3 projected;
        if (!CloudShadowProject(worldPosKm, cascade, projected)) continue;
        float transmittance = CloudShadowCascadeTransmittance(projected, sunDir, cascade);
        float edge = min(min(projected.x, 1.0f - projected.x),
                         min(projected.y, 1.0f - projected.y));
        float blendWidth = max(4.0f / float(cascade.resolution), 0.05f);
        if (edge < blendWidth) {
            // Blend by map-space coverage, not camera distance, including the
            // outermost map's transition to unshadowed space.
            float nextTransmittance = 1.0f;
            [loop]
            for (uint next = index + 1; next < cascadeCount; ++next) {
                CloudShadowCascade nextCascade = cascades[next];
                float3 nextProjected;
                if (CloudShadowProject(worldPosKm, nextCascade, nextProjected)) {
                    nextTransmittance = CloudShadowCascadeTransmittance(nextProjected, sunDir, nextCascade);
                    break;
                }
            }
            transmittance = lerp(nextTransmittance, transmittance,
                                 smoothstep(0.0f, blendWidth, edge));
        }
        return -log(max(transmittance, 1e-30f));
    }
    return 0.0f;
}

float SampleCloudShadow(float3 worldPosKm, float3 sunDir,
                        uint cascadeBufferSrv, uint cascadeCount) {
    return exp(-CloudShadowOpticalDepth(worldPosKm, sunDir, cascadeBufferSrv, cascadeCount));
}

#endif
