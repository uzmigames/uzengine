/// Unit tests for the VFX editor Slate-UI rebuild (Phase 18 / Step 10).

#include "engine/ui/slate_box.h"
#include "engine/ui/slate_text_block.h"
#include "engine/ui/slate_widget.h"
#include "engine/vfx/editor/vfx_editor_slate.h"

#include <gtest/gtest.h>

#include <memory>

using namespace uz;
using namespace uz::ui;
using namespace uz::vfx;

TEST(VfxEditorSlate, BuildRootProduces4RowVerticalBox) {
    VfxEditorSlate editor;
    editor.BuildRoot();

    auto root = std::dynamic_pointer_cast<VerticalBox>(editor.GetRoot());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->SlotCount(), 4u);

    // Slots in order: title / toolbar / workbench / statusbar.
    EXPECT_EQ(root->Slots()[0].widget, editor.GetTitleBar());
    EXPECT_EQ(root->Slots()[1].widget, editor.GetToolbar());
    EXPECT_EQ(root->Slots()[2].widget, editor.GetWorkbench());
    EXPECT_EQ(root->Slots()[3].widget, editor.GetStatusBar());

    // The workbench slot is the only one with FillSize > 0.
    EXPECT_EQ(root->Slots()[0].sizeRule, SizeRule::Auto);
    EXPECT_EQ(root->Slots()[1].sizeRule, SizeRule::Auto);
    EXPECT_EQ(root->Slots()[2].sizeRule, SizeRule::Fill);
    EXPECT_EQ(root->Slots()[3].sizeRule, SizeRule::Auto);
}

TEST(VfxEditorSlate, WorkbenchHasTwoColumns) {
    VfxEditorSlate editor;
    editor.BuildRoot();
    auto workbench = std::dynamic_pointer_cast<HorizontalBox>(editor.GetWorkbench());
    ASSERT_NE(workbench, nullptr);
    EXPECT_EQ(workbench->SlotCount(), 2u);
    // Left column 2x weight (viewport+graph), right 1x (details/stack/curve).
    EXPECT_FLOAT_EQ(workbench->Slots()[0].fillFactor, 2.0f);
    EXPECT_FLOAT_EQ(workbench->Slots()[1].fillFactor, 1.0f);
}

TEST(VfxEditorSlate, ActiveToolStateIsMutable) {
    VfxEditorSlate editor;
    EXPECT_EQ(editor.GetActiveTool(), 0);
    editor.SetActiveTool(3);
    EXPECT_EQ(editor.GetActiveTool(), 3);
}

TEST(VfxEditorSlate, EmitterNameDefaultsAndIsMutable) {
    VfxEditorSlate editor;
    EXPECT_EQ(editor.GetEmitterName(), "particle.uzfx");
    editor.SetEmitterName("explosion.uzfx");
    EXPECT_EQ(editor.GetEmitterName(), "explosion.uzfx");
}

TEST(VfxEditorSlate, TitleBarReflectsEmitterName) {
    VfxEditorSlate editor;
    editor.SetEmitterName("rain.uzfx");
    editor.BuildRoot();

    auto title = std::dynamic_pointer_cast<HorizontalBox>(editor.GetTitleBar());
    ASSERT_NE(title, nullptr);
    // Find a TextBlock inside the title bar whose lambda Text resolves
    // to the emitter name. Walk via HitTestChildren which exposes slot widgets.
    bool found = false;
    if (auto* kids = title->HitTestChildren()) {
        for (const auto& c : *kids) {
            if (auto tb = std::dynamic_pointer_cast<TextBlock>(c)) {
                if (tb->Text.Get() == "rain.uzfx") { found = true; break; }
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(VfxEditorSlate, StatusBarBindingsReflectTelemetry) {
    VfxEditorSlate editor;
    editor.BuildRoot();

    VfxEditorTelemetry t;
    t.fps = 60;
    t.aliveCount = 1234;
    t.maxCount = 5000;
    t.vramUsedMb = 42.5f;
    editor.SetTelemetry(t);

    auto status = std::dynamic_pointer_cast<HorizontalBox>(editor.GetStatusBar());
    ASSERT_NE(status, nullptr);

    bool sawFps = false, sawAlive = false, sawMax = false, sawVram = false;
    if (auto* kids = status->HitTestChildren()) {
        for (const auto& c : *kids) {
            if (auto tb = std::dynamic_pointer_cast<TextBlock>(c)) {
                std::string s = tb->Text.Get();
                if (s == "FPS: 60")           sawFps = true;
                if (s == "Alive: 1234")       sawAlive = true;
                if (s == "Max: 5000")         sawMax = true;
                if (s == "VRAM: 42.5 MB")     sawVram = true;
            }
        }
    }
    EXPECT_TRUE(sawFps);
    EXPECT_TRUE(sawAlive);
    EXPECT_TRUE(sawMax);
    EXPECT_TRUE(sawVram);
}

TEST(VfxEditorSlate, BuildRootIsIdempotent) {
    VfxEditorSlate editor;
    editor.BuildRoot();
    auto root1 = editor.GetRoot();
    editor.BuildRoot();
    auto root2 = editor.GetRoot();

    // Calling BuildRoot twice produces a fresh tree (root2 != root1)
    // but the topology stays the same.
    EXPECT_NE(root1, root2);
    auto vb = std::dynamic_pointer_cast<VerticalBox>(root2);
    ASSERT_NE(vb, nullptr);
    EXPECT_EQ(vb->SlotCount(), 4u);
}

TEST(VfxEditorSlate, SelectedModuleInitialAndMutable) {
    VfxEditorSlate editor;
    EXPECT_EQ(editor.GetSelectedModule(), -1);
    editor.SetSelectedModule(2);
    EXPECT_EQ(editor.GetSelectedModule(), 2);
}

TEST(VfxEditorSlate, TickWithoutRootIsNoOp) {
    VfxEditorSlate editor;
    // No BuildRoot — Tick should not crash.
    editor.Tick(0.016f);
    SUCCEED();
}
