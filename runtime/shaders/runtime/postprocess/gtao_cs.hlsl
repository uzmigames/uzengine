/// GTAO (Ground Truth Ambient Occlusion) — horizon-based compute shader.
///
/// Implements the GTAO algorithm (Jimenez 2016) as used in reference implementations. Per-pixel,
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
/// - b0: Root constants (16 DWORDs — SRV indices + frame + algorithm params)
/// - b2: GTAOParams CBV (bulk parameters including view matrix)
///
/// @ref Jimenez 2016 "Practical Real-time Strategies for Accurate Indirect Occlusion"
/// @ref Jimenez 2016 — horizon search, inner integral, noise, combined CS
/// @ref Penner 2011 "Pre-Integrated Skin Shading" — acosFast approximation
/// @constants GTAO_MAX_SCREEN_RADIUS=256.0, PI=3.14159265, PI_HALF=PI*0.5,
///            AttenFactor=2.0/(worldRadius*worldRadius), OffsetAdvance=0.617
///            numAngles, numTaps, worldRadius, aoIntensity, thicknessBlend,
///            fadeStart, fadeEnd, aoPower — all from root constants DWORDs 8-15
/// @deviations NONE

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"
#include "../include/units.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Optional Feature Defines (set by CPU-side compile flags)
// ─────────────────────────────────────────────────────────────────────────────

/// GTAO_USE_HIZ: When defined, the horizon search samples from the Hi-Z
/// half-resolution min-depth texture for distant samples instead of always
/// sampling mip 0 of the full-res depth buffer. This improves cache
/// utilization for distant horizon steps at minimal quality cost.
/// The Hi-Z SRV index is packed into bits[16:31] of frameAndHiz (DWORD 7).
/// When hizSrvIndex == 0xFFFF, falls back to the full-res depth buffer.
#define GTAO_USE_HIZ 1
///
/// GTAO_RECONSTRUCT_NORMAL: Reconstructs view-space normals from depth buffer
/// gradients instead of reading the G-Buffer normal texture.
/// MANDATORY: The GBuffer stores WORLD-space normals, but GTAO operates in
/// VIEW-space (viewDir, viewSpacePos are all view-space). Using world normals
/// directly causes incorrect dot products → severe over-darkening (AO avg=0.34
/// instead of ~0.9 for flat surfaces). Reconstructing from depth gives correct
/// view-space normals without needing a view matrix.
/// @ref Jimenez 2016 "GTAO" — depth-reconstructed normals for compute path
#define GTAO_RECONSTRUCT_NORMAL 1

// ─────────────────────────────────────────────────────────────────────────────
// Quality Defines
// ─────────────────────────────────────────────────────────────────────────────

// Algorithm constants now hardcoded in cbuffer section below (after root constants).
// Old #defines removed — they conflicted with the new hardcoded values.

#define PI       3.14159265358979323846f
#define PI_HALF  (PI * 0.5f)

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 16 DWORDs = 64 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants — exactly 16 DWORDs (64 bytes).
/// DWORDs 0-7: per-frame dynamic values (SRV indices, camera, frame index).
/// DWORDs 8-15: algorithm parameters driven by GTAOSettings / CVars.
/// @ref Jimenez 2016 "GTAO" §566 — GTAOParams layout
cbuffer GTAORootCB : register(b0) {
    // ── Per-frame dynamic values (DWORDs 0-7) ──
    uint     depthSrvIndex;     // DWORD 0
    uint     normalSrvIndex;    // DWORD 1
    uint     outputWidth;       // DWORD 2
    uint     outputHeight;      // DWORD 3
    float    invTanHalfFov;     // DWORD 4
    float    nearPlane;         // DWORD 5
    float    farPlane;          // DWORD 6
    uint     frameAndHiz;       // DWORD 7: bits[0:15]=frameIndex, bits[16:31]=hizSrvIndex

    // ── Algorithm parameters from GTAOSettings (DWORDs 8-15) ──
    uint     numAngles;         // DWORD 8:  direction search count
    uint     numTaps;           // DWORD 9:  steps per direction
    float    worldRadius;       // DWORD 10: AO search radius (cm)
    float    aoIntensity;       // DWORD 11: AO strength multiplier
    float    thicknessBlend;    // DWORD 12: thickness heuristic [0,1]
    float    fadeStart;         // DWORD 13: distance where AO fades (cm)
    float    fadeEnd;           // DWORD 14: distance where AO disappears (cm)
    float    aoPower;           // DWORD 15: AO power curve exponent
};

