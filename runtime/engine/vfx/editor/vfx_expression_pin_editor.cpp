#include "vfx_expression_pin_editor.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Open / Close
// ─────────────────────────────────────────────────────────────────────────────

void VfxExpressionPinEditor::Open(const std::string& pinEndpoint,
                                    const std::string& initialExpr) {
    open_         = true;
    pinEndpoint_  = pinEndpoint;
    expression_   = initialExpr;
    lastError_.clear();
    errorOffset_  = 0;
    generatedHLSL_.clear();
    Validate(); // initial parse
}

void VfxExpressionPinEditor::Close() noexcept {
    open_ = false;
    pinEndpoint_.clear();
    expression_.clear();
    lastError_.clear();
    errorOffset_ = 0;
    generatedHLSL_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Validate
// ─────────────────────────────────────────────────────────────────────────────

void VfxExpressionPinEditor::Validate() {
    lastError_.clear();
    errorOffset_ = 0;
    generatedHLSL_.clear();

    if (expression_.empty()) {
        lastError_ = "expression is empty";
        return;
    }

    auto parsed = VfxParseExpression(expression_);
    if (!parsed.root) {
        lastError_   = parsed.errorMessage;
        errorOffset_ = parsed.errorOffset;
        return;
    }

    generatedHLSL_ = VfxExprToHLSL(parsed.root.get());
}

// ─────────────────────────────────────────────────────────────────────────────
// Commit
// ─────────────────────────────────────────────────────────────────────────────

bool VfxExpressionPinEditor::Commit(VfxNodeGraph& graph) {
    if (!open_) return false;

    // Re-validate to guarantee we don't commit a stale broken expression.
    Validate();
    if (!IsValid()) return false;

    // Remove any existing expression entry for the same pin.
    graph.pinExpressions.erase(
        std::remove_if(graph.pinExpressions.begin(), graph.pinExpressions.end(),
                        [this](const VfxGraphPinExpression& e) {
                            return e.pinEndpoint == pinEndpoint_;
                        }),
        graph.pinExpressions.end());

    // Remove any wire whose `toEndpoint` matches the target pin —
    // expressions override wires on the same pin.
    graph.wires.erase(
        std::remove_if(graph.wires.begin(), graph.wires.end(),
                        [this](const VfxGraphWire& w) {
                            return w.toEndpoint == pinEndpoint_;
                        }),
        graph.wires.end());

    // Append the new expression binding.
    VfxGraphPinExpression entry;
    entry.pinEndpoint = pinEndpoint_;
    entry.expression  = expression_;
    graph.pinExpressions.push_back(std::move(entry));

    Close();
    return true;
}

} // namespace uz
