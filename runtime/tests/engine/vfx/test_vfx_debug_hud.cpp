/// Unit tests for phase 7.17.0 - VFX debug HUD collector + aggregator.
///
/// Covers:
///   - ReportEmitter / PendingSampleCount / Clear semantics.
///   - Aggregate sorts rows by totalMs descending.
///   - Top-N flag only set on the N most expensive rows.
///   - Totals reduce correctly across every sample.
///   - Budget percent derived from alive / max with zero-max fallback.
///   - culledCount counts exactly the culled samples.
///   - Aggregate drains the pending buffer.
///   - topNCount = 0 disables highlight but still sorts.
///   - topNCount > rowCount clamps to rowCount (no UB).
///
/// @ref phase7_7.17.0-vfx-debug-hud-per-emitter-stats

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_debug_hud.h"

using namespace uz;

// ─── ReportEmitter / PendingSampleCount ────────────────────────────────────

TEST(VfxDebugHudCollector, StartsEmpty) {
    VfxDebugHudCollector c;
    EXPECT_EQ(c.PendingSampleCount(), 0u);
}

TEST(VfxDebugHudCollector, ReportAppendsOneSample) {
    VfxDebugHudCollector c;
    c.ReportEmitter("A", /*alive=*/ 100, /*max=*/ 500,
                    /*simMs=*/ 0.1f, /*renderMs=*/ 0.2f,
                    /*lod=*/ 0, /*culled=*/ false);
    EXPECT_EQ(c.PendingSampleCount(), 1u);
}

TEST(VfxDebugHudCollector, ClearDropsPending) {
    VfxDebugHudCollector c;
    c.ReportEmitter("A", 0, 0, 0, 0, 0, false);
    c.Clear();
    EXPECT_EQ(c.PendingSampleCount(), 0u);
}

// ─── Aggregate: sort + top-N ───────────────────────────────────────────────

TEST(VfxDebugHudCollector, AggregateSortsByTotalMsDescending) {
    VfxDebugHudCollector c;
    c.ReportEmitter("cheap",    10, 100, 0.1f, 0.1f, 0, false);  // total 0.2
    c.ReportEmitter("expensive", 1000, 2000, 2.5f, 1.0f, 0, false);  // total 3.5
    c.ReportEmitter("medium",    500, 1000, 0.5f, 0.5f, 0, false);  // total 1.0

    const auto view = c.Aggregate(/*topN=*/ 1);
    ASSERT_EQ(view.rows.size(), 3u);
    EXPECT_EQ(view.rows[0].emitterName, "expensive");
    EXPECT_EQ(view.rows[1].emitterName, "medium");
    EXPECT_EQ(view.rows[2].emitterName, "cheap");
}

TEST(VfxDebugHudCollector, TopNFlagOnlyOnMostExpensive) {
    VfxDebugHudCollector c;
    c.ReportEmitter("a", 0, 0, 3.0f, 0.0f, 0, false);
    c.ReportEmitter("b", 0, 0, 2.0f, 0.0f, 0, false);
    c.ReportEmitter("c", 0, 0, 1.0f, 0.0f, 0, false);
    c.ReportEmitter("d", 0, 0, 0.5f, 0.0f, 0, false);

    const auto view = c.Aggregate(/*topN=*/ 2);
    ASSERT_EQ(view.rows.size(), 4u);
    EXPECT_TRUE (view.rows[0].isTopN);   // a
    EXPECT_TRUE (view.rows[1].isTopN);   // b
    EXPECT_FALSE(view.rows[2].isTopN);   // c
    EXPECT_FALSE(view.rows[3].isTopN);   // d
}

TEST(VfxDebugHudCollector, TopNZeroDisablesHighlightButStillSorts) {
    VfxDebugHudCollector c;
    c.ReportEmitter("cheap",     0, 0, 0.1f, 0.0f, 0, false);
    c.ReportEmitter("expensive", 0, 0, 3.0f, 0.0f, 0, false);

    const auto view = c.Aggregate(/*topN=*/ 0);
    ASSERT_EQ(view.rows.size(), 2u);
    EXPECT_EQ(view.rows[0].emitterName, "expensive");
    EXPECT_FALSE(view.rows[0].isTopN);
    EXPECT_FALSE(view.rows[1].isTopN);
}

TEST(VfxDebugHudCollector, TopNLargerThanRowCountClamps) {
    VfxDebugHudCollector c;
    c.ReportEmitter("only", 0, 0, 1.0f, 0.0f, 0, false);
    const auto view = c.Aggregate(/*topN=*/ 99);
    ASSERT_EQ(view.rows.size(), 1u);
    EXPECT_TRUE(view.rows[0].isTopN);
}

// ─── Totals ────────────────────────────────────────────────────────────────

TEST(VfxDebugHudCollector, TotalsReduceAcrossSamples) {
    VfxDebugHudCollector c;
    c.ReportEmitter("a", /*alive=*/ 100, /*max=*/ 500, 1.0f, 0.5f, 0, false);
    c.ReportEmitter("b", /*alive=*/ 200, /*max=*/ 400, 0.5f, 0.5f, 0, false);

    const auto view = c.Aggregate();
    EXPECT_EQ(view.totalAliveParticles, 300u);
    EXPECT_EQ(view.totalBudget,         900u);
    EXPECT_NEAR(view.totalGpuMs, 2.5f, 1e-3f);
}

TEST(VfxDebugHudCollector, CulledCountExactlyMatchesFlaggedSamples) {
    VfxDebugHudCollector c;
    c.ReportEmitter("vis1", 0, 0, 0, 0, 0, false);
    c.ReportEmitter("cull1", 0, 0, 0, 0, 0, true);
    c.ReportEmitter("cull2", 0, 0, 0, 0, 0, true);
    const auto view = c.Aggregate();
    EXPECT_EQ(view.culledCount, 2u);
}

// ─── Derived per-row fields ────────────────────────────────────────────────

TEST(VfxDebugHudRow, BudgetPercentDerivedFromAliveOverMax) {
    VfxDebugHudCollector c;
    c.ReportEmitter("a", /*alive=*/ 250, /*max=*/ 1000, 0, 0, 0, false);
    const auto view = c.Aggregate();
    ASSERT_EQ(view.rows.size(), 1u);
    EXPECT_NEAR(view.rows[0].budgetPercent, 25.0f, 1e-3f);
}

TEST(VfxDebugHudRow, BudgetPercentZeroWhenMaxIsZero) {
    VfxDebugHudCollector c;
    c.ReportEmitter("weird", /*alive=*/ 10, /*max=*/ 0, 0, 0, 0, false);
    const auto view = c.Aggregate();
    ASSERT_EQ(view.rows.size(), 1u);
    EXPECT_FLOAT_EQ(view.rows[0].budgetPercent, 0.0f);
}

TEST(VfxDebugHudRow, TotalMsIsSimPlusRender) {
    VfxDebugHudCollector c;
    c.ReportEmitter("a", 0, 0, 1.25f, 0.75f, 0, false);
    const auto view = c.Aggregate();
    ASSERT_EQ(view.rows.size(), 1u);
    EXPECT_FLOAT_EQ(view.rows[0].totalMs, 2.0f);
}

// ─── Lifecycle ─────────────────────────────────────────────────────────────

TEST(VfxDebugHudCollector, AggregateDrainsPending) {
    VfxDebugHudCollector c;
    c.ReportEmitter("a", 0, 0, 0, 0, 0, false);
    (void)c.Aggregate();
    EXPECT_EQ(c.PendingSampleCount(), 0u);
}
