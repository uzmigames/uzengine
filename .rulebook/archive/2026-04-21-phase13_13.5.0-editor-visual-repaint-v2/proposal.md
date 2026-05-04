# Proposal: phase13_13.5.0-editor-visual-repaint-v2

## Why

Phases 13.1–13.4 ship the rendering primitives (icon sprites,
Unicode glyphs, rounded rects, MSDF text). On their own they don't
change how the editor looks — they enable it. This task is the
visual repaint: every phase-12 widget is re-authored to use the new
primitives so the editor finally matches the Niagara / UE5 fidelity
the user asked for.

## What Changes

Per-widget migration from phase-12's rect+ASCII to phase-13
primitives:

1. **Menu bar + emitter tab bar** — rounded bottom corners on
   dropdowns, drop shadow beneath popups, MSDF labels, sprite-atlas
   icons for the `+ Add Emitter` and each menu's leading icon.
2. **Stack panel** — every module card becomes a rounded-rect with a
   subtle border + hover shadow. Drag grip becomes `VfxUiIcon::GripDots`
   sprite. Checkbox becomes sprite-based with a 2-state atlas. Stage
   header has a left accent stripe + rounded top corners on the first
   card of each stage.
3. **Module picker dialog** — rounded 8-px panel + drop shadow. Tiles
   become rounded cards with sprite icons per category. Search bar is
   a rounded input with an inline magnifying-glass sprite.
4. **Parameter inspector** — section headers are rounded capsules.
   Reset + Bind buttons are sprite-based. Out-of-range validation
   renders a red rounded-rect border.
5. **Curves editor** — key handles become diamond sprites (atlas) +
   tangent handles are tiny circle sprites. Grid uses Unicode
   thin-line dividers instead of 1-px rect spam.
6. **Timeline** — every transport button is a sprite (triangle for
   Play, heavy bars for Pause, double-triangles for step fwd/back,
   solid square for skip, circular arrow for restart, infinity for
   loop). Speed dropdown is a rounded pill. Time readout uses MSDF
   text for crispness.
7. **Context menu** — rounded popup + drop shadow, sprite-based
   submenu chevrons, separator uses Unicode divider line.
8. **System overview panel** — rounded row cards, sprite-based status
   dots (glowing green / amber / dim), action buttons are sprites.
9. **Theme V2 palette**: minor palette tweaks to lean into the new
   primitives (accent glow alpha, shadow densities). Stays fully in
   `vfx_editor_theme.h` — no widget touches literal colors.
10. **Motion polish (optional)**: simple frame-local lerps on hover
    so cards feel responsive without a retained-mode state machine.

## Impact

- Affected specs: `specs/editor-visual-repaint-v2/spec.md` (new).
- Affected code: every file under
  `runtime/engine/vfx/editor/` that renders into `TextRenderer` —
  stack panel, module picker, parameter inspector, curves editor,
  timeline scrubber, menu bar / emitter tab bar, context menu,
  system overview widget. Theme palette file for V2 tweaks.
- Breaking change: NO — only visual output changes. Public APIs and
  callbacks of every widget remain stable.
- User benefit: the editor finally has visual identity. Triangular
  play buttons, rounded cards with shadow, crisp text at every
  zoom, sprite-based icons that read at a glance. This is the pass
  that closes the "parece um programa dos anos 90" complaint.
