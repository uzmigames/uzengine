# Proposal: phase14_14.12.0-menu-accelerator-input

## Why

Phase 14.8 added accelerator-index metadata + underline rendering
on every menu label. The metadata is in place but the input
routing is not — pressing Alt+F does not open the File menu, and
pressing `A` while the File dropdown is open does not fire Save As.
The visual cue lies; users expect the shortcut to work.

## What Changes

1. New free function `VfxMatchAccelerator(ascii, topMenus)` that
   returns the matching top-menu index (or -1).
2. New free function `VfxMatchAcceleratorInMenu(ascii, items)`
   that returns the matching item index inside a dropdown.
3. Extend `VfxEditorOverlay::HandleKey` (or equivalent) with:
   - `Alt` held + ASCII letter → open the matching top menu,
     swallow the event.
   - No modifier + ASCII letter while a dropdown is open → fire
     the matching item's command, close the dropdown.
4. Case-insensitive compare via `tolower`.
5. Items flagged `isSeparator` or `!enabled` skipped.

## Impact

- Affected specs: none (metadata already in place).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_menu.{h,cpp}` — two new
    free helpers.
  - `runtime/engine/vfx/editor/vfx_editor_overlay.cpp` — key
    handler extension.
- Breaking change: NO.
- User benefit: menu accelerator underlines now actually work.
