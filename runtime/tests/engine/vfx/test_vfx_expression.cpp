/// Unit tests for phase 8.10.0 — VFX dynamic-input expression DSL.
///
/// Validates the parser (happy path + error recovery) and the HLSL codegen
/// for all 10 built-in functions plus infix operators.
///
/// @ref engine/vfx/editor/vfx_expression.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_expression.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Parser — literals + identifiers
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpression, ParseLiteral) {
    auto r = VfxParseExpression("42.5");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Literal);
    EXPECT_FLOAT_EQ(r.root->literalValue, 42.5f);
}

TEST(VfxExpression, ParseIdentifier) {
    auto r = VfxParseExpression("normalizedAge");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Identifier);
    EXPECT_EQ(r.root->name, "normalizedAge");
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser — infix operators + precedence
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpression, ParseAddSub) {
    auto r = VfxParseExpression("a + b - c");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Binary);
    EXPECT_EQ(r.root->name, "-");
}

TEST(VfxExpression, PrecedenceMulOverAdd) {
    // "a + b * c" should parse as "a + (b * c)"
    auto r = VfxParseExpression("a + b * c");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Binary);
    EXPECT_EQ(r.root->name, "+");
    EXPECT_EQ(r.root->right->kind, VfxExprKind::Binary);
    EXPECT_EQ(r.root->right->name, "*");
}

TEST(VfxExpression, Parentheses) {
    auto r = VfxParseExpression("(a + b) * c");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Binary);
    EXPECT_EQ(r.root->name, "*");
    EXPECT_EQ(r.root->left->kind, VfxExprKind::Binary);
    EXPECT_EQ(r.root->left->name, "+");
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser — unary
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpression, UnaryMinus) {
    auto r = VfxParseExpression("-x");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Unary);
    EXPECT_EQ(r.root->name, "-");
}

TEST(VfxExpression, UnaryBang) {
    auto r = VfxParseExpression("!flag");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Unary);
    EXPECT_EQ(r.root->name, "!");
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser — function calls
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpression, ParseCallSingleArg) {
    auto r = VfxParseExpression("sin(x)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Call);
    EXPECT_EQ(r.root->name, "sin");
    ASSERT_EQ(r.root->args.size(), 1u);
}

TEST(VfxExpression, ParseCallMultipleArgs) {
    auto r = VfxParseExpression("lerp(a, b, 0.5)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Call);
    EXPECT_EQ(r.root->name, "lerp");
    ASSERT_EQ(r.root->args.size(), 3u);
}

TEST(VfxExpression, NestedCalls) {
    auto r = VfxParseExpression("clamp(sin(x), 0, 1)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Call);
    EXPECT_EQ(r.root->name, "clamp");
    ASSERT_EQ(r.root->args.size(), 3u);
    EXPECT_EQ(r.root->args[0]->kind, VfxExprKind::Call);
    EXPECT_EQ(r.root->args[0]->name, "sin");
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser — ternary
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpression, Ternary) {
    auto r = VfxParseExpression("flag ? a : b");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Ternary);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser — comparison + logic
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpression, Comparison) {
    auto r = VfxParseExpression("a > b");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Binary);
    EXPECT_EQ(r.root->name, ">");
}

TEST(VfxExpression, LogicAndOr) {
    auto r = VfxParseExpression("a > 0 && b < 1 || c == 2");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(r.root->kind, VfxExprKind::Binary);
    EXPECT_EQ(r.root->name, "||");
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser — error handling
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExpression, ErrorOnEmptyInput) {
    auto r = VfxParseExpression("");
    EXPECT_EQ(r.root, nullptr);
    EXPECT_FALSE(r.errorMessage.empty());
}

TEST(VfxExpression, ErrorOnMismatchedParen) {
    auto r = VfxParseExpression("(a + b");
    EXPECT_EQ(r.root, nullptr);
    EXPECT_NE(r.errorMessage.find(")"), std::string::npos);
}

TEST(VfxExpression, ErrorOnTrailingToken) {
    auto r = VfxParseExpression("a b");
    EXPECT_EQ(r.root, nullptr);
    EXPECT_FALSE(r.errorMessage.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL codegen
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExprCodegen, LiteralEmitsFloat) {
    auto r = VfxParseExpression("42");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "42.0");
}

TEST(VfxExprCodegen, IdentPassesThrough) {
    auto r = VfxParseExpression("myVar");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "myVar");
}

TEST(VfxExprCodegen, BinaryParenthesised) {
    auto r = VfxParseExpression("a + b");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "(a + b)");
}

TEST(VfxExprCodegen, AddBuiltinInlinesOperator) {
    auto r = VfxParseExpression("add(x, y)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "(x + y)");
}

TEST(VfxExprCodegen, MulBuiltinInlinesOperator) {
    auto r = VfxParseExpression("mul(x, y)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "(x * y)");
}

TEST(VfxExprCodegen, LerpEmitsDirectCall) {
    auto r = VfxParseExpression("lerp(a, b, t)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "lerp(a, b, t)");
}

TEST(VfxExprCodegen, ClampEmitsDirectCall) {
    auto r = VfxParseExpression("clamp(x, 0, 1)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "clamp(x, 0.0, 1.0)");
}

TEST(VfxExprCodegen, NormalizeEmitsDirectCall) {
    auto r = VfxParseExpression("normalize(v)");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "normalize(v)");
}

TEST(VfxExprCodegen, TernaryEmitsConditional) {
    auto r = VfxParseExpression("flag ? a : b");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()), "((flag) ? (a) : (b))");
}

TEST(VfxExprCodegen, ComplexExpression) {
    auto r = VfxParseExpression("lerp(colorStart, colorEnd, clamp(normalizedAge, 0, 1))");
    ASSERT_NE(r.root, nullptr);
    EXPECT_EQ(VfxExprToHLSL(r.root.get()),
              "lerp(colorStart, colorEnd, clamp(normalizedAge, 0.0, 1.0))");
}

TEST(VfxExprCodegen, NullRootReturnsEmpty) {
    EXPECT_EQ(VfxExprToHLSL(nullptr), "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Built-in registry
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxExprBuiltins, AllTenRecognised) {
    EXPECT_TRUE(VfxExprIsBuiltinFunction("add"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("sub"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("mul"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("div"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("lerp"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("clamp"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("sin"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("cos"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("dot"));
    EXPECT_TRUE(VfxExprIsBuiltinFunction("normalize"));
    EXPECT_FALSE(VfxExprIsBuiltinFunction("unknown"));
}

TEST(VfxExprBuiltins, ArgCountsCorrect) {
    EXPECT_EQ(VfxExprBuiltinArgCount("sin"), 1);
    EXPECT_EQ(VfxExprBuiltinArgCount("add"), 2);
    EXPECT_EQ(VfxExprBuiltinArgCount("lerp"), 3);
    EXPECT_EQ(VfxExprBuiltinArgCount("unknown"), -1);
}
