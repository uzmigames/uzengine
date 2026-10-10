#pragma once

/// VFX Expression Pin Editor — modal editor for per-pin expression bindings.
///
/// Phase 9.6.0. Surfaces the phase 8.10.0 expression DSL (infix parser +
/// HLSL codegen) in the editor: authors attach an HLSL-like expression
/// to any input pin in lieu of a concrete wire, and see live parser
/// feedback (error offset + generated HLSL preview) as they type.
///
/// # Lifecycle
///
/// 1. The node-graph widget's pin context menu triggers
///    `Open(pinEndpoint, initialExpr)` when the user picks
///    "Set Expression…".
/// 2. The host renders the modal (text-area + Test/Apply/Cancel) using
///    the 10.1 `StringInput` widget for the text area. It reads
///    `ExpressionBuffer()` (mutable) and calls `Validate()` each frame.
/// 3. On "Test", `GeneratedHLSL()` shows the current codegen output.
/// 4. On "Apply", `Commit(graph)` writes the expression into
///    `VfxNodeGraph::pinExpressions`, removing any matching wire.
/// 5. On "Cancel", `Close()` reverts without touching the graph.
///
/// @ref engine/vfx/editor/vfx_expression.h (parser + codegen)
/// @ref engine/vfx/editor/vfx_node_graph.h (VfxGraphPinExpression)

#include "engine/vfx/editor/vfx_expression.h"
#include "engine/vfx/editor/vfx_node_graph.h"

#include <cstdint>
#include <string>

namespace uz {

/// Pin expression editor state machine.
class VfxExpressionPinEditor {
public:
    VfxExpressionPinEditor() = default;

    /// Opens the editor on `pinEndpoint` (e.g. "n1:color") with
    /// `initialExpr` as the starting buffer. `initialExpr` may be empty.
    void Open(const std::string& pinEndpoint, const std::string& initialExpr);

    /// Closes the editor. Discards any unapplied edits.
    void Close() noexcept;

    /// Returns true when the editor modal is active.
    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

    /// Returns the target pin endpoint (valid when `IsOpen()`).
    [[nodiscard]] const std::string& PinEndpoint() const noexcept { return pinEndpoint_; }

    /// Mutable reference to the expression source buffer (for
    /// `StringInput` wiring).
    [[nodiscard]] std::string& ExpressionBuffer() noexcept { return expression_; }

    /// Read-only reference.
    [[nodiscard]] const std::string& Expression() const noexcept { return expression_; }

    /// Re-parses the current buffer. Call each frame after typing to
    /// update `LastError()` / `ErrorOffset()` / `GeneratedHLSL()`.
    void Validate();

    /// Returns the most recent parser error message (empty on success).
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

    /// Returns the byte offset of the parse error (0 when no error).
    [[nodiscard]] uint32_t ErrorOffset() const noexcept { return errorOffset_; }

    /// Returns the HLSL snippet generated from the current expression
    /// (empty on parse error).
    [[nodiscard]] const std::string& GeneratedHLSL() const noexcept { return generatedHLSL_; }

    /// Returns true when the current expression parses without error.
    [[nodiscard]] bool IsValid() const noexcept { return lastError_.empty(); }

    /// Commits the expression to the target graph's `pinExpressions`
    /// vector. Removes any existing `pinExpressions` entry for the same
    /// pin, plus any wire whose `toEndpoint` matches (expressions override
    /// wires). Closes the editor on success.
    ///
    /// # Returns
    /// `true` when commit succeeded (expression was valid and written).
    /// `false` on parse error — the editor stays open.
    bool Commit(VfxNodeGraph& graph);

private:
    bool        open_         = false;
    std::string pinEndpoint_;
    std::string expression_;
    std::string lastError_;
    uint32_t    errorOffset_  = 0;
    std::string generatedHLSL_;
};

} // namespace uz
