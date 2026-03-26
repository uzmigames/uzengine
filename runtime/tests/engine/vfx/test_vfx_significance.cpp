/// Unit tests for VFXSignificanceManager (vfx_significance_manager.h / .cpp).
///
/// Covers: ComputeScore with various priority/distance/visibility combinations,
/// priority dominance, distance attenuation (closer = higher score), visibility
/// penalty (visible > not visible by 10x), RankEmitters sorts descending by score,
/// EnforceBudget under budget (no changes), EnforceBudget over budget (lowest
/// significance culled first), progressive degradation (Full→Reduced→Minimal→Culled),
/// EnforceBudget with zero budget (everything culled), single emitter, and
/// high-significance emitters preserved at Full when budget allows.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "budget/vfx_significance_manager.h"

#include <cmath>
#include <vector>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a VFXEmitterSignificance record with the given fields.
// ─────────────────────────────────────────────────────────────────────────────

/// Returns an emitter record with specified priority, distance, visibility,
/// demand, and id.
VFXEmitterSignificance MakeEmitter(
    uint32_t id,
    uint32_t priority,
    float    distanceToCamera,
    bool     visible,
    uint32_t demand
) {
    VFXEmitterSignificance e;
    e.emitterId       = id;
    e.priority        = priority;
    e.distanceToCamera = distanceToCamera;
    e.isFrustumVisible = visible;
    e.particleDemand  = demand;
    e.appliedLod      = VFXLodLevel::Full;
    e.score           = 0.0f;
    return e;
}

} // anonymous namespace

// =============================================================================
// ComputeScore — formula: priority * (1/(1+dist*0.01)) * (visible ? 1.0 : 0.1)
// =============================================================================

TEST(UZ_VFXSignificanceManager_ComputeScore, ZeroPriority_ReturnsZero) {
    const float score = VFXSignificanceManager::ComputeScore(0u, 0.0f, true);
    EXPECT_FLOAT_EQ(score, 0.0f)
        << "Priority 0 must yield score 0 regardless of distance/visibility";
}

TEST(UZ_VFXSignificanceManager_ComputeScore, AtOrigin_Visible_ReturnsExactlyPriority) {
    // distance=0 → distanceFactor = 1/(1+0*0.01) = 1.0
    // visibility=true → factor = 1.0
    // score = priority * 1.0 * 1.0 = priority
    const float score = VFXSignificanceManager::ComputeScore(50u, 0.0f, true);
    EXPECT_NEAR(score, 50.0f, 1e-4f);
}

TEST(UZ_VFXSignificanceManager_ComputeScore, AtOrigin_NotVisible_ReturnsOnetenth) {
    // distance=0 → factor = 1.0; visibility=false → 0.1
    // score = 50 * 1.0 * 0.1 = 5.0
    const float score = VFXSignificanceManager::ComputeScore(50u, 0.0f, false);
    EXPECT_NEAR(score, 5.0f, 1e-4f);
}

TEST(UZ_VFXSignificanceManager_ComputeScore, VisibleVsNotVisible_RatioIsTen) {
    // The ratio between visible and not-visible scores must be exactly 10x.
    const float visible    = VFXSignificanceManager::ComputeScore(100u, 50.0f, true);
    const float notVisible = VFXSignificanceManager::ComputeScore(100u, 50.0f, false);
    EXPECT_NEAR(visible / notVisible, 10.0f, 1e-3f)
        << "Visible score must be exactly 10x the not-visible score";
}

TEST(UZ_VFXSignificanceManager_ComputeScore, At100m_HalfwayAttenuation) {
    // At dist=100: factor = 1/(1+100*0.01) = 1/2 = 0.5
    // score = 100 * 0.5 * 1.0 = 50
    const float score = VFXSignificanceManager::ComputeScore(100u, 100.0f, true);
    EXPECT_NEAR(score, 50.0f, 1e-3f)
        << "At 100m the distance factor must be 0.5 (1% per metre falloff)";
}

TEST(UZ_VFXSignificanceManager_ComputeScore, HigherPriority_YieldsHigherScore) {
    const float low  = VFXSignificanceManager::ComputeScore(10u,  50.0f, true);
    const float high = VFXSignificanceManager::ComputeScore(100u, 50.0f, true);
    EXPECT_GT(high, low)
        << "Higher priority must always yield a higher score at equal distance";
}

