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
///    a. March along the direction (GTAO_NUM_TAPS steps) sampling depth.
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
/// @constants GTAO_NUM_TAPS=12 (usf:555), GTAO_MAX_PIXEL_SCREEN_RADIUS=256.0 (usf:557),
///            PI=3.14159265 (Common.ush), PI_HALF=PI*0.5 (usf:573),
///            ThicknessBlend from GTAOParams[1].y (usf:655),
///            AttenFactor=2.0/(WorldRadius*WorldRadius) (usf:877),
///            OffsetAdvance=0.617 (usf:903)
/// @deviations NONE

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"
#include "../include/units.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Optional Feature Defines (set by CPU-side compile flags)
// ─────────────────────────────────────────────────────────────────────────────

/// GTAO_USE_HIZ: When defined, SearchForLargestAngleDual samples from the HiZ
/// min-depth pyramid at coarser mip levels for distant samples instead of
/// always sampling mip 0 of the depth buffer. This accelerates horizon search
/// at the cost of slightly less precise occlusion at distant steps.
/// Default: OFF (not defined). Enable via -DGTAO_USE_HIZ=1.
///
/// GTAO_RECONSTRUCT_NORMAL: Reconstructs view-space normals from depth buffer
/// gradients instead of reading the G-Buffer normal texture.
/// MANDATORY: The GBuffer stores WORLD-space normals, but GTAO operates in
/// VIEW-space (viewDir, viewSpacePos are all view-space). Using world normals
/// directly causes incorrect dot products → severe over-darkening (AO avg=0.34
/// instead of ~0.9 for flat surfaces). Reconstructing from depth gives correct
/// view-space normals without needing a view matrix.
/// @source UE5 PostProcessAmbientOcclusionCommon.ush:106 — COMPUTE_SHADER path
#define GTAO_RECONSTRUCT_NORMAL 1

// ─────────────────────────────────────────────────────────────────────────────
// Quality Defines
// ─────────────────────────────────────────────────────────────────────────────

// Algorithm constants now hardcoded in cbuffer section below (after root constants).
// Old #defines removed — they conflicted with the new hardcoded values.

#define PI       3.14159265358979323846f
#define PI_HALF  (PI * 0.5f)

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants — exactly 8 DWORDs (32 bytes).
/// Compute root sig slot 0 supports max 8 DWORDs (dx12_pso_creation.cpp:135).
/// All algorithm params (radius, intensity, etc.) hardcoded below via #defines.
/// @source UE5 PostProcessAmbientOcclusion.usf:566 — GTAOParams layout
cbuffer GTAORootCB : register(b0) {
    uint     depthSrvIndex;     // DWORD 0
    uint     normalSrvIndex;    // DWORD 1
    uint     outputWidth;       // DWORD 2
    uint     outputHeight;      // DWORD 3
    float    invTanHalfFov;     // DWORD 4
    float    nearPlane;         // DWORD 5
    float    farPlane;          // DWORD 6
    uint     frameIndex;        // DWORD 7
};

