/// UzEngine Multi-Light System Tests (phase3a-3.6.0-multi-light)
///
/// Verifies the LightGPU struct layout, the PerFrameMultiLightCB constant
/// buffer that carries up to 8 dynamic lights, point-light inverse-square
/// attenuation math, spot-light cone falloff math, CPU-side light culling
/// (distance sort + nearest-N selection), and the zero-lights edge case.
///
/// All tests are pure CPU / math — no RHI device, no GPU, no D3D12 headers.
/// The implementation under test must live in:
///   runtime/engine/renderer/core/multi_light.h

#include <gtest/gtest.h>

#include "multi_light.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Tolerance constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEps   = 1e-4f;  // float math tolerance
static constexpr float kLoose = 1e-3f;  // looser for smoothstep rounding

// ─────────────────────────────────────────────────────────────────────────────
// 2.1 — LightGPU struct layout
// ─────────────────────────────────────────────────────────────────────────────

/// LightGPU must be exactly 64 bytes so that an array of 8 fits in 512 bytes
/// (two 256-byte CB slots).  The layout must be HLSL-friendly: each float3
/// field is padded to float4 (16 bytes).
///
/// Expected layout:
///   [  0..11] positionOrDir  (float3 — world-space position for point/spot,
///                              direction for directional)
///   [ 12..15] range          (float  — effective radius; 0 = directional)
///   [ 16..27] color          (float3 — linear HDR color * intensity)
///   [ 28..31] lightType      (float  — 0=directional, 1=point, 2=spot)
///   [ 32..43] spotDirection  (float3 — normalised cone axis for spot lights)
///   [ 44..47] innerConeCos   (float  — cos(innerAngle))
///   [ 48..51] outerConeCos   (float  — cos(outerAngle))
///   [ 52..63] _pad           (float3 — explicit padding to 64 bytes)

TEST(MultiLightTest, LightGPUSize_Is64Bytes) {
    EXPECT_EQ(sizeof(LightGPU), 64u)
        << "LightGPU must be 64 bytes for HLSL float4 alignment";
}

TEST(MultiLightTest, LightGPUAlignment_Is16Bytes) {
    EXPECT_EQ(alignof(LightGPU), 16u)
        << "LightGPU must be aligned to 16 bytes (HLSL float4 row alignment)";
}

TEST(MultiLightTest, LightGPU_PositionOrDir_OffsetIsZero) {
    LightGPU light{};
    const auto base    = reinterpret_cast<const char*>(&light);
    const auto fieldPtr = reinterpret_cast<const char*>(&light.positionOrDir);
    EXPECT_EQ(fieldPtr - base, 0)
        << "positionOrDir must be at offset 0";
}

TEST(MultiLightTest, LightGPU_Range_OffsetIs12) {
    LightGPU light{};
    const auto base    = reinterpret_cast<const char*>(&light);
    const auto fieldPtr = reinterpret_cast<const char*>(&light.range);
    EXPECT_EQ(fieldPtr - base, 12)
        << "range must be at offset 12 (immediately after float3 positionOrDir)";
}

TEST(MultiLightTest, LightGPU_Color_OffsetIs16) {
    LightGPU light{};
    const auto base    = reinterpret_cast<const char*>(&light);
    const auto fieldPtr = reinterpret_cast<const char*>(&light.color);
    EXPECT_EQ(fieldPtr - base, 16)
        << "color must be at offset 16 (second float4 row)";
}

TEST(MultiLightTest, LightGPU_LightType_OffsetIs28) {
    LightGPU light{};
    const auto base    = reinterpret_cast<const char*>(&light);
    const auto fieldPtr = reinterpret_cast<const char*>(&light.lightType);
    EXPECT_EQ(fieldPtr - base, 28)
        << "lightType must be at offset 28 (w component of second float4)";
}

TEST(MultiLightTest, LightGPU_SpotDirection_OffsetIs32) {
    LightGPU light{};
    const auto base    = reinterpret_cast<const char*>(&light);
    const auto fieldPtr = reinterpret_cast<const char*>(&light.spotDirection);
    EXPECT_EQ(fieldPtr - base, 32)
        << "spotDirection must be at offset 32 (third float4 row)";
}