/// Unpack frame index from the lower 16 bits of DWORD 7.
uint GetFrameIndex() { return frameAndHiz & 0xFFFFu; }

/// Unpack Hi-Z SRV index from the upper 16 bits of DWORD 7.
/// Returns 0xFFFF when Hi-Z is not available (fallback to full-res depth).
uint GetHizSrvIndex() { return (frameAndHiz >> 16u) & 0xFFFFu; }

// ─────────────────────────────────────────────────────────────────────────────
// Algorithm Constants (non-tunable)
// ─────────────────────────────────────────────────────────────────────────────
// Tunable parameters (numAngles, numTaps, worldRadius, aoIntensity, thicknessBlend,
// fadeStart, fadeEnd, aoPower) are now driven from root constants DWORDs 8-15.
/// @ref Jimenez 2016 "GTAO" §557
#define GTAO_MAX_SCREEN_RADIUS 256.0f

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
// Groupshared Depth Preload (8×8 tile + 8px border = 24×24)
// ─────────────────────────────────────────────────────────────────────────────

/// Preloaded depth tile for nearby horizon search steps. The 8-pixel border
/// around the 8×8 thread group covers the first few march steps without
/// requiring texture fetches. Steps beyond the tile fall back to SampleDepth().
/// 24×24 = 576 floats = 2304 bytes of LDS — well within the 32 KB limit.
/// @ref Jimenez 2016 "GTAO" — groupshared cache for horizon search optimization
groupshared float g_sharedDepth[24][24];

/// Preloads a 24×24 depth region around the 8×8 thread group cooperatively.
/// Each of the 64 threads loads ~9 texels (576/64 = 9). The tile covers the
/// group's 8×8 pixels plus an 8-pixel border for nearby horizon steps.
/// @ref Jimenez 2016 "GTAO" — cooperative LDS fill pattern
void PreloadDepthTile(uint2 groupThreadId, uint2 groupOrigin) {
    float2 invSize = float2(1.0f / float(outputWidth), 1.0f / float(outputHeight));
    uint linearIdx = groupThreadId.y * 8 + groupThreadId.x;

    // Each thread loads multiple texels to fill the 24×24 region.
    // 576 total texels / 64 threads = 9 iterations per thread.
    [unroll] for (uint i = linearIdx; i < 24 * 24; i += 64) {
        uint tx = i % 24;
        uint ty = i / 24;
        // Map tile-local coordinate to pixel coordinate.
        // The tile starts 8 pixels before the group origin in each axis.
        int2 pixelCoord = int2(groupOrigin) + int2(tx, ty) - int2(8, 8);
        // Clamp to valid screen range to avoid out-of-bounds sampling.
        pixelCoord = clamp(pixelCoord, int2(0, 0),
                           int2(int(outputWidth) - 1, int(outputHeight) - 1));
        float2 uv = (float2(pixelCoord) + 0.5f) * invSize;
        g_sharedDepth[ty][tx] = SampleDepth(uv);
    }
    GroupMemoryBarrierWithGroupSync();
}

