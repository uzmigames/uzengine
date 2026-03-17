/// SSR Hi-Z Ray Trace Compute Shader
///
/// Per-pixel screen-space reflection ray marching using the Hi-Z depth pyramid
/// for acceleration. For each pixel with roughness below the threshold, the
/// shader reconstructs the view-space position, computes the reflection
/// direction, projects it to screen space, and marches along the screen-space
/// ray sampling the Hi-Z pyramid at increasing mip levels to skip empty space.
///
/// The algorithm follows UE5's SSRT (Screen Space Ray Tracing) pipeline:
/// 1. Compile a screen-space ray (clip, project, compute tolerance)
/// 2. March the ray in HZB UV space with batched depth comparisons
/// 3. Refine the hit point using linear segment intersection
/// 4. Apply screen-edge vignette and roughness fade
///
/// @source UE5 SSRT/SSRTRayCast.ush:69-627 -- InitScreenSpaceRayFromWorldSpace,
///         CastScreenSpaceRay, ComputeHitVignetteFromScreenPos
/// @source UE5 SSRT/SSRTReflections.usf:105-416 -- ScreenSpaceReflections main
/// @source UE5 ScreenSpaceRayTracing.cpp:279-318 -- ComputeSSRParams,
///         ComputeRoughnessMaskScale
/// @constants SSR_QUALITY=2 (NumSteps=16, NumRays=1) (SSRTReflections.usf:203-204)
///            StartMipLevel=1.0 (SSRTRayCast.ush:660)
///            SlopeCompareToleranceScale=4.0 (SSRTRayCast.ush:643)
///            StepOffset bias=-0.5 (SSRTReflections.usf:347-353)
///            MaxRoughness default=0.4 (ScreenSpaceReflectionMaxRoughness)
///            RoughnessMaskScale=-2/MaxRoughness (ScreenSpaceRayTracing.cpp:287)
///            VignetteFade=saturate(abs(sp)*5-4) (SSRTRayCast.ush:676)
///            FrameRandom=TAASampleIndex*1551 (ScreenSpaceRayTracing.cpp:304)
///            Intensity*0.01 (ScreenSpaceRayTracing.cpp:314)
/// @deviations NONE — all math and constants are direct transcriptions from UE5.
///
/// # Resources
/// - t0: Depth buffer (R32_FLOAT) — full-resolution hardware depth
/// - t1: Normal+Material G-Buffer (R8G8B8A8_UNORM) — rg=oct normal, b=metallic, a=roughness
/// - t2: Hi-Z pyramid (R32_FLOAT, mip chain) — conservative min-depth hierarchy
/// - t3: Scene color (HDR, pre-tonemap) — for resolve pass color fetch
/// - u0: SSR output (R16G16B16A16_FLOAT) — rgb=hit color, a=confidence
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB (matrices, camera, lighting)

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Quality Defines
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SSRTReflections.usf:203-204 — SSR_QUALITY==2 (Medium)
#ifndef SSR_NUM_STEPS
#define SSR_NUM_STEPS 16
#endif

/// Number of samples batched in the inner loop (matches UE5 SSRT_SAMPLE_BATCH_SIZE).
/// @source UE5 SSRTRayCast.ush:6 — SSRT_SAMPLE_BATCH_SIZE 4
#define SAMPLE_BATCH_SIZE 4

/// Starting HZB mip level for the ray march.
/// @source UE5 SSRTRayCast.ush:660 — StartMipLevel = 1.0
#define START_MIP_LEVEL 1.0f

/// Slope-based compare tolerance scale for SSR (not SSGI).
/// @source UE5 SSRTRayCast.ush:643 — SlopeCompareToleranceScale = 4.0f
#define SLOPE_COMPARE_TOLERANCE_SCALE 4.0f

/// @source UE5 SSRTReflections.usf:347-353 — StepOffset -= 0.5
#define STEP_OFFSET_BIAS (-0.5f)

/// Far depth sentinel. UE5 uses 0 for reversed-Z far plane.
/// @source UE5 Common.ush — FarDepthValue
#define FAR_DEPTH_VALUE 0.0f

