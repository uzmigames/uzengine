/// Unit tests for phase 8.5.0 — VFX debug HUD per-module breakdown.
///
/// Validates the `ReportModule` entry point and the `Aggregate` merge that
/// attaches sorted, percent-annotated module entries to each row:
///   - empty module samples → empty breakdowns.
///   - matching samples attach to the right row, sorted by gpuMs desc.
///   - duplicate (emitter, module, stage) accumulate.
///   - stage isolation: same name in Spawn vs Update stays distinct.
///   - orphan samples (no matching emitter row) are discarded.
///   - percentOfEmitterTotal uses row total, zero-guarded.
///   - Aggregate drains the module buffer.
///   - Clear() drops both buffers.
///
/// @ref phase8_8.5.0-vfx-module-profiler-panel

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_debug_hud.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Reporting
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDebugHudModules, ReportAppendsOneSample) {
    VfxDebugHudCollector c;
    EXPECT_EQ(c.PendingModuleSampleCount(), 0u);

    c.ReportModule("fire", "Gravity", VfxDebugHudStage::Update, 0.5f);
    EXPECT_EQ(c.PendingModuleSampleCount(), 1u);
}

TEST(VfxDebugHudModules, ClearDropsBothBuffers) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire", 1, 1, 1.0f, 0.0f, 0, false);
    c.ReportModule("fire", "Gravity", VfxDebugHudStage::Update, 0.5f);
    EXPECT_EQ(c.PendingSampleCount(),       1u);
    EXPECT_EQ(c.PendingModuleSampleCount(), 1u);

    c.Clear();
    EXPECT_EQ(c.PendingSampleCount(),       0u);
    EXPECT_EQ(c.PendingModuleSampleCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Aggregation attaches breakdown
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDebugHudModules, Aggregate_AttachesSortedBreakdownToRow) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire", 1000, 2000, /*simMs=*/1.0f, /*rendMs=*/1.0f,
                     /*lod=*/0, /*culled=*/false);
    c.ReportModule("fire", "Gravity",       VfxDebugHudStage::Update, 0.40f);
    c.ReportModule("fire", "ColorOverLife", VfxDebugHudStage::Update, 1.20f);
    c.ReportModule("fire", "Billboard",     VfxDebugHudStage::Render, 0.60f);

    const auto view = c.Aggregate(/*topN=*/0);
    ASSERT_EQ(view.rows.size(), 1u);
    const auto& bd = view.rows[0].moduleBreakdown;
    ASSERT_EQ(bd.entries.size(), 3u);

    // Sorted by gpuMs desc.
    EXPECT_EQ(bd.entries[0].moduleName, "ColorOverLife");
    EXPECT_EQ(bd.entries[1].moduleName, "Billboard");
    EXPECT_EQ(bd.entries[2].moduleName, "Gravity");
    EXPECT_FLOAT_EQ(bd.entries[0].gpuMs, 1.20f);

    // Stage preserved.
    EXPECT_EQ(bd.entries[1].stage, VfxDebugHudStage::Render);

    // totalMs is the sum.
    EXPECT_FLOAT_EQ(bd.totalMs, 2.20f);
}

TEST(VfxDebugHudModules, Aggregate_AccumulatesDuplicateModuleSamples) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire", 1, 1, 0.5f, 0.0f, 0, false);
    // Same (emitter, module, stage) dispatched twice this frame.
    c.ReportModule("fire", "Gravity", VfxDebugHudStage::Update, 0.20f);
    c.ReportModule("fire", "Gravity", VfxDebugHudStage::Update, 0.30f);

    const auto view = c.Aggregate();
    ASSERT_EQ(view.rows.size(), 1u);
    ASSERT_EQ(view.rows[0].moduleBreakdown.entries.size(), 1u);
    EXPECT_FLOAT_EQ(view.rows[0].moduleBreakdown.entries[0].gpuMs, 0.50f);
}