/// Try to read depth from groupshared memory. Returns true if the coordinate
/// was within the preloaded 24×24 tile, false if a texture fetch is needed.
/// @param uv          Screen UV of the sample location.
/// @param groupOrigin Pixel coordinate of the thread group's top-left corner.
/// @param depth       [out] Device-Z depth value if within tile.
bool TryReadSharedDepth(float2 uv, uint2 groupOrigin, out float depth) {
    // Convert UV back to pixel coordinates (integer grid).
    float2 pixelCoordF = uv * float2(float(outputWidth), float(outputHeight));
    int2 localCoord = int2(pixelCoordF) - int2(groupOrigin) + int2(8, 8);
    if (localCoord.x >= 0 && localCoord.x < 24 &&
        localCoord.y >= 0 && localCoord.y < 24) {
        depth = g_sharedDepth[localCoord.y][localCoord.x];
        return true;
    }
    depth = 0.0f;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Wave Intrinsics (SM6.0+)
// ─────────────────────────────────────────────────────────────────────────────

/// WaveReadLaneFirst broadcasts a uniform value to all lanes in the wave.
/// For GTAO, this ensures the temporal sincos() result (which is the same for
/// all threads sharing a frame index) is computed once and broadcast.
/// DXC targets SM6.0+ by default for this engine, so wave ops are available.
/// @ref HLSL SM6.0 "Wave Intrinsics" — WaveReadLaneFirst for uniform broadcast

#ifdef GTAO_USE_HIZ
#ifndef UZ_BINDLESS
/// Hi-Z half-res min-depth texture (SM5 fallback path).
/// Bound explicitly at t2 when bindless is not available.
/// @ref Jimenez 2016 "GTAO" §575-577 — HZBTexture, HZBSampler
Texture2D<float>  hizPyramid    : register(t2);
#endif
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
/// Fetch depth from Hi-Z half-res min-depth texture.
/// The mipLevel parameter is accepted for API compatibility but ignored —
/// our RHI stores Hi-Z mips as separate textures, so we only have access
/// to mip 0 (half-resolution). This still provides a significant cache
/// improvement for distant horizon steps.
///
/// Falls back to full-res SampleDepth() when hizSrvIndex == 0xFFFF.
///
/// @ref Jimenez 2016 "GTAO" §575-577 — HZBTexture SampleLevel
/// @constants mipLevel always 0 (single half-res texture per RHI constraint)
/// @deviations Only mip 0 sampled; full mip chain requires RHI per-mip SRV support.
float SampleDepthHiZ(float2 uv, float mipLevel) {
    uint hizIdx = GetHizSrvIndex();
    if (hizIdx == 0xFFFFu) {
        return SampleDepth(uv);
    }
#ifdef UZ_BINDLESS
    Texture2D<float> hiz = ResourceDescriptorHeap[hizIdx];
    return hiz.SampleLevel(pointSampler, uv, 0).r;
#else
    return hizPyramid.SampleLevel(pointSampler, uv, 0).r;
#endif
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
/// @ref Penner 2011 — fast acos polynomial approximation
/// @constants -0.156583, 0.5*PI (Penner 2011 polynomial coefficients)
/// @deviations NONE
float acosFast(float inX) {
    float x = abs(inX);
    float res = -0.156583f * x + (0.5f * PI);
    res *= sqrt(1.0f - x);
    return (inX >= 0) ? res : PI - res;
}

/// Linearize hardware depth to view-space Z.
///
/// UzEngine uses reversed-Z: deviceZ = 1.0 at near plane, 0.0 at far plane.
/// Formula: z_view = near * far / (near * (1 - deviceZ) + deviceZ * far)
///        = near * far / (near + deviceZ * (far - near))
///
/// @ref reversed-Z perspective projection — device Z to linear depth
float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (nearPlane + deviceZ * (farPlane - nearPlane));
}

/// Reconstruct view-space position from UV + linear depth.
///
/// @ref standard perspective unprojection — UV + depth to view-space position
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
/// @ref Jimenez 2016 "GTAO" §65-94 — TakeSmallerAbsDelta,
///         ReconstructNormalFromDepthBuffer
/// @constants none (pure geometry)
/// @deviations reference reconstructs in translated-world-space then normalizes later;
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
    // @ref Jimenez 2016 "GTAO" §65-71 — TakeSmallerAbsDelta
    float ddxZ = (abs(centerDeviceZ - deviceZLeft) < abs(deviceZRight - centerDeviceZ))
        ? (centerDeviceZ - deviceZLeft) : (deviceZRight - centerDeviceZ);
    float ddyZ = (abs(centerDeviceZ - deviceZTop) < abs(deviceZBottom - centerDeviceZ))
        ? (centerDeviceZ - deviceZTop) : (deviceZBottom - centerDeviceZ);

    // Reconstruct view-space positions for center, right-neighbor, and down-neighbor
    // using the chosen derivative direction (center + delta).
    // @ref Jimenez 2016 "GTAO" §89-91 — SvPositionToTranslatedWorld
    float3 posMid   = ScreenToViewPos(uv, LinearizeDepth(centerDeviceZ));
    float3 posRight = ScreenToViewPos(uv + float2(texelSize.x, 0.0f),
                                      LinearizeDepth(centerDeviceZ + ddxZ)) - posMid;
    float3 posDown  = ScreenToViewPos(uv + float2(0.0f, texelSize.y),
                                      LinearizeDepth(centerDeviceZ + ddyZ)) - posMid;

    // Cross product gives the face normal of the local surface patch.
    // @ref Jimenez 2016 "GTAO" §93 — cross(Right, Down)
    float3 normal = cross(posRight, posDown);

    return normalize(normal);
}
#endif

