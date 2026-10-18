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
    float3 value = NrdCurrent(p);
    float count = 1.0;
    if (historyValid != 0u && guide.z > 0.0) {
        Texture2D<float2> velocity = ResourceDescriptorHeap[velocitySrv];
        float2 uv = (float2(p) + .5) / float2(width, height);
        float2 motion = velocity.Load(int3(p, 0));
        float2 previousUV = float2(NrdHistoryUV(uv.x, motion.x, width),
                                  NrdHistoryUV(uv.y, motion.y, height));
        float expectedDepth = guide.z;
        if ((flags & 1u) != 0u) {
            float4 world = mul(float4(uv * float2(2, -2) + float2(-1, 1), guide.z, 1), inverseViewProj);
            world /= world.w;
            float4 previousClip = mul(world, previousViewProj);
            expectedDepth = previousClip.z / previousClip.w;
        }
        // Surface validation stays at the surface-motion coordinate. Reflection
        // history has a separate virtual coordinate and must not use its depth.
        int2 surfaceQ = int2(floor(previousUV * float2(width, height)));
        if (channel != 0u) {
            Texture2D<float4> positions = ResourceDescriptorHeap[virtualPositionSrv];
            Texture2D<float4> modulation = ResourceDescriptorHeap[modulationSrv];
            float4 virtualPoint = positions.Load(int3(p, 0));
            float4 clip = mul(float4(virtualPoint.xyz, 1), previousViewProj);
            float4 currentClip = mul(float4(virtualPoint.xyz, 1), currentViewProj);
            if (virtualPoint.w > 0.0 && clip.w > 0.0 && currentClip.w > 0.0) {
                float2 virtualUV = float2(NrdClipToU(clip.x, clip.w), NrdClipToV(clip.y, clip.w));
                float2 currentVirtualUV = float2(NrdClipToU(currentClip.x, currentClip.w),
                                                NrdClipToV(currentClip.y, currentClip.w));
                float roughness = modulation.Load(int3(p, 0)).w;
                previousUV = lerp(uv + virtualUV - currentVirtualUV, previousUV, roughness * roughness);
            }
        }
        int2 q = int2(floor(previousUV * float2(width, height)));
        if (all(q >= 0) && q.x < int(width) && q.y < int(height)
            && all(surfaceQ >= 0) && surfaceQ.x < int(width) && surfaceQ.y < int(height)) {
            Texture2D<float4> previousGuides = ResourceDescriptorHeap[guideSrv];
            float4 oldGuide = previousGuides.Load(int3(surfaceQ, 0));
            bool accepted = oldGuide.z > 0.0
                && abs(oldGuide.z - expectedDepth) <= .02 * max(oldGuide.z, expectedDepth)
                && dot(OctDecode(oldGuide.xy), OctDecode(guide.xy)) > .9
                && abs(oldGuide.w - guide.w) < .1;
            if (accepted) {
                Texture2D<float4> history = ResourceDescriptorHeap[historySrv];
                float4 previous = history.Load(int3(q, 0));
                float historyCap = 32.0;
                if (channel != 0u) {
                    Texture2D<float4> modulation = ResourceDescriptorHeap[modulationSrv];
                    historyCap = NrdSpecularHistory(modulation.Load(int3(p, 0)).w);
                }
                count = min(previous.a + 1.0, historyCap);
                if ((flags & 2u) != 0u) {
                    // Large screen-space motion makes the stationary variance
                    // envelope too permissive. Tighten it and shorten reuse
                    // instead of carrying a long, stale illumination trail.
                    bool moving = length((previousUV - uv) * float2(width, height)) > 1.0;
                    float clampK = moving ? .5 : 3.0;
                    if (moving) {
                        float motionCap = 2.0;
                        if (channel != 0u) {
                            Texture2D<float4> modulation = ResourceDescriptorHeap[modulationSrv];
                            float r = modulation.Load(int3(p, 0)).w;
                            motionCap = 2.0 + 2.0 * r * r;
                        }
                        count = min(count, motionCap);
                    }
                    float3 mean = 0.0, square = 0.0;
                    float weightSum = 0.0;
                    for (int y = -2; y <= 2; ++y) {
                        for (int x = -2; x <= 2; ++x) {
                            int2 s = p + int2(x, y);
                            if (any(s < 0) || s.x >= int(width) || s.y >= int(height)) continue;
                            float4 neighbor = NrdGuide(s);
                            float w = NrdDepthWeight(neighbor.z - guide.z, .02 * guide.z)
                                * NrdNormalWeight(dot(OctDecode(guide.xy), OctDecode(neighbor.xy)));
                            float3 c = NrdCurrent(s);
                            mean += w * c; square += w * c * c; weightSum += w;
                        }
                    }
                    mean /= max(weightSum, 1e-6);
                    float3 sigma = sqrt(max(square / max(weightSum, 1e-6) - mean * mean, 0.0));
                    previous.rgb = float3(
                        NrdClampHistory(previous.r, mean.r, sigma.r, clampK),
                        NrdClampHistory(previous.g, mean.g, sigma.g, clampK),
                        NrdClampHistory(previous.b, mean.b, sigma.b, clampK));
                }
                value = lerp(previous.rgb, value, 1.0 / count);
            }
        }
    }
    if ((flags & 4u) != 0u && count < 2.0 && guide.z > 0.0) {
        // Dense current-frame reconstruction, not a larger a-trous stride:
        // increasing the stride alone leaves holes around newly revealed edges.
        float3 sum = 0.0;
        float total = 0.0;
        Texture2D<float4> modulation = ResourceDescriptorHeap[modulationSrv];
        float roughness = modulation.Load(int3(p, 0)).w;
        int radius = channel == 0u ? 3 : (roughness < .15 ? 1 : 3);
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                int2 s = p + int2(x, y);
                if (any(s < 0) || s.x >= int(width) || s.y >= int(height)) continue;
                float4 neighbor = NrdGuide(s);
                if (neighbor.z <= 0.0) continue;
                float w = NrdDepthWeight(neighbor.z - guide.z, .02 * max(guide.z, neighbor.z))
                    * NrdNormalWeight(dot(OctDecode(guide.xy), OctDecode(neighbor.xy)))
                    * exp(-abs(neighbor.w - guide.w) / .1);
                sum += NrdCurrent(s) * w;
                total += w;
            }
        }
        value = sum / max(total, 1e-6);
    }
    output[p] = float4(value, count);
}
