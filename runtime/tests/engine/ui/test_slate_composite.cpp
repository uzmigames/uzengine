/// Unit tests for ScrollBox, EditableText, ComboBox<T>
/// (Phase 18 / Step 7).

#include "engine/ui/slate_combobox.h"
#include "engine/ui/slate_editable_text.h"
#include "engine/ui/slate_renderer.h"
#include "engine/ui/slate_scrollbox.h"
#include "engine/ui/slate_text_block.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace uz;
using namespace uz::ui;

// ─────────────────────────────────────────────────────────────────────────────
// ScrollBox
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateScrollBox, MeasureClampsToMaxSize) {
    auto box = std::make_shared<ScrollBox>();
    auto huge = std::make_shared<TextBlock>();
    huge->Text = std::string(2000, 'x');
    huge->FontSize = 12.0f;
    box->SetContent(huge);

    Vec2 m = box->Measure(Vec2{200.0f, 100.0f});
    EXPECT_LE(m.x, 200.0f);
    EXPECT_LE(m.y, 100.0f);
}

TEST(SlateScrollBox, ArrangeClampsScrollToMaxScroll) {
    auto box = std::make_shared<ScrollBox>();
    auto big = std::make_shared<TextBlock>();
    big->Text = std::string(50, 'x'); // Single-line: small height, doesn't overflow.
    box->SetContent(big);

    Geometry g{};
    g.absolutePos = Vec2{0, 0};
    g.size = Vec2{120.0f, 200.0f}; // viewport bigger than content
    box->ScrollY = 9999.0f; // intentionally over
    box->Arrange(g);

    EXPECT_LE(box->ScrollY.Get(), box->MaxScroll());
}

TEST(SlateScrollBox, WheelEventScrollsWithinBounds) {
    auto box = std::make_shared<ScrollBox>();
    auto child = std::make_shared<TextBlock>();
    // Force tall content by overriding measure semantics through long text size.
    child->Text = std::string(20, 'x');
    child->FontSize = 200.0f; // each line is ~202 vertical
    box->SetContent(child);

    Geometry g{};
    g.size = Vec2{500.0f, 100.0f};
    box->Arrange(g);

    PointerEvent e{};
    e.wheelDelta = -1.0f; // user scrolls down → ScrollY increases
    box->OnPointerWheel(e);
    EXPECT_GT(box->ScrollY.Get(), 0.0f);
}

TEST(SlateScrollBox, PaintEmitsScissorPushAndPop) {
    auto box = std::make_shared<ScrollBox>();
    auto child = std::make_shared<TextBlock>();
    child->Text = "hi";
    box->SetContent(child);

    Geometry g{};
    g.size = Vec2{100.0f, 50.0f};
    box->Arrange(g);

    SlateRenderer r;
    r.BeginFrame();
    box->Paint(r);

    bool sawPush = false, sawPop = false;
    for (const auto& el : r.Elements()) {
        if (el.kind == DrawKind::ScissorPush) sawPush = true;
        if (el.kind == DrawKind::ScissorPop)  sawPop = true;
    }
    EXPECT_TRUE(sawPush);
    EXPECT_TRUE(sawPop);
}

// ─────────────────────────────────────────────────────────────────────────────
// EditableText
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateEditableText, InitiallyUnfocused) {
    EditableText t;
    EXPECT_FALSE(t.IsFocused());
    EXPECT_EQ(t.Caret(), 0u);
}

TEST(SlateEditableText, FocusOnPointerDown) {
    EditableText t;
    Geometry g{};
    g.absolutePos = Vec2{0, 0};
    g.size = Vec2{200, 24};
    t.Arrange(g);

    PointerEvent e{};
    e.button = PointerButton::Left;
    e.isDown = true;
    e.position = Vec2{20, 10};
    t.OnPointerDown(e);
    EXPECT_TRUE(t.IsFocused());
}

TEST(SlateEditableText, BackspaceErasesCharBeforeCaret) {
    EditableText t;
    t.Focus();
    t.Text = std::string("hello");

    PointerEvent down{};
    down.button = PointerButton::Left;
    down.isDown = true;
    down.position = Vec2{1000, 0}; // far right → caret = end
    Geometry g{};
    g.size = Vec2{200, 24};
    t.Arrange(g);
    t.OnPointerDown(down);
    EXPECT_EQ(t.Caret(), 5u);

    KeyEvent k{};
    k.keyCode = EditableText::KeyBackspace;
    k.isDown = true;
    t.OnKeyDown(k);
    EXPECT_EQ(t.Text.Get(), "hell");
    EXPECT_EQ(t.Caret(), 4u);
}

