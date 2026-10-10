/// Unit tests for the VFX LOD system (vfx_lod.h / vfx_lod.cpp).
///
/// Covers: VFXLodLevelToString for all 4 levels + unknown, VFXLodLevelToSpawnFactor
/// for all 4 levels + unknown, VFXLodSettings default values and IsValid with valid
/// and invalid threshold configurations, VFXLodEvaluator::ComputeFromDistance at
/// various distances (before/at/after each threshold), emitter at camera position
/// (distance=0), VFXScreenCuller::ComputeScreenPixels formula verification,
/// VFXScreenCuller::ShouldCull at threshold boundary, VFXLodEvaluator::Evaluate
/// combining distance and screen criteria (takes worst), and Evaluate with zero
/// bounding radius.
///
/// No GPU hardware required — all tests are CPU-only and stateless.

#include <gtest/gtest.h>

#include "lod/vfx_lod.h"

#include <cmath>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// World units are centimetres (engine convention, see vfx_lod.h).
constexpr float kCentimetresPerMetre = 100.0f;

/// Returns a Vec3 at the given distance along the X axis from the origin.
Vec3 AtDistance(float dist) {
    return Vec3{dist, 0.0f, 0.0f};
}

/// Returns a Vec3 at the given distance, expressed in metres, along the X axis
/// (converted to centimetre world units).
Vec3 AtMetres(float metres) {
    return AtDistance(metres * kCentimetresPerMetre);
}

/// Returns a default (valid) VFXLodSettings with thresholds 50/100/200 m
/// (5000/10000/20000 cm).
VFXLodSettings DefaultSettings() {
    return VFXLodSettings{};
}

} // anonymous namespace

// =============================================================================
// VFXLodLevelToString — all 4 levels + unknown
// =============================================================================

TEST(UZ_VFXLodLevelToString, Full_ReturnsFull) {
    EXPECT_STREQ(VFXLodLevelToString(VFXLodLevel::Full), "Full");
}

TEST(UZ_VFXLodLevelToString, Reduced_ReturnsReduced) {
    EXPECT_STREQ(VFXLodLevelToString(VFXLodLevel::Reduced), "Reduced");
}

TEST(UZ_VFXLodLevelToString, Minimal_ReturnsMinimal) {
    EXPECT_STREQ(VFXLodLevelToString(VFXLodLevel::Minimal), "Minimal");
}

TEST(UZ_VFXLodLevelToString, Culled_ReturnsCulled) {
    EXPECT_STREQ(VFXLodLevelToString(VFXLodLevel::Culled), "Culled");
}

TEST(UZ_VFXLodLevelToString, UnknownValue_ReturnsUnknown) {
    const auto unknown = static_cast<VFXLodLevel>(200u);
    EXPECT_STREQ(VFXLodLevelToString(unknown), "Unknown");
}

// =============================================================================
// VFXLodLevelToSpawnFactor — all 4 levels + unknown
// =============================================================================

TEST(UZ_VFXLodLevelToSpawnFactor, Full_ReturnsOnePointZero) {
    EXPECT_FLOAT_EQ(VFXLodLevelToSpawnFactor(VFXLodLevel::Full), 1.0f);
}

TEST(UZ_VFXLodLevelToSpawnFactor, Reduced_ReturnsPointFive) {
    EXPECT_FLOAT_EQ(VFXLodLevelToSpawnFactor(VFXLodLevel::Reduced), 0.5f);
}

TEST(UZ_VFXLodLevelToSpawnFactor, Minimal_ReturnsPointTwoFive) {
    EXPECT_FLOAT_EQ(VFXLodLevelToSpawnFactor(VFXLodLevel::Minimal), 0.25f);
}

TEST(UZ_VFXLodLevelToSpawnFactor, Culled_ReturnsZero) {
    EXPECT_FLOAT_EQ(VFXLodLevelToSpawnFactor(VFXLodLevel::Culled), 0.0f);
}

TEST(UZ_VFXLodLevelToSpawnFactor, UnknownValue_ReturnsZero) {
    const auto unknown = static_cast<VFXLodLevel>(99u);
    EXPECT_FLOAT_EQ(VFXLodLevelToSpawnFactor(unknown), 0.0f)
        << "Unknown LOD level must return 0.0 (safe/culled fallback)";
}

// =============================================================================
// VFXLodSettings — default values
// =============================================================================

TEST(UZ_VFXLodSettings, DefaultValues_DistanceZeroIsFiftyMetres) {
    const VFXLodSettings s;
    EXPECT_FLOAT_EQ(s.distances[0], 50.0f * kCentimetresPerMetre);
}

