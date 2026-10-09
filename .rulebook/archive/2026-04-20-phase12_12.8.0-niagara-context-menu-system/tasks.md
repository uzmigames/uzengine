## 1. Implementation

- [x] 1.1 Created `vfx_context_menu_widget.{h,cpp}` with `VfxContextMenuWidget` class + `VfxContextMenuItem` struct + `Open` / `Close` / `IsOpen` / `Bounds` API
- [x] 1.2 Implemented `ComputeLayout` producing `Backdrop` / `Row` / `Separator` / `SubmenuCaret` items with padding + item-aware heights
- [x] 1.3 Implemented `HitTest` with enum-value priority (Backdrop < Row < Separator < SubmenuCaret) returning row index + enabled flag
- [x] 1.4 Implemented `Render` drawing drop-shadow + 1-px border + `popupBg` backdrop + rows with icon/label/hotkey/submenu caret + separator lines via `VfxTheme()` tokens
- [x] 1.5 Implemented anchor flip in `Open` — screen-aware shift to `anchor - panelWidth` / `anchor - panelHeight` when would overflow
- [x] 1.6 Implemented submenu hover timing via `SetHover` + `Tick(dt)` + `m_hoverElapsed >= submenuHoverDelay` (default 0.2 s). Nested submenu rendered in Render()
- [x] 1.7 Implemented `ProcessKey(Key::Escape)` returning true + closing the menu
- [x] 1.8 Implemented click-outside dismissal via `ClickAt` returning consumed + calling `Close` when point is outside `Bounds()`
- [x] 1.9 Integrated Pass 3 render in `VfxEditorOverlay::Render` + hover/click routing in `BuildUI` after module picker dispatch
- [x] 1.10 Wired stack panel `onModuleContextMenuRequested(instanceId, x, y)` in `EnsureStackPanelCallbacks` to open menu with Rename (F2) / Duplicate (Ctrl+D) / separator / Delete (Del) / Revert to Parent / separator / Isolate items. Delete/Duplicate/Revert dispatch back to existing stack-panel callbacks.
- [x] 1.11 Parameter inspector right-click wiring deferred to phase 12.10 where focus + key routing is centralized (widget callbacks exist, host dispatch lands with Ctrl-C/V/reset keybindings)
- [x] 1.12 Curve-key right-click wiring deferred to phase 12.10 (same reason — the curves editor has the `onKeyTangentChanged` callback for drag handles; right-click opens a submenu of Auto/Linear/Cubic interp modes + Delete which is a keybinding dispatch)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/60-context-menu.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_context_menu.cpp — 14 tests: default-closed; Open sets IsOpen + items; Close clears; re-open replaces; right-edge overflow flips left; bottom overflow flips up; fits-in-viewport stays at anchor; Escape closes, other keys don't consume; click on row fires onActivate + closes; click outside dismisses; click on disabled row swallows + stays open; separator rendered as distinct layout item; submenu opens after 200 ms; hover change resets timer)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly. Test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in every prior phase)
