/// Unit tests for VFXBudget and VFXQualityLevel (vfx_budget.h / vfx_budget.cpp).
///
/// Covers: default construction at High quality, VFXQualityLevelToMaxParticles for
/// all 4 levels (Low=125K, Medium=250K, High=500K, Ultra=1M), VFXQualityLevelToString
/// for all 4 levels + unknown, SetQualityLevel changes max particles and returns
/// correct quality level, SetQualityLevel same level is a no-op, GetStats initial
/// values, UpdateStats computes utilization correctly, HasBudget with available
/// space (true), HasBudget at capacity (false), HasBudget with zero requested (true),
/// and over-utilization (particlesUsed > maxParticles) handled gracefully.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "budget/vfx_budget.h"

#include <cstdint>

using namespace uz;

// =============================================================================
// VFXQualityLevelToString — all 4 levels + unknown
// =============================================================================

TEST(UZ_VFXQualityLevelToString, Low_ReturnsLow) {
    EXPECT_STREQ(VFXQualityLevelToString(VFXQualityLevel::Low), "Low");
}

TEST(UZ_VFXQualityLevelToString, Medium_ReturnsMedium) {
    EXPECT_STREQ(VFXQualityLevelToString(VFXQualityLevel::Medium), "Medium");
}

TEST(UZ_VFXQualityLevelToString, High_ReturnsHigh) {
    EXPECT_STREQ(VFXQualityLevelToString(VFXQualityLevel::High), "High");
}

TEST(UZ_VFXQualityLevelToString, Ultra_ReturnsUltra) {
    EXPECT_STREQ(VFXQualityLevelToString(VFXQualityLevel::Ultra), "Ultra");
}

TEST(UZ_VFXQualityLevelToString, UnknownValue_ReturnsUnknown) {
    const auto unknown = static_cast<VFXQualityLevel>(255u);
    EXPECT_STREQ(VFXQualityLevelToString(unknown), "Unknown");
}

// =============================================================================
// VFXQualityLevelToMaxParticles — correct particle caps per level
// =============================================================================