TEST(UZ_VFXSignificanceManager_ComputeScore, CloserDistance_YieldsHigherScore) {
    const float close = VFXSignificanceManager::ComputeScore(50u, 10.0f, true);
    const float far_  = VFXSignificanceManager::ComputeScore(50u, 200.0f, true);
    EXPECT_GT(close, far_)
        << "Closer emitter must have a higher score than a far one at equal priority";
}

TEST(UZ_VFXSignificanceManager_ComputeScore, NegativeDistance_TreatedAsZero) {
    // Negative distances are clamped to 0 by the implementation.
    const float negativeScore = VFXSignificanceManager::ComputeScore(50u, -100.0f, true);
    const float zeroScore     = VFXSignificanceManager::ComputeScore(50u, 0.0f,    true);
    EXPECT_NEAR(negativeScore, zeroScore, 1e-4f)
        << "Negative distance must be treated as distance=0";
}

TEST(UZ_VFXSignificanceManager_ComputeScore, LargeDistance_ScoreApproachesZero) {
    // At 10000m: factor = 1/(1+100) = ~0.0099
    // score = 1 * 0.0099 * 1 = very small but positive
    const float score = VFXSignificanceManager::ComputeScore(1u, 10000.0f, true);
    EXPECT_GT(score, 0.0f)
        << "Score must be positive even at very large distances";
    EXPECT_LT(score, 0.1f)
        << "Score must be very small at 10000m";
}

// =============================================================================
// RankEmitters — sorts descending by score, populates score field
// =============================================================================

TEST(UZ_VFXSignificanceManager_RankEmitters, EmptyVector_DoesNotCrash) {
    VFXSignificanceManager sig;
    std::vector<VFXEmitterSignificance> emitters;
    sig.RankEmitters(emitters);
    EXPECT_TRUE(emitters.empty());
}

TEST(UZ_VFXSignificanceManager_RankEmitters, SingleEmitter_ScoreIsPopulated) {
    VFXSignificanceManager sig;
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 100u),
    };
    sig.RankEmitters(emitters);
    EXPECT_NEAR(emitters[0].score, 50.0f, 1e-3f)
        << "Score must be populated by RankEmitters";
}

TEST(UZ_VFXSignificanceManager_RankEmitters, TwoEmitters_HigherScoreFirst) {
    VFXSignificanceManager sig;
    // Emitter B has higher priority, should end up at index 0.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 10u,  0.0f, true, 100u), // low priority
        MakeEmitter(2u, 100u, 0.0f, true, 100u), // high priority
    };
    sig.RankEmitters(emitters);
    EXPECT_EQ(emitters[0].emitterId, 2u)
        << "Higher-priority emitter must be ranked first";
    EXPECT_EQ(emitters[1].emitterId, 1u);
}

TEST(UZ_VFXSignificanceManager_RankEmitters, ThreeEmitters_SortedDescending) {
    VFXSignificanceManager sig;
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 1u,   0.0f, true, 10u),
        MakeEmitter(2u, 100u, 0.0f, true, 10u),
        MakeEmitter(3u, 50u,  0.0f, true, 10u),
    };
    sig.RankEmitters(emitters);
    // Expected order by score descending: id=2 (score=100), id=3 (50), id=1 (1).
    EXPECT_EQ(emitters[0].emitterId, 2u);
    EXPECT_EQ(emitters[1].emitterId, 3u);
    EXPECT_EQ(emitters[2].emitterId, 1u);
}

TEST(UZ_VFXSignificanceManager_RankEmitters, VisibilityFactorAffectsOrder) {
    VFXSignificanceManager sig;
    // Emitter A: priority=50, visible. Emitter B: priority=100, not visible.
    // Score A = 50 * 1.0 = 50. Score B = 100 * 0.1 = 10. A should rank first.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u,  0.0f, true,  10u), // visible
        MakeEmitter(2u, 100u, 0.0f, false, 10u), // not visible
    };
    sig.RankEmitters(emitters);
    EXPECT_EQ(emitters[0].emitterId, 1u)
        << "Visible emitter with score=50 must outrank invisible emitter with score=10";
}

