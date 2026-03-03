## 1. DragContext Object

- [ ] 1.1 Create `src/ui/drag_context.h` declaring `DragContext` with fields: `payload_json` (std::string), `source_element_id` (std::string), `cursor_pos` (Vec2), `grab_offset` (Vec2), `is_active` (bool)
- [ ] 1.2 Create `src/ui/drag_context.cpp` implementing `DragContext::begin(element_id, payload_table_json, cursor_pos, grab_offset)` and `DragContext::end()`
- [ ] 1.3 Store a single global `DragContext` instance in the `UIPass` compositor; only one drag operation can be active at a time
- [ ] 1.4 Trigger `DragContext::begin()` from `mousedown` on a `draggable="true"` element only after the cursor has moved >= 4px from the initial `mousedown` position (minimum drag distance threshold)

## 2. Drag Ghost Rendering

- [ ] 2.1 In `UIPass`, when `DragContext::is_active == true`, clone the source element into a detached overlay layer rendered at 80% opacity
- [ ] 2.2 Position the ghost at `cursor_pos - grab_offset` each frame; update on every `mousemove` event
- [ ] 2.3 Composite the ghost above all other UI layers in the `UICompositor` render order

## 3. Drop Target Events

- [ ] 3.1 In `src/ui/ui_pass.cpp`, on each `mousemove` while a drag is active, hit-test all elements with `dropzone="true"` attribute; fire `ondragenter` on the newly entered element and `ondragleave` on the previously hovered element
- [ ] 3.2 Fire `ondragover` continuously on the currently hovered `dropzone` element while the cursor is over it during a drag
- [ ] 3.3 On `mouseup` over a `dropzone` element, fire `ondrop` with a `DragEvent` object containing `payload` (the JSON payload) and `sourceElementId`
- [ ] 3.4 On `mouseup` outside any `dropzone`, fire `ondragcancel` on the source element

## 4. Cross-Panel Drag Support

- [ ] 4.1 In `src/ui/ui_compositor.cpp`, route `DragContext` events to elements in separate HTML document layers by transforming cursor coordinates into each layer's local space
- [ ] 4.2 Ensure `ondragenter`, `ondragleave`, and `ondrop` fire correctly across panels rendered in different `UIPass` layers

## 5. Cancellation

- [ ] 5.1 Cancel the active drag and fire `ondragcancel` on the source element when the `Escape` key is pressed during a drag
- [ ] 5.2 Cancel the active drag and fire `ondragcancel` when the application window loses focus

## 6. Lua API

- [ ] 6.1 Add `ctx.ui:begin_drag(element_id, payload_table)` Lua binding that serializes the payload table to JSON and calls `DragContext::begin()`
- [ ] 6.2 Add `ctx.ui:on_drop(element_id, handler_fn)` Lua binding registering a Lua function as the `ondrop` callback for a `dropzone` element
- [ ] 6.3 Add `ctx.ui:get_drag_payload()` Lua binding that deserializes `DragContext::payload_json` and returns it as a Lua table; callable only inside a drop handler

## 7. Gamepad and Keyboard Accessibility

- [ ] 7.1 Initiate drag on the focused `draggable="true"` element when the `Space` key or gamepad A button is pressed
- [ ] 7.2 Navigate D-pad/arrow keys to move the virtual cursor between `dropzone` elements while dragging (tab-order traversal of drop targets)
- [ ] 7.3 Confirm drop by pressing `Space`/A on the selected `dropzone` element; cancel with `Escape`/B

## 8. Testing

- [ ] 8.1 Create `tests/ui/test_ui_drag_drop.cpp` testing that `DragContext::begin()` is not triggered until the cursor moves >= 4px from the `mousedown` position
- [ ] 8.2 Test `DragContext::begin()` stores the payload JSON and source element ID correctly
- [ ] 8.3 Test ghost position updates to `cursor_pos - grab_offset` on each `mousemove` event
- [ ] 8.4 Test `ondragenter` fires when cursor enters a `dropzone` element during a drag
- [ ] 8.5 Test `ondragleave` fires when cursor exits a `dropzone` element during a drag
- [ ] 8.6 Test `ondrop` fires with the correct payload when `mouseup` occurs over a `dropzone` element
- [ ] 8.7 Test `ondragcancel` fires on `Escape` key press during an active drag
- [ ] 8.8 Test `ondragcancel` fires on focus loss during an active drag
- [ ] 8.9 Test `ctx.ui:get_drag_payload()` returns a Lua table matching the original payload passed to `ctx.ui:begin_drag()`
- [ ] 8.10 Test cross-panel drag: simulate drag from panel A drop target in panel B; verify `ondrop` fires in panel B
- [ ] 8.11 Achieve >= 95% line and branch coverage across `drag_context.cpp`, `ui_compositor.cpp` drag routing paths, and Lua drag-drop bindings

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md` documenting `draggable` and `dropzone` HTML attributes, `DragContext`, event callbacks, and cross-panel drag behavior
- [ ] 9.2 Update `docs/specs/lua-scripting.md` documenting `ctx.ui:begin_drag()`, `ctx.ui:on_drop()`, and `ctx.ui:get_drag_payload()`
- [ ] 9.3 Update `docs/specs/input-system.md` documenting keyboard/gamepad drag initiation and navigation
