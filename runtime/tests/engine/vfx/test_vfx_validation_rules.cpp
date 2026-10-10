/// Unit tests for phase 8.9.0 — VFX validator rule engine + baseline rules.
///
/// Each baseline rule is tested against a crafted bad input (triggers) and
/// a clean input (passes). Also validates JUnit XML output structure.
///
/// @ref engine/vfx/asset/vfx_validation_rules.h

#include <gtest/gtest.h>

#include "engine/vfx/asset/vfx_validation_rules.h"
#include "engine/vfx/asset/vfx_graph.h"
#include "engine/vfx/system/vfx_system.h"
#include "engine/vfx/modules/module_registry.h"

using namespace uz;

namespace {

/// Loads a minimal VFXGraph from JSON using an empty registry (no modules).
std::unique_ptr<VFXGraph> MakeGraph(const char* json) {
    ModuleRegistry reg;
    return VFXGraph::LoadFromString(json, reg);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Report basics
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxValidationReport, EmptyReportPasses) {
    VfxValidationReport r;
    EXPECT_TRUE(r.Passed());
    EXPECT_EQ(r.ErrorCount(), 0u);
    EXPECT_EQ(r.TotalCount(), 0u);
}

TEST(VfxValidationReport, AddErrorFailsReport) {
    VfxValidationReport r;
    r.Add(VfxValidationSeverity::Error, "test", "bad");
    EXPECT_FALSE(r.Passed());
    EXPECT_EQ(r.ErrorCount(), 1u);
}

TEST(VfxValidationReport, AddWarningDoesNotFailReport) {
    VfxValidationReport r;
    r.Add(VfxValidationSeverity::Warning, "test", "meh");
    EXPECT_TRUE(r.Passed());
    EXPECT_EQ(r.WarningCount(), 1u);
}

TEST(VfxValidationReport, ClearResetsCounters) {
    VfxValidationReport r;
    r.Add(VfxValidationSeverity::Error, "test", "x");
    r.Add(VfxValidationSeverity::Warning, "test", "y");
    r.Clear();
    EXPECT_TRUE(r.Passed());
    EXPECT_EQ(r.TotalCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// JUnit XML
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxValidationReport, JUnitXmlContainsFailureForError) {
    VfxValidationReport r;
    r.Add(VfxValidationSeverity::Error, "BudgetViolation", "too many",
          "fire.uzvfxsys");
    const std::string xml = r.ToJUnitXml("MyCI");
    EXPECT_NE(xml.find("<testsuite name=\"MyCI\""), std::string::npos);
    EXPECT_NE(xml.find("<failure message=\"too many\""), std::string::npos);
    EXPECT_NE(xml.find("BudgetViolation"), std::string::npos);
}

TEST(VfxValidationReport, JUnitXmlWarningUsesSystemOut) {
    VfxValidationReport r;
    r.Add(VfxValidationSeverity::Warning, "EmptyModuleStack", "no modules");
    const std::string xml = r.ToJUnitXml();
    EXPECT_NE(xml.find("<system-out>no modules</system-out>"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Rule: BudgetViolation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxRule_BudgetViolation, TriggersOnExcessiveBudget) {
    auto graph = MakeGraph(R"({
        "format": "uzvfx", "emitter": { "max_particles": 200000 },
        "modules": [{"type":"Spawn"}]
    })");
    ASSERT_NE(graph, nullptr);

    VfxValidationContext ctx;
    ctx.graph = graph.get();
    ctx.maxParticleBudget = 100000u;

    VfxValidationReport r;
    VfxRule_BudgetViolation{}.Check(ctx, r);
    EXPECT_FALSE(r.Passed());
}

TEST(VfxRule_BudgetViolation, PassesUnderBudget) {
    auto graph = MakeGraph(R"({
        "format": "uzvfx", "emitter": { "max_particles": 5000 },
        "modules": [{"type":"Spawn"}]
    })");
    ASSERT_NE(graph, nullptr);

    VfxValidationContext ctx;
    ctx.graph = graph.get();
    ctx.maxParticleBudget = 100000u;

    VfxValidationReport r;
    VfxRule_BudgetViolation{}.Check(ctx, r);
    EXPECT_TRUE(r.Passed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Rule: EmptyModuleStack
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxRule_EmptyModuleStack, WarnsOnNoModules) {
    auto graph = MakeGraph(R"({ "format": "uzvfx" })");
    ASSERT_NE(graph, nullptr);

    VfxValidationContext ctx;
    ctx.graph = graph.get();

    VfxValidationReport r;
    VfxRule_EmptyModuleStack{}.Check(ctx, r);
    EXPECT_TRUE(r.Passed()); // warning, not error
    EXPECT_EQ(r.WarningCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Rule: NegativeLifespan
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxRule_NegativeLifespan, TriggersOnZeroLifespan) {
    auto graph = MakeGraph(R"({
        "format": "uzvfx", "emitter": { "lifespan_default": 0.0 }
    })");
    ASSERT_NE(graph, nullptr);

    VfxValidationContext ctx;
    ctx.graph = graph.get();

    VfxValidationReport r;
    VfxRule_NegativeLifespan{}.Check(ctx, r);
    EXPECT_FALSE(r.Passed());
}

TEST(VfxRule_NegativeLifespan, PassesOnPositiveLifespan) {
    auto graph = MakeGraph(R"({
        "format": "uzvfx", "emitter": { "lifespan_default": 5.0 }
    })");
    ASSERT_NE(graph, nullptr);

    VfxValidationContext ctx;
    ctx.graph = graph.get();

    VfxValidationReport r;
    VfxRule_NegativeLifespan{}.Check(ctx, r);
    EXPECT_TRUE(r.Passed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Rule: DuplicateParameters
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxRule_DuplicateParameters, TriggersOnDuplicate) {
    auto graph = MakeGraph(R"({
        "format": "uzvfx",
        "parameters": [
            { "name": "intensity", "type": "float", "default": 1.0 },
            { "name": "intensity", "type": "float", "default": 2.0 }
        ]
    })");
    ASSERT_NE(graph, nullptr);

    VfxValidationContext ctx;
    ctx.graph = graph.get();

    VfxValidationReport r;
    VfxRule_DuplicateParameters{}.Check(ctx, r);
    EXPECT_FALSE(r.Passed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Rule: UnresolvedPresetRef
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxRule_UnresolvedPresetRef, TriggersOnUnknownPreset) {
    VfxSystem sys;
    sys.name = "TestSystem";
    VfxSystemEmitterRef ref;
    ref.name = "head";
    ref.emitterPath = "preset:MissingPreset";
    ref.enabled = true;
    sys.emitters.push_back(ref);

    VfxValidationContext ctx;
    ctx.system = &sys;
    ctx.knownPresets = {"FireballHead", "SmokeTrail"};

    VfxValidationReport r;
    VfxRule_UnresolvedPresetRef{}.Check(ctx, r);
    EXPECT_FALSE(r.Passed());
}

TEST(VfxRule_UnresolvedPresetRef, PassesOnKnownPreset) {
    VfxSystem sys;
    VfxSystemEmitterRef ref;
    ref.name = "head";
    ref.emitterPath = "preset:FireballHead";
    ref.enabled = true;
    sys.emitters.push_back(ref);

    VfxValidationContext ctx;
    ctx.system = &sys;
    ctx.knownPresets = {"FireballHead"};

    VfxValidationReport r;
    VfxRule_UnresolvedPresetRef{}.Check(ctx, r);
    EXPECT_TRUE(r.Passed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Rule: SubEmitterRecursion
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxRule_SubEmitterRecursion, TriggersOnSelfRef) {
    VfxSystem sys;
    VfxSystemEmitterRef ref;
    ref.name = "self";
    ref.emitterPath = "fire.uzvfxsys";
    ref.enabled = true;
    sys.emitters.push_back(ref);

    VfxValidationContext ctx;
    ctx.system = &sys;
    ctx.assetPath = "fire.uzvfxsys";

    VfxValidationReport r;
    VfxRule_SubEmitterRecursion{}.Check(ctx, r);
    EXPECT_FALSE(r.Passed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Default rule set factory
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxValidationRules, DefaultSetHasEightRules) {
    auto rules = VfxGetDefaultValidationRules();
    EXPECT_EQ(rules.size(), 8u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Convenience VfxValidateAsset
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxValidateAsset, CleanGraphPassesAllRules) {
    auto graph = MakeGraph(R"({
        "format": "uzvfx",
        "emitter": { "max_particles": 1000, "lifespan_default": 5.0 },
        "modules": [{"type":"Spawn"}],
        "parameters": [{ "name": "intensity", "type": "float", "default": 1.0 }],
        "events": [{ "name": "burst", "emission_count": 50 }]
    })");
    ASSERT_NE(graph, nullptr);

    VfxValidationContext ctx;
    ctx.graph = graph.get();
    auto report = VfxValidateAsset(ctx);
    EXPECT_TRUE(report.Passed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Severity name helper
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxValidationSeverity, NamesAreStable) {
    EXPECT_STREQ(VfxValidationSeverityName(VfxValidationSeverity::Info),    "info");
    EXPECT_STREQ(VfxValidationSeverityName(VfxValidationSeverity::Warning), "warning");
    EXPECT_STREQ(VfxValidationSeverityName(VfxValidationSeverity::Error),   "error");
}
