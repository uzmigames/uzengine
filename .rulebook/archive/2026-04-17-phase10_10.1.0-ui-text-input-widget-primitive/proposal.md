# Proposal: phase10_10.1.0-ui-text-input-widget-primitive

## Why
The VFX editor overlay currently has a `NumberInput` helper for scalar float editing. Every other text-ish widget (tag names, expression source, asset paths) reinvents keyboard capture + buffer management ad-hoc. Phase 9 tasks 9.1, 9.2, 9.6 all need a generic string-input primitive before they can land their widgets.

## What Changes
Extract a single `StringInput(x, y, w, std::string* buffer, const InputFilter& filter)` helper on `VfxEditorOverlay`. Handles focus tracking (one active editor at a time), typed-char dispatch, backspace, Enter/Escape, and an optional validator filter (lowercase-only, numeric-only, identifier-char-only). Shared focus state across all inputs prevents the "two cursors blink" bug.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_editor_overlay.* + any consumer
- Breaking change: NO (existing NumberInput keeps its public API; it internally rebinds onto the new helper).
- User benefit: Every widget that needs a text input picks it up for free; no more duplicated keystroke loops.
- Estimated effort: M

## Source
Dear ImGui `InputText` + Niagara inline string editors.
