# Proposal: phase3-ui-drag-drop

## Why
Inventory systems, skill trees, crafting interfaces, and editor panels all require drag-and-drop interaction. No drag-and-drop API exists in the UzEngine UI system (`uz_ui_html.dll`). Current workarounds using continuous mouse position polling inside `onmousemove` handlers are unreliable (miss frames, no drop target awareness) and do not support cross-panel dragging where the drag source and drop target belong to different HTML documents rendered in separate UIPass layers.

## What Changes
- Add `DragContext` object created on `mousedown` + minimum drag distance threshold (4px): stores payload data (arbitrary Lua table serialized to JSON), drag source element reference, and current cursor position
- Visual drag ghost: cloned element rendered at 80% opacity following the cursor, offset by the grab point; ghost is composited above all other UI layers in UIPass
- CSS event callbacks on drop target elements: `ondragenter`, `ondragover`, `ondragleave`, `ondrop` — fired with `DragEvent` containing payload and source element
- Drop target registration: `draggable="true"` HTML attribute marks drag sources; `dropzone="true"` marks valid targets
- Cross-panel drag support: `DragContext` is global to the UIPass compositor so drag events route correctly across separate HTML document layers
- Drag cancellation on `Escape` key or focus loss fires `ondragcancel` on the source element
- Lua API: `ctx.ui:begin_drag(element, payload_table)`, `ctx.ui:on_drop(element, handler)`, `ctx.ui:get_drag_payload()` inside a drop handler
- Gamepad/keyboard accessibility: drag initiation via `Space` key on focused draggable element, directional navigation selects drop target, `Space` to drop

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/input-system.md`
- Affected code: `src/ui/drag_context.h`, `src/ui/drag_context.cpp`, `src/ui/ui_pass.cpp`, `src/ui/ui_compositor.cpp`, `src/lua/lua_ui_api.cpp`
- Breaking change: NO
- User benefit: Inventory, skill tree, and crafting UIs can be built with standard HTML drag-and-drop semantics; cross-panel drag (e.g. dragging an item from inventory to hotbar) works without any custom mouse tracking code