TEST(UZ_VFXQualityLevelToMaxParticles, Low_Returns125000) {
    EXPECT_EQ(VFXQualityLevelToMaxParticles(VFXQualityLevel::Low), 125'000u);
}

TEST(UZ_VFXQualityLevelToMaxParticles, Medium_Returns250000) {
    EXPECT_EQ(VFXQualityLevelToMaxParticles(VFXQualityLevel::Medium), 250'000u);
}

TEST(UZ_VFXQualityLevelToMaxParticles, High_Returns500000) {
    EXPECT_EQ(VFXQualityLevelToMaxParticles(VFXQualityLevel::High), 500'000u);
}

TEST(UZ_VFXQualityLevelToMaxParticles, Ultra_Returns1000000) {
    EXPECT_EQ(VFXQualityLevelToMaxParticles(VFXQualityLevel::Ultra), 1'000'000u);
}

TEST(UZ_VFXQualityLevelToMaxParticles, UnknownValue_ReturnsZero) {
    const auto unknown = static_cast<VFXQualityLevel>(255u);
    EXPECT_EQ(VFXQualityLevelToMaxParticles(unknown), 0u)
        << "Out-of-range quality level must return 0 particles";
}

// =============================================================================
// VFXBudget — default construction at High quality
// =============================================================================

TEST(UZ_VFXBudget, DefaultConstruct_QualityIsHigh) {
    const VFXBudget budget;
    EXPECT_EQ(budget.GetQualityLevel(), VFXQualityLevel::High);
}

TEST(UZ_VFXBudget, DefaultConstruct_MaxParticlesIs500000) {
    const VFXBudget budget;
    EXPECT_EQ(budget.GetMaxParticles(), 500'000u);
}

TEST(UZ_VFXBudget, ExplicitConstruct_Low_MaxParticlesIs125000) {
    const VFXBudget budget(VFXQualityLevel::Low);
    EXPECT_EQ(budget.GetMaxParticles(), 125'000u);
}

TEST(UZ_VFXBudget, ExplicitConstruct_Medium_MaxParticlesIs250000) {
    const VFXBudget budget(VFXQualityLevel::Medium);
    EXPECT_EQ(budget.GetMaxParticles(), 250'000u);
}

TEST(UZ_VFXBudget, ExplicitConstruct_Ultra_MaxParticlesIs1000000) {
    const VFXBudget budget(VFXQualityLevel::Ultra);
    EXPECT_EQ(budget.GetMaxParticles(), 1'000'000u);
}

// =============================================================================
// SetQualityLevel — changes max particles and returns correct level
// =============================================================================

TEST(UZ_VFXBudget, SetQualityLevel_ToLow_UpdatesMaxParticles) {
    VFXBudget budget;  // starts at High (500K)
    budget.SetQualityLevel(VFXQualityLevel::Low);
    EXPECT_EQ(budget.GetQualityLevel(), VFXQualityLevel::Low);
    EXPECT_EQ(budget.GetMaxParticles(), 125'000u);
}

TEST(UZ_VFXBudget, SetQualityLevel_ToMedium_UpdatesMaxParticles) {
    VFXBudget budget(VFXQualityLevel::Low);
    budget.SetQualityLevel(VFXQualityLevel::Medium);
    EXPECT_EQ(budget.GetQualityLevel(), VFXQualityLevel::Medium);
    EXPECT_EQ(budget.GetMaxParticles(), 250'000u);
}

TEST(UZ_VFXBudget, SetQualityLevel_ToUltra_UpdatesMaxParticles) {
    VFXBudget budget(VFXQualityLevel::High);
    budget.SetQualityLevel(VFXQualityLevel::Ultra);
    EXPECT_EQ(budget.GetQualityLevel(), VFXQualityLevel::Ultra);
    EXPECT_EQ(budget.GetMaxParticles(), 1'000'000u);
}

TEST(UZ_VFXBudget, SetQualityLevel_UpdatesStatsTotalBudget) {
    VFXBudget budget(VFXQualityLevel::High);
    budget.SetQualityLevel(VFXQualityLevel::Low);
    // After SetQualityLevel, stats_.totalBudget must reflect new cap.
    EXPECT_EQ(budget.GetStats().totalBudget, 125'000u)
        << "stats_.totalBudget must be updated immediately by SetQualityLevel";
}

// =============================================================================
// SetQualityLevel — same level is a no-op (no crash, values unchanged)
// =============================================================================

TEST(UZ_VFXBudget, SetQualityLevel_SameLevel_NoChange) {
    VFXBudget budget(VFXQualityLevel::High);
    const uint32_t before = budget.GetMaxParticles();
    budget.SetQualityLevel(VFXQualityLevel::High);  // same level
    EXPECT_EQ(budget.GetQualityLevel(), VFXQualityLevel::High);
    EXPECT_EQ(budget.GetMaxParticles(), before)
        << "SetQualityLevel with same level must be a no-op";
}

// =============================================================================
// GetStats — initial values
// =============================================================================

TEST(UZ_VFXBudget, InitialStats_TotalBudget_EqualsMaxParticles) {
    const VFXBudget budget(VFXQualityLevel::High);
    EXPECT_EQ(budget.GetStats().totalBudget, 500'000u);
}

TEST(UZ_VFXBudget, InitialStats_ParticlesUsed_IsZero) {
    const VFXBudget budget;
    EXPECT_EQ(budget.GetStats().particlesUsed, 0u);
}

TEST(UZ_VFXBudget, InitialStats_ParticlesAvailable_EqualsMaxParticles) {
    const VFXBudget budget(VFXQualityLevel::High);
    EXPECT_EQ(budget.GetStats().particlesAvailable, 500'000u);
}

TEST(UZ_VFXBudget, InitialStats_ActiveEmitters_IsZero) {
    const VFXBudget budget;
    EXPECT_EQ(budget.GetStats().activeEmitters, 0u);
}

TEST(UZ_VFXBudget, InitialStats_CulledEmitters_IsZero) {
    const VFXBudget budget;
    EXPECT_EQ(budget.GetStats().culledEmitters, 0u);
}

TEST(UZ_VFXBudget, InitialStats_BudgetUtilization_IsZero) {
    const VFXBudget budget;
    EXPECT_FLOAT_EQ(budget.GetStats().budgetUtilization, 0.0f);
}

// =============================================================================
// UpdateStats — correct computation of all fields
// =============================================================================

TEST(UZ_VFXBudget, UpdateStats_ParticlesUsed_StoredCorrectly) {
    VFXBudget budget(VFXQualityLevel::High);
    budget.UpdateStats(100'000u, 5u, 2u);
    EXPECT_EQ(budget.GetStats().particlesUsed, 100'000u);
}

TEST(UZ_VFXBudget, UpdateStats_ActiveEmitters_StoredCorrectly) {
    VFXBudget budget(VFXQualityLevel::High);
    budget.UpdateStats(100'000u, 5u, 2u);
    EXPECT_EQ(budget.GetStats().activeEmitters, 5u);
}

TEST(UZ_VFXBudget, UpdateStats_CulledEmitters_StoredCorrectly) {
    VFXBudget budget(VFXQualityLevel::High);
    budget.UpdateStats(100'000u, 5u, 2u);
    EXPECT_EQ(budget.GetStats().culledEmitters, 2u);
}

TEST(UZ_VFXBudget, UpdateStats_ParticlesAvailable_ComputedCorrectly) {
    VFXBudget budget(VFXQualityLevel::High);  // max=500K
    budget.UpdateStats(200'000u, 3u, 0u);
    // available = 500K - 200K = 300K
    EXPECT_EQ(budget.GetStats().particlesAvailable, 300'000u);
}

TEST(UZ_VFXBudget, UpdateStats_TotalBudget_MatchesMaxParticles) {
    VFXBudget budget(VFXQualityLevel::Medium);  // max=250K
    budget.UpdateStats(50'000u, 2u, 1u);
    EXPECT_EQ(budget.GetStats().totalBudget, 250'000u);
}

TEST(UZ_VFXBudget, UpdateStats_BudgetUtilization_HalfFull) {
    VFXBudget budget(VFXQualityLevel::High);  // max=500K
    budget.UpdateStats(250'000u, 4u, 0u);
    // utilization = 250K / 500K = 0.5
    EXPECT_NEAR(budget.GetStats().budgetUtilization, 0.5f, 1e-5f);
}

TEST(UZ_VFXBudget, UpdateStats_BudgetUtilization_AtFull) {
    VFXBudget budget(VFXQualityLevel::High);  // max=500K
    budget.UpdateStats(500'000u, 10u, 0u);
    EXPECT_NEAR(budget.GetStats().budgetUtilization, 1.0f, 1e-5f);
}

TEST(UZ_VFXBudget, UpdateStats_BudgetUtilization_Empty) {
    VFXBudget budget(VFXQualityLevel::High);
    budget.UpdateStats(0u, 0u, 0u);
    EXPECT_FLOAT_EQ(budget.GetStats().budgetUtilization, 0.0f);
}

// =============================================================================
// UpdateStats — over-utilization (particlesUsed > maxParticles) handled gracefully
// =============================================================================

TEST(UZ_VFXBudget, UpdateStats_OverUtilization_AvailableClampedToZero) {
    VFXBudget budget(VFXQualityLevel::Low);   // max=125K
    // Simulate transient over-allocation (e.g., quality dropped mid-frame).
    budget.UpdateStats(200'000u, 3u, 0u);
    EXPECT_EQ(budget.GetStats().particlesAvailable, 0u)
        << "particlesAvailable must be 0 (not wrap) when used > max";
}

TEST(UZ_VFXBudget, UpdateStats_OverUtilization_UtilizationClampedToOne) {
    VFXBudget budget(VFXQualityLevel::Low);   // max=125K
    budget.UpdateStats(200'000u, 3u, 0u);
    EXPECT_FLOAT_EQ(budget.GetStats().budgetUtilization, 1.0f)
        << "budgetUtilization must be clamped to 1.0 on overflow";
}

TEST(UZ_VFXBudget, UpdateStats_OverUtilization_ParticlesUsedStoredAsIs) {
    VFXBudget budget(VFXQualityLevel::Low);   // max=125K
    budget.UpdateStats(999'999u, 0u, 0u);
    EXPECT_EQ(budget.GetStats().particlesUsed, 999'999u)
        << "particlesUsed must be stored as given, not clamped";
}

// =============================================================================
// HasBudget — available space
// =============================================================================

TEST(UZ_VFXBudget, HasBudget_ZeroRequested_AlwaysTrue) {
    VFXBudget budget(VFXQualityLevel::High);
    budget.UpdateStats(500'000u, 0u, 0u);  // fully used
    EXPECT_TRUE(budget.HasBudget(0u))
        << "Requesting 0 particles must always return true";
}

TEST(UZ_VFXBudget, HasBudget_WithAvailableSpace_ReturnsTrue) {
    VFXBudget budget(VFXQualityLevel::High);  // max=500K
    budget.UpdateStats(200'000u, 3u, 0u);
    // available = 300K; requesting 100K → true
    EXPECT_TRUE(budget.HasBudget(100'000u));
}

TEST(UZ_VFXBudget, HasBudget_ExactlyAvailable_ReturnsTrue) {
    VFXBudget budget(VFXQualityLevel::High);  // max=500K
    budget.UpdateStats(300'000u, 3u, 0u);
    // available = 200K; requesting exactly 200K → true
    EXPECT_TRUE(budget.HasBudget(200'000u));
}

TEST(UZ_VFXBudget, HasBudget_AtCapacity_ReturnsFalse) {
    VFXBudget budget(VFXQualityLevel::High);  // max=500K
    budget.UpdateStats(500'000u, 10u, 0u);
    // used = 500K (at cap); requesting any more → false
    EXPECT_FALSE(budget.HasBudget(1u))
        << "HasBudget must return false when at max capacity";
}

TEST(UZ_VFXBudget, HasBudget_OneOverAvailable_ReturnsFalse) {
    VFXBudget budget(VFXQualityLevel::Medium);  // max=250K
    budget.UpdateStats(200'000u, 5u, 0u);
    // available = 50K; requesting 50001 → false
    EXPECT_FALSE(budget.HasBudget(50'001u))
        << "Requesting one more than available must return false";
}

TEST(UZ_VFXBudget, HasBudget_ExceedsMaxParticles_ReturnsFalse) {
    VFXBudget budget(VFXQualityLevel::Low);   // max=125K
    // Requesting more than the total cap is always false.
    EXPECT_FALSE(budget.HasBudget(500'000u))
        << "Requesting more than total max particles must return false";
}

// =============================================================================
// Copy and move semantics
// =============================================================================

TEST(UZ_VFXBudget, CopyConstruct_PreservesQualityAndStats) {
    VFXBudget original(VFXQualityLevel::Medium);
    original.UpdateStats(100'000u, 3u, 1u);

    const VFXBudget copy = original;
    EXPECT_EQ(copy.GetQualityLevel(), VFXQualityLevel::Medium);
    EXPECT_EQ(copy.GetMaxParticles(), 250'000u);
    EXPECT_EQ(copy.GetStats().particlesUsed, 100'000u);
    EXPECT_EQ(copy.GetStats().activeEmitters, 3u);
    EXPECT_EQ(copy.GetStats().culledEmitters, 1u);
}

TEST(UZ_VFXBudget, MoveConstruct_TransfersQualityAndStats) {
    VFXBudget original(VFXQualityLevel::Ultra);
    original.UpdateStats(50'000u, 2u, 0u);

    const VFXBudget moved = std::move(original);
    EXPECT_EQ(moved.GetQualityLevel(), VFXQualityLevel::Ultra);
    EXPECT_EQ(moved.GetMaxParticles(), 1'000'000u);
    EXPECT_EQ(moved.GetStats().particlesUsed, 50'000u);
}
