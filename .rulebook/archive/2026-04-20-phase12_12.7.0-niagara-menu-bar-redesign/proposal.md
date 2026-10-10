# Proposal: phase12_12.7.0-niagara-menu-bar-redesign

## Why

The current `VfxEditorOverlay::BuildMenuBar` + `BuildMenuDropdowns`
paths render a minimal 4-entry menu (File/Edit/View/Presets) with a
placeholder dropdown that dismisses on click. There is:

- No hotkey hints aligned to the right of each menu item.
- No section separators inside dropdowns.
- No disabled-state styling.
- No emitter tab bar — we currently stack a flat preset list.
- No icons in menu items.

Niagara's top strip is what establishes the editor's "chrome
identity": menus with hotkey hints, submenu indicators, a proper
emitter tab bar directly below the menu. This task rebuilds the top
strip to match.

## What Changes

1. **Menu bar redesign** — entries: File (New, Open, Save, Save As,
   Recent submenu, Exit), Edit (Undo, Redo, separator, Cut, Copy,
   Paste, Delete, separator, Preferences), View (Niagara Mode,
   Show Emitter Overview, Show Timeline, Show Curves, Show
   Parameters), Presets (FireballProj, Fire, Smoke, …), Help (Docs,
   About). Each dropdown item has left icon glyph + label + right
   hotkey hint + optional submenu arrow.

2. **Dropdown chrome** — rounded corner dialog with `borderSubtle`,
   3-px accent underline on active menu item, separator rows rendered
   as 1-px `borderSubtle` lines.

3. **Disabled-state** — items like Undo / Redo that can't fire get
   `textDisabled` coloring and are not hit-testable.

4. **Emitter tab bar** — new row below the menu bar showing one tab
   per emitter in the active VfxSystem, with:
   - Tab label (emitter name).
   - Small colored dot indicating emitter state (running vs
     paused).
   - Active tab has `accentPrimary` underline + brighter background.
   - Scrollable horizontally when tabs overflow.
   - Rightmost `+` button that adds a new emitter (fires new
     callback).

5. **Hotkey hint rendering** — right-align a `textDim` string for
   each hotkey (e.g. "Ctrl+S", "F10"). Column width reserved so
   multiple items in a menu align.

## Impact

- Affected specs: `specs/vfx-menu-bar-redesign/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}` —
    `BuildMenuBar` / `BuildMenuDropdowns` / `BuildEmitterBar` rewritten.
  - New callbacks: `onMenuCommand(MenuCommand)` dispatcher.
  - `runtime/tests/engine/vfx/test_vfx_menu_bar.cpp` — new.
  - `docs/engine/25-vfx/59-menu-bar-visuals.md` — new.
- Breaking change: NO — all new methods / layout items; old behavior
  intact behind the legacy-mode fallback.
- User benefit: menu bar finally looks like a modern editor chrome;
  hotkeys discoverable; emitter tabs first-class.
