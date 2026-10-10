# vfx-editor-mount-overhaul

## ADDED Requirements

### Requirement: BuildNiagaraUI uses theme tokens only

No call in `VfxEditorOverlay::BuildNiagaraUI` SHALL reference a
hard-coded `Vec4{...}` literal for color. Every color value must come
from `VfxTheme()`.

#### Scenario: Static analysis finds no color literals

Given the source of `BuildNiagaraUI`
When a grep for `Vec4{[0-9]` is run
Then zero matches are found inside the function body.

### Requirement: Pass 3 renders context menu above everything

`VfxEditorOverlay::Render` SHALL render the bound context menu last
(after the dropdowns Pass 2) so it always appears on top.

#### Scenario: Context menu rects emit after all other rects

Given a context menu opened over a stack panel
When `Render` runs with Pass 1 + Pass 2 + Pass 3 in sequence
Then the index of the first context-menu rect is greater than the
index of every stack-panel rect in the emitted list.

### Requirement: Center panel mode routes to redesigned widget

When `m_composition.CenterMode() == Curves`, the center panel SHALL
render the redesigned curves editor (12.5) — not the placeholder
"Select a module" text.

#### Scenario: Curves mode renders curves

Given a center mode of `Curves` with a bound curve
When `BuildNiagaraUI` runs
Then `m_curvesEditor.Render` is invoked with the center content rect
(observable via a capture-enabled mock).

### Requirement: Right panel hosts system overview

`BuildNiagaraUI` SHALL invoke `m_systemOverview.Render` (not the
parameter inspector) for the right panel rect.

#### Scenario: Right panel draws overview rows

Given a bound system with 2 emitters
When `BuildNiagaraUI` runs
Then the right-panel emitted rect list contains at least 2
`EmitterRow` items from the overview widget.

### Requirement: InjectKey dispatches to keybindings

`VfxEditorOverlay::InjectKey(Key, ctrl, shift)` SHALL forward to
`m_keybindings.ProcessKeyEvent` and return its return value.

#### Scenario: Ctrl+Z fires undo

Given an overlay with a command pushed on the undo stack + Stack
focus
When `InjectKey(Key::Z, ctrl=true, shift=false)` is called
Then the return is `true` and `keybindings.UndoCount()` is 1.

### Requirement: Menu command callback wired in demo

The `VFXViewportApp` SHALL bind `overlay->onMenuCommand` to a handler
that processes at least `File_Save`, `Edit_Undo`, `Edit_Redo`,
`View_NiagaraMode`.

#### Scenario: View_NiagaraMode toggles flag

Given the overlay is visible with Niagara mode off
When `onMenuCommand(View_NiagaraMode)` fires
Then `overlay.IsNiagaraMode()` returns `true`.
