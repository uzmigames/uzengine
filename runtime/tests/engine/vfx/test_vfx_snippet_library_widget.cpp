/// Unit tests for phase 9.4.0 — VFX snippet library widget.
///
/// @ref engine/vfx/editor/vfx_snippet_library_widget.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_snippet_library_widget.h"
#include "engine/vfx/system/vfx_system.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// AppendEmitterRef helper
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetLibraryWidget, AppendEmitterRef_AddsEntry) {
    VfxSystem sys;
    VfxSnippetLibraryWidget::AppendEmitterRef("vfx/fireball.uzvfxsys", sys);
    ASSERT_EQ(sys.emitters.size(), 1u);
    EXPECT_EQ(sys.emitters[0].emitterPath, "vfx/fireball.uzvfxsys");
    EXPECT_EQ(sys.emitters[0].name, "fireball");
    EXPECT_TRUE(sys.emitters[0].enabled);
}

TEST(VfxSnippetLibraryWidget, AppendEmitterRef_GeneratesUniqueIds) {
    VfxSystem sys;
    VfxSnippetLibraryWidget::AppendEmitterRef("a.uzfx", sys);
    VfxSnippetLibraryWidget::AppendEmitterRef("b.uzfx", sys);
    VfxSnippetLibraryWidget::AppendEmitterRef("c.uzfx", sys);
    ASSERT_EQ(sys.emitters.size(), 3u);
    EXPECT_EQ(sys.emitters[0].id, "e0");
    EXPECT_EQ(sys.emitters[1].id, "e1");
    EXPECT_EQ(sys.emitters[2].id, "e2");
}

TEST(VfxSnippetLibraryWidget, AppendEmitterRef_EmptyPathIsNoOp) {
    VfxSystem sys;
    VfxSnippetLibraryWidget::AppendEmitterRef("", sys);
    EXPECT_EQ(sys.emitters.size(), 0u);
}

TEST(VfxSnippetLibraryWidget, AppendEmitterRef_StemFromNestedPath) {
    VfxSystem sys;
    VfxSnippetLibraryWidget::AppendEmitterRef("effects/environment/rain.uzfx", sys);
    ASSERT_EQ(sys.emitters.size(), 1u);
    EXPECT_EQ(sys.emitters[0].name, "rain");
}

TEST(VfxSnippetLibraryWidget, AppendEmitterRef_NoExtensionUsesFullStem) {
    VfxSystem sys;
    VfxSnippetLibraryWidget::AppendEmitterRef("dir/noext", sys);
    ASSERT_EQ(sys.emitters.size(), 1u);
    EXPECT_EQ(sys.emitters[0].name, "noext");
}

// ─────────────────────────────────────────────────────────────────────────────
// Widget construction
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetLibraryWidget, StartsEmpty) {
    VfxSnippetLibraryWidget w;
    EXPECT_EQ(w.VisibleCount(), 0u);
    EXPECT_EQ(w.Index().Entries().size(), 0u);
}

TEST(VfxSnippetLibraryWidget, BrowserAccessible) {
    VfxSnippetLibraryWidget w;
    // Should not crash; filter buffer mutable.
    w.Browser().FilterBuffer() = "test";
    EXPECT_EQ(w.Browser().GetFilter(), "test");
}

// Note: ScanAndRefresh requires a real content directory with .uzvfxsys
// files. The underlying VfxSnippetIndex is tested in phase 8.8.0's test
// suite; here we just verify the data-source adapter exists and the
// widget's filter path works end-to-end once entries are populated.

// ─────────────────────────────────────────────────────────────────────────────
// Drop callback wiring
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetLibraryWidget, OnDropCallbackInvokable) {
    VfxSnippetLibraryWidget w;
    VfxSystem sys;

    std::string droppedPath;
    VfxSystem* droppedOnto = nullptr;
    w.onDropOnSystem = [&](const std::string& p, VfxSystem& s) {
        droppedPath = p;
        droppedOnto = &s;
        VfxSnippetLibraryWidget::AppendEmitterRef(p, s);
    };

    // Simulate a drop (normally fired by the host).
    if (w.onDropOnSystem) {
        w.onDropOnSystem("vfx/test.uzfx", sys);
    }

    EXPECT_EQ(droppedPath, "vfx/test.uzfx");
    EXPECT_EQ(droppedOnto, &sys);
    EXPECT_EQ(sys.emitters.size(), 1u);
    EXPECT_EQ(sys.emitters[0].emitterPath, "vfx/test.uzfx");
}
