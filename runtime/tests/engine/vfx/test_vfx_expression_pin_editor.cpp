/// Unit tests for phase 9.6.0 — VFX expression pin editor.
///
/// @ref engine/vfx/editor/vfx_expression_pin_editor.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_expression_pin_editor.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Open / Close lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpressionPinEditor, StartsClosed) {
    VfxExpressionPinEditor ed;
    EXPECT_FALSE(ed.IsOpen());
    EXPECT_TRUE(ed.PinEndpoint().empty());
}

TEST(VfxExpressionPinEditor, OpenSetsState) {
    VfxExpressionPinEditor ed;
    ed.Open("n1:color", "lerp(a, b, t)");
    EXPECT_TRUE(ed.IsOpen());
    EXPECT_EQ(ed.PinEndpoint(), "n1:color");
    EXPECT_EQ(ed.Expression(), "lerp(a, b, t)");
}

TEST(VfxExpressionPinEditor, OpenParsesInitialExpression) {
    VfxExpressionPinEditor ed;
    ed.Open("n1:x", "1 + 2");
    EXPECT_TRUE(ed.IsValid());
    EXPECT_FALSE(ed.GeneratedHLSL().empty());
}

TEST(VfxExpressionPinEditor, OpenWithEmptyExprFlagsError) {
    VfxExpressionPinEditor ed;
    ed.Open("n1:x", "");
    EXPECT_FALSE(ed.IsValid());
    EXPECT_FALSE(ed.LastError().empty());
}

TEST(VfxExpressionPinEditor, CloseResetsState) {
    VfxExpressionPinEditor ed;
    ed.Open("n1:x", "1 + 2");
    ed.Close();
    EXPECT_FALSE(ed.IsOpen());
    EXPECT_TRUE(ed.PinEndpoint().empty());
    EXPECT_TRUE(ed.Expression().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Validate (live parser feedback)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpressionPinEditor, ValidateOnValidExpression) {
    VfxExpressionPinEditor ed;
    ed.Open("n:p", "x * 2.0");
    ed.Validate();
    EXPECT_TRUE(ed.IsValid());
    EXPECT_TRUE(ed.LastError().empty());
    EXPECT_FALSE(ed.GeneratedHLSL().empty());
}

TEST(VfxExpressionPinEditor, ValidateOnInvalidExpression) {
    VfxExpressionPinEditor ed;
    ed.Open("n:p", "1 + ");
    ed.Validate();
    EXPECT_FALSE(ed.IsValid());
    EXPECT_FALSE(ed.LastError().empty());
    EXPECT_TRUE(ed.GeneratedHLSL().empty());
}

TEST(VfxExpressionPinEditor, ValidateUpdatesAfterBufferEdit) {
    VfxExpressionPinEditor ed;
    ed.Open("n:p", "1 + 2");
    EXPECT_TRUE(ed.IsValid());

    // Simulate user typing broken expression.
    ed.ExpressionBuffer() = "1 +";
    ed.Validate();
    EXPECT_FALSE(ed.IsValid());

    // Fix it.
    ed.ExpressionBuffer() = "1 + 3";
    ed.Validate();
    EXPECT_TRUE(ed.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// Commit
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpressionPinEditor, CommitValidExpressionAppendsEntry) {
    VfxNodeGraph g;
    g.nodes.push_back({"n1", "Module", {0, 0}});

    VfxExpressionPinEditor ed;
    ed.Open("n1:color", "lerp(a, b, t)");

    const bool ok = ed.Commit(g);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(ed.IsOpen()); // closed on success
    ASSERT_EQ(g.pinExpressions.size(), 1u);
    EXPECT_EQ(g.pinExpressions[0].pinEndpoint, "n1:color");
    EXPECT_EQ(g.pinExpressions[0].expression,  "lerp(a, b, t)");
}

TEST(VfxExpressionPinEditor, CommitOverwritesExistingExpression) {
    VfxNodeGraph g;
    g.pinExpressions.push_back({"n1:color", "old_expr"});

    VfxExpressionPinEditor ed;
    ed.Open("n1:color", "new_expr + 1");
    ed.Commit(g);

    ASSERT_EQ(g.pinExpressions.size(), 1u);
    EXPECT_EQ(g.pinExpressions[0].expression, "new_expr + 1");
}

TEST(VfxExpressionPinEditor, CommitRemovesMatchingWire) {
    VfxNodeGraph g;
    g.wires.push_back({"n0:out", "n1:color", VfxPinType::Vec4});
    g.wires.push_back({"n2:out", "n3:in",    VfxPinType::Float});

    VfxExpressionPinEditor ed;
    ed.Open("n1:color", "1 + 1");
    ed.Commit(g);

    // The wire targeting n1:color is removed; the unrelated wire stays.
    ASSERT_EQ(g.wires.size(), 1u);
    EXPECT_EQ(g.wires[0].toEndpoint, "n3:in");
    EXPECT_EQ(g.pinExpressions.size(), 1u);
}

TEST(VfxExpressionPinEditor, CommitRejectsInvalidExpression) {
    VfxNodeGraph g;

    VfxExpressionPinEditor ed;
    ed.Open("n:p", "1 + ");
    const bool ok = ed.Commit(g);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(ed.IsOpen()); // stays open on error
    EXPECT_EQ(g.pinExpressions.size(), 0u);
}

TEST(VfxExpressionPinEditor, CommitWhenNotOpenFails) {
    VfxNodeGraph g;
    VfxExpressionPinEditor ed;
    // Not opened — commit should fail cleanly.
    const bool ok = ed.Commit(g);
    EXPECT_FALSE(ok);
}
