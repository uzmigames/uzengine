/// Unit tests for phase 7.6.0 - GPU simulation stage declaration + dependency
/// inference.
///
/// Covers:
///   - Empty list is "implicit single stage" (default compat).
///   - JSON round-trip preserves name / cs path / iterations / reads / writes.
///   - JSON accepts bare array or object form.
///   - Malformed entries are skipped rather than aborting the whole load.
///   - iterations = 0 clamps to 1 (safety).
///   - Empty name / empty cs path entries skipped.
///   - FindReadersOf detects read-after-write dependencies.
///   - Unrelated stages are not flagged as dependents.
///
/// @ref phase7_7.6.0-vfx-gpu-simulation-stages

#include <gtest/gtest.h>

#include "engine/vfx/system/vfx_simulation_stage.h"

using namespace uz;

// ─── Implicit-single-stage shortcut ────────────────────────────────────────

TEST(VfxSimulationStageList, EmptyListIsImplicitSingleStage) {
    VfxSimulationStageList list;
    EXPECT_TRUE(list.IsImplicitSingleStage());
}

// ─── JSON round-trip ───────────────────────────────────────────────────────

TEST(VfxSimulationStageAsset, JsonRoundTripPreservesEveryField) {
    const char* json = R"([
        {
            "name": "Curl",
            "cs":   "/engine/vfx/curl_field_cs.hlsl",
            "iterations": 1,
            "reads":  ["positions", "velocities"],
            "writes": ["velocityDelta"]
        },
        {
            "name": "Advect",
            "cs":   "/engine/vfx/advect_cs.hlsl",
            "iterations": 2,
            "reads":  ["velocityDelta"],
            "writes": ["positions"]
        }
    ])";

    VfxSimulationStageList list;
    ASSERT_TRUE(VfxSimulationStageAsset::LoadFromJson(json, list));
    ASSERT_EQ(list.stages.size(), 2u);

    EXPECT_EQ(list.stages[0].name, "Curl");
    EXPECT_EQ(list.stages[0].csPath, "/engine/vfx/curl_field_cs.hlsl");
    EXPECT_EQ(list.stages[0].iterationCount, 1u);
    ASSERT_EQ(list.stages[0].readSet.size(), 2u);
    EXPECT_EQ(list.stages[0].readSet[0], "positions");
    EXPECT_EQ(list.stages[0].readSet[1], "velocities");
    ASSERT_EQ(list.stages[0].writeSet.size(), 1u);
    EXPECT_EQ(list.stages[0].writeSet[0], "velocityDelta");

    EXPECT_EQ(list.stages[1].iterationCount, 2u);

    // Re-serialise and parse again to verify idempotency.
    const std::string roundTripped = VfxSimulationStageAsset::SaveToJson(list);
    VfxSimulationStageList reloaded;
    ASSERT_TRUE(VfxSimulationStageAsset::LoadFromJson(roundTripped, reloaded));
    ASSERT_EQ(reloaded.stages.size(), 2u);
    EXPECT_EQ(reloaded.stages[0].name, list.stages[0].name);
    EXPECT_EQ(reloaded.stages[1].csPath, list.stages[1].csPath);
}

TEST(VfxSimulationStageAsset, AcceptsObjectWithStagesArrayForm) {
    const char* json = R"({
        "stages": [
            { "name": "Foo", "cs": "/engine/vfx/foo.hlsl" }
        ]
    })";
    VfxSimulationStageList list;
    ASSERT_TRUE(VfxSimulationStageAsset::LoadFromJson(json, list));
    ASSERT_EQ(list.stages.size(), 1u);
    EXPECT_EQ(list.stages[0].name, "Foo");
    EXPECT_EQ(list.stages[0].iterationCount, 1u);  // default when missing
}

TEST(VfxSimulationStageAsset, RejectsMalformedRoot) {
    VfxSimulationStageList list;
    EXPECT_FALSE(VfxSimulationStageAsset::LoadFromJson("not json", list));
    // Bare scalar (not array, not object) — should also fail.
    EXPECT_FALSE(VfxSimulationStageAsset::LoadFromJson("42", list));
}

