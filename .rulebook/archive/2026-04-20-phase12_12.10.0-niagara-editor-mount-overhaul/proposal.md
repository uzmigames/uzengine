# Proposal: phase12_12.10.0-niagara-editor-mount-overhaul

## Why

Phases 12.1 through 12.9 each ship visual upgrades for a specific
widget in isolation. `VfxEditorOverlay::BuildNiagaraUI` (written in
phase 11.14 as a placeholder) still:

- Uses hard-coded color constants instead of `VfxTheme()`.
- Routes clicks with bespoke logic duplicated across widgets.
- Does not wire the context menu system (12.8) into the Pass 3
  render path.
- Does not use the redesigned menu bar (12.7) or emitter tabs.
- Does not host the system overview widget (12.9) — still points
  at the parameter inspector in the right panel.
- Does not handle keyboard focus routing to the Niagara keybindings
  (F2 rename, Del, Ctrl+D, etc.).
- Does not visually verify the result with screenshots.

This final task consolidates everything: the overlay is rebuilt from
the ground up to host the full composition, every panel is wired to
its redesigned widget, context menus work end-to-end, keyboard focus
routes correctly, and visual verification via headless screenshots
confirms the result looks like Niagara.

## What Changes

1. **`BuildNiagaraUI` rewritten** from the ground up:
   - Draws background + panel chrome with
     `VfxTheme().backgroundBase` and `VfxTheme().panelBg`.
   - Runs the redesigned menu bar (12.7) above the 4-column content.
   - Runs the redesigned emitter tab bar (12.7).
   - Left column: stack panel (12.2).
   - Center column: mode-aware (ModuleParams / Curves / Parameters).
     Routes to the redesigned parameter inspector (12.4) or curves
     editor (12.5) depending on the composition's center mode.
   - Right column: system overview widget (12.9).
   - Bottom strip: timeline scrubber (12.6).

2. **Pass 3 context menu render** — after Pass 2 (dropdowns), the
   overlay renders `m_contextMenu` (12.8) on top so it's drawn
   above every other element.

3. **Menu command dispatch** — wire
   `overlay.onMenuCommand = [&](VfxMenuCommand cmd) { ... }` in
   `vfx_viewport_app.cpp` with handlers for every command
   (File_New, Edit_Undo, View_NiagaraMode toggle, etc.).

4. **Keyboard focus routing** — the overlay's `HandleMouse` updates
   `m_keybindings.SetFocusedPanel` based on which panel was clicked.
   Keystrokes then route through `m_keybindings.ProcessKeyEvent` in
   an `InjectKey(Key, bool ctrl, bool shift)` method the demo calls.

5. **Visual verification** — demo app's `--screenshot` output is
   captured at 1280x720 showing the Niagara editor with:
   - The active preset's emitter + stack in the left panel.
   - Module parameters in the center panel.
   - System overview in the right panel.
   - Timeline at the bottom with burst markers.
   - Menu bar + emitter tabs at the top.
   Screenshot is read via the Read tool and visually compared
   against a Niagara reference.

## Impact

- Affected specs: `specs/vfx-editor-mount-overhaul/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}` —
    `BuildNiagaraUI` rewritten, Pass 3 added, focus routing added,
    `InjectKey` method added.
  - `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` —
    `onMenuCommand` handlers, keystroke forwarding.
  - `runtime/tests/engine/vfx/test_vfx_editor_mount_overhaul.cpp` —
    new.
  - `docs/engine/25-vfx/62-editor-final-composition.md` — new.
- Breaking change: NO — still opt-in via F11.
- User benefit: the Niagara editor is now fully usable end-to-end,
  matching the look + behavior of reference Niagara.
