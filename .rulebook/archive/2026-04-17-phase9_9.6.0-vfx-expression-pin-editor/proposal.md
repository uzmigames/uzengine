# Proposal: phase9_9.6.0-vfx-expression-pin-editor

## Why
Phase 8.10.0 shipped the expression parser + HLSL codegen but the author has no way to attach an expression to a pin via the UI — the `VfxNodeGraph::pinExpressions` vector is only populated by hand-written JSON.

## What Changes
Add a pin context menu entry "Set Expression…" that opens a modal expression editor: syntax-highlighted input, live parser feedback (error offset highlights the problem char), "Test" button shows the generated HLSL.

## Impact
- Affected specs: docs/engine/25-vfx/27-dynamic-input-expressions.md
- Affected code: VFX node-graph editor widget
- Breaking change: NO
- User benefit: Inline expression authoring on any input pin.
- Estimated effort: M

## Source
UE5 Niagara Dynamic Input Script popup editor.