TEST(SlateEditableText, ArrowKeysMoveCaret) {
    EditableText t;
    t.Focus();
    t.Text = std::string("ab");

    KeyEvent k{};
    k.isDown = true;

    k.keyCode = EditableText::KeyEnd;
    t.OnKeyDown(k);
    EXPECT_EQ(t.Caret(), 2u);

    k.keyCode = EditableText::KeyLeft;
    t.OnKeyDown(k);
    EXPECT_EQ(t.Caret(), 1u);

    k.keyCode = EditableText::KeyHome;
    t.OnKeyDown(k);
    EXPECT_EQ(t.Caret(), 0u);

    k.keyCode = EditableText::KeyRight;
    t.OnKeyDown(k);
    EXPECT_EQ(t.Caret(), 1u);
}

TEST(SlateEditableText, EnterFiresOnCommittedOnce) {
    EditableText t;
    t.Focus();
    t.Text = std::string("changed");
    int commits = 0;
    std::string lastValue;
    t.OnCommitted = [&](const std::string& v) { ++commits; lastValue = v; };

    KeyEvent k{};
    k.isDown = true;
    k.keyCode = EditableText::KeyEnter;
    t.OnKeyDown(k);
    EXPECT_EQ(commits, 1);
    EXPECT_EQ(lastValue, "changed");

    // Pressing Enter again with no edit should NOT re-fire.
    t.OnKeyDown(k);
    EXPECT_EQ(commits, 1);
}

TEST(SlateEditableText, BlurFiresOnCommittedIfTextChanged) {
    EditableText t;
    t.Focus();
    int commits = 0;
    t.OnCommitted = [&](const std::string&) { ++commits; };
    t.Text = std::string("edit");
    t.Blur();
    EXPECT_EQ(commits, 1);
    EXPECT_FALSE(t.IsFocused());
}

TEST(SlateEditableText, CharacterInputInsertsAtCaret) {
    EditableText t;
    t.Focus();

    KeyEvent k{};
    k.isDown = true;
    k.keyCode = 0xFF; // generic — ignored
    k.character = 'a';
    t.OnKeyDown(k);
    k.character = 'b';
    t.OnKeyDown(k);
    EXPECT_EQ(t.Text.Get(), "ab");
    EXPECT_EQ(t.Caret(), 2u);
}

TEST(SlateEditableText, KeyEscapeBlurs) {
    EditableText t;
    t.Focus();
    EXPECT_TRUE(t.IsFocused());

    KeyEvent k{};
    k.isDown = true;
    k.keyCode = EditableText::KeyEscape;
    t.OnKeyDown(k);
    EXPECT_FALSE(t.IsFocused());
}

// ─────────────────────────────────────────────────────────────────────────────
// ComboBox<T>
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateComboBox, SetOptionsResetsSelectedIndex) {
    ComboBox<std::string> cb;
    cb.SetOptions({"Mesh", "Sprite", "MeshRenderer"},
                  [](const std::string& s) { return s; });
    EXPECT_EQ(cb.OptionCount(), 3u);
    EXPECT_EQ(cb.SelectedLabel(), "Mesh");
    EXPECT_EQ(cb.SelectedIndex.Get(), 0);
}

TEST(SlateComboBox, SelectOptionFiresCallback) {
    ComboBox<std::string> cb;
    cb.SetOptions({"A", "B", "C"}, [](const std::string& s) { return s; });
    int last = -1;
    cb.OnSelectionChanged = [&last](int i) { last = i; };

    cb.SelectOption(2);
    EXPECT_EQ(cb.SelectedIndex.Get(), 2);
    EXPECT_EQ(last, 2);
    EXPECT_FALSE(cb.IsOpen());
}

TEST(SlateComboBox, PointerDownTogglesOpen) {
    ComboBox<std::string> cb;
    cb.SetOptions({"A", "B"}, [](const std::string& s) { return s; });

    PointerEvent e{};
    e.button = PointerButton::Left;
    e.isDown = true;

    cb.OnPointerDown(e);
    EXPECT_TRUE(cb.IsOpen());

    cb.OnPointerDown(e);
    EXPECT_FALSE(cb.IsOpen());
}

TEST(SlateComboBox, SelectOutOfRangeIsIgnored) {
    ComboBox<std::string> cb;
    cb.SetOptions({"A"}, [](const std::string& s) { return s; });
    cb.SelectOption(5);
    EXPECT_EQ(cb.SelectedIndex.Get(), 0); // unchanged
    cb.SelectOption(-1);
    EXPECT_EQ(cb.SelectedIndex.Get(), 0);
}
