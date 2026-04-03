/// UzEngine SSR Trace — Hi-Z screen-space ray march.
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections"
/// @ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing"
/// @constants MaxRoughness = 0.8, NumSteps = 32, BinarySteps = 4

#include "../common/encoding.hlsli"

cbuffer SsrTraceRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   depthSrvIdx;         // 0
    uint   normalSrvIdx;        // 1
    uint   hizSrvIdx;           // 2
    uint   sceneColorSrvIdx;    // 3
#else
    uint   _srvPad0;            // 0
    uint   _srvPad1;            // 1
    uint   _srvPad2;            // 2
    uint   _srvPad3;            // 3
#endif
    uint   outputWidth;         // 4
    uint   outputHeight;        // 5
    float  intensity;           // 6
    float  roughnessMaskScale;  // 7  = -2.0/MaxRoughness
};

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
};

RWTexture2D<float4> outputSSR : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float>  t_depth      : register(t0);
Texture2D<float4> t_normal     : register(t1);
Texture2D<float>  t_hiz        : register(t2);
Texture2D<float4> t_sceneColor : register(t3);
#endif
SamplerState pointClamp  : register(s0);
SamplerState linearClamp : register(s2);

static const uint  NUM_STEPS    = 32;
static const uint  BINARY_STEPS = 4;
static const float MAX_DISTANCE = 5000.0; // cm

/// @ref IGN — Jimenez 2014
float IGN(float2 p) {
    return frac(52.9829189 * frac(p.x * 0.06711056 + p.y * 0.00583715));
}

float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (nearPlane + deviceZ * (farPlane - nearPlane));
}

float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos  = float4(ndc, deviceZ, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

float3 ProjectToScreen(float3 worldPos) {
    float4 clipPos = mul(float4(worldPos, 1.0), viewProj);
    clipPos.xyz /= clipPos.w;
    float2 uv = clipPos.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return float3(uv, clipPos.z);
}

/// Screen-edge vignette — fades reflections near screen borders
/// @ref Stachowiak 2015 — ramp: 5×|screenPos| - 4
float ScreenFade(float2 uv) {
    float2 d = abs(uv * 2.0 - 1.0);
    float2 fade = saturate(d * 5.0 - 4.0);
    return saturate(1.0 - dot(fade, fade));
}

/// Roughness fade — smooth falloff toward MaxRoughness
float RoughnessFade(float roughness) {
    return saturate(roughness * roughnessMaskScale + 2.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

#ifdef UZ_BINDLESS
    Texture2D<float>  depthTex  = ResourceDescriptorHeap[depthSrvIdx];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[normalSrvIdx];
    Texture2D<float4> colorTex  = ResourceDescriptorHeap[sceneColorSrvIdx];
    Texture2D<float>  hizTex    = ResourceDescriptorHeap[hizSrvIdx];
#else
    Texture2D<float>  depthTex  = t_depth;
    Texture2D<float4> normalTex = t_normal;
    Texture2D<float4> colorTex  = t_sceneColor;
    Texture2D<float>  hizTex    = t_hiz;
#endif

    float deviceZ = depthTex.SampleLevel(pointClamp, uv, 0).r;

    // Sky
    if (deviceZ <= 0.0001) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Read GBuffer normal (oct-encoded) and roughness
    float4 normalData = normalTex.Load(int3(DTid.xy, 0));
    float3 N         = OctDecode(normalData.rg);
    float  metallic  = normalData.b;
    float  roughness = max(normalData.a, 0.04);

    // Roughness mask — skip very rough surfaces
    float rFade = RoughnessFade(roughness);
    if (rFade <= 0.0) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Reconstruct world position and reflection direction
    float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    float3 V = normalize(cameraPos.xyz - worldPos);
    float3 R = reflect(-V, N);

    // Jitter start position along the ray with IGN noise
    float jitter = IGN(float2(DTid.xy) + timeParams.x * 100.0);

    // Ray march in screen space
    float3 rayStart = worldPos + R * 1.0; // small offset to avoid self-hit
    float3 rayEnd   = worldPos + R * MAX_DISTANCE;

    float3 ssStart = ProjectToScreen(rayStart);
    float3 ssEnd   = ProjectToScreen(rayEnd);

    // Skip rays going behind the camera
    if (ssEnd.z < 0.0) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float3 ssDir = ssEnd - ssStart;

    // Clip ray to screen bounds [0,1]
    float tMax = 1.0;
    if (abs(ssDir.x) > 1e-6) {
        float t0 = (0.0 - ssStart.x) / ssDir.x;
        float t1 = (1.0 - ssStart.x) / ssDir.x;
        if (ssDir.x > 0) tMax = min(tMax, t1); else tMax = min(tMax, t0);
    }
    if (abs(ssDir.y) > 1e-6) {
        float t0 = (0.0 - ssStart.y) / ssDir.y;
        float t1 = (1.0 - ssStart.y) / ssDir.y;
        if (ssDir.y > 0) tMax = min(tMax, t1); else tMax = min(tMax, t0);
    }
    tMax = max(tMax, 0.0);

    float stepSize = tMax / float(NUM_STEPS);
    float t = stepSize * jitter;

    // Linear march
    float3 hitUVZ = float3(0, 0, 0);
    bool hit = false;

    float lastDepthDiff = 0.0;
    float lastT = 0.0;

    for (uint i = 0; i < NUM_STEPS; i++) {
        float3 ssSample = ssStart + ssDir * t;

        if (any(ssSample.xy < 0.0) || any(ssSample.xy > 1.0)) break;

        float sceneDepth = hizTex.SampleLevel(pointClamp, ssSample.xy, 0).r;
        float rayDepth   = ssSample.z;

        // Reversed-Z: ray is behind scene when rayDepth < sceneDepth
        float depthDiff = rayDepth - sceneDepth;

        // Tolerance scales with step size to avoid misses
        float tolerance = stepSize * abs(ssDir.z) * 2.0 + 0.0001;

        if (depthDiff < 0.0 && depthDiff > -tolerance && sceneDepth > 0.0001) {
            hitUVZ = ssSample;
            hit = true;
            break;
        }

        lastDepthDiff = depthDiff;
        lastT = t;
        t += stepSize;
    }

    // Binary refinement
    if (hit) {
        float tLow  = lastT;
        float tHigh = lastT + stepSize;

        for (uint b = 0; b < BINARY_STEPS; b++) {
            float tMid = (tLow + tHigh) * 0.5;
            float3 ssMid = ssStart + ssDir * tMid;

            float sceneZ = hizTex.SampleLevel(pointClamp, ssMid.xy, 0).r;
            float diff = ssMid.z - sceneZ;

            if (diff < 0.0) {
                tHigh = tMid;
                hitUVZ = ssMid;
            } else {
                tLow = tMid;
            }
        }
    }

    if (!hit) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Fetch scene color at hit point
    float3 hitColor = colorTex.SampleLevel(linearClamp, hitUVZ.xy, 0).rgb;

    // Confidence: screen-edge fade × roughness fade × distance fade
    float screenMask  = ScreenFade(hitUVZ.xy);
    float confidence  = screenMask * rFade * intensity;

    outputSSR[DTid.xy] = float4(hitColor * confidence, confidence);
}
