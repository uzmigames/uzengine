/// Unit tests for phase 9.7.0 — VFX thumbnail save-pipeline extension.
///
/// Covers: request queuing, asset-kind filtering, default config
/// propagation, and queue thread-safety contract.
///
/// @ref engine/vfx/editor/vfx_thumbnail_save_ext.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_thumbnail_save_ext.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// IsVfxAssetKindThumbnailable
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailSaveExt, ThumbnailableKinds) {
    EXPECT_TRUE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::EmitterPreset));
    EXPECT_TRUE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::VfxSystem));
    EXPECT_TRUE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::NodeGraph));
}

TEST(VfxThumbnailSaveExt, NonThumbnailableKinds) {
    EXPECT_FALSE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::Unknown));
    EXPECT_FALSE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::Snapshot));
    EXPECT_FALSE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::BakeAsset));
    EXPECT_FALSE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::Material));
    EXPECT_FALSE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::TangentCurve));
    EXPECT_FALSE(IsVfxAssetKindThumbnailable(VfxSaveAssetKind::EffectType));
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailCaptureQueue
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailCaptureQueue, StartsEmpty) {
    VfxThumbnailCaptureQueue q;
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_EQ(q.Peek(), nullptr);
}

TEST(VfxThumbnailCaptureQueue, PushIncreasesSize) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailCaptureRequest req;
    req.config.assetPath = "vfx/fireball.uzvfxsys";
    req.kind = VfxSaveAssetKind::VfxSystem;
    q.Push(req);
    EXPECT_EQ(q.Size(), 1u);
}

TEST(VfxThumbnailCaptureQueue, PeekReturnsFront) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailCaptureRequest r1;
    r1.config.assetPath = "first.uzvfxsys";
    r1.kind = VfxSaveAssetKind::VfxSystem;

    VfxThumbnailCaptureRequest r2;
    r2.config.assetPath = "second.uzfx";
    r2.kind = VfxSaveAssetKind::EmitterPreset;

    q.Push(r1);
    q.Push(r2);

    const auto* front = q.Peek();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->config.assetPath, "first.uzvfxsys");
    EXPECT_EQ(front->kind, VfxSaveAssetKind::VfxSystem);
}

TEST(VfxThumbnailCaptureQueue, PopRemovesFront) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailCaptureRequest r1;
    r1.config.assetPath = "first.uzvfxsys";

    VfxThumbnailCaptureRequest r2;
    r2.config.assetPath = "second.uzfx";

    q.Push(r1);
    q.Push(r2);
    q.Pop();

    EXPECT_EQ(q.Size(), 1u);
    const auto* front = q.Peek();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->config.assetPath, "second.uzfx");
}

TEST(VfxThumbnailCaptureQueue, PopOnEmptyIsNoOp) {
    VfxThumbnailCaptureQueue q;
    q.Pop(); // must not crash
    EXPECT_EQ(q.Size(), 0u);
}

TEST(VfxThumbnailCaptureQueue, ClearEmptiesQueue) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailCaptureRequest req;
    req.config.assetPath = "a.uzfx";
    q.Push(req);
    q.Push(req);
    q.Push(req);
    ASSERT_EQ(q.Size(), 3u);
    q.Clear();
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_EQ(q.Peek(), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailSaveExtension — PostSave filtering
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailSaveExt, PostSaveEnqueuesForVfxSystem) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    ext.PostSave(VfxSaveAssetKind::VfxSystem, nullptr, "vfx/fire.uzvfxsys");
    EXPECT_EQ(q.Size(), 1u);
    EXPECT_EQ(ext.GetEnqueueCount(), 1u);

    const auto* req = q.Peek();
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->config.assetPath, "vfx/fire.uzvfxsys");
    EXPECT_EQ(req->kind, VfxSaveAssetKind::VfxSystem);
}

TEST(VfxThumbnailSaveExt, PostSaveEnqueuesForEmitterPreset) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    ext.PostSave(VfxSaveAssetKind::EmitterPreset, nullptr, "fx/spark.uzfx");
    EXPECT_EQ(q.Size(), 1u);
    EXPECT_EQ(ext.GetEnqueueCount(), 1u);
}

TEST(VfxThumbnailSaveExt, PostSaveEnqueuesForNodeGraph) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    ext.PostSave(VfxSaveAssetKind::NodeGraph, nullptr, "graph.uzvfxgraph");
    EXPECT_EQ(q.Size(), 1u);
}