TEST(UZ_VFXLodSettings, DefaultValues_DistanceOneIsOneHundredMetres) {
    const VFXLodSettings s;
    EXPECT_FLOAT_EQ(s.distances[1], 100.0f * kCentimetresPerMetre);
}

TEST(UZ_VFXLodSettings, DefaultValues_DistanceTwoIsTwoHundredMetres) {
    const VFXLodSettings s;
    EXPECT_FLOAT_EQ(s.distances[2], 200.0f * kCentimetresPerMetre);
}

// =============================================================================
// VFXLodSettings::IsValid — valid configurations
// =============================================================================

TEST(UZ_VFXLodSettings_IsValid, DefaultSettings_IsValid) {
    EXPECT_TRUE(DefaultSettings().IsValid());
}

TEST(UZ_VFXLodSettings_IsValid, CustomStrictlyIncreasing_IsValid) {
    VFXLodSettings s;
    s.distances[0] = 10.0f;
    s.distances[1] = 50.0f;
    s.distances[2] = 150.0f;
    EXPECT_TRUE(s.IsValid());
}

TEST(UZ_VFXLodSettings_IsValid, VerySmallButPositiveDistances_IsValid) {
    VFXLodSettings s;
    s.distances[0] = 0.001f;
    s.distances[1] = 0.002f;
    s.distances[2] = 0.003f;
    EXPECT_TRUE(s.IsValid());
}

// =============================================================================
// VFXLodSettings::IsValid — invalid configurations
// =============================================================================

TEST(UZ_VFXLodSettings_IsValid, FirstDistanceZero_IsInvalid) {
    VFXLodSettings s;
    s.distances[0] = 0.0f;
    s.distances[1] = 100.0f;
    s.distances[2] = 200.0f;
    EXPECT_FALSE(s.IsValid())
        << "distances[0] == 0 must be invalid";
}

TEST(UZ_VFXLodSettings_IsValid, FirstDistanceNegative_IsInvalid) {
    VFXLodSettings s;
    s.distances[0] = -10.0f;
    s.distances[1] = 100.0f;
    s.distances[2] = 200.0f;
    EXPECT_FALSE(s.IsValid());
}

TEST(UZ_VFXLodSettings_IsValid, EqualFirstAndSecond_IsInvalid) {
    VFXLodSettings s;
    s.distances[0] = 50.0f;
    s.distances[1] = 50.0f;
    s.distances[2] = 200.0f;
    EXPECT_FALSE(s.IsValid())
        << "distances[0] == distances[1] must be invalid (not strictly increasing)";
}

TEST(UZ_VFXLodSettings_IsValid, EqualSecondAndThird_IsInvalid) {
    VFXLodSettings s;
    s.distances[0] = 50.0f;
    s.distances[1] = 100.0f;
    s.distances[2] = 100.0f;
    EXPECT_FALSE(s.IsValid());
}

TEST(UZ_VFXLodSettings_IsValid, DescendingDistances_IsInvalid) {
    VFXLodSettings s;
    s.distances[0] = 200.0f;
    s.distances[1] = 100.0f;
    s.distances[2] = 50.0f;
    EXPECT_FALSE(s.IsValid());
}

TEST(UZ_VFXLodSettings_IsValid, SecondLessThanFirst_IsInvalid) {
    VFXLodSettings s;
    s.distances[0] = 100.0f;
    s.distances[1] = 50.0f;
    s.distances[2] = 200.0f;
    EXPECT_FALSE(s.IsValid());
}

// =============================================================================
// VFXLodEvaluator::ComputeFromDistance — threshold boundaries
// =============================================================================