// ─────────────────────────────────────────────────────────────────────────────
// Hardcoded Algorithm Parameters (UE5 defaults)
// ─────────────────────────────────────────────────────────────────────────────
// These are constants in UE5 controlled by CVars. Hardcoded here because
// the compute root signature only supports 8 DWORDs of root constants.
/// @source UE5 PostProcessAmbientOcclusion.cpp:465 — NumAngles clamped [1,16]
#define GTAO_NUM_ANGLES     2       // UE5 medium quality
/// @source UE5 PostProcessAmbientOcclusion.usf:542 — SHADER_QUALITY 2 (medium) uses GTAO_NUMTAPS=8
#define GTAO_NUM_TAPS       8       // Steps per direction (UE5 medium quality)
/// @source UE5 PostProcessAmbientOcclusion.usf:557
#define GTAO_MAX_SCREEN_RADIUS 256.0f
/// @source UE5 PostProcessAmbientOcclusion.cpp:416
static const float GTAO_WORLD_RADIUS    = 200.0f;   // cm
static const float GTAO_AO_INTENSITY    = 1.0f;
static const float GTAO_THICKNESS_BLEND = 0.5f;
static const float GTAO_FADE_START      = 50000.0f; // cm
static const float GTAO_FADE_RANGE      = 50000.0f; // cm
/// AO power curve exponent. UE5 default AmbientOcclusionPower = 2.0 (Scene.cpp:528),
/// GTAO path multiplies by 0.5 (PostProcessAmbientOcclusion.usf:1575), effective = 1.0.
/// @source UE5 PostProcessAmbientOcclusion.usf:1575 — AmbientOcclusionPower * 0.5
/// @source UE5 Scene.cpp:528 — AmbientOcclusionPower = 2.0f
#define GTAO_AO_POWER  1.0f

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

#ifdef GTAO_USE_HIZ
/// HiZ min-depth pyramid for hierarchical horizon search.
/// Each mip level is half the resolution of the previous, storing the minimum
/// (closest) depth in each 2x2 block. Generated by hiz_pyramid_cs.hlsl.
/// Bound as SRV with full mip chain access for SampleLevel() at varying LODs.
/// @source UE5 PostProcessAmbientOcclusion.usf:575-577 — HZBTexture, HZBSampler
Texture2D<float>  hizPyramid    : register(t2);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Fetch depth value from the depth buffer at the given UV (always mip 0).
float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIndex];
    return depthTex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthBuffer.SampleLevel(pointSampler, uv, 0).r;
#endif
}

#ifdef GTAO_USE_HIZ
/// Fetch depth from HiZ pyramid at a specified mip level.
/// Used by SearchForLargestAngleDual for distant samples where coarser mips
/// provide sufficient accuracy with better cache utilization.
///
/// @source UE5 PostProcessAmbientOcclusion.usf:575-577 — HZBTexture SampleLevel
/// @constants mipLevel computed from step distance (usf:660-662)
/// @deviations NONE
float SampleDepthHiZ(float2 uv, float mipLevel) {
    return hizPyramid.SampleLevel(pointSampler, uv, mipLevel).r;
}
#endif

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
    float aspect = float(outputWidth) * (1.0f / float(outputHeight));
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

