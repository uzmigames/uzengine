/// Full-resolution cloud Bayer resolve, enabled only by r.Clouds.TemporalUpscale.
/// @ref Karis 2014 "High Quality Temporal Supersampling"
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling"
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
/// Current Bayer pixels pass through without blending; other pixels reproject
/// and variance-clip history. TAA therefore receives a complete cloud image,
/// not a sparse Bayer mask. The 512-byte CB holds every descriptor and matrix.

#ifndef UZ_BINDLESS
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {}
#else
#include "../common/bindless.hlsli"
#include "cloud_bayer.hlsli"

cbuffer CloudTemporalCB : register(b2) {
    row_major float4x4 invViewProj;
    row_major float4x4 previousViewProj;
    float3 cameraPosKm;
    uint fullWidth;
    uint fullHeight;
    uint quarterWidth;
    uint quarterHeight;
    uint frameIndex;
    uint historyValid;
    uint currentSrv;
    uint historySrv;
    uint outputUav;
    uint frontDepthSrv;
    uint depthSrv;
    uint velocitySrv;
    float varianceGamma;
    float4 _padding[20];
};

SamplerState linearSampler : register(s3);

float3 RGBtoYCoCg(float3 rgb) {
    return float3(dot(rgb, float3(0.25, 0.5, 0.25)),
                  0.5 * (rgb.r - rgb.b),
                  dot(rgb, float3(-0.25, 0.5, -0.25)));
}

float3 YCoCgToRGB(float3 c) {
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float3 ClipToAABB(float3 color, float3 center, float3 extent) {
    float3 offset = color - center;
    float3 ratio = extent / max(abs(offset), 1e-6);
    return center + offset * saturate(min(ratio.x, min(ratio.y, ratio.z)));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= fullWidth || DTid.y >= fullHeight) return;
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUav];
    Texture2D<float4> currentTex = ResourceDescriptorHeap[currentSrv];
    int2 quarterPixel = int2(DTid.xy / 4);
    float4 current = currentTex.Load(int3(quarterPixel, 0));
    uint2 slot = DTid.xy % 4;
    bool currentSlot = bayerMatrix[slot.x][slot.y] == frameIndex % 16;
    if (currentSlot || historyValid == 0) {
        output[DTid.xy] = current;
        return;
    }

    float2 size = float2(fullWidth, fullHeight);
    float2 uv = (float2(DTid.xy) + 0.5) / size;
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float4 nearPoint = mul(float4(ndc, 0.25, 1), invViewProj);
    float4 farPoint = mul(float4(ndc, 0.75, 1), invViewProj);
    float3 ray = normalize(nearPoint.xyz / nearPoint.w - farPoint.xyz / farPoint.w);
    Texture2D<float> frontDepthTex = ResourceDescriptorHeap[frontDepthSrv];
    float frontDepthKm = frontDepthTex.Load(int3(quarterPixel, 0));
    // Finite cloud depth includes translation. Transparent sky uses direction only.
    float4 world = frontDepthKm > 0
        ? float4((cameraPosKm + ray * frontDepthKm) * 1e5, 1)
        : float4(ray, 0);
    float4 previousClip = mul(world, previousViewProj);
    float2 previousUV = float2(previousClip.x / previousClip.w * 0.5 + 0.5,
                              0.5 - previousClip.y / previousClip.w * 0.5);

    // @ref Karis 2014 - 3x3 nearest-fragment velocity dilation.
    // @constants reversed-Z: largest depth is closest; GBuffer velocity is in pixels.
    Texture2D<float> sceneDepth = ResourceDescriptorHeap[depthSrv];
    Texture2D<float2> velocity = ResourceDescriptorHeap[velocitySrv];
    float closestDepth = 0;
    int2 closestPixel = int2(DTid.xy);
    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            int2 p = clamp(int2(DTid.xy) + int2(dx, dy), int2(0, 0),
                           int2(fullWidth - 1, fullHeight - 1));
            float depth = sceneDepth.Load(int3(p, 0));
            if (depth > closestDepth) {
                closestDepth = depth;
                closestPixel = p;
            }
        }
    }
    if (closestDepth > 0.0001) {
        previousUV = uv - velocity.Load(int3(closestPixel, 0)) / size;
    }
    if (previousClip.w <= 0 || any(previousUV < 0) || any(previousUV > 1)) {
        output[DTid.xy] = current;
        return;
    }

    // A dense 3x3 neighborhood of this frame's quarter-resolution samples,
    // not the sparse full-resolution Bayer lattice (which contains no zeros).
    float4 mean = 0;
    float4 meanSquare = 0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            int2 p = clamp(quarterPixel + int2(x, y), int2(0, 0),
                           int2(quarterWidth - 1, quarterHeight - 1));
            float4 s = currentTex.Load(int3(p, 0));
            s.rgb = RGBtoYCoCg(s.rgb);
            mean += s;
            meanSquare += s * s;
        }
    }
    mean /= 9;
    meanSquare /= 9;
    float4 extent = varianceGamma * sqrt(max(meanSquare - mean * mean, 0));
    Texture2D<float4> historyTex = ResourceDescriptorHeap[historySrv];
    // Clamp sampling to texel centers because s3 is a wrapping sampler.
    float4 history = historyTex.SampleLevel(linearSampler,
        clamp(previousUV, 0.5 / size, 1 - 0.5 / size), 0);
    float3 clipped = ClipToAABB(RGBtoYCoCg(history.rgb), mean.rgb, extent.rgb);
    output[DTid.xy] = float4(max(YCoCgToRGB(clipped), 0),
                            saturate(clamp(history.a, mean.a - extent.a, mean.a + extent.a)));
}
#endif