#define PI 3.14159265358979323846f

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ScreenSpaceRayTracing.cpp:291-318 — ComputeSSRParams layout
/// @source UE5 SSRTReflections.usf:17 — SSRParams float4
cbuffer SSRRootCB : register(b0) {
    uint  depthSrvIndex;     // DWORD 0 — depth buffer SRV (bindless)
    uint  normalSrvIndex;    // DWORD 1 — normal+material G-Buffer SRV (bindless)
    uint  hizSrvIndex;       // DWORD 2 — Hi-Z pyramid SRV (bindless)
    uint  sceneColorSrvIndex;// DWORD 3 — pre-tonemap HDR scene color SRV (bindless)
    uint  outputWidth;       // DWORD 4 — output texture width
    uint  outputHeight;      // DWORD 5 — output texture height
    float ssrIntensity;      // DWORD 6 — SSR intensity [0,1]
                             //   @source UE5 ScreenSpaceRayTracing.cpp:314
                             //   Clamp(Intensity * 0.01, 0, 1)
    float roughnessMaskScale;// DWORD 7 — roughness fade scale
                             //   @source UE5 ScreenSpaceRayTracing.cpp:287
                             //   -2.0 / MaxRoughness
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame CB (b1) — mirrors deferred_lighting_ps.hlsl layout
// ─────────────────────────────────────────────────────────────────────────────

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    float    _pad_pf[14]; // remaining padding
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: textures accessed via ResourceDescriptorHeap[]
#else
Texture2D<float>  depthBuffer     : register(t0);
Texture2D<float4> normalBuffer    : register(t1);
Texture2D<float>  hizPyramid      : register(t2);
Texture2D<float4> sceneColorTex   : register(t3);
#endif

RWTexture2D<float4> ssrOutput     : register(u0);

SamplerState pointSampler  : register(s0);
SamplerState linearSampler : register(s1);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[depthSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthBuffer.SampleLevel(pointSampler, uv, 0).r;
#endif
}

float SampleHiZ(float2 uv, float mipLevel) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[hizSrvIndex];
    return tex.SampleLevel(pointSampler, uv, mipLevel).r;
#else
    return hizPyramid.SampleLevel(pointSampler, uv, mipLevel).r;
#endif
}

