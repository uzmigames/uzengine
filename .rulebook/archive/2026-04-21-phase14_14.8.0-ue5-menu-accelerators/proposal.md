# Proposal: phase14_14.8.0-ue5-menu-accelerators

## Why

UE5 menu bar labels underline the accelerator character — e.g.
`File` renders as `F` with a 1-px underline + `ile`, indicating
Alt+F opens the menu. Dropdown items follow the same pattern
(`Save _A_s...`). This is pervasive muscle memory for Windows
users and a zero-cost visual polish when the MSDF text pipeline
is already live.

## What Changes

1. Extend `VfxTopMenu` + `VfxMenuItem` to store an optional
   `acceleratorIndex` (byte offset into the `label` string at
   which the underlined character sits; default `-1` = no
   accelerator).
2. Declare accelerators on all shipped menu entries (File → F,
   Edit → E, View → V, Presets → P, Help → H; sub-items follow
   standard Windows convention: `_N_ew`, `_O_pen`, `Save _A_s`).
3. Render pass: when `acceleratorIndex >= 0`, after the label is
   drawn, emit a 1-px `textPrimary` rectangle underneath the
   accelerator character. Underline width = character advance
   width at the rendered pixel height.
4. Handle Alt+key chord in `HandleKey`: intercept Alt-held key
   presses and open the matching menu.
5. When a dropdown is open and the user presses a bare letter
   key, fire the matching menu item's command.

## Impact

- Affected specs: `specs/ue5-menu-accelerators/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_menu.h` (schema +
    default table).
  - `runtime/engine/vfx/editor/vfx_editor_overlay.cpp` (render +
    key handler).
- Breaking change: NO — defaulting `acceleratorIndex = -1` keeps
  phase-12 data-free menu definitions compiling.
- User benefit: Alt-key menu navigation matches every Windows
  editor expectation.