// ─── Defensive paths ───────────────────────────────────────────────────────

TEST(VfxSimulationStageAsset, SkipsEntriesWithoutName) {
    const char* json = R"([
        { "name": "", "cs": "/engine/vfx/a.hlsl" },
        { "name": "valid", "cs": "/engine/vfx/b.hlsl" }
    ])";
    VfxSimulationStageList list;
    ASSERT_TRUE(VfxSimulationStageAsset::LoadFromJson(json, list));
    ASSERT_EQ(list.stages.size(), 1u);
    EXPECT_EQ(list.stages[0].name, "valid");
}

TEST(VfxSimulationStageAsset, SkipsEntriesWithoutCsPath) {
    const char* json = R"([
        { "name": "noshader" },
        { "name": "ok", "cs": "/engine/vfx/b.hlsl" }
    ])";
    VfxSimulationStageList list;
    ASSERT_TRUE(VfxSimulationStageAsset::LoadFromJson(json, list));
    ASSERT_EQ(list.stages.size(), 1u);
    EXPECT_EQ(list.stages[0].name, "ok");
}

TEST(VfxSimulationStageAsset, IterationCountZeroClampsToOne) {
    const char* json = R"([
        { "name": "Zero", "cs": "/engine/vfx/z.hlsl", "iterations": 0 }
    ])";
    VfxSimulationStageList list;
    ASSERT_TRUE(VfxSimulationStageAsset::LoadFromJson(json, list));
    ASSERT_EQ(list.stages.size(), 1u);
    EXPECT_EQ(list.stages[0].iterationCount, 1u);
}

// ─── FindReadersOf dependency inference ───────────────────────────────────

TEST(VfxSimulationStageList, FindReadersOfDetectsReadAfterWrite) {
    VfxSimulationStageList list;
    VfxSimulationStage a; a.name = "A"; a.csPath = "/a";
    a.writeSet = {"velocityDelta"};
    VfxSimulationStage b; b.name = "B"; b.csPath = "/b";
    b.readSet  = {"velocityDelta"};
    b.writeSet = {"positions"};
    VfxSimulationStage c; c.name = "C"; c.csPath = "/c";
    c.readSet  = {"positions"};
    list.stages = {a, b, c};

    const auto readersOfA = list.FindReadersOf(0);
    ASSERT_EQ(readersOfA.size(), 1u);
    EXPECT_EQ(readersOfA[0], "B");

    const auto readersOfB = list.FindReadersOf(1);
    ASSERT_EQ(readersOfB.size(), 1u);
    EXPECT_EQ(readersOfB[0], "C");

    const auto readersOfC = list.FindReadersOf(2);
    EXPECT_TRUE(readersOfC.empty());
}

TEST(VfxSimulationStageList, FindReadersOfIgnoresIndependentStages) {
    VfxSimulationStageList list;
    VfxSimulationStage a; a.name = "A"; a.csPath = "/a"; a.writeSet = {"pressure"};
    VfxSimulationStage b; b.name = "B"; b.csPath = "/b"; b.writeSet = {"density"};
    list.stages = {a, b};

    // B reads nothing A writes — no dependency.
    EXPECT_TRUE(list.FindReadersOf(0).empty());
}

TEST(VfxSimulationStageList, FindReadersOfOutOfRangeIndexReturnsEmpty) {
    VfxSimulationStageList list;
    VfxSimulationStage a; a.name = "A"; a.csPath = "/a"; a.writeSet = {"x"};
    list.stages = {a};
    EXPECT_TRUE(list.FindReadersOf(999).empty());
}

TEST(VfxSimulationStageList, FindReadersOfStageWithEmptyWriteSetReturnsEmpty) {
    VfxSimulationStageList list;
    VfxSimulationStage a; a.name = "A"; a.csPath = "/a";
    VfxSimulationStage b; b.name = "B"; b.csPath = "/b"; b.readSet = {"x"};
    list.stages = {a, b};
    EXPECT_TRUE(list.FindReadersOf(0).empty());
}