// Default thresholds: 50 / 100 / 200 m (5000 / 10000 / 20000 cm)
// Bands: [0, 50) → Full, [50, 100) → Reduced, [100, 200) → Minimal, [200, ∞) → Culled

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, ZeroDistance_ReturnsFull) {
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f}, s);
    EXPECT_EQ(lod, VFXLodLevel::Full)
        << "Emitter coincident with camera (distance=0) must be Full";
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, DistanceJustBelowFirstThreshold_ReturnsFull) {
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        AtMetres(49.9f), Vec3{0, 0, 0}, s);
    EXPECT_EQ(lod, VFXLodLevel::Full);
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, DistanceAtFirstThreshold_ReturnsReduced) {
    const auto s = DefaultSettings();
    // distances[0] = 50 m; at exactly 50 m → Reduced (no longer < 50^2)
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        AtMetres(50.0f), Vec3{0, 0, 0}, s);
    EXPECT_EQ(lod, VFXLodLevel::Reduced);
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, DistanceBetweenFirstAndSecond_ReturnsReduced) {
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        AtMetres(75.0f), Vec3{0, 0, 0}, s);
    EXPECT_EQ(lod, VFXLodLevel::Reduced);
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, DistanceAtSecondThreshold_ReturnsMinimal) {
    const auto s = DefaultSettings();
    // distances[1] = 100; exactly 100m → Minimal
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        AtMetres(100.0f), Vec3{0, 0, 0}, s);
    EXPECT_EQ(lod, VFXLodLevel::Minimal);
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, DistanceBetweenSecondAndThird_ReturnsMinimal) {
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        AtMetres(150.0f), Vec3{0, 0, 0}, s);
    EXPECT_EQ(lod, VFXLodLevel::Minimal);
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, DistanceAtThirdThreshold_ReturnsCulled) {
    const auto s = DefaultSettings();
    // distances[2] = 200; exactly 200m → Culled
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        AtMetres(200.0f), Vec3{0, 0, 0}, s);
    EXPECT_EQ(lod, VFXLodLevel::Culled);
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, DistanceBeyondThirdThreshold_ReturnsCulled) {
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        AtMetres(500.0f), Vec3{0, 0, 0}, s);
    EXPECT_EQ(lod, VFXLodLevel::Culled);
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, NonAxisAlignedVector_CorrectTier) {
    // Place emitter at (30, 40, 0) m → distance = sqrt(900+1600) = 50 m → Reduced.
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::ComputeFromDistance(
        Vec3{30.0f * kCentimetresPerMetre, 40.0f * kCentimetresPerMetre, 0.0f},
        Vec3{0.0f, 0.0f, 0.0f}, s);
    EXPECT_EQ(lod, VFXLodLevel::Reduced)
        << "3D distance at exactly 50m must return Reduced";
}

TEST(UZ_VFXLodEvaluator_ComputeFromDistance, CustomThresholds_WorkCorrectly) {
    VFXLodSettings s;
    s.distances[0] = 10.0f;
    s.distances[1] = 30.0f;
    s.distances[2] = 60.0f;

    EXPECT_EQ(VFXLodEvaluator::ComputeFromDistance(AtDistance(5.0f),  Vec3{0,0,0}, s), VFXLodLevel::Full);
    EXPECT_EQ(VFXLodEvaluator::ComputeFromDistance(AtDistance(15.0f), Vec3{0,0,0}, s), VFXLodLevel::Reduced);
    EXPECT_EQ(VFXLodEvaluator::ComputeFromDistance(AtDistance(45.0f), Vec3{0,0,0}, s), VFXLodLevel::Minimal);
    EXPECT_EQ(VFXLodEvaluator::ComputeFromDistance(AtDistance(80.0f), Vec3{0,0,0}, s), VFXLodLevel::Culled);
}

// =============================================================================
// VFXScreenCuller::ComputeScreenPixels — formula verification
// =============================================================================

TEST(UZ_VFXScreenCuller_ComputeScreenPixels, Formula_Verification) {
    // screenPixels = (radius / distance) * screenHeight * 0.5
    // With radius=10, distance=100, screenHeight=1080:
    //   expected = (10/100) * 1080 * 0.5 = 54.0
    const float pixels = VFXScreenCuller::ComputeScreenPixels(10.0f, 100.0f, 1080.0f);
    EXPECT_NEAR(pixels, 54.0f, 0.01f);
}

TEST(UZ_VFXScreenCuller_ComputeScreenPixels, LargeObjectClose_ReturnsLargeValue) {
    // radius=50, distance=10, screenHeight=1080: 50/10 * 1080 * 0.5 = 2700
    const float pixels = VFXScreenCuller::ComputeScreenPixels(50.0f, 10.0f, 1080.0f);
    EXPECT_NEAR(pixels, 2700.0f, 1.0f);
}

TEST(UZ_VFXScreenCuller_ComputeScreenPixels, SmallObjectFar_ReturnsSmallValue) {
    // radius=0.5, distance=1000, screenHeight=1080: 0.5/1000 * 1080 * 0.5 = 0.27
    const float pixels = VFXScreenCuller::ComputeScreenPixels(0.5f, 1000.0f, 1080.0f);
    EXPECT_NEAR(pixels, 0.27f, 0.01f);
}

TEST(UZ_VFXScreenCuller_ComputeScreenPixels, ZeroRadius_ReturnsZero) {
    const float pixels = VFXScreenCuller::ComputeScreenPixels(0.0f, 100.0f, 1080.0f);
    EXPECT_FLOAT_EQ(pixels, 0.0f);
}