TEST(VfxDebugHudModules, Aggregate_StageIsolatesIdenticalModuleName) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire", 1, 1, 0.2f, 0.2f, 0, false);
    // Same name, different stages — must not accumulate.
    c.ReportModule("fire", "SetAttribute", VfxDebugHudStage::Spawn,  0.10f);
    c.ReportModule("fire", "SetAttribute", VfxDebugHudStage::Update, 0.30f);

    const auto view = c.Aggregate();
    const auto& entries = view.rows[0].moduleBreakdown.entries;
    ASSERT_EQ(entries.size(), 2u);
    // Sorted by gpuMs desc.
    EXPECT_EQ(entries[0].stage, VfxDebugHudStage::Update);
    EXPECT_FLOAT_EQ(entries[0].gpuMs, 0.30f);
    EXPECT_EQ(entries[1].stage, VfxDebugHudStage::Spawn);
    EXPECT_FLOAT_EQ(entries[1].gpuMs, 0.10f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Percent-of-emitter
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDebugHudModules, Aggregate_PercentOfEmitterTotalComputed) {
    VfxDebugHudCollector c;
    // Row total 2.0ms.
    c.ReportEmitter("fire", 1, 1, 1.5f, 0.5f, 0, false);
    c.ReportModule("fire", "Gravity",   VfxDebugHudStage::Update, 0.5f);
    c.ReportModule("fire", "Billboard", VfxDebugHudStage::Render, 0.5f);

    const auto view = c.Aggregate();
    const auto& entries = view.rows[0].moduleBreakdown.entries;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_FLOAT_EQ(entries[0].percentOfEmitterTotal, 25.0f);
    EXPECT_FLOAT_EQ(entries[1].percentOfEmitterTotal, 25.0f);
}

TEST(VfxDebugHudModules, Aggregate_ZeroRowTotalGivesZeroPercent) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire", 0, 1, 0.0f, 0.0f, 0, true);
    // Culled emitter has 0ms but modules may still report 0ms samples.
    c.ReportModule("fire", "Gravity", VfxDebugHudStage::Update, 0.0f);

    const auto view = c.Aggregate();
    const auto& entries = view.rows[0].moduleBreakdown.entries;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_FLOAT_EQ(entries[0].percentOfEmitterTotal, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Orphan + drain
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDebugHudModules, Aggregate_OrphanSampleDiscarded) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire", 1, 1, 0.1f, 0.0f, 0, false);
    // Sample refers to a non-reported emitter.
    c.ReportModule("smoke", "Gravity", VfxDebugHudStage::Update, 0.5f);

    const auto view = c.Aggregate();
    ASSERT_EQ(view.rows.size(), 1u);
    EXPECT_TRUE(view.rows[0].moduleBreakdown.entries.empty());
}

TEST(VfxDebugHudModules, Aggregate_DrainsModuleBuffer) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire", 1, 1, 0.1f, 0.0f, 0, false);
    c.ReportModule("fire", "Gravity", VfxDebugHudStage::Update, 0.5f);
    (void)c.Aggregate();
    EXPECT_EQ(c.PendingModuleSampleCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-emitter
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDebugHudModules, Aggregate_TwoEmittersGetIndependentBreakdowns) {
    VfxDebugHudCollector c;
    c.ReportEmitter("fire",  1, 1, 1.0f, 0.0f, 0, false);
    c.ReportEmitter("smoke", 1, 1, 0.5f, 0.0f, 0, false);
    c.ReportModule("fire",  "Gravity",   VfxDebugHudStage::Update, 0.5f);
    c.ReportModule("smoke", "Turbulence",VfxDebugHudStage::Update, 0.3f);

    auto view = c.Aggregate(/*topN=*/0);
    ASSERT_EQ(view.rows.size(), 2u);
    // Rows sorted by totalMs desc: fire (1.0) before smoke (0.5).
    EXPECT_EQ(view.rows[0].emitterName, "fire");
    ASSERT_EQ(view.rows[0].moduleBreakdown.entries.size(), 1u);
    EXPECT_EQ(view.rows[0].moduleBreakdown.entries[0].moduleName, "Gravity");
    EXPECT_EQ(view.rows[1].emitterName, "smoke");
    ASSERT_EQ(view.rows[1].moduleBreakdown.entries.size(), 1u);
    EXPECT_EQ(view.rows[1].moduleBreakdown.entries[0].moduleName, "Turbulence");
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage name helper
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDebugHudStageNames, ReturnsStableStrings) {
    EXPECT_STREQ(VfxDebugHudStageName(VfxDebugHudStage::Spawn),  "Spawn");
    EXPECT_STREQ(VfxDebugHudStageName(VfxDebugHudStage::Update), "Update");
    EXPECT_STREQ(VfxDebugHudStageName(VfxDebugHudStage::Render), "Render");
}