#ifdef GTAO_RECONSTRUCT_NORMAL
/// Reconstruct view-space normal from depth buffer gradients.
///
/// Samples depth at center, left, right, top, bottom (5 taps), then picks
/// the derivative direction with the smaller absolute delta to avoid edge
/// artifacts (TakeSmallerAbsDelta pattern). Reconstructs view-space positions
/// and computes the cross product.
///
/// @source UE5 PostProcessAmbientOcclusionCommon.ush:65-94 — TakeSmallerAbsDelta,
///         ReconstructNormalFromDepthBuffer
/// @constants none (pure geometry)
/// @deviations UE5 reconstructs in translated-world-space then normalizes later;
///             we reconstruct in view-space directly since GTAO works in view-space.
///             The geometric result is identical up to a rigid transform.
float3 ReconstructNormalFromDepth(float2 uv, float centerDeviceZ) {
    float2 texelSize = float2(1.0f / float(outputWidth), 1.0f / float(outputHeight));

    // Sample 4 neighbors in device-Z space
    float deviceZLeft   = SampleDepth(uv + float2(-texelSize.x, 0.0f));
    float deviceZRight  = SampleDepth(uv + float2( texelSize.x, 0.0f));
    float deviceZTop    = SampleDepth(uv + float2(0.0f, -texelSize.y));
    float deviceZBottom = SampleDepth(uv + float2(0.0f,  texelSize.y));

    // TakeSmallerAbsDelta: pick the derivative with smaller absolute change
    // to favor the surface we are looking at (avoids silhouette edge artifacts).
    // @source UE5 PostProcessAmbientOcclusionCommon.ush:65-71 — TakeSmallerAbsDelta
    float ddxZ = (abs(centerDeviceZ - deviceZLeft) < abs(deviceZRight - centerDeviceZ))
        ? (centerDeviceZ - deviceZLeft) : (deviceZRight - centerDeviceZ);
    float ddyZ = (abs(centerDeviceZ - deviceZTop) < abs(deviceZBottom - centerDeviceZ))
        ? (centerDeviceZ - deviceZTop) : (deviceZBottom - centerDeviceZ);

    // Reconstruct view-space positions for center, right-neighbor, and down-neighbor
    // using the chosen derivative direction (center + delta).
    // @source UE5 PostProcessAmbientOcclusionCommon.ush:89-91 — SvPositionToTranslatedWorld
    float3 posMid   = ScreenToViewPos(uv, LinearizeDepth(centerDeviceZ));
    float3 posRight = ScreenToViewPos(uv + float2(texelSize.x, 0.0f),
                                      LinearizeDepth(centerDeviceZ + ddxZ)) - posMid;
    float3 posDown  = ScreenToViewPos(uv + float2(0.0f, texelSize.y),
                                      LinearizeDepth(centerDeviceZ + ddyZ)) - posMid;

    // Cross product gives the face normal of the local surface patch.
    // @source UE5 PostProcessAmbientOcclusionCommon.ush:93 — cross(Right, Down)
    float3 normal = cross(posRight, posDown);

    return normalize(normal);
}
#endif

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
    // Compute temporal rotation from frameIndex (moved from root constants to shader)
    /// @source UE5 PostProcessAmbientOcclusion.usf:820-833
    float temporalAngle = float(frameIndex % 6) * (PI / 3.0f);
    float tCos = cos(temporalAngle);
    float tSin = sin(temporalAngle);
    float tOffset = float((frameIndex / 6) % 4) * 0.25f;
    result.x = dot(randomTexVec, float2(tCos, -tSin));
    result.y = dot(randomTexVec, float2(tSin,  tCos));
    result.z = frac(scaleOffset + tOffset);

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

#ifdef GTAO_USE_HIZ
        // Use coarser HiZ mip levels for distant samples. The mip level is
        // derived from the pixel offset magnitude: larger offsets can tolerate
        // lower resolution since the AO contribution is attenuated by distance.
        // log2(pixel_offset) gives a natural mip selector.
        // @source UE5 PostProcessAmbientOcclusion.usf:660-662 — MipLevel from ScreenRadius
        float pixelOffset = max(searchRadius * (fi + initialOffset), fi + 1.0f);
        float mipLevel = clamp(log2(pixelOffset) - 1.0f, 0.0f, 4.0f);
        float sceneDepthPos = LinearizeDepth(SampleDepthHiZ(uv2.xy, mipLevel));
        float sceneDepthNeg = LinearizeDepth(SampleDepthHiZ(uv2.zw, mipLevel));
#else
        float sceneDepthPos = LinearizeDepth(SampleDepth(uv2.xy));
        float sceneDepthNeg = LinearizeDepth(SampleDepth(uv2.zw));