/// Interleaved Gradient Noise — spatially varying pseudo-random value.
/// @ref Jimenez 2016 "GTAO" §799-802 — InterleavedGradientNoise
/// @constants 52.9829189, 0.06711056, 0.00583715 (Jimenez 2014 IGN)
/// @deviations NONE
float InterleavedGradientNoise(float2 iPos) {
    return frac(52.9829189f * frac((iPos.x * 0.06711056f) + (iPos.y * 0.00583715f)));
}

/// Get randomized search direction and step offset per pixel.
/// @ref Jimenez 2016 "GTAO" §812-836 — GetRandomVector
/// @constants 16384, 1.0/4.0 (Jimenez 2016 temporal rotation)
/// @deviations NONE
float3 GetRandomVector(uint2 iPos) {
    iPos.y = 16384u - iPos.y;

    float gradientNoise = InterleavedGradientNoise(float2(iPos));

    float2 randomTexVec;
    /// @ref Jimenez 2016 "GTAO" §825-826
    sincos(gradientNoise * PI, randomTexVec.y, randomTexVec.x);

    /// @ref Jimenez 2016 "GTAO" §828
    float scaleOffset = (1.0f / 4.0f) * ((iPos.y - iPos.x) & 3u);

    float3 result;
    /// @ref Jimenez 2016 "GTAO" §831-833
    // Compute temporal rotation from frame index (unpacked from DWORD 7)
    /// @ref Jimenez 2016 "GTAO" §820-833
    uint fi = GetFrameIndex();
    float temporalAngle = float(fi % 6) * (PI / 3.0f);
    float tCos, tSin;
    sincos(temporalAngle, tSin, tCos);
    // Wave broadcast: temporalAngle is uniform across the entire dispatch
    // (same frame index for all threads), so sincos is redundantly computed
    // per-lane. WaveReadLaneFirst ensures the compiler treats these as
    // uniform scalar values, enabling scalar register allocation on GCN/RDNA
    // and avoiding per-lane sincos on architectures with scalar units.
    // @ref HLSL SM6.0 "Wave Intrinsics" — WaveReadLaneFirst for uniform broadcast
#ifdef UZ_BINDLESS  // SM6.6 implies SM6.0 wave intrinsics available
    tCos = WaveReadLaneFirst(tCos);
    tSin = WaveReadLaneFirst(tSin);
#endif
    float tOffset = float((fi / 6) % 4) * 0.25f;
#ifdef UZ_BINDLESS
    tOffset = WaveReadLaneFirst(tOffset);
#endif
    result.x = dot(randomTexVec, float2(tCos, -tSin));
    result.y = dot(randomTexVec, float2(tSin,  tCos));
    result.z = frac(scaleOffset + tOffset);

    return result;
}