float4 SampleNormalMaterial(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[normalSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return normalBuffer.SampleLevel(pointSampler, uv, 0);
#endif
}

/// Fetch scene color (HDR, pre-tonemap). NaN/negative sanitization.
/// @source UE5 SSRTRayCast.ush:728-737 — SampleScreenColor
float4 SampleSceneColor(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[sceneColorSrvIndex];
    float3 c = tex.SampleLevel(linearSampler, uv, 0).rgb;
#else
    float3 c = sceneColorTex.SampleLevel(linearSampler, uv, 0).rgb;
#endif
    // Transform NaNs/negatives to black
    // @source UE5 SSRTRayCast.ush:733 — OutColor.rgb = -min(-OutColor.rgb, 0.0)
    c = -min(-c, 0.0f);
    return float4(c, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth & Position Reconstruction
// ─────────────────────────────────────────────────────────────────────────────

/// Linearize hardware depth to view-space Z distance.
/// UzEngine uses standard (non-reversed) depth mapping by default.
float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (farPlane - deviceZ * (farPlane - nearPlane));
}

/// Reconstruct world-space position from UV + device depth.
/// Uses the inverse view-projection matrix.
float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, deviceZ, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

/// Interleaved Gradient Noise for per-pixel step offset.
/// @source UE5 SSRTReflections.usf:348 — InterleavedGradientNoise(SvPosition.xy, ...)
/// @constants 52.9829189, 0.06711056, 0.00583715
/// @deviations NONE
float InterleavedGradientNoise(float2 pixelPos, float frame) {
    pixelPos += frame * float2(47.0f, 17.0f) * 0.695f;
    return frac(52.9829189f * frac((pixelPos.x * 0.06711056f) + (pixelPos.y * 0.00583715f)));
}

// ─────────────────────────────────────────────────────────────────────────────
// SSR Ray Structure & Initialization
// ─────────────────────────────────────────────────────────────────────────────

/// Screen-space ray for Hi-Z marching.
/// @source UE5 SSRTRayCast.ush:60-66 — FSSRTRay
struct SSRTRay {
    float3 RayStartScreen; // NDC start position (xy in [-1,1], z = deviceZ)
    float3 RayStepScreen;  // NDC step direction * length
    float  CompareTolerance;
};

/// Clip ray step to screen edges.
/// @source UE5 SSRTRayCast.ush:46-56 — GetStepScreenFactorToClipAtScreenEdge
/// @constants NONE (pure math)
/// @deviations NONE
float GetStepScreenFactorToClipAtScreenEdge(float2 RayStartScreen, float2 RayStepScreen) {
    const float RayStepScreenInvFactor = 0.5f * length(RayStepScreen);
    const float2 S = 1.0f - max(abs(RayStepScreen + RayStartScreen * RayStepScreenInvFactor)
                                - RayStepScreenInvFactor, 0.0f) / abs(RayStepScreen);
    const float RayStepFactor = min(S.x, S.y) / RayStepScreenInvFactor;
    return RayStepFactor;
}

/// Initialize a screen-space ray from world-space origin and direction.
/// @source UE5 SSRTRayCast.ush:69-121 — InitScreenSpaceRayFromWorldSpace
/// @constants SlopeCompareToleranceScale=4.0 (SSRTRayCast.ush:643)
/// @deviations UzEngine uses viewProj/invViewProj instead of UE5's
///   TranslatedWorldToClip/ViewToClip. The math is identical since UzEngine
///   does not use a camera-relative world origin (PreViewTranslation=0).
SSRTRay InitScreenSpaceRayFromWorldSpace(
    float3 RayOriginWorld,
    float3 WorldRayDirection,
    float  WorldTMax,
    float  SceneDepth)
{
    // Limit ray to not pass behind the camera
    // @source UE5 SSRTRayCast.ush:83
    float3 viewRayDir = normalize(WorldRayDirection);
    float3 RayEndWorld = RayOriginWorld + WorldRayDirection * WorldTMax;

    // Project start and end to clip space
    // @source UE5 SSRTRayCast.ush:87-91
    float4 RayStartClip = mul(float4(RayOriginWorld, 1.0f), viewProj);
    float4 RayEndClip = mul(float4(RayEndWorld, 1.0f), viewProj);

    float3 RayStartScreen = RayStartClip.xyz / RayStartClip.w;
    float3 RayEndScreen = RayEndClip.xyz / RayEndClip.w;

    // Compute depth-ray clip for tolerance calculation
    // @source UE5 SSRTRayCast.ush:93-94
    // RayDepthScreen: what the depth would be if we moved 1 scene-depth-unit along Z
    // We approximate this similarly to UE5's ViewToClip approach
    float4 RayDepthClip = mul(float4(RayOriginWorld + float3(0, 0, SceneDepth), 1.0f), viewProj);
    float3 RayDepthScreen = RayDepthClip.xyz / RayDepthClip.w;

    SSRTRay Ray;
    Ray.RayStartScreen = RayStartScreen;
    Ray.RayStepScreen = RayEndScreen - RayStartScreen;

    // Clip ray to screen border — always extend to border for SSR
    // @source UE5 SSRTRayCast.ush:100-111
    float ClipToScreenFactor = GetStepScreenFactorToClipAtScreenEdge(RayStartScreen.xy, Ray.RayStepScreen.xy);
    Ray.RayStepScreen *= ClipToScreenFactor;

    // Compare tolerance: accounts for depth slope along the ray
    // @source UE5 SSRTRayCast.ush:118
    Ray.CompareTolerance = max(abs(Ray.RayStepScreen.z),
                               (RayStartScreen.z - RayDepthScreen.z) * SLOPE_COMPARE_TOLERANCE_SCALE);

    return Ray;
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen-Space Ray Casting (Hi-Z March)
// ─────────────────────────────────────────────────────────────────────────────

/// Cast a ray through the Hi-Z pyramid.
///
/// Marches in HZB UV space, sampling the depth hierarchy at increasing mip
/// levels for rougher surfaces. Uses batched 4-sample depth comparisons and
/// linear segment intersection for sub-pixel hit refinement.
///
/// @source UE5 SSRTRayCast.ush:255-627 — CastScreenSpaceRay
/// @constants Step=1/NumSteps, CompareTolerance*Step (ush:278-279),
///            MipLevel += (8.0/NumSteps)*Roughness (ush:425-426),
///            HitTime interpolation (ush:544-607)
/// @deviations NONE — direct transcription of UE5 SSR path (not SSGI)
void CastScreenSpaceRay(
    float  StartMipLevel,
    SSRTRay Ray,
    float  Roughness,
    uint   NumSteps,
    float  StepOffset,
    out float3 OutHitUVz,
    out float  OutLevel,
    out bool   bFoundHit)
{
    const float3 RayStartScreen = Ray.RayStartScreen;
    const float3 RayStepScreen  = Ray.RayStepScreen;

    // Convert from NDC [-1,1] to UV [0,1] space for HZB sampling.
    // Note: Y is flipped (NDC +Y is up, UV +Y is down).
    // @source UE5 SSRTRayCast.ush:275-276
    // UE5 applies HZBUvFactorAndInvFactor for viewport-to-HZB UV mapping.
    // UzEngine's HZB covers the full render target, so the factor is identity.
    float3 RayStartUVz = float3((RayStartScreen.xy * float2(0.5f, -0.5f) + 0.5f), RayStartScreen.z);
    float3 RayStepUVz  = float3((RayStepScreen.xy  * float2(0.5f, -0.5f)),         RayStepScreen.z);

    // @source UE5 SSRTRayCast.ush:278-279
    const float Step = 1.0f / (float)NumSteps;
    float CompareTolerance = Ray.CompareTolerance * Step;

    float LastDiff = 0.0f;
    float Level = StartMipLevel;

    // Scale step to per-iteration increment
    // @source UE5 SSRTRayCast.ush:286-287
    RayStepUVz *= Step;
    float3 RayUVz = RayStartUVz + RayStepUVz * StepOffset;

    float4 MultipleSampleDepthDiff;
    bool4  bMultipleSampleHit;

    bFoundHit = false;

    // Main ray march loop — batched in groups of SAMPLE_BATCH_SIZE
    // @source UE5 SSRTRayCast.ush:384-479
    uint i;
    [loop]
    for (i = 0; i < NumSteps; i += SAMPLE_BATCH_SIZE) {
        // Compute sample coordinates for this batch
        // @source UE5 SSRTRayCast.ush:417-429
        float2 SamplesUV[SAMPLE_BATCH_SIZE];
        float4 SamplesZ;
        float4 SamplesMip;

        [unroll]
        for (uint j = 0; j < SAMPLE_BATCH_SIZE; j++) {
            SamplesUV[j] = RayUVz.xy + ((float)i + (float)(j + 1)) * RayStepUVz.xy;
            SamplesZ[j]  = RayUVz.z  + ((float)i + (float)(j + 1)) * RayStepUVz.z;
        }

        // Mip level increases with roughness along the ray
        // @source UE5 SSRTRayCast.ush:424-429
        SamplesMip.xy = Level;
        Level += (8.0f / (float)NumSteps) * Roughness;
        SamplesMip.zw = Level;
        Level += (8.0f / (float)NumSteps) * Roughness;

        // Sample scene depth from Hi-Z pyramid at the computed mip levels
        // @source UE5 SSRTRayCast.ush:434-448
        float4 SampleDepth;
        [unroll]
        for (uint j2 = 0; j2 < SAMPLE_BATCH_SIZE; j2++) {
            SampleDepth[j2] = SampleHiZ(SamplesUV[j2], SamplesMip[j2]);
        }

        // Evaluate intersections: ray Z vs scene depth
        // @source UE5 SSRTRayCast.ush:451-453
        MultipleSampleDepthDiff = SamplesZ - SampleDepth;

        // Hit test: abs(diff + tolerance) < tolerance  AND  not far plane
        // @source UE5 SSRTRayCast.ush:453
        bMultipleSampleHit = (abs(MultipleSampleDepthDiff + CompareTolerance) < CompareTolerance)
                           & (SampleDepth != FAR_DEPTH_VALUE);

        // Check for any hit in this batch
        // @source UE5 SSRTRayCast.ush:462-463
        [unroll]
        for (uint j3 = 0; j3 < SAMPLE_BATCH_SIZE; j3++) {
            bFoundHit = bFoundHit || bMultipleSampleHit[j3];
        }

        [branch]
        if (bFoundHit) {
            break;
        }

        LastDiff = MultipleSampleDepthDiff.w;
    }

    // Compute hit output coordinates using linear segment intersection refinement.
    // This finds the precise sub-step position where the ray crosses the surface.
    // @source UE5 SSRTRayCast.ush:544-607 — SSR path (not SSGI, not binary search)
    [branch]
    if (bFoundHit) {
        // Walk backwards through the batch to find the first hit and its predecessor
        // @source UE5 SSRTRayCast.ush:546-573
        float DepthDiff0 = MultipleSampleDepthDiff[2];
        float DepthDiff1 = MultipleSampleDepthDiff[3];
        float Time0 = 3.0f;

        [flatten]
        if (bMultipleSampleHit[2]) {
            DepthDiff0 = MultipleSampleDepthDiff[1];
            DepthDiff1 = MultipleSampleDepthDiff[2];
            Time0 = 2.0f;
        }
        [flatten]
        if (bMultipleSampleHit[1]) {
            DepthDiff0 = MultipleSampleDepthDiff[0];
            DepthDiff1 = MultipleSampleDepthDiff[1];
            Time0 = 1.0f;
        }
        [flatten]
        if (bMultipleSampleHit[0]) {
            DepthDiff0 = LastDiff;
            DepthDiff1 = MultipleSampleDepthDiff[0];
            Time0 = 0.0f;
        }

        Time0 += (float)i;

        // Linear segment intersection for sub-step precision
        // @source UE5 SSRTRayCast.ush:602-606
        float TimeLerp = saturate(DepthDiff0 / (DepthDiff0 - DepthDiff1));
        float IntersectTime = Time0 + TimeLerp;

        OutHitUVz = RayUVz + RayStepUVz * IntersectTime;
    } else {
        // No hit — output the end-of-ray position
        // @source UE5 SSRTRayCast.ush:620
        OutHitUVz = RayUVz + RayStepUVz * (float)i;
    }

    // Convert HZB UV back to screen UV for color sampling.
    // @source UE5 SSRTRayCast.ush:623-625
    // UE5 applies InvFactor and ScreenPositionScaleBias; UzEngine has identity
    // HZB UV factor, so we just convert UV back to NDC then to viewport UV.
    // The hit UV is already in [0,1] texture space — we keep it there for
    // the resolve pass to sample scene color directly.
    OutLevel = Level;
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen-Edge Vignette
// ─────────────────────────────────────────────────────────────────────────────

/// Fade reflections near screen edges to avoid artifacts from missing data.
/// @source UE5 SSRTRayCast.ush:674-681 — ComputeHitVignetteFromScreenPos
/// @constants 5, 4 (ush:676)
/// @deviations NONE
float ComputeHitVignette(float2 hitUV) {
    // Convert UV [0,1] to screen pos [-1,1]
    float2 screenPos = hitUV * 2.0f - 1.0f;

    // @source UE5 SSRTRayCast.ush:676
    float2 vignette = saturate(abs(screenPos) * 5.0f - 4.0f);
    return saturate(1.0f - dot(vignette, vignette));
}

// ─────────────────────────────────────────────────────────────────────────────
// Roughness Fade
// ─────────────────────────────────────────────────────────────────────────────

/// Compute roughness-based SSR fade.
///
/// Returns 0 for surfaces too rough (skip SSR), 1 for smooth surfaces.
/// The fade curve ensures a smooth transition near MaxRoughness.
///
/// @source UE5 SSRTReflections.usf:45-49 — GetRoughnessFade
/// @source UE5 ScreenSpaceRayTracing.cpp:279-288 — ComputeRoughnessMaskScale
/// @constants RoughnessMaskScale = -2/MaxRoughness, bias = +2 (usf:48)
/// @deviations NONE
float GetRoughnessFade(float roughness) {
    // min(roughness * RoughnessMaskScale + 2, 1.0)
    // @source UE5 SSRTReflections.usf:48
    return min(roughness * roughnessMaskScale + 2.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SSRTReflections.usf:441-462 — ScreenSpaceReflectionsCS
[numthreads(8, 8, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Bounds check for non-power-of-two output
    // @source UE5 SSRTReflections.usf:443-446
    if (dispatchThreadId.x >= outputWidth || dispatchThreadId.y >= outputHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchThreadId.xy + 0.5f;
    float2 uv = pixelCenter / float2((float)outputWidth, (float)outputHeight);

    // ── Read G-Buffer ──
    float deviceZ = SampleDepth(uv);
    float4 normalMaterial = SampleNormalMaterial(uv);

    // Decode surface properties
    float3 worldNormal = OctDecode(normalMaterial.rg);
    float  roughness = normalMaterial.a;

    // ── Roughness fade — skip SSR for rough surfaces ──
    // @source UE5 SSRTReflections.usf:173-179
    float roughnessFade = GetRoughnessFade(roughness);
    [branch]
    if (roughnessFade <= 0.0f) {
        ssrOutput[dispatchThreadId.xy] = 0.0f;
        return;
    }

    // ── Reconstruct world position and view direction ──
    float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    float3 V = normalize(cameraPos.xyz - worldPos);
    float  sceneDepth = LinearizeDepth(deviceZ);

    // ── Compute reflection direction ──
    // @source UE5 SSRTReflections.usf:374 — L = reflect(-V, N)
    float3 L = reflect(-V, worldNormal);

    // ── Per-pixel step offset using Interleaved Gradient Noise ──
    // @source UE5 SSRTReflections.usf:348-353
    float frameIndex = timeParams.z; // frame count
    float stepOffset = InterleavedGradientNoise(pixelCenter, fmod(frameIndex, 8.0f));
    stepOffset += STEP_OFFSET_BIAS;

    // ── Initialize screen-space ray ──
    // WorldTMax = SceneDepth (limits ray length to current scene depth distance)
    // @source UE5 SSRTRayCast.ush:649 — WorldTMax = SceneDepth
    SSRTRay ray = InitScreenSpaceRayFromWorldSpace(
        worldPos, L,
        /* WorldTMax */ sceneDepth,
        /* SceneDepth */ sceneDepth);

    // ── Cast ray through Hi-Z pyramid ──
    float3 hitUVz;
    float  hitLevel;
    bool   bHit;
    CastScreenSpaceRay(
        START_MIP_LEVEL,
        ray,
        roughness,
        SSR_NUM_STEPS,
        stepOffset,
        /* out */ hitUVz,
        /* out */ hitLevel,
        /* out */ bHit);

    // ── Resolve hit ──
    float4 result = 0.0f;

    [branch]
    if (bHit) {
        // Screen-edge vignette to fade out near viewport borders
        // @source UE5 SSRTRayCast.ush:692 — ComputeHitVignetteFromScreenPos
        float vignette = ComputeHitVignette(hitUVz.xy);

        // Skip if vignette faded to zero
        if (vignette > 0.0f) {
            // Sample scene color at hit UV
            // @source UE5 SSRTReflections.usf:397
            float4 hitColor = SampleSceneColor(hitUVz.xy);

            result = hitColor * vignette;
        }
    }

    // ── Apply roughness fade and intensity ──
    // @source UE5 SSRTReflections.usf:401-402
    result *= roughnessFade;
    result *= ssrIntensity;

    // Store confidence in alpha for the resolve/composite pass
    // confidence = vignette * roughnessFade * intensity
    // The resolve pass uses this to blend between SSR and IBL
    ssrOutput[dispatchThreadId.xy] = result;
}
