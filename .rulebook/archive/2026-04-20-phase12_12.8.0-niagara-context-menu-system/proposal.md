# Proposal: phase12_12.8.0-niagara-context-menu-system

## Why

Phase 11 shipped a handful of callbacks that imply a context menu
(`onModuleRemoveRequested`, `onModuleDuplicateRequested`,
`onRevertOverrideRequested`, the new 12.2
`onModuleContextMenuRequested`), but there is no actual context menu
widget — clicking the overflow menu today does nothing. Phases 12.2
(stack), 12.4 (parameters), 12.5 (curves) each need right-click /
overflow menus with consistent styling and behavior.

Building three separate menu widgets is wasteful — a unified context
menu system serves all three panels and remains open across frames
until dismissed.

## What Changes

1. **`VfxContextMenuWidget` new class** in
   `engine/vfx/editor/vfx_context_menu_widget.{h,cpp}`:
   - `struct Item { std::string label; std::string hotkey; std::string iconGlyph; std::function<void()> onActivate; bool isSeparator; bool enabled; std::vector<Item> submenu; }`
   - `void Open(float anchorX, float anchorY, std::vector<Item>&& items, float screenW, float screenH)`
   - `void Close() noexcept`
   - `bool IsOpen() const noexcept`
   - `ComputeLayout()` / `HitTest()` / `Render()` (same pure-data
     convention as every other widget).
   - Submenu support: hovering an item with a non-empty `submenu`
     for > 200 ms opens the nested menu to the side.
   - Anchor-aware positioning: if the menu would overflow the
     screen, flips to the opposite side of the anchor.
   - Escape dismisses; click-outside dismisses.

2. **Module card context menu items** (12.2 wires this) —
   `Rename…`, `Duplicate` (Ctrl+D), separator, `Delete` (Del),
   `Revert to Parent` (only when overrideFlag != None), separator,
   `Isolate` (solo the module), `Copy Path`.

3. **Parameter inspector context menu items** (12.4 wires this) —
   `Copy Value`, `Paste Value`, separator, `Reset to Default`,
   separator, `Bind to Curve…`, `Bind to Expression…`.

4. **Curve key context menu items** (12.5 wires this) —
   `Set Tangent ▸` (submenu: Auto, Linear, Cubic, Constant),
   separator, `Delete Key` (Del).

5. **VfxEditorOverlay integration** — adds a `VfxContextMenuWidget
   m_contextMenu` member rendered in Pass 3 (on top of Niagara UI +
   dropdowns). All affected widgets dispatch their context menu items
   through the same widget so styling is consistent.

## Impact

- Affected specs: `specs/vfx-context-menu-system/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_context_menu_widget.{h,cpp}` — new.
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}` —
    member + Pass 3 render + callback wiring.
  - `runtime/tests/engine/vfx/test_vfx_context_menu.cpp` — new.
  - `docs/engine/25-vfx/60-context-menu.md` — new.
- Breaking change: NO — purely additive.
- User benefit: right-click / overflow menu finally works, with
  consistent look + behavior across all panels.
