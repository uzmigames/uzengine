/// GTAO (Ground Truth Ambient Occlusion) — horizon-based compute shader.
///
/// Implements the GTAO algorithm (Jimenez 2016) as used in UE5. Per-pixel,
/// searches for the largest horizon angle in multiple directions, then
/// integrates the visible hemisphere fraction using the surface normal to
/// produce a physically-motivated AO term.
///
/// # Algorithm Overview
/// 1. Reconstruct view-space position and normal from depth + normal buffer.
/// 2. For each direction (NumAngles, rotated per-pixel via IGN):
///    a. March along the direction (GTAO_NUMTAPS steps) sampling depth.
///    b. Find the largest horizon angle (with thickness heuristic + falloff).
///    c. Repeat in the negative direction (dual search).
///    d. Integrate the visible arc against the projected normal.
/// 3. Average over all directions, normalize, and apply distance fade.
///
/// # Resources
/// - t0: Depth buffer (R32_FLOAT or hardware depth SRV)
/// - t1: Normal buffer (R8G8B8A8_UNORM, rg = octahedral normal)
/// - u0: Output AO texture (R8_UNORM)
/// - b0: Root constants (8 DWORDs — SRV indices + frame index)
/// - b2: GTAOParams CBV (bulk parameters including view matrix)
///
/// @source UE5 PostProcessAmbientOcclusion.usf:538-948 — GTAO quality defines,
///         GTAOCombinedPSandCS, SearchForLargestAngleDual, ComputeInnerIntegral,
///         GetRandomVector, InterleavedGradientNoise
/// @source UE5 PostProcessAmbientOcclusionCommon.ush:54-57 — ReconstructCSPos
/// @source UE5 FastMathThirdParty.ush:108-114 — acosFast
/// @constants GTAO_NUMTAPS=12 (usf:555), GTAO_MAX_PIXEL_SCREEN_RADIUS=256.0 (usf:557),
///            PI=3.14159265 (Common.ush), PI_HALF=PI*0.5 (usf:573),
///            ThicknessBlend from GTAOParams[1].y (usf:655),
///            AttenFactor=2.0/(WorldRadius*WorldRadius) (usf:877),
///            OffsetAdvance=0.617 (usf:903)
/// @deviations NONE

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Quality Defines
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 PostProcessAmbientOcclusion.usf:555-557 — SHADER_QUALITY == 3 (high)
#ifndef GTAO_NUMTAPS
#define GTAO_NUMTAPS 12
#endif

/// @source UE5 PostProcessAmbientOcclusion.usf:557 — all quality levels
#define GTAO_MAX_PIXEL_SCREEN_RADIUS 256.0f

/// Number of search directions. Default 2 (UE5 uses 1-4, 2 = good quality/perf).
/// @source UE5 PostProcessAmbientOcclusion.usf:886 — NumAngles from GTAOParams[4].y
#ifndef GTAO_NUM_ANGLES
#define GTAO_NUM_ANGLES 2
#endif

#define PI       3.14159265358979323846f
#define PI_HALF  (PI * 0.5f)

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// All GTAO parameters in 16 DWORDs (64 bytes) — compute root constant limit.
/// No CBV used — compute root sig only has root constants at slot 0.
cbuffer GTAORootCB : register(b0) {
    uint     depthSrvIndex;     // DWORD 0
    uint     normalSrvIndex;    // DWORD 1
    uint     frameIndex;        // DWORD 2
    uint     outputWidth;       // DWORD 3
    float    outputHeight;      // DWORD 4
    float    invWidth;          // DWORD 5
    float    invHeight;         // DWORD 6
    float    worldRadius;       // DWORD 7
    float    invTanHalfFov;     // DWORD 8
    float    nearPlane;         // DWORD 9
    float    farPlane;          // DWORD 10
    float    aoIntensity;       // DWORD 11
    float    temporalCos;       // DWORD 12
    float    temporalSin;       // DWORD 13
    float    temporalOffset;    // DWORD 14
    float    thicknessBlend;    // DWORD 15
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: access textures via ResourceDescriptorHeap[]
#else
Texture2D<float>  depthBuffer   : register(t0);
Texture2D<float4> normalBuffer  : register(t1);
#endif

RWTexture2D<float> aoOutput     : register(u0);

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Fetch depth value from the depth buffer at the given UV.
float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIndex];
    return depthTex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthBuffer.SampleLevel(pointSampler, uv, 0).r;