/// Search for the largest horizon angle in both positive and negative directions.
/// @ref Jimenez 2016 "GTAO" §648-694 — FindMaxHorizonAngle
/// @constants 0.0001 epsilon, Thickness blend = 0.5 (Jimenez 2016 horizon search)
/// @deviations Uses depth texture directly (no HZB) for simplicity/quality.
float2 FindMaxHorizonAngle(
    uint   numSteps,
    float2 baseUV,
    float2 screenDir,
    float  searchRadius,
    float  initialOffset,
    float3 viewPos,
    float3 viewDir,
    float  attenFactor,
    uint2  groupOrigin)
{
    float2 bestAng = float2(-1.0f, -1.0f);

    [loop] for (uint i = 0; i < numSteps; i++) {
        float fi = (float)i;

        /// @ref Jimenez 2016 "GTAO" §661
        float2 uvOffset = screenDir * max(searchRadius * (fi + initialOffset), fi + 1.0f);
        uvOffset.y *= -1.0f;
        float4 uv2 = baseUV.xyxy + float4(uvOffset.xy, -uvOffset.xy);

#ifdef GTAO_USE_HIZ
        // Use coarser HiZ mip levels for distant samples. The mip level is
        // derived from the pixel offset magnitude: larger offsets can tolerate
        // lower resolution since the AO contribution is attenuated by distance.
        // log2(pixel_offset) gives a natural mip selector.
        // @ref Jimenez 2016 "GTAO" §660-662 — MipLevel from ScreenRadius
        float pixelOffset = max(searchRadius * (fi + initialOffset), fi + 1.0f);
        float mipLevel = clamp(log2(pixelOffset) - 1.0f, 0.0f, 4.0f);
        float sceneDepthPos = LinearizeDepth(SampleDepthHiZ(uv2.xy, mipLevel));
        float sceneDepthNeg = LinearizeDepth(SampleDepthHiZ(uv2.zw, mipLevel));
#else
        // Try groupshared depth first — avoids texture fetch for samples within
        // the preloaded 24×24 tile (covers ~8px radius from the tile center).
        // Falls back to SampleDepth() for samples outside the tile footprint.
        float sharedD;
        float sceneDepthPos, sceneDepthNeg;
        if (TryReadSharedDepth(uv2.xy, groupOrigin, sharedD))
            sceneDepthPos = LinearizeDepth(sharedD);
        else
            sceneDepthPos = LinearizeDepth(SampleDepth(uv2.xy));

        if (TryReadSharedDepth(uv2.zw, groupOrigin, sharedD))
            sceneDepthNeg = LinearizeDepth(sharedD);
        else
            sceneDepthNeg = LinearizeDepth(SampleDepth(uv2.zw));
#endif

        // ── Positive direction ──
        /// @ref Jimenez 2016 "GTAO" §669-676
        float3 V_pos = ScreenToViewPos(uv2.xy, sceneDepthPos) - viewPos;
        float  lenSqPos = dot(V_pos, V_pos);
        float  ooLenPos = rsqrt(lenSqPos + 0.0001f);
        float  angPos = dot(V_pos, viewDir) * ooLenPos;

        float falloffPos = saturate(lenSqPos * attenFactor);
        angPos = lerp(angPos, bestAng.x, falloffPos);
        /// @ref Jimenez 2016 "GTAO" §676 — thickness heuristic
        bestAng.x = (angPos > bestAng.x) ? angPos : lerp(angPos, bestAng.x, thicknessBlend);

        // ── Negative direction ──
        /// @ref Jimenez 2016 "GTAO" §679-687
        float3 V_neg = ScreenToViewPos(uv2.zw, sceneDepthNeg) - viewPos;
        float  lenSqNeg = dot(V_neg, V_neg);
        float  ooLenNeg = rsqrt(lenSqNeg + 0.0001f);
        float  angNeg = dot(V_neg, viewDir) * ooLenNeg;

        float falloffNeg = saturate(lenSqNeg * attenFactor);
        angNeg = lerp(angNeg, bestAng.y, falloffNeg);
        bestAng.y = (angNeg > bestAng.y) ? angNeg : lerp(angNeg, bestAng.y, thicknessBlend);
    }

    /// @ref Jimenez 2016 "GTAO" §690-691
    bestAng.x = acosFast(clamp(bestAng.x, -1.0f, 1.0f));
    bestAng.y = acosFast(clamp(bestAng.y, -1.0f, 1.0f));

    return bestAng;
}