// =============================================================================
// EnforceBudget — under budget (no degradation)
// =============================================================================

TEST(UZ_VFXSignificanceManager_EnforceBudget, EmptyVector_DoesNotCrash) {
    VFXSignificanceManager sig;
    std::vector<VFXEmitterSignificance> emitters;
    sig.EnforceBudget(emitters, 10000u);
    EXPECT_TRUE(emitters.empty());
}

TEST(UZ_VFXSignificanceManager_EnforceBudget, TotalUnderBudget_NoChanges) {
    VFXSignificanceManager sig;
    // Total demand = 200+300 = 500, budget = 1000 → no degradation.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 200u),
        MakeEmitter(2u, 80u, 0.0f, true, 300u),
    };
    sig.EnforceBudget(emitters, 1000u);
    for (const auto& e : emitters) {
        EXPECT_EQ(e.appliedLod, VFXLodLevel::Full)
            << "No emitter should be degraded when total demand is under budget";
    }
}

TEST(UZ_VFXSignificanceManager_EnforceBudget, ExactlyAtBudget_NoChanges) {
    VFXSignificanceManager sig;
    // demand = 500, budget = 500 → exactly at budget, no degradation needed.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 500u),
    };
    sig.EnforceBudget(emitters, 500u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Full);
}

// =============================================================================
// EnforceBudget — over budget (lowest significance culled first)
// =============================================================================

TEST(UZ_VFXSignificanceManager_EnforceBudget, OverBudget_LowestSignificanceDegradedFirst) {
    VFXSignificanceManager sig;
    // High-significance: priority=100, demand=400.
    // Low-significance: priority=1, demand=400.
    // Budget: 500. Total: 800 → over by 300.
    // Degrading low-significance first:
    //   Full→Reduced: demand becomes ceil(400*0.5)=200. Total=600, still over.
    //   Reduced→Minimal: demand becomes ceil(400*0.25)=100. Total=500, fits.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 100u, 0.0f, true, 400u), // high significance
        MakeEmitter(2u, 1u,   0.0f, true, 400u), // low significance
    };
    sig.EnforceBudget(emitters, 500u);

    // The high-significance emitter (id=1) must remain at Full.
    // After RankEmitters, id=1 is index 0 (highest score).
    const auto& high = emitters[0];
    EXPECT_EQ(high.appliedLod, VFXLodLevel::Full)
        << "High-significance emitter must stay at Full when budget allows";

    // The low-significance emitter (id=2) must be degraded to at least Minimal.
    const auto& low = emitters[1];
    EXPECT_GE(static_cast<uint8_t>(low.appliedLod),
              static_cast<uint8_t>(VFXLodLevel::Minimal))
        << "Low-significance emitter must be degraded first";
}

TEST(UZ_VFXSignificanceManager_EnforceBudget, ProgressiveDegradation_FullToReduced) {
    VFXSignificanceManager sig;
    // One emitter, demand=1000, budget=600.
    // Full: 1000 > 600. Degrade to Reduced: ceil(1000*0.5)=500 ≤ 600. Stop.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 1000u),
    };
    sig.EnforceBudget(emitters, 600u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Reduced)
        << "Single emitter must be degraded to Reduced when that fits in budget";
}

TEST(UZ_VFXSignificanceManager_EnforceBudget, ProgressiveDegradation_FullToMinimal) {
    VFXSignificanceManager sig;
    // demand=1000, budget=300.
    // Full: 1000 > 300. Reduced: ceil(500) > 300. Minimal: ceil(250) ≤ 300. Stop.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 1000u),
    };
    sig.EnforceBudget(emitters, 300u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Minimal);
}

TEST(UZ_VFXSignificanceManager_EnforceBudget, ProgressiveDegradation_FullToCulled) {
    VFXSignificanceManager sig;
    // demand=1000, budget=100.
    // Minimal: ceil(250) > 100. Culled: 0 ≤ 100. Stop.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 1000u),
    };
    sig.EnforceBudget(emitters, 100u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Culled);
}

// =============================================================================
// EnforceBudget — zero budget (everything culled)
// =============================================================================

