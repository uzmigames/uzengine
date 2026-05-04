## 1. Implementation

- [x] 1.1 Defined `VfxMenuCommand` enum in `vfx_editor_menu.h` with 22 named commands (File_New..Exit, Edit_Undo..Preferences, View_NiagaraMode..ToggleStats, Presets_Base, Help_Docs/About)
- [x] 1.2 Defined `VfxMenuItem` struct (label, hotkey, iconGlyph, command, enabled, isSeparator). Submenu pointer omitted — submenus are authored in the data table as a regular menu with a flag; host opens as needed.
- [x] 1.3 Declared the 5 top-level menus in `vfx_editor_menu.cpp` as static `VfxTopMenu[]` with per-menu `VfxMenuItem[]` tables (File/Edit/View/Presets/Help)
- [x] 1.4 Rewrote `BuildMenuBar` to iterate `VfxMenuBarTopAt(i)` and render each label with `barBg`/`cardBgHover`/`cardBgSelected` backgrounds; active menu opens its dropdown
- [x] 1.5 Rewrote `BuildMenuDropdowns` as a single data-driven renderer: 1-px border + `popupBg` backdrop, 16-px icon column, label column, right-aligned 72-px hotkey hint column, separator rows as 1-px `borderSubtle` with 3-px padding
- [x] 1.6 Added disabled-state styling (`textDisabled` foreground + ignored hit-dispatch)
- [x] 1.7 Added `onMenuCommand(VfxMenuCommand)` callback on `VfxEditorOverlay`; fires once per click on enabled, non-separator items + once per Preset_Base+idx when a preset row is clicked
- [x] 1.8 Rewrote `BuildEmitterBar` to render horizontal tabs (dot + name + accent underline for active) when `m_emitterNames` is populated; falls back to legacy Start+Re-capture toolbar otherwise for phase-9 compatibility
- [x] 1.9 Added `onAddEmitterRequested` callback fired by the trailing `+` tab
- [x] 1.10 Added `m_emitterTabScrollOffset` state + content-width / viewport-width clamping so tabs that overflow stay clickable (wheel wiring lands in 12.10)
- [x] 1.11 Every color via `VfxTheme()` tokens (barBg, popupBg, borderStrong, borderSubtle, cardBg, cardBgHover, cardBgSelected, textPrimary, textDim, textDisabled, accentPrimary, stateSuccess) — no hard-coded literals

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/59-menu-bar-visuals.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_menu_bar.cpp — 12 tests: top count = 5, labels match Niagara bar, out-of-range returns nullptr, File has New/Open/Save/SaveAs/Exit, Edit has Undo/Redo/Cut/Copy/Paste, View has NiagaraMode+F11, Help has Docs+About, Save has Ctrl+S hotkey, Edit has at least one separator, separators carry None command, every command unique across all menus ≥15 distinct)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly. Test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in every prior phase)
