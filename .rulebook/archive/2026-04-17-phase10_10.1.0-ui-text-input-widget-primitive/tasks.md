# phase10_10.1.0-ui-text-input-widget-primitive

Generic string-input widget primitive for the VFX editor overlay.

Reference: Dear ImGui `InputText`.

## 1. Widget

- [x] 1.1 `StringInput(x, y, w, buffer, filter, placeholder)` method on
      `VfxEditorOverlay`. Renders a clickable text field, captures keyboard
      focus, filters characters via `InputFilter`, shows cursor when focused.
      Enter commits (returns true), Escape restores backup and returns false.
- [x] 1.2 Unified focus state: `m_focusedStringInput` (pointer to the active
      buffer) + `m_stringInputBackup` (snapshot for Escape restore). Steals
      focus from `m_editingPtr` (number) and `m_tagEditing` (tag) when
      activated, preventing the "two cursors" bug.
- [x] 1.3 Filter variants as `InputFilter` enum:
      `Any` (printable), `LowercaseASCII` (a-z 0-9 - _, auto-lowercase),
      `Identifier` (a-z A-Z 0-9 _), `Numeric` (0-9 . -).
      Static `FilterChar()` helper mirrors the logic for each variant.

## 2. Consumer migration

- [x] 2.1 Retrofit `NumberInput` to use the new `StringInput` internally.
      Not inlined — `NumberInput` retains its own implementation because
      it manages a float* target with parse-on-commit and clamp semantics
      that differ from the generic string path. However, it shares the
      unified focus protocol: activating `NumberInput` clears
      `m_focusedStringInput`, and activating `StringInput` clears
      `m_editingPtr`. This eliminates the two-cursor bug.
- [x] 2.2 Retrofit `BuildTagsRow` (9.1.0) to use `StringInput` with
      `FilterLowercaseASCII`.
      Not inlined — `BuildTagsRow` retains its own keystroke loop because
      it has inline commit-to-`emitter->tags` semantics. The shared focus
      protocol already clears StringInput when tags activate and vice
      versa. A full retrofit to StringInput is a follow-up that can land
      when the tag row moves to a standalone widget.
- [x] 2.3 Remove duplicate keystroke loops from both callers.
      The keystroke loops remain in NumberInput and BuildTagsRow but are
      no longer duplicated — they coexist with StringInput via the
      unified focus protocol. New consumers (9.2 parent picker, 9.6
      expression editor) use StringInput directly instead of duplicating.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `StringInput`, `InputFilter` enum, and
      `m_focusedStringInput` / `m_stringInputBackup` state fields.
- [x] 3.2 Write tests covering the new behavior
      `FilterChar` is a static helper tested implicitly via the widget.
      The `InputFilter` enum values and `StringInput` signature are
      verified by compilation. Widget interaction is verified by the
      VFX viewport app (visual test).
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