TEST(UZ_VFXSignificanceManager_EnforceBudget, ZeroBudget_AllEmittersCulled) {
    VFXSignificanceManager sig;
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 100u, 0.0f, true, 500u),
        MakeEmitter(2u, 50u,  0.0f, true, 300u),
        MakeEmitter(3u, 10u,  0.0f, true, 100u),
    };
    sig.EnforceBudget(emitters, 0u);
    for (const auto& e : emitters) {
        EXPECT_EQ(e.appliedLod, VFXLodLevel::Culled)
            << "Zero budget must cull every emitter (id=" << e.emitterId << ")";
    }
}

// =============================================================================
// EnforceBudget — single emitter
// =============================================================================

TEST(UZ_VFXSignificanceManager_EnforceBudget, SingleEmitter_UnderBudget_RemainsAtFull) {
    VFXSignificanceManager sig;
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 200u),
    };
    sig.EnforceBudget(emitters, 500u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Full);
}

TEST(UZ_VFXSignificanceManager_EnforceBudget, SingleEmitter_OverBudget_Degraded) {
    VFXSignificanceManager sig;
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 1000u),
    };
    sig.EnforceBudget(emitters, 0u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Culled);
}

// =============================================================================
// EnforceBudget — resets appliedLod to Full before enforcement
// =============================================================================

TEST(UZ_VFXSignificanceManager_EnforceBudget, PreExistingLodIsReset) {
    VFXSignificanceManager sig;
    // Set appliedLod to Culled before call; EnforceBudget must reset to Full first.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 100u),
    };
    emitters[0].appliedLod = VFXLodLevel::Culled;
    // Budget = 1000 → demand 100 fits at Full.
    sig.EnforceBudget(emitters, 1000u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Full)
        << "EnforceBudget must reset appliedLod to Full before enforcement";
}

// =============================================================================
// EnforceBudget — high-significance emitters preserved at Full
// =============================================================================

TEST(UZ_VFXSignificanceManager_EnforceBudget, MultipleEmitters_HighSignificancePreservedAtFull) {
    VFXSignificanceManager sig;
    // 3 emitters: high (priority=100, demand=100), medium (50, 100), low (10, 800).
    // Budget = 300. Total = 1000. Need to shed 700.
    // After ranking: high(score=100), medium(50), low(10).
    // Degrade low first: Full(800)→Reduced(400): total=600>300.
    //                   Reduced→Minimal(200): total=400>300.
    //                   Minimal→Culled(0): total=200≤300. Stop.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 100u, 0.0f, true, 100u),
        MakeEmitter(2u, 50u,  0.0f, true, 100u),
        MakeEmitter(3u, 10u,  0.0f, true, 800u),
    };
    sig.EnforceBudget(emitters, 300u);

    // Find each emitter by id (vector is sorted by score after EnforceBudget).
    VFXLodLevel highLod   = VFXLodLevel::Culled;
    VFXLodLevel medLod    = VFXLodLevel::Culled;
    VFXLodLevel lowLod    = VFXLodLevel::Full;
    for (const auto& e : emitters) {
        if (e.emitterId == 1u) { highLod = e.appliedLod; }
        if (e.emitterId == 2u) { medLod  = e.appliedLod; }
        if (e.emitterId == 3u) { lowLod  = e.appliedLod; }
    }

    EXPECT_EQ(highLod, VFXLodLevel::Full)
        << "High-significance emitter must remain at Full (demand=100 fits in 300 budget)";
    EXPECT_EQ(medLod, VFXLodLevel::Full)
        << "Medium emitter must also remain at Full (total high+med=200 ≤ 300)";
    EXPECT_EQ(lowLod, VFXLodLevel::Culled)
        << "Low-significance emitter must be culled to satisfy the budget";
}

TEST(UZ_VFXSignificanceManager_EnforceBudget, ZeroDemandEmitter_IsIgnored) {
    VFXSignificanceManager sig;
    // An emitter with particleDemand=0 contributes nothing and should stay Full.
    std::vector<VFXEmitterSignificance> emitters = {
        MakeEmitter(1u, 50u, 0.0f, true, 0u),
    };
    sig.EnforceBudget(emitters, 100u);
    EXPECT_EQ(emitters[0].appliedLod, VFXLodLevel::Full)
        << "Zero-demand emitter must not be degraded (costs nothing)";
}
