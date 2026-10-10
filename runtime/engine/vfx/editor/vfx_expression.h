#pragma once

/// VFX dynamic-input expression DSL — compact inline expressions for
/// node-graph input pins.
///
/// Niagara's Dynamic Input Scripts let authors plug small expressions
/// (multiply, lerp, clamp) into a module's input pin without creating a
/// full module asset. UzEngine's equivalent is a compact infix DSL parsed
/// at graph-compile time into an AST, then lowered to HLSL by the codegen.
///
/// # Grammar (PEG-style)
///
/// ```
/// expr     <- ternary
/// ternary  <- logic ('?' expr ':' expr)?
/// logic    <- compare (('&&' | '||') compare)*
/// compare  <- addSub (('==' | '!=' | '<' | '>' | '<=' | '>=') addSub)?
/// addSub   <- mulDiv (('+' | '-') mulDiv)*
/// mulDiv   <- unary (('*' | '/') unary)*
/// unary    <- ('-' | '!') unary | call
/// call     <- IDENT '(' argList ')' | primary
/// argList  <- expr (',' expr)*
/// primary  <- NUMBER | IDENT | '(' expr ')'
/// ```
///
/// # Built-in functions (10)
///
/// | Name      | Signature             | HLSL                           |
/// |-----------|-----------------------|--------------------------------|
/// | add       | add(a, b)             | (a) + (b)                      |
/// | sub       | sub(a, b)             | (a) - (b)                      |
/// | mul       | mul(a, b)             | (a) * (b)                      |
/// | div       | div(a, b)             | (a) / (b)                      |
/// | lerp      | lerp(a, b, t)         | lerp(a, b, t)                  |
/// | clamp     | clamp(x, lo, hi)      | clamp(x, lo, hi)               |
/// | sin       | sin(x)                | sin(x)                         |
/// | cos       | cos(x)                | cos(x)                         |
/// | dot       | dot(a, b)             | dot(a, b)                      |
/// | normalize | normalize(v)          | normalize(v)                   |
///
/// # JSON round-trip
///
/// ```json
/// { "expr": "lerp(colorStart, colorEnd, normalizedAge)" }
/// ```
///
/// The expression is stored as a plain string in the pin's JSON binding.
/// At compile time the parser produces an AST and the codegen emits the
/// HLSL snippet inline into the composed shader.
///
/// @ref UE5 UNiagaraDynamicInputScript + FNiagaraInputScriptCompiler
/// @ref docs/engine/25-vfx/27-dynamic-input-expressions.md

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AST node types
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxExprKind : uint8_t {
    Literal,    ///< Numeric constant (float).
    Identifier, ///< Named variable or parameter reference.
    Unary,      ///< Prefix operator (- !)
    Binary,     ///< Infix operator (+ - * / && || == != < > <= >=)
    Ternary,    ///< cond ? a : b
    Call,       ///< Function call: name(arg0, arg1, …)
};

/// Single node in the expression AST. Owning children via unique_ptr.
struct VfxExprNode {
    VfxExprKind kind = VfxExprKind::Literal;

    float       literalValue = 0.0f;   ///< Valid when kind == Literal.
    std::string name;                  ///< Ident name, operator char, or function name.

    std::unique_ptr<VfxExprNode> left;   ///< Unary operand, binary LHS, ternary condition.
    std::unique_ptr<VfxExprNode> right;  ///< Binary RHS, ternary true-branch.
    std::unique_ptr<VfxExprNode> third;  ///< Ternary false-branch.

    std::vector<std::unique_ptr<VfxExprNode>> args; ///< Call arguments.
};

// ─────────────────────────────────────────────────────────────────────────────
// Parse result
// ─────────────────────────────────────────────────────────────────────────────

/// Outcome of parsing an expression string.
struct VfxExprParseResult {
    std::unique_ptr<VfxExprNode> root;  ///< nullptr on failure.
    std::string errorMessage;            ///< Non-empty when root is null.
    uint32_t    errorOffset = 0u;        ///< Byte offset of the error in the source.
};

// ─────────────────────────────────────────────────────────────────────────────
// Parser
// ─────────────────────────────────────────────────────────────────────────────

/// Parses an expression source string into an AST.
///
/// Returns a result with `root != nullptr` on success. On failure, `root`
/// is null and `errorMessage` + `errorOffset` describe the problem.
///
/// Thread-safe: no global state.
[[nodiscard]] VfxExprParseResult VfxParseExpression(std::string_view source);

// ─────────────────────────────────────────────────────────────────────────────
// HLSL codegen
// ─────────────────────────────────────────────────────────────────────────────

/// Emits an HLSL snippet from a parsed AST. Parenthesises aggressively so
/// the generated code is always unambiguous regardless of HLSL operator
/// precedence.
///
/// Returns an empty string when `root` is null.
[[nodiscard]] std::string VfxExprToHLSL(const VfxExprNode* root);

// ─────────────────────────────────────────────────────────────────────────────
// Built-in function registry
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true when `name` is a recognised built-in function. Used by the
/// codegen to decide whether to emit `name(args)` directly (built-in) or
/// prefix with `uz_expr_` (user function — future extension).
[[nodiscard]] bool VfxExprIsBuiltinFunction(std::string_view name);

/// Returns the expected argument count for a built-in, or -1 if unknown.
[[nodiscard]] int VfxExprBuiltinArgCount(std::string_view name);

} // namespace uz
