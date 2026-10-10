/// Unit tests for phase 10.11.0 — editor save-pipeline hook.
///
/// @ref engine/vfx/editor/vfx_editor_save_pipeline.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_save_pipeline.h"

using namespace uz;

namespace {

/// Extension that records every call + optionally aborts PreSave.
struct RecordingExt : IEditorSaveExtension {
    uint32_t preSaveCalls  = 0;
    uint32_t postSaveCalls = 0;
    VfxSaveAssetKind lastKind = VfxSaveAssetKind::Unknown;
    std::string lastPath;
    bool abortPreSave = false;
    const char* name = "RecordingExt";

    bool PreSave(VfxSaveAssetKind kind,
                  const void* /*asset*/,
                  const std::string& path) override {
        ++preSaveCalls;
        lastKind = kind;
        lastPath = path;
        return !abortPreSave;
    }
    void PostSave(VfxSaveAssetKind kind,
                   const void* /*asset*/,
                   const std::string& path) override {
        ++postSaveCalls;
        lastKind = kind;
        lastPath = path;
    }
    const char* Name() const noexcept override { return name; }
};

// Reset the singleton between tests so state doesn't leak.
struct PipelineFixture : ::testing::Test {
    void SetUp()    override { EditorSavePipeline::Instance().Clear(); }
    void TearDown() override { EditorSavePipeline::Instance().Clear(); }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Asset kind names
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PipelineFixture, AssetKindNamesStable) {
    EXPECT_STREQ(VfxSaveAssetKindName(VfxSaveAssetKind::EmitterPreset), "emitter_preset");
    EXPECT_STREQ(VfxSaveAssetKindName(VfxSaveAssetKind::VfxSystem),     "vfx_system");
    EXPECT_STREQ(VfxSaveAssetKindName(VfxSaveAssetKind::NodeGraph),     "node_graph");
    EXPECT_STREQ(VfxSaveAssetKindName(VfxSaveAssetKind::Snapshot),      "snapshot");
    EXPECT_STREQ(VfxSaveAssetKindName(VfxSaveAssetKind::Unknown),       "unknown");
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry basics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PipelineFixture, StartsEmpty) {
    EXPECT_EQ(EditorSavePipeline::Instance().ExtensionCount(), 0u);
}

TEST_F(PipelineFixture, RegisterReturnsHandle) {
    RecordingExt ext;
    const auto h = EditorSavePipeline::Instance().Register(&ext);
    EXPECT_NE(h, kInvalidEditorSaveExtensionHandle);
    EXPECT_EQ(EditorSavePipeline::Instance().ExtensionCount(), 1u);
}

TEST_F(PipelineFixture, NullExtensionRejected) {
    EXPECT_EQ(EditorSavePipeline::Instance().Register(nullptr),
               kInvalidEditorSaveExtensionHandle);
    EXPECT_EQ(EditorSavePipeline::Instance().ExtensionCount(), 0u);
}

TEST_F(PipelineFixture, UnregisterRemovesSlot) {
    RecordingExt ext;
    auto& pipe = EditorSavePipeline::Instance();
    const auto h = pipe.Register(&ext);
    pipe.Unregister(h);
    EXPECT_EQ(pipe.ExtensionCount(), 0u);
}

TEST_F(PipelineFixture, UnregisterInvalidHandleNoOp) {
    auto& pipe = EditorSavePipeline::Instance();
    pipe.Unregister(kInvalidEditorSaveExtensionHandle);
    pipe.Unregister(9999);
    EXPECT_EQ(pipe.ExtensionCount(), 0u);
}

TEST_F(PipelineFixture, ClearRemovesAll) {
    RecordingExt a, b, c;
    auto& pipe = EditorSavePipeline::Instance();
    pipe.Register(&a); pipe.Register(&b); pipe.Register(&c);
    ASSERT_EQ(pipe.ExtensionCount(), 3u);
    pipe.Clear();
    EXPECT_EQ(pipe.ExtensionCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// PreSave dispatch
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PipelineFixture, PreSaveFiresForEveryExtension) {
    RecordingExt a, b, c;
    auto& pipe = EditorSavePipeline::Instance();
    pipe.Register(&a); pipe.Register(&b); pipe.Register(&c);
    EXPECT_TRUE(pipe.RunPreSave(VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys"));
    EXPECT_EQ(a.preSaveCalls, 1u);
    EXPECT_EQ(b.preSaveCalls, 1u);
    EXPECT_EQ(c.preSaveCalls, 1u);
    EXPECT_EQ(a.lastKind, VfxSaveAssetKind::VfxSystem);
    EXPECT_EQ(a.lastPath, "out.uzvfxsys");
}

TEST_F(PipelineFixture, PreSaveAbortStopsOnFirstFalse) {
    RecordingExt a, b, c;
    b.abortPreSave = true;
    b.name = "BlockingExt";
    auto& pipe = EditorSavePipeline::Instance();
    pipe.Register(&a); pipe.Register(&b); pipe.Register(&c);

    std::string reason;
    EXPECT_FALSE(pipe.RunPreSave(VfxSaveAssetKind::NodeGraph, nullptr,
                                    "graph.uzvfxgraph", reason));
    EXPECT_EQ(a.preSaveCalls, 1u);
    EXPECT_EQ(b.preSaveCalls, 1u);
    EXPECT_EQ(c.preSaveCalls, 0u); // never reached
    EXPECT_EQ(reason, "BlockingExt");
}

TEST_F(PipelineFixture, PreSaveAbortReasonEmptyOnSuccess) {
    RecordingExt a;
    EditorSavePipeline::Instance().Register(&a);
    std::string reason = "pre-existing";
    EXPECT_TRUE(EditorSavePipeline::Instance().RunPreSave(
        VfxSaveAssetKind::Snapshot, nullptr, "x.uzvfxsnap", reason));
    EXPECT_EQ(reason, ""); // cleared on success
}

// ─────────────────────────────────────────────────────────────────────────────
// PostSave dispatch
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PipelineFixture, PostSaveFiresForEveryExtension) {
    RecordingExt a, b;
    auto& pipe = EditorSavePipeline::Instance();
    pipe.Register(&a); pipe.Register(&b);
    pipe.RunPostSave(VfxSaveAssetKind::EmitterPreset, nullptr, "e.uzfx");
    EXPECT_EQ(a.postSaveCalls, 1u);
    EXPECT_EQ(b.postSaveCalls, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveWithPipeline wrapper
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PipelineFixture, SaveWithPipelineCallsBothHooksOnSuccess) {
    RecordingExt ext;
    EditorSavePipeline::Instance().Register(&ext);

    bool saveCbInvoked = false;
    const bool ok = VfxSaveWithPipeline(
        VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys",
        [&]() { saveCbInvoked = true; return true; });

    EXPECT_TRUE(ok);
    EXPECT_TRUE(saveCbInvoked);
    EXPECT_EQ(ext.preSaveCalls,  1u);
    EXPECT_EQ(ext.postSaveCalls, 1u);
}

TEST_F(PipelineFixture, SaveWithPipelinePreSaveAbortSkipsCallback) {
    RecordingExt ext;
    ext.abortPreSave = true;
    EditorSavePipeline::Instance().Register(&ext);

    bool saveCbInvoked = false;
    const bool ok = VfxSaveWithPipeline(
        VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys",
        [&]() { saveCbInvoked = true; return true; });

    EXPECT_FALSE(ok);
    EXPECT_FALSE(saveCbInvoked);
    EXPECT_EQ(ext.preSaveCalls,  1u);
    EXPECT_EQ(ext.postSaveCalls, 0u); // never runs on abort
}

TEST_F(PipelineFixture, SaveWithPipelineSaveFailureSkipsPostSave) {
    RecordingExt ext;
    EditorSavePipeline::Instance().Register(&ext);

    const bool ok = VfxSaveWithPipeline(
        VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys",
        [&]() { return false; }); // save callback fails

    EXPECT_FALSE(ok);
    EXPECT_EQ(ext.preSaveCalls,  1u);
    EXPECT_EQ(ext.postSaveCalls, 0u); // PostSave only fires on success
}

TEST_F(PipelineFixture, NoExtensionsRegistered_SaveCallbackPassesThrough) {
    bool saveCbInvoked = false;
    const bool ok = VfxSaveWithPipeline(
        VfxSaveAssetKind::Snapshot, nullptr, "out.uzvfxsnap",
        [&]() { saveCbInvoked = true; return true; });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(saveCbInvoked);
}

// ─────────────────────────────────────────────────────────────────────────────
// Re-entrancy safety
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Extension that unregisters itself during PreSave — exercises the
/// snapshot-before-iterate contract.
struct SelfUnregisteringExt : IEditorSaveExtension {
    EditorSaveExtensionHandle self = kInvalidEditorSaveExtensionHandle;
    uint32_t preSaveCalls = 0;

    bool PreSave(VfxSaveAssetKind,
                  const void*,
                  const std::string&) override {
        ++preSaveCalls;
        EditorSavePipeline::Instance().Unregister(self);
        self = kInvalidEditorSaveExtensionHandle;
        return true;
    }
    const char* Name() const noexcept override { return "SelfUnreg"; }
};

} // namespace

TEST_F(PipelineFixture, SelfUnregisterDuringPreSaveIsSafe) {
    SelfUnregisteringExt se;
    RecordingExt next;
    auto& pipe = EditorSavePipeline::Instance();
    se.self = pipe.Register(&se);
    pipe.Register(&next);

    EXPECT_TRUE(pipe.RunPreSave(VfxSaveAssetKind::VfxSystem, nullptr, "x"));
    EXPECT_EQ(se.preSaveCalls,   1u);
    EXPECT_EQ(next.preSaveCalls, 1u);
    EXPECT_EQ(pipe.ExtensionCount(), 1u); // self removed, next remains
}
