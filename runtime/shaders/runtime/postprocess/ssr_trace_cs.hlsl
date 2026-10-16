/// UzEngine SSR Trace — Hi-Z screen-space ray march with GGX VNDF importance sampling.
/// @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps"
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections"
/// @ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing"
/// @ref AMD 2020 "FidelityFX Stochastic Screen-Space Reflections" (pipeline pattern)
/// @constants MaxRoughness = 0.8, NumSteps = 32, BinarySteps = 4, MirrorThreshold = 0.04

#include "../common/encoding.hlsli"
#include "../common/sampling.hlsli"

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
    uint   hitDistUavIdx;       // 8 — bindless UAV index for hit distance output
    uint   hizMip0SrvIdx;       // 9 — Hi-Z mip 0 (half-res min-reduced depth)
    uint   hizMip1SrvIdx;       // 10 — Hi-Z mip 1 (quarter-res)
    uint   hizMipCount;         // 11 — number of Hi-Z mip levels available (0 = use full-res only)
    float  exposureCorrection;  // 12 — always 1.0: scene colour is linear, not pre-exposed
    uint   numSteps;            // 13 — ray march step count (r.SSR.NumSteps, default 16)
    float  startMipLevel;       // 14 — initial HZB mip level (r.SSR.StartMipLevel, default 1.0)
    float  slopeToleranceScale; // 15 — slope tolerance scale (r.SSR.SlopeToleranceScale, default 4.0)
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

static const uint  BINARY_STEPS    = 4;
static const float MAX_DISTANCE    = 5000.0; // cm
static const float MIRROR_THRESHOLD = 0.04;  // roughness below this uses mirror reflect

/// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty"
/// @ref Stachowiak 2015 — temporal IGN rotation per frame for stochastic convergence
/// @constants IGN = {0.06711056, 0.00583715, 52.9829189}, frame rotation = float2(47, 17) * 0.695
float IGN(float2 p, uint frameIndex) {
    p += float(frameIndex % 64) * float2(47.0, 17.0) * 0.695;
    return frac(52.9829189 * frac(p.x * 0.06711056 + p.y * 0.00583715));
}

