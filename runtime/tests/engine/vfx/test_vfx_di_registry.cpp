/// Unit tests for phase 10.7.0 — VFX Data Interface registry.
///
/// @ref engine/vfx/modules/vfx_di_registry.h

#include <gtest/gtest.h>

#include "engine/vfx/modules/vfx_di_registry.h"

#include <algorithm>

using namespace uz;

namespace {

/// Concrete test DI that returns fixed HLSL snippets.
class TestDI : public IVfxDataInterface {
public:
    explicit TestDI(std::string name) : name_(std::move(name)) {}

    const char* TypeName() const noexcept override { return name_.c_str(); }

    std::string EmitCBFields() const override {
        return "float " + name_ + "_field;";
    }
    std::string EmitHelperFunctions() const override {
        return "float Get" + name_ + "() { return " + name_ + "_field; }";
    }
    std::string EmitCallSite() const override {
        return "value += Get" + name_ + "();";
    }

private:
    std::string name_;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Basic operations
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDIRegistry, StartsEmpty) {
    VfxDIRegistry reg;
    EXPECT_EQ(reg.Count(), 0u);
    EXPECT_EQ(reg.Find("Advection"), nullptr);
}

TEST(VfxDIRegistry, RegisterAndFind) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Advection"));
    EXPECT_EQ(reg.Count(), 1u);
    auto* found = reg.Find("Advection");
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->TypeName(), "Advection");
}

TEST(VfxDIRegistry, FindUnknownReturnsNull) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Advection"));
    EXPECT_EQ(reg.Find("SceneSDF"), nullptr);
}

TEST(VfxDIRegistry, RegisterOverwrites) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Advection"));
    reg.Register(std::make_unique<TestDI>("Advection")); // overwrite
    EXPECT_EQ(reg.Count(), 1u);
}

TEST(VfxDIRegistry, NullIgnored) {
    VfxDIRegistry reg;
    reg.Register(nullptr);
    EXPECT_EQ(reg.Count(), 0u);
}

TEST(VfxDIRegistry, Clear) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("A"));
    reg.Register(std::make_unique<TestDI>("B"));
    ASSERT_EQ(reg.Count(), 2u);
    reg.Clear();
    EXPECT_EQ(reg.Count(), 0u);
}

TEST(VfxDIRegistry, RegisteredNames) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Alpha"));
    reg.Register(std::make_unique<TestDI>("Beta"));
    auto names = reg.RegisteredNames();
    EXPECT_EQ(names.size(), 2u);
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names[0], "Alpha");
    EXPECT_EQ(names[1], "Beta");
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader composition
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDIRegistry, ComposeForEmitter_SingleDI) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Wind"));

    auto result = reg.ComposeForEmitter({"Wind"});
    EXPECT_NE(result.cbFields.find("Wind_field"), std::string::npos);
    EXPECT_NE(result.helperFunctions.find("GetWind"), std::string::npos);
    EXPECT_NE(result.callSite.find("GetWind()"), std::string::npos);
}

TEST(VfxDIRegistry, ComposeForEmitter_MultipleDIs) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Wind"));
    reg.Register(std::make_unique<TestDI>("Gravity"));

    auto result = reg.ComposeForEmitter({"Wind", "Gravity"});
    EXPECT_NE(result.cbFields.find("Wind_field"), std::string::npos);
    EXPECT_NE(result.cbFields.find("Gravity_field"), std::string::npos);
    EXPECT_NE(result.callSite.find("GetWind()"), std::string::npos);
    EXPECT_NE(result.callSite.find("GetGravity()"), std::string::npos);
}

TEST(VfxDIRegistry, ComposeForEmitter_UnknownDISkipped) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Wind"));

    auto result = reg.ComposeForEmitter({"Wind", "Unknown"});
    EXPECT_NE(result.cbFields.find("Wind_field"), std::string::npos);
    EXPECT_EQ(result.cbFields.find("Unknown_field"), std::string::npos);
}

TEST(VfxDIRegistry, ComposeForEmitter_EmptyList) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<TestDI>("Wind"));

    auto result = reg.ComposeForEmitter({});
    EXPECT_TRUE(result.cbFields.empty());
    EXPECT_TRUE(result.helperFunctions.empty());
    EXPECT_TRUE(result.callSite.empty());
}