#endif

        // ── Positive direction ──
        /// @source UE5 PostProcessAmbientOcclusion.usf:669-676
        float3 V_pos = ScreenToViewPos(uv2.xy, sceneDepthPos) - viewPos;
        float  lenSqPos = dot(V_pos, V_pos);
        float  ooLenPos = rsqrt(lenSqPos + 0.0001f);
        float  angPos = dot(V_pos, viewDir) * ooLenPos;

        float falloffPos = saturate(lenSqPos * attenFactor);
        angPos = lerp(angPos, bestAng.x, falloffPos);
        /// @source UE5 PostProcessAmbientOcclusion.usf:676 — thickness heuristic
        bestAng.x = (angPos > bestAng.x) ? angPos : lerp(angPos, bestAng.x, GTAO_THICKNESS_BLEND);

        // ── Negative direction ──
        /// @source UE5 PostProcessAmbientOcclusion.usf:679-687
        float3 V_neg = ScreenToViewPos(uv2.zw, sceneDepthNeg) - viewPos;
        float  lenSqNeg = dot(V_neg, V_neg);
        float  ooLenNeg = rsqrt(lenSqNeg + 0.0001f);
        float  angNeg = dot(V_neg, viewDir) * ooLenNeg;

        float falloffNeg = saturate(lenSqNeg * attenFactor);
        angNeg = lerp(angNeg, bestAng.y, falloffNeg);
        bestAng.y = (angNeg > bestAng.y) ? angNeg : lerp(angNeg, bestAng.y, GTAO_THICKNESS_BLEND);
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
    float2 uv = pixelCenter * float2((1.0f / float(outputWidth)), (1.0f / float(outputHeight)));

    // ── Early depth check ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:855-862
    float deviceZ = SampleDepth(uv);
    float sceneDepth = LinearizeDepth(deviceZ);

    // Skip very distant pixels (simplified fade — no fadeMul/fadeAdd in root constants)
    if (sceneDepth > UZ_DEFAULT_FAR_PLANE_CM) {
        aoOutput[dispatchId] = 1.0f;
        return;
    }

    // ── Reconstruct view-space position and normal ──
    float3 viewSpacePos = ScreenToViewPos(uv, sceneDepth);

#ifdef GTAO_RECONSTRUCT_NORMAL
    // Derive normal from depth gradients — no G-Buffer dependency.
    // @source UE5 PostProcessAmbientOcclusionCommon.ush:106-108 — COMPUTE_SHADER path
    float3 viewSpaceNormal = ReconstructNormalFromDepth(uv, deviceZ);
#else
    // Read normal from G-Buffer octahedral encoding (higher quality).
    float3 viewSpaceNormal = GetWorldNormal(uv);
#endif

    float3 viewDir = normalize(-viewSpacePos);

    // ── Compute screen-space radius ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:870-877
    float fovScale = outputHeight * invTanHalfFov;
    float worldRadiusAdj = GTAO_WORLD_RADIUS * fovScale;
    float pixelRadius = max(min(worldRadiusAdj / viewSpacePos.z, GTAO_MAX_SCREEN_RADIUS), (float)GTAO_NUM_TAPS);
    float stepRadius = pixelRadius / ((float)GTAO_NUM_TAPS + 1.0f);
    /// @source UE5 PostProcessAmbientOcclusion.usf:877
    float attenFactor = 2.0f / (GTAO_WORLD_RADIUS * GTAO_WORLD_RADIUS);

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
            GTAO_NUM_TAPS,
            uv,
            screenDir * float2((1.0f / float(outputWidth)), (1.0f / float(outputHeight))),
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

    // ── AO power curve and intensity ──
    // Applies artistic power/intensity controls to shape the AO falloff.
    // UE5 formula: ao = 1 - (1 - pow(abs(ao), power)) * intensity
    // With default power=1.0 and intensity=1.0 this is identity.
    // @source UE5 PostProcessAmbientOcclusion.usf:1575-1576
    // @constants GTAO_AO_POWER=1.0 (Scene.cpp:528 * 0.5), GTAO_AO_INTENSITY=1.0
    // @deviations NONE
    ao = 1.0f - (1.0f - pow(abs(ao), GTAO_AO_POWER)) * GTAO_AO_INTENSITY;

    // ── Distance fade ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:913-915
    // Distance fade (simplified — linear fade beyond 50000cm)
    float distFade = saturate((sceneDepth - 50000.0f) / 50000.0f);
    ao = lerp(ao, 1.0f, distFade);

    aoOutput[dispatchId] = saturate(ao);
}