#endif
}

/// Fetch normal from the normal G-Buffer at the given UV.
float4 SampleNormal(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> normalTex = ResourceDescriptorHeap[normalSrvIndex];
    return normalTex.SampleLevel(pointSampler, uv, 0);
#else
    return normalBuffer.SampleLevel(pointSampler, uv, 0);
#endif
}

/// Fast acos approximation — max error ~0.018 rad.
/// @source UE5 FastMathThirdParty.ush:108-114 — acosFast
/// @constants -0.156583 (FastMathThirdParty.ush:111), 0.5*PI (FastMathThirdParty.ush:111)
/// @deviations NONE
float acosFast(float inX) {
    float x = abs(inX);
    float res = -0.156583f * x + (0.5f * PI);
    res *= sqrt(1.0f - x);
    return (inX >= 0) ? res : PI - res;
}

/// Linearize hardware depth to view-space Z.
///
/// UzEngine uses a standard (non-reversed) depth buffer:
///   z = near * far / (far - deviceZ * (far - near))
///
/// @source UE5 Common.ush:1644-1646 — ConvertFromDeviceZ
float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (farPlane - deviceZ * (farPlane - nearPlane));
}

/// Reconstruct view-space position from UV + linear depth.
///
/// @source UE5 Common.ush:1659-1666 — ScreenToViewPos
float3 ScreenToViewPos(float2 uv, float linearDepth) {
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float3 viewPos;
    float aspect = float(outputWidth) * invHeight;
    viewPos.x = ndc.x * linearDepth / (invTanHalfFov / aspect);
    viewPos.y = ndc.y * linearDepth / invTanHalfFov;
    viewPos.z = linearDepth;
    return viewPos;
}

/// Reconstruct world-space normal from G-Buffer octahedral encoding.
/// GTAO works in screen-space with world normals — no view matrix needed.
float3 GetWorldNormal(float2 uv) {
    float4 normalPacked = SampleNormal(uv);
    return OctDecode(normalPacked.rg);
}

/// Interleaved Gradient Noise — spatially varying pseudo-random value.
/// @source UE5 PostProcessAmbientOcclusion.usf:799-802 — InterleavedGradientNoise
/// @constants 52.9829189, 0.06711056, 0.00583715 (usf:801)
/// @deviations NONE
float InterleavedGradientNoise(float2 iPos) {
    return frac(52.9829189f * frac((iPos.x * 0.06711056f) + (iPos.y * 0.00583715f)));
}

/// Get randomized search direction and step offset per pixel.
/// @source UE5 PostProcessAmbientOcclusion.usf:812-836 — GetRandomVector
/// @constants 16384 (usf:814), 1.0/4.0 (usf:828)
/// @deviations NONE
float3 GetRandomVector(uint2 iPos) {
    iPos.y = 16384u - iPos.y;

    float gradientNoise = InterleavedGradientNoise(float2(iPos));

    float2 randomTexVec;
    /// @source UE5 PostProcessAmbientOcclusion.usf:825-826
    randomTexVec.x = cos(gradientNoise * PI);
    randomTexVec.y = sin(gradientNoise * PI);

    /// @source UE5 PostProcessAmbientOcclusion.usf:828
    float scaleOffset = (1.0f / 4.0f) * ((iPos.y - iPos.x) & 3u);

    float3 result;
    /// @source UE5 PostProcessAmbientOcclusion.usf:831-833
    result.x = dot(randomTexVec, float2(temporalCos, -temporalSin));
    result.y = dot(randomTexVec, float2(temporalSin,  temporalCos));
    result.z = frac(scaleOffset + temporalOffset);

    return result;
}

