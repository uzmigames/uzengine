/// Unit tests for phase 9.5.0 editor integration — validator bridge.
///
/// @ref engine/vfx/editor/vfx_validator_editor_bridge.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_validator_editor_bridge.h"

using namespace uz;

namespace {

/// Provider that returns a known-empty context — rules will fire no errors
/// on an empty context, so `Passed()` returns true.
VfxValidationContext EmptyContext(VfxSaveAssetKind, const void*, const std::string&) {
    return VfxValidationContext{};
}

/// Fixture that resets the save pipeline so Register/Unregister don't
/// leak across tests.
struct ValidatorFixture : ::testing::Test {
    void SetUp()    override { EditorSavePipeline::Instance().Clear(); }
    void TearDown() override { EditorSavePipeline::Instance().Clear(); }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Save extension — default state
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ValidatorFixture, SaveExtensionNameStable) {
    VfxValidatorSaveExtension ext;
    EXPECT_STREQ(ext.Name(), "VfxValidator");
}

TEST_F(ValidatorFixture, PreSaveWithoutProviderAllowsSave) {
    VfxValidatorSaveExtension ext;
    const bool ok = ext.PreSave(VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys");
    EXPECT_TRUE(ok);
}

TEST_F(ValidatorFixture, PreSaveWithPassingContextAllowsSave) {
    VfxValidatorSaveExtension ext;
    ext.SetContextProvider(EmptyContext);
    const bool ok = ext.PreSave(VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ext.LastReport().Passed());
}

// ─────────────────────────────────────────────────────────────────────────────
// Bridge — debounced edit tick
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ValidatorFixture, BridgeInitialState) {
    VfxValidatorEditorBridge bridge;
    EXPECT_NE(bridge.SaveExtension(), nullptr);
    EXPECT_EQ(bridge.RunCount(), 0u);
    EXPECT_FLOAT_EQ(bridge.debounceSeconds, 0.5f);
}

TEST_F(ValidatorFixture, TickWithoutEditIsNoOp) {
    VfxValidatorEditorBridge bridge;
    bridge.SetContextProvider(EmptyContext);
    EXPECT_FALSE(bridge.TickDebounced(100.0f));
    EXPECT_EQ(bridge.RunCount(), 0u);
}

TEST_F(ValidatorFixture, TickRunsAfterDebounceInterval) {
    VfxValidatorEditorBridge bridge;
    bridge.SetContextProvider(EmptyContext);
    bridge.OnEdit(0.0f);

    // Not yet elapsed.
    EXPECT_FALSE(bridge.TickDebounced(0.2f));
    EXPECT_EQ(bridge.RunCount(), 0u);

    // Debounce window crossed.
    EXPECT_TRUE(bridge.TickDebounced(0.6f));
    EXPECT_EQ(bridge.RunCount(), 1u);
}

TEST_F(ValidatorFixture, MultipleEditsResetDebounce) {
    VfxValidatorEditorBridge bridge;
    bridge.SetContextProvider(EmptyContext);

    bridge.OnEdit(0.0f);
    bridge.OnEdit(0.3f); // reset
    EXPECT_FALSE(bridge.TickDebounced(0.5f)); // only 0.2s since last edit

    EXPECT_TRUE(bridge.TickDebounced(0.9f)); // 0.6s since last edit
    EXPECT_EQ(bridge.RunCount(), 1u);
}

TEST_F(ValidatorFixture, TickDoesNotReFireWithoutNewEdit) {
    VfxValidatorEditorBridge bridge;
    bridge.SetContextProvider(EmptyContext);
    bridge.OnEdit(0.0f);

    EXPECT_TRUE(bridge.TickDebounced(1.0f));
    EXPECT_EQ(bridge.RunCount(), 1u);

    // No new edit → tick does nothing.
    EXPECT_FALSE(bridge.TickDebounced(2.0f));
    EXPECT_EQ(bridge.RunCount(), 1u);
}

TEST_F(ValidatorFixture, CustomDebounceInterval) {
    VfxValidatorEditorBridge bridge;
    bridge.debounceSeconds = 0.1f;
    bridge.SetContextProvider(EmptyContext);
    bridge.OnEdit(0.0f);

    EXPECT_FALSE(bridge.TickDebounced(0.05f));
    EXPECT_TRUE(bridge.TickDebounced(0.15f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration with EditorSavePipeline
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ValidatorFixture, PipelineRegisterDoesNotBlockEmptyContext) {
    VfxValidatorEditorBridge bridge;
    bridge.SetContextProvider(EmptyContext);
    EditorSavePipeline::Instance().Register(bridge.SaveExtension());

    bool saveFn = false;
    const bool ok = VfxSaveWithPipeline(
        VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys",
        [&]() { saveFn = true; return true; });

    EXPECT_TRUE(ok);
    EXPECT_TRUE(saveFn);
}