/// Generate 2D quasi-random sample for VNDF using per-pixel IGN with temporal rotation.
/// @ref Stachowiak 2015 — use IGN as low-discrepancy 2D seed per pixel per frame.
/// Two independent IGN evaluations with different spatial offsets provide the 2D sample.
float2 GetBlueNoiseSample(uint2 pixel, uint frameIndex) {
    float u0 = IGN(float2(pixel), frameIndex);
    float u1 = IGN(float2(pixel) + float2(37.0, 59.0), frameIndex);
    return float2(u0, u1);
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

/// Build an orthonormal basis (tangent frame) from a normal vector.
/// @ref Frisvad 2012 "Building an Orthonormal Basis from a 3D Unit Vector"
/// Returns tangent T and bitangent B such that (T, B, N) is right-handed.
void BuildTangentFrame(float3 N, out float3 T, out float3 B) {
    if (N.z < -0.9999) {
        T = float3(0.0, -1.0, 0.0);
        B = float3(-1.0, 0.0, 0.0);
    } else {
        float a = 1.0 / (1.0 + N.z);
        float b = -N.x * N.y * a;
        T = float3(1.0 - N.x * N.x * a, b, -N.x);
        B = float3(b, 1.0 - N.y * N.y * a, -N.y);
    }
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
        RWTexture2D<float> hd = ResourceDescriptorHeap[hitDistUavIdx]; hd[DTid.xy] = 0.0;
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
        RWTexture2D<float> hd = ResourceDescriptorHeap[hitDistUavIdx]; hd[DTid.xy] = 0.0;
        return;
    }

    // Reconstruct world position and view direction
    float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    float3 V = normalize(cameraPos.xyz - worldPos);

    // Frame index for temporal variation
    uint frameIndex = uint(timeParams.z);

    // ── Ray direction selection: mirror vs VNDF stochastic ──
    // @ref Dupuy & Benyoub 2023 — VNDF importance sampling for rough surfaces
    // @ref Stachowiak 2015 — stochastic multi-ray with temporal accumulation
    //
    // For roughness < MIRROR_THRESHOLD (perfect mirrors), use deterministic mirror
    // reflection. For rougher surfaces, use VNDF importance sampling with
    // temporally-rotated blue noise seeds. The temporal pass accumulates multiple
    // frames to converge the stochastic result.
    float3 R;
    if (roughness <= MIRROR_THRESHOLD) {
        // Mirror reflection — deterministic, no noise
        R = reflect(-V, N);
    } else {
        // GGX VNDF importance sampling — stochastic ray per pixel per frame
        float alpha = roughness * roughness;

        // Build tangent frame from the world-space normal
        float3 T, B;
        BuildTangentFrame(N, T, B);

        // Transform view direction to tangent space
        float3 V_tangent = float3(dot(V, T), dot(V, B), dot(V, N));

        // Get 2D blue noise sample with temporal rotation
        float2 xi = GetBlueNoiseSample(DTid.xy, frameIndex);

        // Sample the VNDF — returns half vector H in tangent space
        float3 H_tangent = SampleGGX_VNDF(alpha, V_tangent, xi);

        // Transform H back to world space
        float3 H_world = H_tangent.x * T + H_tangent.y * B + H_tangent.z * N;

        // Reflect view direction around the sampled microfacet normal
        R = reflect(-V, H_world);
    }

    // Jitter for ray march step offset (spatial only for mirror, temporal for stochastic)
    float jitter = IGN(float2(DTid.xy), roughness > MIRROR_THRESHOLD ? frameIndex : 0u);

    // Ray march in screen space
    float3 rayStart = worldPos + R * 1.0; // small offset to avoid self-hit
    float3 rayEnd   = worldPos + R * MAX_DISTANCE;

    float3 ssStart = ProjectToScreen(rayStart);
    float3 ssEnd   = ProjectToScreen(rayEnd);

    // Skip rays going behind the camera
    if (ssEnd.z < 0.0) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        RWTexture2D<float> hd = ResourceDescriptorHeap[hitDistUavIdx]; hd[DTid.xy] = 0.0;
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

    float3 hitUVZ = float3(0, 0, 0);
    bool hit = false;
    float lastT = 0.0;
    float stepSize = tMax / float(numSteps);

    // ── Hi-Z hierarchical ray march (when mip chain is available) ───────
    // @ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing"
    // @ref AMD FidelityFX SSSR — hierarchical mip stepping
    //
    // Phase 1: Coarse march at Hi-Z mip 1 (quarter-res) — large steps, skip empty space
    // Phase 2: Fine march at Hi-Z mip 0 (half-res) — refine near intersection
    // Phase 3: Binary refinement at full-res depth
    //
    // Fallback: when hizMipCount == 0, use flat linear march (original algorithm).

    if (hizMipCount >= 2) {
        // Hi-Z hierarchical march available
        Texture2D<float> hizMip0 = ResourceDescriptorHeap[hizMip0SrvIdx]; // half-res
        Texture2D<float> hizMip1 = ResourceDescriptorHeap[hizMip1SrvIdx]; // quarter-res

        float t = stepSize * jitter;

        // Phase 1: Coarse march at mip 1 (quarter-res) — 4x coverage per step
        // Each texel covers 4x4 source pixels, so we step 4x larger.
        float coarseStepSize = tMax / float(numSteps / 4);
        float lastCoarseT = 0.0;

        for (uint i = 0; i < numSteps / 4; i++) {
            float3 ssSample = ssStart + ssDir * t;
            if (any(ssSample.xy < 0.0) || any(ssSample.xy > 1.0)) break;

            // Reversed-Z min-reduced: mip stores min(depth) = closest surface
            float sceneDepth = hizMip1.SampleLevel(pointClamp, ssSample.xy, 0).r;
            float depthDiff = ssSample.z - sceneDepth;
            float tolerance = coarseStepSize * abs(ssDir.z) * 2.0 + 0.001;

            if (depthDiff < 0.0 && depthDiff > -tolerance && sceneDepth > 0.0001) {
                // Potential hit at coarse level — refine in phase 2
                hitUVZ = ssSample;
                hit = true;
                break;
            }

            lastCoarseT = t;
            t += coarseStepSize;
        }

        // Phase 2: Fine march at mip 0 (half-res) — refine around the coarse hit
        if (hit) {
            hit = false;
            float fineStart = max(lastCoarseT, 0.0);
            float fineEnd   = min(lastCoarseT + coarseStepSize * 2.0, tMax);
            float fineStepSize = (fineEnd - fineStart) / float(numSteps / 2);
            float fineT = fineStart;
            float lastFineT = fineStart;

            for (uint j = 0; j < numSteps / 2; j++) {
                float3 ssSample = ssStart + ssDir * fineT;
                if (any(ssSample.xy < 0.0) || any(ssSample.xy > 1.0)) break;

                float sceneDepth = hizMip0.SampleLevel(pointClamp, ssSample.xy, 0).r;
                float depthDiff = ssSample.z - sceneDepth;
                float tolerance = fineStepSize * abs(ssDir.z) * 2.0 + 0.0005;

                if (depthDiff < 0.0 && depthDiff > -tolerance && sceneDepth > 0.0001) {
                    hitUVZ = ssSample;
                    hit = true;
                    lastT = lastFineT;
                    stepSize = fineStepSize;
                    break;
                }

                lastFineT = fineT;
                fineT += fineStepSize;
            }
        }
    } else {
        // Fallback: flat linear march (no Hi-Z mips available)
        float t = stepSize * jitter;

        for (uint i = 0; i < numSteps; i++) {
            float3 ssSample = ssStart + ssDir * t;
            if (any(ssSample.xy < 0.0) || any(ssSample.xy > 1.0)) break;

            float sceneDepth = hizTex.SampleLevel(pointClamp, ssSample.xy, 0).r;
            float depthDiff = ssSample.z - sceneDepth;
            float tolerance = stepSize * abs(ssDir.z) * 2.0 + 0.0001;

            if (depthDiff < 0.0 && depthDiff > -tolerance && sceneDepth > 0.0001) {
                hitUVZ = ssSample;
                hit = true;
                break;
            }

            lastT = t;
            t += stepSize;
        }
    }

    // Phase 3: Binary refinement at full-res depth.
    // @ref McGuire 2014 — final refinement at highest resolution.
    // Skip refinement for grazing-angle rays where depth comparison is unreliable.
    if (hit && abs(ssDir.z) > 1e-5) {
        float tLow  = lastT;
        float tHigh = lastT + stepSize;

        for (uint b = 0; b < BINARY_STEPS; b++) {
            float tMid = (tLow + tHigh) * 0.5;
            float3 ssMid = ssStart + ssDir * tMid;

            if (any(ssMid.xy < 0.0) || any(ssMid.xy > 1.0)) break;

            // Full-res depth for final refinement
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
        RWTexture2D<float> hd = ResourceDescriptorHeap[hitDistUavIdx]; hd[DTid.xy] = 0.0;
        return;
    }

    // Compute hit distance in world space for temporal virtual reprojection.
    // @ref AMD FidelityFX SSSR — store hit distance for reproject via prev VP matrix.
    float3 hitWorldPos = ReconstructWorldPos(hitUVZ.xy, hitUVZ.z);
    float hitDist = length(hitWorldPos - worldPos);

    // Fetch scene color at hit point (prev-frame HDR copy).
    // Linear HDR is not pre-exposed, so exposureCorrection is always 1.0.
    float3 hitColor = colorTex.SampleLevel(linearClamp, hitUVZ.xy, 0).rgb * exposureCorrection;

    // Confidence: screen-edge fade × roughness fade × depth-thickness fade
    // @ref AMD FidelityFX SSSR ValidateHit — smoothstep over depth thickness, squared for sharper falloff
    float screenMask  = ScreenFade(hitUVZ.xy);
    float sceneDepthAtHit = hizTex.SampleLevel(pointClamp, hitUVZ.xy, 0).r;
    float depthThickness = abs(hitUVZ.z - sceneDepthAtHit);
    float thicknessFade  = saturate(1.0 - depthThickness * 500.0);
    thicknessFade *= thicknessFade; // squared for sharper falloff per SSSR
    float confidence  = screenMask * rFade * thicknessFade * intensity;

    // Store linear (non-premultiplied) color + confidence.
    // Premultiply is done in deferred_lighting_ps.hlsl to avoid triple-weighting.
    outputSSR[DTid.xy] = float4(hitColor, confidence);
    RWTexture2D<float> hdOut = ResourceDescriptorHeap[hitDistUavIdx]; hdOut[DTid.xy] = hitDist;
}
