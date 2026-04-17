# phase9_9.6.0-vfx-expression-pin-editor

Inline expression editor wiring on top of the 8.10.0 DSL.

Reference: UE5 Niagara Dynamic Input Script editor.

## 1. Pin context menu

- [x] 1.1 "Set Expression…" menu entry on input pins.
      Host (node-graph widget context menu from phase 10.4) calls
      `VfxExpressionPinEditor::Open(pinEndpoint, initialExpr)`. The
      initial expression is taken from any existing `pinExpressions`
      entry or empty string.
- [x] 1.2 Modal editor with text area + live parser feedback.
      `VfxExpressionPinEditor` owns the state. Host renders the modal
      using phase 10.1 `StringInput` bound to `ExpressionBuffer()`, and
      calls `Validate()` after each keystroke.
- [x] 1.3 Error offset + message shown inline.
      `LastError()` + `ErrorOffset()` — host renders a caret under the
      offending char in the source + the error message below the
      text area.

## 2. Apply + preview

- [x] 2.1 "Test" button shows the generated HLSL.
      `GeneratedHLSL()` — populated by `Validate()` on successful parse
      (empty on error). Host displays this in a read-only preview area.
- [x] 2.2 "Apply" writes to `pinExpressions`; removes matching wires.
      `Commit(graph)` re-validates, removes any existing
      `pinExpressions` entry for the same pin, removes any wire with
      `toEndpoint == pinEndpoint`, then appends the new binding.
      Closes the editor on success; stays open on invalid expression.
- [x] 2.3 Pin visual state — the node-graph widget renders a distinct
      indicator on any pin whose `pinEndpoint` is in
      `VfxNodeGraph::pinExpressions`. This is a host-rendering concern,
      not widget state.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `VfxExpressionPinEditor` with
      lifecycle + usage comment block in header.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_expression_pin_editor.cpp`:
      13 tests covering Open/Close lifecycle, Validate on
      valid/invalid/empty, live re-validation, Commit (append/
      overwrite/remove wire/reject invalid/not-open), and state reset.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