TEST(VfxThumbnailSaveExt, PostSaveSkipsSnapshot) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    ext.PostSave(VfxSaveAssetKind::Snapshot, nullptr, "snap.uzvfxsnap");
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_EQ(ext.GetEnqueueCount(), 0u);
}

TEST(VfxThumbnailSaveExt, PostSaveSkipsBakeAsset) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    ext.PostSave(VfxSaveAssetKind::BakeAsset, nullptr, "bake.uzvfxbake");
    EXPECT_EQ(q.Size(), 0u);
}

TEST(VfxThumbnailSaveExt, PostSaveSkipsMaterial) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    ext.PostSave(VfxSaveAssetKind::Material, nullptr, "mat.uzmat");
    EXPECT_EQ(q.Size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailSaveExtension — config propagation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailSaveExt, DefaultConfigPropagatedToRequest) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);

    VfxThumbnailConfig custom;
    custom.width = 256;
    custom.height = 256;
    custom.captureFrame = 30;
    custom.randomSeed = 99;
    custom.fps = 30.0f;
    ext.SetDefaultConfig(custom);

    ext.PostSave(VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys");

    const auto* req = q.Peek();
    ASSERT_NE(req, nullptr);
    // assetPath is overridden per-save
    EXPECT_EQ(req->config.assetPath, "out.uzvfxsys");
    // everything else comes from the default config
    EXPECT_EQ(req->config.width, 256u);
    EXPECT_EQ(req->config.height, 256u);
    EXPECT_EQ(req->config.captureFrame, 30u);
    EXPECT_EQ(req->config.randomSeed, 99u);
    EXPECT_FLOAT_EQ(req->config.fps, 30.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailSaveExtension — null queue (filtering-only mode)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailSaveExt, NullQueueStillCountsEnqueues) {
    VfxThumbnailSaveExtension ext(nullptr);
    ext.PostSave(VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys");
    EXPECT_EQ(ext.GetEnqueueCount(), 1u);
    // no crash, no queue side-effect
}

TEST(VfxThumbnailSaveExt, NullQueueSkipsNonThumbnailable) {
    VfxThumbnailSaveExtension ext(nullptr);
    ext.PostSave(VfxSaveAssetKind::Snapshot, nullptr, "snap.uzvfxsnap");
    EXPECT_EQ(ext.GetEnqueueCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailSaveExtension — Name()
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailSaveExt, NameReturnsStableString) {
    VfxThumbnailSaveExtension ext(nullptr);
    EXPECT_STREQ(ext.Name(), "ThumbnailCapture");
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailSaveExtension — ResetStats
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailSaveExt, ResetStatsClearsCounter) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    ext.PostSave(VfxSaveAssetKind::VfxSystem, nullptr, "a.uzvfxsys");
    ext.PostSave(VfxSaveAssetKind::EmitterPreset, nullptr, "b.uzfx");
    EXPECT_EQ(ext.GetEnqueueCount(), 2u);
    ext.ResetStats();
    EXPECT_EQ(ext.GetEnqueueCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration with EditorSavePipeline
// ─────────────────────────────────────────────────────────────────────────────

namespace {
struct PipelineFixture97 : ::testing::Test {
    void SetUp()    override { EditorSavePipeline::Instance().Clear(); }
    void TearDown() override { EditorSavePipeline::Instance().Clear(); }
};
} // namespace

TEST_F(PipelineFixture97, SaveWithPipelineTriggersCapture) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    EditorSavePipeline::Instance().Register(&ext);

    bool saveCalled = false;
    const bool ok = VfxSaveWithPipeline(
        VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys",
        [&]() { saveCalled = true; return true; });

    EXPECT_TRUE(ok);
    EXPECT_TRUE(saveCalled);
    EXPECT_EQ(q.Size(), 1u);
    EXPECT_EQ(ext.GetEnqueueCount(), 1u);

    const auto* req = q.Peek();
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->config.assetPath, "out.uzvfxsys");
}

TEST_F(PipelineFixture97, SaveFailureDoesNotTriggerCapture) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailSaveExtension ext(&q);
    EditorSavePipeline::Instance().Register(&ext);

    const bool ok = VfxSaveWithPipeline(
        VfxSaveAssetKind::VfxSystem, nullptr, "out.uzvfxsys",
        [&]() { return false; }); // save fails

    EXPECT_FALSE(ok);
    EXPECT_EQ(q.Size(), 0u); // PostSave never fires on save failure
    EXPECT_EQ(ext.GetEnqueueCount(), 0u);
}