/// Search for the largest horizon angle in both positive and negative directions.
/// @source UE5 PostProcessAmbientOcclusion.usf:648-694 — SearchForLargestAngleDual
/// @constants 0.0001 epsilon (usf:671), Thickness from GTAOParams[1].y (usf:655)
/// @deviations Uses depth texture directly (no HZB) for simplicity/quality.
float2 SearchForLargestAngleDual(
    uint   numSteps,
    float2 baseUV,
    float2 screenDir,
    float  searchRadius,
    float  initialOffset,
    float3 viewPos,
    float3 viewDir,
    float  attenFactor)
{
    float2 bestAng = float2(-1.0f, -1.0f);

    for (uint i = 0; i < numSteps; i++) {
        float fi = (float)i;

        /// @source UE5 PostProcessAmbientOcclusion.usf:661
        float2 uvOffset = screenDir * max(searchRadius * (fi + initialOffset), fi + 1.0f);
        uvOffset.y *= -1.0f;
        float4 uv2 = baseUV.xyxy + float4(uvOffset.xy, -uvOffset.xy);

        float sceneDepthPos = LinearizeDepth(SampleDepth(uv2.xy));
        float sceneDepthNeg = LinearizeDepth(SampleDepth(uv2.zw));

        // ── Positive direction ──
        /// @source UE5 PostProcessAmbientOcclusion.usf:669-676
        float3 V_pos = ScreenToViewPos(uv2.xy, sceneDepthPos) - viewPos;
        float  lenSqPos = dot(V_pos, V_pos);
        float  ooLenPos = rsqrt(lenSqPos + 0.0001f);
        float  angPos = dot(V_pos, viewDir) * ooLenPos;

        float falloffPos = saturate(lenSqPos * attenFactor);
        angPos = lerp(angPos, bestAng.x, falloffPos);
        /// @source UE5 PostProcessAmbientOcclusion.usf:676 — thickness heuristic
        bestAng.x = (angPos > bestAng.x) ? angPos : lerp(angPos, bestAng.x, thicknessBlend);

        // ── Negative direction ──
        /// @source UE5 PostProcessAmbientOcclusion.usf:679-687
        float3 V_neg = ScreenToViewPos(uv2.zw, sceneDepthNeg) - viewPos;
        float  lenSqNeg = dot(V_neg, V_neg);
        float  ooLenNeg = rsqrt(lenSqNeg + 0.0001f);
        float  angNeg = dot(V_neg, viewDir) * ooLenNeg;

        float falloffNeg = saturate(lenSqNeg * attenFactor);
        angNeg = lerp(angNeg, bestAng.y, falloffNeg);
        bestAng.y = (angNeg > bestAng.y) ? angNeg : lerp(angNeg, bestAng.y, thicknessBlend);
    }

    /// @source UE5 PostProcessAmbientOcclusion.usf:690-691
    bestAng.x = acosFast(clamp(bestAng.x, -1.0f, 1.0f));
    bestAng.y = acosFast(clamp(bestAng.y, -1.0f, 1.0f));

    return bestAng;
}