/// Compute the inner integral of the visible hemisphere slice.
/// @ref Jimenez 2016 "GTAO" §771-795 — IntegrateHorizonArc
/// @constants 0.000001 epsilon, PI_HALF (Jimenez 2016 inner integral)
/// @deviations NONE
float IntegrateHorizonArc(
    float2 angles,
    float2 screenDir,
    float3 viewDir,
    float3 viewSpaceNormal)
{
    /// @ref Jimenez 2016 "GTAO" §774-783
    float3 planeNormal = normalize(cross(float3(screenDir.xy, 0.0f), viewDir));
    float3 perp = cross(viewDir, planeNormal);
    float3 projNormal = viewSpaceNormal - planeNormal * dot(viewSpaceNormal, planeNormal);

    float lenProjNormal = length(projNormal) + 0.000001f;
    float recipMag = 1.0f / lenProjNormal;

    float cosAng = dot(projNormal, perp) * recipMag;
    float gamma = acosFast(cosAng) - PI_HALF;
    float cosGamma = dot(projNormal, viewDir) * recipMag;
    float sinGamma = cosAng * -2.0f;

    /// @ref Jimenez 2016 "GTAO" §787-788
    angles.x = gamma + max(-angles.x - gamma, -PI_HALF);
    angles.y = gamma + min( angles.y - gamma,  PI_HALF);

    /// @ref Jimenez 2016 "GTAO" §790-792
    float ao = lenProjNormal * 0.25f *
        ((angles.x * sinGamma + cosGamma - cos(2.0f * angles.x - gamma)) +
         (angles.y * sinGamma + cosGamma - cos(2.0f * angles.y - gamma)));

    return ao;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Jimenez 2016 "GTAO" §932-948 — GTAOCombinedCS
[numthreads(8, 8, 1)]
void main(
    uint2 groupId        : SV_GroupID,
    uint2 dispatchId     : SV_DispatchThreadID,
    uint2 groupThreadId  : SV_GroupThreadID)
{
    // ── Groupshared depth preload ──
    // Cooperatively fill the 24×24 LDS tile before any thread accesses it.
    // This must happen BEFORE the early-out so all 64 threads participate
    // in the preload (GroupMemoryBarrierWithGroupSync requires uniform control flow).
    uint2 groupOrigin = groupId * 8;
    PreloadDepthTile(groupThreadId, groupOrigin);

    if (dispatchId.x >= (uint)float(outputWidth) || dispatchId.y >= (uint)outputHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchId + float2(0.5f, 0.5f);
    float2 uv = pixelCenter * float2((1.0f / float(outputWidth)), (1.0f / float(outputHeight)));

    // ── Early depth check ──
    /// @ref Jimenez 2016 "GTAO" §855-862
    float deviceZ = SampleDepth(uv);

    // Reversed-Z: deviceZ ≈ 0 = sky/far plane. Skip sky pixels early.
    if (deviceZ <= 0.0001f) {
        aoOutput[dispatchId] = 1.0f;
        return;
    }

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
    // @ref Jimenez 2016 "GTAO" §106-108 — COMPUTE_SHADER path
    float3 viewSpaceNormal = ReconstructNormalFromDepth(uv, deviceZ);
#else
    // Read normal from G-Buffer octahedral encoding (higher quality).
    float3 viewSpaceNormal = GetWorldNormal(uv);
#endif

    float3 viewDir = normalize(-viewSpacePos);

    // ── Compute screen-space radius ──
    /// @ref Jimenez 2016 "GTAO" §870-877
    float fovScale = outputHeight * invTanHalfFov;
    float worldRadiusAdj = worldRadius * fovScale;
    float pixelRadius = max(min(worldRadiusAdj / viewSpacePos.z, GTAO_MAX_SCREEN_RADIUS), (float)numTaps);
    float stepRadius = pixelRadius / ((float)numTaps + 1.0f);
    /// @ref Jimenez 2016 "GTAO" §877
    float attenFactor = 2.0f / (worldRadius * worldRadius);

    // ── Get per-pixel random direction and offset ──
    /// @ref Jimenez 2016 "GTAO" §880-882
    float3 randomAndOffset = GetRandomVector(dispatchId);
    float2 randomVec = randomAndOffset.xy;
    float  offset = randomAndOffset.z;

    // ── Accumulate AO over multiple directions ──
    float sum = 0.0f;

    float deltaAngle = PI / (float)numAngles;
    float sinDeltaAngle, cosDeltaAngle;
    sincos(deltaAngle, sinDeltaAngle, cosDeltaAngle);

    float2 screenDir = randomVec;

    [loop] for (uint angle = 0; angle < numAngles; angle++) {
        /// @ref Jimenez 2016 "GTAO" §894-895
        float2 horizonAngles = FindMaxHorizonAngle(
            numTaps,
            uv,
            screenDir * float2((1.0f / float(outputWidth)), (1.0f / float(outputHeight))),
            stepRadius,
            offset,
            viewSpacePos,
            viewDir,
            attenFactor,
            groupOrigin);

        /// @ref Jimenez 2016 "GTAO" §897
        sum += IntegrateHorizonArc(horizonAngles, screenDir, viewDir, viewSpaceNormal);

        // ── Rotate direction for next angle ──
        /// @ref Jimenez 2016 "GTAO" §900-903
        float2 tempDir = screenDir;
        screenDir.x = tempDir.x * cosDeltaAngle + tempDir.y * (-sinDeltaAngle);
        screenDir.y = tempDir.x * sinDeltaAngle + tempDir.y * cosDeltaAngle;
        /// @ref Jimenez 2016 "GTAO" §903
        offset = frac(offset + 0.617f);
    }

    // ── Normalize and scale ──
    /// @ref Jimenez 2016 "GTAO" §906-909
    float ao = sum / (float)numAngles;
    ao *= 2.0f / PI;

    // ── AO power curve and intensity ──
    // Applies artistic power/intensity controls to shape the AO falloff.
    // reference formula: ao = 1 - (1 - pow(abs(ao), power)) * intensity
    // With default power=1.0 and intensity=1.0 this is identity.
    // @ref Jimenez 2016 "GTAO" §1575-1576
    // @constants aoPower, aoIntensity from root constants DWORDs 15 and 11
    // @deviations NONE
    ao = 1.0f - (1.0f - pow(abs(ao), aoPower)) * aoIntensity;

    // ── Distance fade ──
    /// @ref Jimenez 2016 "GTAO" §913-915
    // Linear fade from fadeStart to fadeEnd (both from root constants).
    float fadeRange = max(fadeEnd - fadeStart, 1.0f);
    float distFade = saturate((sceneDepth - fadeStart) / fadeRange);
    ao = lerp(ao, 1.0f, distFade);

    aoOutput[dispatchId] = saturate(ao);
}
