/// Unit tests for phase 14.5.0: `VfxToolbarWidget`.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_toolbar_widget.h"

using namespace uz;

namespace {

VfxToolbarButton MakeAction(uint32_t id, const char* label) {
    VfxToolbarButton b;
    b.id    = id;
    b.label = label;
    b.kind  = VfxToolbarKind::Action;
    b.icon  = VfxUiIcon::Play;
    return b;
}

} // namespace

TEST(VfxToolbarWidget, MeasureButtonHonoursPadding) {
    VfxToolbarWidget w;
    const float width = w.MeasureButton(MakeAction(1, "Save"));
    // Floor: 2*pad (8) + icon 16 + gap 4 + label (~28) = 56.
    EXPECT_GT(width, 40.0f);
}

TEST(VfxToolbarWidget, MeasureSeparatorFixedGeometry) {
    VfxToolbarWidget w;
    VfxToolbarButton sep;
    sep.kind = VfxToolbarKind::Separator;
    EXPECT_FLOAT_EQ(w.MeasureButton(sep),
                     1.0f + w.sepPad * 2.0f);
}

TEST(VfxToolbarWidget, LayoutFlowsLeftToRight) {
    VfxToolbarWidget w;
    w.AddButton(MakeAction(1, "One"));
    w.AddButton(MakeAction(2, "Two"));
    w.AddButton(MakeAction(3, "Three"));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 800.0f, 28.0f});
    float prevEnd = 0.0f;
    for (const auto& it : layout) {
        EXPECT_GE(it.rect.x, prevEnd);
        prevEnd = it.rect.x + it.rect.width;
    }
}

TEST(VfxToolbarWidget, SeparatorItemEmittedInsideRun) {
    VfxToolbarWidget w;
    w.AddButton(MakeAction(1, "One"));
    w.AddSeparator();
    w.AddButton(MakeAction(2, "Two"));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 800.0f, 28.0f});
    size_t seps = 0;
    for (const auto& it : layout) {
        if (it.kind == VfxToolbarKind::Separator) ++seps;
    }
    EXPECT_EQ(seps, 1u);
}

TEST(VfxToolbarWidget, HitTestFindsButton) {
    VfxToolbarWidget w;
    w.AddButton(MakeAction(42, "Save"));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 800.0f, 28.0f});
    ASSERT_FALSE(layout.empty());
    const auto& first = layout.front();
    const auto r = w.HitTest(layout, first.rect.x + 4.0f,
                                     first.rect.y + 14.0f);
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(r.id, 42u);
    EXPECT_EQ(r.kind, VfxToolbarKind::Action);
}

TEST(VfxToolbarWidget, HitTestIgnoresSeparators) {
    VfxToolbarWidget w;
    w.AddButton(MakeAction(1, "One"));
    w.AddSeparator();
    w.AddButton(MakeAction(2, "Two"));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 800.0f, 28.0f});
    // Find separator rect centre.
    for (const auto& it : layout) {
        if (it.kind != VfxToolbarKind::Separator) continue;
        const float cx = it.rect.x + 0.5f;
        const float cy = it.rect.y + it.rect.height * 0.5f;
        const auto r = w.HitTest(layout, cx, cy);
        EXPECT_FALSE(r.hit);
        return;
    }
    FAIL() << "separator not found";
}

TEST(VfxToolbarWidget, ToggleSticksAfterSetToggled) {
    VfxToolbarWidget w;
    VfxToolbarButton b;
    b.id = 7;
    b.label = "Play";
    b.kind = VfxToolbarKind::Toggle;
    w.AddButton(b);

    EXPECT_FALSE(w.IsToggled(7u));
    w.SetToggled(7u, true);
    EXPECT_TRUE(w.IsToggled(7u));
    w.SetToggled(7u, false);
    EXPECT_FALSE(w.IsToggled(7u));
}

TEST(VfxToolbarWidget, SetToggledIsNoopOnAction) {
    VfxToolbarWidget w;
    w.AddButton(MakeAction(10, "X"));
    w.SetToggled(10u, true);
    EXPECT_FALSE(w.IsToggled(10u));
}