/// Compute the inner integral of the visible hemisphere slice.
/// @source UE5 PostProcessAmbientOcclusion.usf:771-795 — ComputeInnerIntegral
/// @constants 0.000001 epsilon (usf:778), PI_HALF (usf:573)
/// @deviations NONE
float ComputeInnerIntegral(
    float2 angles,
    float2 screenDir,
    float3 viewDir,
    float3 viewSpaceNormal)
{
    /// @source UE5 PostProcessAmbientOcclusion.usf:774-783
    float3 planeNormal = normalize(cross(float3(screenDir.xy, 0.0f), viewDir));
    float3 perp = cross(viewDir, planeNormal);
    float3 projNormal = viewSpaceNormal - planeNormal * dot(viewSpaceNormal, planeNormal);

    float lenProjNormal = length(projNormal) + 0.000001f;
    float recipMag = 1.0f / lenProjNormal;

    float cosAng = dot(projNormal, perp) * recipMag;
    float gamma = acosFast(cosAng) - PI_HALF;
    float cosGamma = dot(projNormal, viewDir) * recipMag;
    float sinGamma = cosAng * -2.0f;

    /// @source UE5 PostProcessAmbientOcclusion.usf:787-788
    angles.x = gamma + max(-angles.x - gamma, -PI_HALF);
    angles.y = gamma + min( angles.y - gamma,  PI_HALF);

    /// @source UE5 PostProcessAmbientOcclusion.usf:790-792
    float ao = lenProjNormal * 0.25f *
        ((angles.x * sinGamma + cosGamma - cos(2.0f * angles.x - gamma)) +
         (angles.y * sinGamma + cosGamma - cos(2.0f * angles.y - gamma)));

    return ao;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 PostProcessAmbientOcclusion.usf:932-948 — GTAOCombinedCS
[numthreads(8, 8, 1)]
void main(
    uint2 groupId        : SV_GroupID,
    uint2 dispatchId     : SV_DispatchThreadID,
    uint2 groupThreadId  : SV_GroupThreadID)
{
    if (dispatchId.x >= (uint)float(outputWidth) || dispatchId.y >= (uint)outputHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchId + float2(0.5f, 0.5f);
    float2 uv = pixelCenter * float2(invWidth, invHeight);

    // ── Early depth check ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:855-862
    float deviceZ = SampleDepth(uv);
    float sceneDepth = LinearizeDepth(deviceZ);

    // Skip very distant pixels (simplified fade — no fadeMul/fadeAdd in root constants)
    if (sceneDepth > 100000.0f) {
        aoOutput[dispatchId] = 1.0f;
        return;
    }

    // ── Reconstruct view-space position and normal ──
    float3 viewSpacePos = ScreenToViewPos(uv, sceneDepth);
    float3 viewSpaceNormal = GetWorldNormal(uv);
    float3 viewDir = normalize(-viewSpacePos);

    // ── Compute screen-space radius ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:870-877
    float fovScale = outputHeight * invTanHalfFov;
    float worldRadiusAdj = worldRadius * fovScale;
    float pixelRadius = max(min(worldRadiusAdj / viewSpacePos.z, GTAO_MAX_PIXEL_SCREEN_RADIUS), (float)GTAO_NUMTAPS);
    float stepRadius = pixelRadius / ((float)GTAO_NUMTAPS + 1.0f);
    /// @source UE5 PostProcessAmbientOcclusion.usf:877
    float attenFactor = 2.0f / (worldRadius * worldRadius);

    // ── Get per-pixel random direction and offset ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:880-882
    float3 randomAndOffset = GetRandomVector(dispatchId);
    float2 randomVec = randomAndOffset.xy;
    float  offset = randomAndOffset.z;

    // ── Accumulate AO over multiple directions ──
    float sum = 0.0f;

    float deltaAngle = PI / (float)GTAO_NUM_ANGLES;
    float sinDeltaAngle = sin(deltaAngle);
    float cosDeltaAngle = cos(deltaAngle);

    float2 screenDir = randomVec;

    for (uint angle = 0; angle < GTAO_NUM_ANGLES; angle++) {
        /// @source UE5 PostProcessAmbientOcclusion.usf:894-895
        float2 horizonAngles = SearchForLargestAngleDual(
            GTAO_NUMTAPS,
            uv,
            screenDir * float2(invWidth, invHeight),
            stepRadius,
            offset,
            viewSpacePos,
            viewDir,
            attenFactor);

        /// @source UE5 PostProcessAmbientOcclusion.usf:897
        sum += ComputeInnerIntegral(horizonAngles, screenDir, viewDir, viewSpaceNormal);

        // ── Rotate direction for next angle ──
        /// @source UE5 PostProcessAmbientOcclusion.usf:900-903
        float2 tempDir = screenDir;
        screenDir.x = tempDir.x * cosDeltaAngle + tempDir.y * (-sinDeltaAngle);
        screenDir.y = tempDir.x * sinDeltaAngle + tempDir.y * cosDeltaAngle;
        /// @source UE5 PostProcessAmbientOcclusion.usf:903
        offset = frac(offset + 0.617f);
    }

    // ── Normalize and scale ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:906-909
    float ao = sum / (float)GTAO_NUM_ANGLES;
    ao *= 2.0f / PI;

    // ── Distance fade ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:913-915
    // Distance fade (simplified — linear fade beyond 50000cm)
    float distFade = saturate((sceneDepth - 50000.0f) / 50000.0f);
    ao = lerp(ao, 1.0f, distFade);

    aoOutput[dispatchId] = saturate(ao);
}