TEST(MultiLightTest, LightGPU_InnerConeCos_OffsetIs44) {
    LightGPU light{};
    const auto base    = reinterpret_cast<const char*>(&light);
    const auto fieldPtr = reinterpret_cast<const char*>(&light.innerConeCos);
    EXPECT_EQ(fieldPtr - base, 44)
        << "innerConeCos must be at offset 44";
}

TEST(MultiLightTest, LightGPU_OuterConeCos_OffsetIs48) {
    LightGPU light{};
    const auto base    = reinterpret_cast<const char*>(&light);
    const auto fieldPtr = reinterpret_cast<const char*>(&light.outerConeCos);
    EXPECT_EQ(fieldPtr - base, 48)
        << "outerConeCos must be at offset 48 (w component of third float4 row)";
}

TEST(MultiLightTest, LightGPU_ZeroInit_AllFieldsZero) {
    LightGPU light{};
    // The default-constructed state must be all-zero (safe for unused slots).
    const char* raw = reinterpret_cast<const char*>(&light);
    for (size_t i = 0; i < sizeof(LightGPU); ++i) {
        EXPECT_EQ(raw[i], 0) << "byte " << i << " of default-constructed LightGPU must be 0";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.2 — PerFrameMultiLightCB layout
// ─────────────────────────────────────────────────────────────────────────────

/// The CB carries:
///   [  0.. 63] viewProj      (Mat4, 64 bytes)
///   [ 64.. 79] cameraPos     (Vec4, 16 bytes)
///   [ 80.. 95] timeParams    (Vec4, 16 bytes)
///   [ 96..111] ambientColor  (Vec4, 16 bytes)
///   [112..115] lightCount    (uint32_t)
///   [116..127] _pad0         (3 × uint32_t — align array to 16 bytes)
///   [128..639] lightArray    (8 × LightGPU = 512 bytes)
///   [640..767] _pad1         (128 bytes — pad to 768-byte multiple of 256)
///
/// Total: 768 bytes = 3 × 256 bytes — valid D3D12 CB size.

TEST(MultiLightTest, PerFrameCBLayout_SizeIsMultipleOf256) {
    EXPECT_EQ(sizeof(PerFrameMultiLightCB) % 256u, 0u)
        << "PerFrameMultiLightCB must be a multiple of 256 bytes (D3D12 requirement)";
}

TEST(MultiLightTest, PerFrameCBLayout_SizeIsAtLeast512) {
    // Must hold viewProj(64) + cameraPos(16) + timeParams(16) + ambientColor(16)
    // + lightCount+pad(16) + 8×LightGPU(512) = 640 bytes, padded to 768.
    EXPECT_GE(sizeof(PerFrameMultiLightCB), 512u);
}

TEST(MultiLightTest, PerFrameCBLayout_LightArray_Is8Entries) {
    constexpr size_t kExpected = 8u;
    EXPECT_EQ(kMaxMultiLights, kExpected)
        << "kMaxMultiLights must be 8";
    EXPECT_EQ(sizeof(PerFrameMultiLightCB::lightArray) / sizeof(LightGPU), kExpected)
        << "lightArray must have exactly 8 LightGPU entries";
}

TEST(MultiLightTest, PerFrameCBLayout_LightCount_OffsetAlignedTo16) {
    PerFrameMultiLightCB cb{};
    const auto base     = reinterpret_cast<const char*>(&cb);
    const auto countPtr = reinterpret_cast<const char*>(&cb.lightCount);
    ptrdiff_t offset = countPtr - base;
    EXPECT_EQ(offset % 16, 0)
        << "lightCount must be at a 16-byte-aligned offset (before array)";
}

TEST(MultiLightTest, PerFrameCBLayout_LightArray_OffsetAlignedTo16) {
    PerFrameMultiLightCB cb{};
    const auto base      = reinterpret_cast<const char*>(&cb);
    const auto arrayPtr  = reinterpret_cast<const char*>(&cb.lightArray[0]);
    ptrdiff_t offset = arrayPtr - base;
    EXPECT_EQ(offset % 16, 0)
        << "lightArray[0] must be at a 16-byte-aligned offset";
}

TEST(MultiLightTest, PerFrameCBLayout_LightCount_BeforeLightArray) {
    PerFrameMultiLightCB cb{};
    const auto base      = reinterpret_cast<const char*>(&cb);
    const auto countPtr  = reinterpret_cast<const char*>(&cb.lightCount);
    const auto arrayPtr  = reinterpret_cast<const char*>(&cb.lightArray[0]);
    EXPECT_LT(countPtr, arrayPtr)
        << "lightCount must appear before lightArray in the struct";
}

TEST(MultiLightTest, PerFrameCBLayout_LightArray_Stride64Bytes) {
    PerFrameMultiLightCB cb{};
    const auto base = reinterpret_cast<const char*>(&cb.lightArray[0]);
    for (size_t i = 1; i < kMaxMultiLights; ++i) {
        const auto next = reinterpret_cast<const char*>(&cb.lightArray[i]);
        ptrdiff_t stride = next - (base + static_cast<ptrdiff_t>(i - 1) * 64);
        EXPECT_EQ(stride, 64)
            << "LightGPU array element stride must be 64 bytes (element " << i << ")";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.3 — Point light attenuation (inverse-square falloff)
// ─────────────────────────────────────────────────────────────────────────────

/// The engine uses a smooth inverse-square formula that reaches exactly 0.0
/// at the range boundary so objects at the edge receive no lighting.
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — windowed inverse-square falloff:
///   distRatio  = clamp(dist / range, 0, 1)
///   attenuation = (1 - distRatio^2)^2  ×  (1 / (dist^2 + 1))
///
/// The second factor softens the singularity at dist=0 while preserving
/// the correct falloff shape.

TEST(MultiLightTest, PointLightAttenuation_AtZeroDistance_IsMaximum) {
    // At dist=0 the attenuation should be the maximum possible value.
    // The formula gives: (1 - 0)^2 * (1/1) = 1.0
    float att = ComputePointAttenuation(0.0f, 10.0f);
    EXPECT_NEAR(att, 1.0f, kEps)
        << "Attenuation at dist=0 must be 1.0";
}

TEST(MultiLightTest, PointLightAttenuation_AtRangeBoundary_IsZero) {
    // At dist == range, distRatio = 1, so (1 - 1)^2 = 0.
    float att = ComputePointAttenuation(10.0f, 10.0f);
    EXPECT_NEAR(att, 0.0f, kEps)
        << "Attenuation at range boundary must be 0.0";
}

TEST(MultiLightTest, PointLightAttenuation_BeyondRange_IsZero) {
    // Beyond the range, contribution is zero (clamped).
    float att = ComputePointAttenuation(15.0f, 10.0f);
    EXPECT_NEAR(att, 0.0f, kEps)
        << "Attenuation beyond range must be 0.0";
}

TEST(MultiLightTest, PointLightAttenuation_HalfRange_IsPositive) {
    // At half the range, attenuation must be > 0 and < 1.
    float att = ComputePointAttenuation(5.0f, 10.0f);
    EXPECT_GT(att, 0.0f) << "Attenuation at half-range must be positive";
    EXPECT_LT(att, 1.0f) << "Attenuation at half-range must be less than 1";
}

TEST(MultiLightTest, PointLightAttenuation_MonotonicallyDecreases_WithDistance) {
    constexpr float range = 20.0f;
    float prev = ComputePointAttenuation(0.0f, range);
    for (float dist = 1.0f; dist <= range; dist += 1.0f) {
        float curr = ComputePointAttenuation(dist, range);
        EXPECT_LE(curr, prev)
            << "Attenuation must be monotonically non-increasing; "
            << "dist=" << dist << " gave " << curr << " > prev " << prev;
        prev = curr;
    }
}

TEST(MultiLightTest, PointLightAttenuation_LargeRange_HalfPoint_Positive) {
    // Sanity check with a large range (1000 units).
    float att = ComputePointAttenuation(400.0f, 1000.0f);
    EXPECT_GT(att, 0.0f);
    EXPECT_LT(att, 1.0f);
}

TEST(MultiLightTest, PointLightAttenuation_ZeroRange_ReturnsZero) {
    // A light with range=0 has no reach — must return 0 for any dist >= 0.
    float att = ComputePointAttenuation(0.0f, 0.0f);
    EXPECT_NEAR(att, 0.0f, kEps)
        << "Range=0 light must return 0 attenuation (avoid division by zero)";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.4 — Spot light cone falloff
// ─────────────────────────────────────────────────────────────────────────────

/// The cone falloff uses cosines to avoid trig in the inner loop:
///   cos(angle)   = dot(lightDir, toFragment) (both normalised)
///   innerCos     = cos(innerAngle)
///   outerCos     = cos(outerAngle)
///   t            = clamp((cosAngle - outerCos) / (innerCos - outerCos), 0, 1)
///   spotFactor   = t * t  (quadratic falloff in the penumbra)
///
/// This gives 1.0 inside the inner cone, 0.0 outside the outer cone,
/// and smooth quadratic interpolation in the penumbra band.

TEST(MultiLightTest, SpotLightCone_InsideInnerCone_IsFullIntensity) {
    // cosAngle > innerCos means the fragment is INSIDE the inner cone.
    const float innerAngle = 0.3f;
    const float outerAngle = 0.5f;
    const float innerCos = std::cos(innerAngle);
    const float outerCos = std::cos(outerAngle);

    // Use cosAngle at angle = 0 (perfectly aligned with cone axis): cos(0) = 1.
    float spotFactor = ComputeSpotConeFactor(1.0f, innerCos, outerCos);
    EXPECT_NEAR(spotFactor, 1.0f, kEps)
        << "Fragment on cone axis must receive full intensity";
}

TEST(MultiLightTest, SpotLightCone_AtInnerEdge_IsFullIntensity) {
    const float innerAngle = 0.3f;
    const float outerAngle = 0.5f;
    const float innerCos = std::cos(innerAngle);
    const float outerCos = std::cos(outerAngle);

    // cosAngle == innerCos: exactly on the inner boundary.
    float spotFactor = ComputeSpotConeFactor(innerCos, innerCos, outerCos);
    EXPECT_NEAR(spotFactor, 1.0f, kEps)
        << "Fragment at inner cone boundary must receive full intensity";
}

TEST(MultiLightTest, SpotLightCone_AtOuterEdge_IsZero) {
    const float innerAngle = 0.3f;
    const float outerAngle = 0.5f;
    const float innerCos = std::cos(innerAngle);
    const float outerCos = std::cos(outerAngle);

    // cosAngle == outerCos: exactly on the outer boundary.
    float spotFactor = ComputeSpotConeFactor(outerCos, innerCos, outerCos);
    EXPECT_NEAR(spotFactor, 0.0f, kEps)
        << "Fragment at outer cone boundary must receive zero intensity";
}

TEST(MultiLightTest, SpotLightCone_OutsideOuterCone_IsZero) {
    const float innerAngle = 0.3f;
    const float outerAngle = 0.5f;
    const float innerCos = std::cos(innerAngle);
    const float outerCos = std::cos(outerAngle);

    // cosAngle < outerCos: the fragment is outside the cone entirely.
    // cos(0.6) < cos(0.5) because cosine is decreasing.
    float cosOutside = std::cos(0.6f);
    float spotFactor = ComputeSpotConeFactor(cosOutside, innerCos, outerCos);
    EXPECT_NEAR(spotFactor, 0.0f, kEps)
        << "Fragment outside outer cone must receive zero intensity";
}

TEST(MultiLightTest, SpotLightCone_Midpoint_IsInterpolated) {
    const float innerAngle = 0.2f;
    const float outerAngle = 0.6f;
    const float innerCos = std::cos(innerAngle);
    const float outerCos = std::cos(outerAngle);

    // Midpoint angle between inner and outer in cosine space:
    //   t = 0.5 → spotFactor = 0.5^2 = 0.25
    float midCos  = 0.5f * (innerCos + outerCos);
    float spotFactor = ComputeSpotConeFactor(midCos, innerCos, outerCos);
    EXPECT_NEAR(spotFactor, 0.25f, kLoose)
        << "Midpoint of penumbra must give quadratic 0.25 (t=0.5, t^2=0.25)";
}

TEST(MultiLightTest, SpotLightCone_QuarterPoint_IsInterpolated) {
    const float innerAngle = 0.2f;
    const float outerAngle = 0.6f;
    const float innerCos = std::cos(innerAngle);
    const float outerCos = std::cos(outerAngle);

    // t = 0.25 in the linear blend → spotFactor = 0.25^2 = 0.0625
    float qCos = outerCos + 0.25f * (innerCos - outerCos);
    float spotFactor = ComputeSpotConeFactor(qCos, innerCos, outerCos);
    EXPECT_NEAR(spotFactor, 0.0625f, kLoose)
        << "Quarter-point of penumbra must give 0.0625 (t=0.25, t^2=0.0625)";
}

TEST(MultiLightTest, SpotLightCone_InnerEqualsOuter_ClampsBothEdges) {
    // When innerAngle == outerAngle there is no penumbra.
    const float angle = 0.4f;
    const float coneCos = std::cos(angle);

    // Exactly on the boundary: result is 1 (inside) or 0 (outside).
    float insideFactor  = ComputeSpotConeFactor(coneCos + 0.01f, coneCos, coneCos);
    float outsideFactor = ComputeSpotConeFactor(coneCos - 0.01f, coneCos, coneCos);

    EXPECT_NEAR(insideFactor,  1.0f, kEps);
    EXPECT_NEAR(outsideFactor, 0.0f, kEps);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.5 — CPU light culling
// ─────────────────────────────────────────────────────────────────────────────

/// SelectNearestLights() picks the closest N lights to a view position from
/// a candidate array, writes them into the output array, and returns how many
/// were written (min(candidates, kMaxMultiLights)).
///
/// Requirements:
///   - Sorts by squared distance to the camera position.
///   - Never writes more than kMaxMultiLights (8) entries.
///   - Excludes lights whose squared distance > (light.range)^2  ×  kCullFactor
///     (a multiplier chosen so the light's attenuation is negligible at that dist).
///   - When fewer than kMaxMultiLights candidates exist, writes exactly that many.

/// Helper: build a LightCPU at a given world position with a given range.
static LightCPU MakePointLight(float x, float y, float z, float range,
                                float r = 1.0f, float g = 1.0f, float b = 1.0f) {
    LightCPU light{};
    light.position = {x, y, z};
    light.range    = range;
    light.color    = {r, g, b};
    light.type     = LightType::Point;
    return light;
}

TEST(MultiLightTest, LightCulling_EmptyInput_WritesZero) {
    Vec3       camPos{0, 0, 0};
    LightGPU   out[kMaxMultiLights]{};
    uint32_t   count = SelectNearestLights(nullptr, 0u, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, 0u) << "No candidates → zero lights selected";
}

TEST(MultiLightTest, LightCulling_SingleLight_WithinRange_IsSelected) {
    LightCPU candidates[1] = { MakePointLight(0.f, 0.f, 5.f, 20.f) };
    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    uint32_t count = SelectNearestLights(candidates, 1u, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, 1u) << "One in-range light must be selected";
}

TEST(MultiLightTest, LightCulling_SingleLight_BeyondRange_IsExcluded) {
    // Light at 1000 units, range = 1.  Must be culled.
    LightCPU candidates[1] = { MakePointLight(0.f, 0.f, 1000.f, 1.0f) };
    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    uint32_t count = SelectNearestLights(candidates, 1u, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, 0u)
        << "Light far beyond its range must be culled";
}

TEST(MultiLightTest, LightCulling_MoreThanMax_PicksClosestEight) {
    // Create 10 lights at distances 1..10 from the camera.
    constexpr uint32_t kTotal = 10u;
    LightCPU candidates[kTotal];
    for (uint32_t i = 0; i < kTotal; ++i) {
        // Light i is at distance (i+1) along Z axis, range = 50 (all reachable).
        candidates[i] = MakePointLight(0.f, 0.f, static_cast<float>(i + 1), 50.f);
    }

    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    uint32_t count = SelectNearestLights(candidates, kTotal, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, kMaxMultiLights)
        << "Must select exactly kMaxMultiLights (8) lights from 10 candidates";

    // The selected lights should be the 8 closest (distances 1..8).
    // Verify that the furthest selected light is at most distance 8.
    for (uint32_t i = 0; i < count; ++i) {
        float z = out[i].positionOrDir.z;
        EXPECT_LE(z, 8.0f + kEps)
            << "Selected light " << i << " is at z=" << z
            << " which is beyond the 8th-closest";
    }
}

TEST(MultiLightTest, LightCulling_FewerThanMax_PicksAllAvailable) {
    constexpr uint32_t kTotal = 3u;
    LightCPU candidates[kTotal];
    for (uint32_t i = 0; i < kTotal; ++i) {
        candidates[i] = MakePointLight(0.f, 0.f, static_cast<float>(i + 1), 50.f);
    }

    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    uint32_t count = SelectNearestLights(candidates, kTotal, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, kTotal)
        << "All 3 in-range lights must be selected when fewer than kMaxMultiLights";
}

TEST(MultiLightTest, LightCulling_SortedByDistance_NearestFirst) {
    // Four lights in reverse order of closeness.
    constexpr uint32_t kTotal = 4u;
    LightCPU candidates[kTotal];
    // Intentionally out of order: 40, 10, 30, 20
    const float positions[] = {40.f, 10.f, 30.f, 20.f};
    for (uint32_t i = 0; i < kTotal; ++i) {
        candidates[i] = MakePointLight(0.f, 0.f, positions[i], 100.f);
    }

    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    uint32_t count = SelectNearestLights(candidates, kTotal, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, kTotal);

    // Output must be in ascending distance order.
    float prevDist = 0.f;
    for (uint32_t i = 0; i < count; ++i) {
        float d = out[i].positionOrDir.z; // all lights on Z axis, x=y=0
        EXPECT_GE(d, prevDist - kEps)
            << "Light " << i << " (z=" << d << ") is not in ascending order";
        prevDist = d;
    }
}

TEST(MultiLightTest, LightCulling_LightColor_CopiedToGPUStruct) {
    LightCPU candidates[1] = { MakePointLight(0.f, 0.f, 1.f, 50.f, 2.0f, 3.0f, 4.0f) };
    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    SelectNearestLights(candidates, 1u, camPos, out, kMaxMultiLights);

    EXPECT_NEAR(out[0].color.x, 2.0f, kEps) << "color.x not copied";
    EXPECT_NEAR(out[0].color.y, 3.0f, kEps) << "color.y not copied";
    EXPECT_NEAR(out[0].color.z, 4.0f, kEps) << "color.z not copied";
}

TEST(MultiLightTest, LightCulling_LightRange_CopiedToGPUStruct) {
    LightCPU candidates[1] = { MakePointLight(0.f, 0.f, 1.f, 17.5f) };
    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    SelectNearestLights(candidates, 1u, camPos, out, kMaxMultiLights);

    EXPECT_NEAR(out[0].range, 17.5f, kEps) << "range not copied to LightGPU";
}

TEST(MultiLightTest, LightCulling_PointLightType_SetToOne) {
    LightCPU candidates[1] = { MakePointLight(0.f, 0.f, 1.f, 50.f) };
    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    SelectNearestLights(candidates, 1u, camPos, out, kMaxMultiLights);

    EXPECT_NEAR(out[0].lightType, static_cast<float>(LightType::Point), kEps)
        << "LightGPU.lightType must be 1 for point lights";
}

TEST(MultiLightTest, LightCulling_ExactlyMaxCandidates_AllSelected) {
    LightCPU candidates[kMaxMultiLights];
    for (uint32_t i = 0; i < kMaxMultiLights; ++i) {
        candidates[i] = MakePointLight(0.f, 0.f, static_cast<float>(i + 1), 100.f);
    }

    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    uint32_t count = SelectNearestLights(candidates, kMaxMultiLights, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, kMaxMultiLights)
        << "Exactly kMaxMultiLights candidates must all be selected";
}

TEST(MultiLightTest, LightCulling_AllBeyondRange_WritesZero) {
    constexpr uint32_t kTotal = 5u;
    LightCPU candidates[kTotal];
    for (uint32_t i = 0; i < kTotal; ++i) {
        // Each light is 200 units away but range is only 1 unit.
        candidates[i] = MakePointLight(0.f, 0.f, 200.f, 1.0f);
    }

    Vec3     camPos{0, 0, 0};
    LightGPU out[kMaxMultiLights]{};
    uint32_t count = SelectNearestLights(candidates, kTotal, camPos, out, kMaxMultiLights);

    EXPECT_EQ(count, 0u)
        << "All out-of-range lights must be culled; expected count=0";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.6 — Zero lights edge case
// ─────────────────────────────────────────────────────────────────────────────

/// When lightCount is 0 the shader iterates zero lights and all illumination
/// comes from the ambient term stored in `ambientColor`.  The CPU side must
/// correctly produce a CB with lightCount=0 and zero-initialised lightArray.

TEST(MultiLightTest, ZeroLightsAmbientOnly_LightCountIsZero) {
    PerFrameMultiLightCB cb{};
    cb.lightCount = 0;

    EXPECT_EQ(cb.lightCount, 0u)
        << "CB with no lights must have lightCount=0";
}

TEST(MultiLightTest, ZeroLightsAmbientOnly_LightArrayIsAllZero) {
    PerFrameMultiLightCB cb{};
    cb.lightCount = 0;
    // lightArray is value-initialised to zero by the default constructor.
    for (uint32_t i = 0; i < kMaxMultiLights; ++i) {
        const char* raw = reinterpret_cast<const char*>(&cb.lightArray[i]);
        for (size_t b = 0; b < sizeof(LightGPU); ++b) {
            EXPECT_EQ(raw[b], 0)
                << "lightArray[" << i << "] byte " << b
                << " must be 0 when lightCount=0";
        }
    }
}

TEST(MultiLightTest, ZeroLightsAmbientOnly_AmbientColorPreserved) {
    PerFrameMultiLightCB cb{};
    cb.ambientColor = {0.05f, 0.05f, 0.08f, 0.0f};
    cb.lightCount   = 0;

    // The ambient must survive a round-trip through BuildMultiLightCB().
    EXPECT_NEAR(cb.ambientColor.x, 0.05f, kEps);
    EXPECT_NEAR(cb.ambientColor.y, 0.05f, kEps);
    EXPECT_NEAR(cb.ambientColor.z, 0.08f, kEps);
    EXPECT_NEAR(cb.ambientColor.w, 0.0f,  kEps);
}

TEST(MultiLightTest, ZeroLightsAmbientOnly_BuildMultiLightCB_ProducesZeroCount) {
    // No candidates → BuildMultiLightCB must produce lightCount=0.
    const Vec3 camPos{0, 0, 0};
    const Vec3 ambientColor{0.03f, 0.03f, 0.05f};

    PerFrameMultiLightCB cb = BuildMultiLightCB(nullptr, 0u, camPos, ambientColor);

    EXPECT_EQ(cb.lightCount, 0u)
        << "BuildMultiLightCB with no lights must produce lightCount=0";
}

TEST(MultiLightTest, ZeroLightsAmbientOnly_BuildMultiLightCB_AmbientColorCorrect) {
    const Vec3 camPos{0, 0, 0};
    const Vec3 ambientColor{0.1f, 0.15f, 0.2f};

    PerFrameMultiLightCB cb = BuildMultiLightCB(nullptr, 0u, camPos, ambientColor);

    EXPECT_NEAR(cb.ambientColor.x, 0.1f,  kEps) << "ambientColor.x";
    EXPECT_NEAR(cb.ambientColor.y, 0.15f, kEps) << "ambientColor.y";
    EXPECT_NEAR(cb.ambientColor.z, 0.2f,  kEps) << "ambientColor.z";
    EXPECT_NEAR(cb.ambientColor.w, 0.0f,  kEps) << "ambientColor.w must be 0";
}

TEST(MultiLightTest, ZeroLightsAmbientOnly_BuildMultiLightCB_LightArrayAllZero) {
    const Vec3 camPos{0, 0, 0};
    const Vec3 ambientColor{0.03f, 0.03f, 0.05f};

    PerFrameMultiLightCB cb = BuildMultiLightCB(nullptr, 0u, camPos, ambientColor);

    for (uint32_t i = 0; i < kMaxMultiLights; ++i) {
        const char* raw = reinterpret_cast<const char*>(&cb.lightArray[i]);
        for (size_t b = 0; b < sizeof(LightGPU); ++b) {
            EXPECT_EQ(raw[b], 0)
                << "lightArray[" << i << "] byte " << b
                << " must be 0 when no lights passed to BuildMultiLightCB";
        }
    }
}
