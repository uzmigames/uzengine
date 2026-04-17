# Proposal: phase10_10.2.0-ui-modal-dialog-framework

## Why
Multiple phase 9 tasks (9.2 inheritance picker, 9.6 expression editor, 9.7 thumbnail progress) need a modal dialog that blocks the underlying UI, captures input, and dismisses via Enter / Escape / click-outside. The overlay has no modal primitive — widgets today render inline and can't offer focused authoring flows.

## What Changes
Add `VfxModalStack` service to `VfxEditorOverlay`: push/pop of named modals, automatic dim overlay behind the topmost modal, exclusive input capture, and a standard title + body + buttons frame. Modal body content provided via a callback that the overlay invokes each frame while the modal is active.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_editor_overlay.*
- Breaking change: NO
- User benefit: Focused authoring flows (parent picker, expression editor, confirm dialog) become possible.
- Estimated effort: M

## Source
Dear ImGui `BeginPopupModal` + standard OS modal dialog convention.
