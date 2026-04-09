## 1. CSS Custom Property Data Binding

- [ ] 1.1 Create `runtime/engine/ui/ui_data_binding.h/.cpp` — UiDataBinding: SetProperty(name, value) stores string value. GetProperty(name) → string. Properties inherited down UIElement tree. Dirty flag triggers re-layout on change.
- [ ] 1.2 Integrate var() resolution into StyleResolver — When CSS value contains `var(--name)`, resolve from UiDataBinding. Support fallback: `var(--name, default)`. Resolve at layout time, not parse time.
- [ ] 1.3 Add calc() support — Basic calc: `calc(var(--health) * 1%)`. Support +, -, *, / with px and % units. Evaluate during layout.

## 2. UI Event System

- [ ] 2.1 Create `runtime/engine/ui/ui_event.h` — UiEvent: element_id (string), element_class (string), event_type enum (Click, Hover, HoverExit, Change, Focus, Blur), value (string), mouse_position (Vec2).
- [ ] 2.2 Create `runtime/engine/ui/ui_event_dispatcher.h/.cpp` — EventDispatcher: hit test mouse position against layout rects. Determine hovered element. On mouse down+up on same element → Click. Track hover state for HoverEnter/HoverExit. Input[range] → Change on drag.
- [ ] 2.3 Add event callback registration — UiDocument::OnEvent(element_id, event_type, callback). Callbacks dispatched on main thread after input processing.

## 3. Input Routing

- [ ] 3.1 Create `runtime/engine/ui/ui_input_router.h/.cpp` — UiInputRouter: manages modal/passthrough stack. When modal UI active: consume all keyboard/mouse input. When passthrough: only consume clicks on UI elements. IsInputBlocked() queried by InputSubsystem.
- [ ] 3.2 Wire to InputSubsystem — InputSubsystem::OnTick: if UiInputRouter::IsInputBlocked(), skip gameplay context evaluation for consumed keys.

## 4. Focus Manager

- [ ] 4.1 Create `runtime/engine/ui/focus_manager.h/.cpp` — FocusManager: tracks focused element. Tab/Shift+Tab cycles focusable elements (button, input). Gamepad: D-pad navigates spatially (nearest neighbor in direction). Focus ring visual (CSS :focus pseudo-class).

## 5. UIAnimator

- [ ] 5.1 Create `runtime/engine/ui/ui_animator.h/.cpp` — UIAnimator: Animate(element_id, property, from, to, duration, easing). Easing: Linear, EaseIn, EaseOut, EaseInOut. Update(dt) interpolates values and calls SetProperty. Queue of active animations per element.

## 6. Lua UI API

- [ ] 6.1 Create `runtime/engine/scripting/api/lua_ui.h/.cpp` — Register `uz.ui` in LuaVM. load(html_path, css_path), unload(), show(), hide(). set_property(name, value), get_property(name). on_event(element_id, event_type, callback). animate(element_id, property, from, to, duration, easing).
- [ ] 6.2 Add element query Lua API — `uz.ui.get_element(id)` → element table with { id, classes, text, visible }. `uz.ui.set_text(id, text)`, `uz.ui.set_visible(id, bool)`.

## 7. CSS Transitions

- [ ] 7.1 Add CSS transition support — `transition: opacity 0.3s ease-in-out;`. When a computed property changes, interpolate over duration. Supported properties: opacity, width, height, background-color, color, transform.

## 8. Testing

- [ ] 8.1 Unit tests for data binding — set property, verify var() resolution, fallback values.
- [ ] 8.2 Unit tests for events — click hit test, hover detection, change value.
- [ ] 8.3 Unit tests for input routing — modal blocks, passthrough allows game input.
- [ ] 8.4 Unit tests for focus manager — tab cycling, spatial navigation.
- [ ] 8.5 Unit tests for UIAnimator — linear interpolation, easing curves, completion callback.

## 9. Demo

- [ ] 9.1 Extend `demos/ui_basic/` — interactive menu with buttons (click → start game), health bar bound to --health, slider for volume control, modal pause menu that blocks game input.

## 10. Documentation

- [ ] 10.1 Update `docs/engine/18-ui-system/` with data binding, events, input routing, animator.
- [ ] 10.2 Add `uz.ui` to `docs/lua-api-reference.md`.

## 11. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 11.1 Update or create documentation covering the implementation
- [ ] 11.2 Write tests covering the new behavior
- [ ] 11.3 Run tests and confirm they pass