TEST(UZ_VFXScreenCuller_ComputeScreenPixels, NegativeRadius_ReturnsZero) {
    const float pixels = VFXScreenCuller::ComputeScreenPixels(-5.0f, 100.0f, 1080.0f);
    EXPECT_FLOAT_EQ(pixels, 0.0f)
        << "Negative radius must be treated as zero";
}

TEST(UZ_VFXScreenCuller_ComputeScreenPixels, ZeroScreenHeight_ReturnsZero) {
    const float pixels = VFXScreenCuller::ComputeScreenPixels(10.0f, 100.0f, 0.0f);
    EXPECT_FLOAT_EQ(pixels, 0.0f)
        << "Zero screen height must return 0 pixels";
}

TEST(UZ_VFXScreenCuller_ComputeScreenPixels, ZeroDistance_UsesEpsilon) {
    // distance=0 must be clamped to 0.001 to avoid division by zero.
    // Result = (10 / 0.001) * 1080 * 0.5 = very large but finite.
    const float pixels = VFXScreenCuller::ComputeScreenPixels(10.0f, 0.0f, 1080.0f);
    EXPECT_GT(pixels, 0.0f)
        << "Zero distance must not produce zero or NaN (epsilon clamping applied)";
    EXPECT_TRUE(std::isfinite(pixels))
        << "Zero distance must not produce infinity";
}

// =============================================================================
// VFXScreenCuller::ShouldCull — threshold boundary
// =============================================================================

TEST(UZ_VFXScreenCuller_ShouldCull, ExactlyAtThreshold_ShouldNotCull) {
    // With radius=4, screenHeight=1080, minPixels=4:
    // Solve distance: (4/d)*1080*0.5 = 4 → d = 4*1080*0.5/4 = 540
    // At d=540: pixels=exactly 4.0 → should NOT cull (not strictly less than)
    const float d = 540.0f;
    const bool cull = VFXScreenCuller::ShouldCull(4.0f, d, 1080.0f, 4.0f);
    EXPECT_FALSE(cull)
        << "Screen coverage == minScreenPixels must not cull (strictly-less-than check)";
}

TEST(UZ_VFXScreenCuller_ShouldCull, JustBelowThreshold_ShouldCull) {
    // At d=541 the coverage drops just below 4px → should cull.
    const bool cull = VFXScreenCuller::ShouldCull(4.0f, 541.0f, 1080.0f, 4.0f);
    EXPECT_TRUE(cull)
        << "Screen coverage just below minScreenPixels must cull";
}

TEST(UZ_VFXScreenCuller_ShouldCull, LargeObjectCloseToCamera_ShouldNotCull) {
    // radius=10, distance=5, screenHeight=1080 → pixels = 10/5 * 1080 * 0.5 = 1080
    EXPECT_FALSE(VFXScreenCuller::ShouldCull(10.0f, 5.0f, 1080.0f, 4.0f));
}

TEST(UZ_VFXScreenCuller_ShouldCull, ZeroRadius_ShouldCull) {
    // Zero radius → 0 pixels → always culled.
    EXPECT_TRUE(VFXScreenCuller::ShouldCull(0.0f, 100.0f, 1080.0f, 4.0f))
        << "Zero bounding radius must be culled";
}

TEST(UZ_VFXScreenCuller_ShouldCull, DefaultMinPixels_IsFour) {
    // Verify the default min pixels argument is 4.0 by testing at the 4px boundary.
    // Distance where pixels = 4: d = (4 * 1080 * 0.5) / 4 = 540.
    EXPECT_FALSE(VFXScreenCuller::ShouldCull(4.0f, 540.0f, 1080.0f));
    EXPECT_TRUE(VFXScreenCuller::ShouldCull(4.0f, 541.0f, 1080.0f));
}

// =============================================================================
// VFXLodEvaluator::ComputeFromScreenSize — binary: Full or Culled
// =============================================================================

TEST(UZ_VFXLodEvaluator_ComputeFromScreenSize, LargeObjectClose_ReturnsFull) {
    // radius=50, distance=5, screenHeight=1080 → huge coverage → Full
    const auto lod = VFXLodEvaluator::ComputeFromScreenSize(50.0f, 5.0f, 1080.0f, 4.0f);
    EXPECT_EQ(lod, VFXLodLevel::Full)
        << "Large object close to camera must return Full";
}

TEST(UZ_VFXLodEvaluator_ComputeFromScreenSize, TinyObjectFar_ReturnsCulled) {
    // radius=0.1, distance=10000, screenHeight=1080 → sub-pixel → Culled
    const auto lod = VFXLodEvaluator::ComputeFromScreenSize(0.1f, 10000.0f, 1080.0f, 4.0f);
    EXPECT_EQ(lod, VFXLodLevel::Culled)
        << "Sub-pixel bounding sphere must be culled by screen-size criterion";
}

TEST(UZ_VFXLodEvaluator_ComputeFromScreenSize, ZeroRadius_ReturnsCulled) {
    const auto lod = VFXLodEvaluator::ComputeFromScreenSize(0.0f, 100.0f, 1080.0f, 4.0f);
    EXPECT_EQ(lod, VFXLodLevel::Culled)
        << "Zero bounding radius must be culled";
}

// =============================================================================
// VFXLodEvaluator::Evaluate — takes worst (most restrictive) of distance + screen
// =============================================================================

TEST(UZ_VFXLodEvaluator_Evaluate, CloseAndLarge_ReturnsFull) {
    // Distance < 50 m (Full by distance) AND large on screen (Full by screen).
    // Result: Full.
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::Evaluate(
        AtMetres(10.0f),
        Vec3{0.0f, 0.0f, 0.0f},
        /*boundingRadius=*/5.0f * kCentimetresPerMetre,
        /*screenHeight=*/1080.0f,
        s,
        /*minScreenPixels=*/4.0f
    );
    EXPECT_EQ(lod, VFXLodLevel::Full);
}

TEST(UZ_VFXLodEvaluator_Evaluate, FarButLarge_ReturnsDistanceTier) {
    // Emitter at 150 m (Minimal by distance) but huge radius → screen says Full.
    // Worst wins → Minimal.
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::Evaluate(
        AtMetres(150.0f),
        Vec3{0.0f, 0.0f, 0.0f},
        /*boundingRadius=*/100.0f * kCentimetresPerMetre,  // many pixels at 150 m
        /*screenHeight=*/1080.0f,
        s,
        /*minScreenPixels=*/4.0f
    );
    EXPECT_EQ(lod, VFXLodLevel::Minimal)
        << "Distance criterion (Minimal) must win over screen (Full)";
}

TEST(UZ_VFXLodEvaluator_Evaluate, CloseButTiny_ReturnsCulled) {
    // Emitter at 10 m (Full by distance) but radius=0.001 cm → sub-pixel → screen
    // says Culled. Worst wins → Culled.
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::Evaluate(
        AtMetres(10.0f),
        Vec3{0.0f, 0.0f, 0.0f},
        /*boundingRadius=*/0.001f,
        /*screenHeight=*/1080.0f,
        s,
        /*minScreenPixels=*/4.0f
    );
    EXPECT_EQ(lod, VFXLodLevel::Culled)
        << "Sub-pixel emitter must be culled even when close to camera";
}

TEST(UZ_VFXLodEvaluator_Evaluate, FarAndTiny_ReturnsCulled) {
    // Both criteria demand Culled (300 m is beyond the 200 m cull threshold).
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::Evaluate(
        AtMetres(300.0f),
        Vec3{0.0f, 0.0f, 0.0f},
        /*boundingRadius=*/0.001f,
        /*screenHeight=*/1080.0f,
        s
    );
    EXPECT_EQ(lod, VFXLodLevel::Culled);
}

TEST(UZ_VFXLodEvaluator_Evaluate, ZeroBoundingRadius_ReturnsCulled) {
    // Even if distance is in Full zone, zero radius means screen says Culled.
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::Evaluate(
        AtMetres(5.0f),
        Vec3{0.0f, 0.0f, 0.0f},
        /*boundingRadius=*/0.0f,
        /*screenHeight=*/1080.0f,
        s
    );
    EXPECT_EQ(lod, VFXLodLevel::Culled)
        << "Zero bounding radius must be culled even at close range";
}

TEST(UZ_VFXLodEvaluator_Evaluate, MediumDistanceMediumSize_ReturnsReducedOrWorse) {
    // Emitter at 75 m (Reduced by distance), reasonable size.
    const auto s = DefaultSettings();
    const auto lod = VFXLodEvaluator::Evaluate(
        AtMetres(75.0f),
        Vec3{0.0f, 0.0f, 0.0f},
        /*boundingRadius=*/2.0f * kCentimetresPerMetre,
        /*screenHeight=*/1080.0f,
        s
    );
    // Distance says Reduced; screen with radius=2 m at 75 m: (2/75)*1080*0.5 = 14.4px > 4 → Full
    // Worst of (Reduced, Full) = Reduced.
    EXPECT_EQ(lod, VFXLodLevel::Reduced);
}
