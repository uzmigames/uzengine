## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-ui-system` is complete — `uz_ui_html.dll`, `UIPass`, Lua `ctx.ui` API exist
- [ ] 1.2 Confirm `phase3-input-system` is complete — gamepad D-pad and button events available
- [ ] 1.3 Read `docs/specs/ui-system.md` — understand `UIDocument`, element tree, event dispatch model
- [ ] 1.4 Read `docs/specs/input-system.md` — understand gamepad event types and Lua input API

## 2. data-focusable Attribute and Element Discovery

- [ ] 2.1 Define `data-focusable` HTML attribute — presence marks element as focus candidate
- [ ] 2.2 Define `data-focus-id` HTML attribute — explicit string ID for programmatic focus
- [ ] 2.3 Define `data-focus-order` HTML attribute — integer for tab order (optional)
- [ ] 2.4 Implement `FocusManager::collect_focusable(UIDocument&) -> vector<FocusNode>` — scans DOM for `data-focusable` elements, records bounding rect
- [ ] 2.5 Define `FocusNode` struct: `element_id`, `focus_id`, `rect (x, y, w, h)`, `focus_order`
- [ ] 2.6 Implement collection update trigger: re-collect on DOM mutation (element add/remove/show/hide)

## 3. FocusManager — Core

- [ ] 3.1 Create `FocusManager` class in `uz_ui_html.dll`
- [ ] 3.2 Implement `FocusManager::set_focus(element_id)` — sets current focused element, dispatches `focus` DOM event, applies `:focus` CSS class
- [ ] 3.3 Implement `FocusManager::clear_focus()` — removes focus, dispatches `blur` event
- [ ] 3.4 Implement `FocusManager::get_focus() -> string` — returns current focused element's `focus_id` or empty string
- [ ] 3.5 Implement `FocusManager::navigate(Direction dir)` — directional focus movement algorithm:
  - From current focused element's rect center, cast search ray in direction
  - Score candidates: cosine similarity of direction × inverse distance
  - Move to highest-scoring candidate; ignore candidates behind or 90° off-axis
- [ ] 3.6 Implement `FocusManager::navigate_tab()` — move to next element by `focus_order`; wrap around
- [ ] 3.7 Implement `FocusManager::navigate_shift_tab()` — move to previous by `focus_order`; wrap around
- [ ] 3.8 Handle edge cases: no focusable elements → no-op; single element → stays on it

## 4. Input Integration

- [ ] 4.1 In `UIPass::on_input_event`, route gamepad D-pad events to `FocusManager::navigate(direction)`
- [ ] 4.2 Route left stick (threshold 0.5) to `FocusManager::navigate` with deadzone hysteresis (require stick to return to center before next navigation)
- [ ] 4.3 Route gamepad A/Cross button to `UIElement::dispatch_event("click")` on focused element
- [ ] 4.4 Route gamepad B/Circle button to `UIElement::dispatch_event("cancel")` on focused element
- [ ] 4.5 Route keyboard Tab to `FocusManager::navigate_tab()`; Shift+Tab to `navigate_shift_tab()`
- [ ] 4.6 Route keyboard Enter/Space to `click` on focused element

## 5. CSS — Focus Ring

- [ ] 5.1 Define `:focus` pseudo-class in UIPass CSS engine — applies when element is `FocusManager` current
- [ ] 5.2 Define `--uz-focus-ring-color` CSS variable (default: `#00AAFF`)
- [ ] 5.3 Define `--uz-focus-ring-width` CSS variable (default: `2px`)
- [ ] 5.4 Define `--uz-focus-ring-offset` CSS variable (default: `2px`)
- [ ] 5.5 Implement focus ring rendering: outline drawn outside element bounds using CSS variables
- [ ] 5.6 Add focus transition animation: 100ms ease-in outline appearance

## 6. C ABI Exports

- [ ] 6.1 Export `ui_set_focus(const char* focus_id)` from `uz_ui_html.dll`
- [ ] 6.2 Export `ui_get_focus(char* out_buf, size_t buf_size) -> bool` — fills buffer; returns false if none
- [ ] 6.3 Export `ui_navigate(int direction)` — 0=up, 1=right, 2=down, 3=left
- [ ] 6.4 Document ABI in `src/ui/ui_api.h`

## 7. Lua API

- [ ] 7.1 Implement `ctx.ui:set_focus(focus_id)` — calls `ui_set_focus` C ABI
- [ ] 7.2 Implement `ctx.ui:get_focus() -> string?` — calls `ui_get_focus`, returns nil if none
- [ ] 7.3 Implement `ctx.ui:on_focus_change(fn)` — callback registered; fn called with `(new_id, old_id)` on each focus change
- [ ] 7.4 Update `docs/specs/ui-system.md` — Lua API table with new focus methods

## 8. Testing

- [ ] 8.1 Unit test: `collect_focusable` finds all elements with `data-focusable` attribute
- [ ] 8.2 Unit test: `navigate(DOWN)` from top button moves focus to nearest element below
- [ ] 8.3 Unit test: `navigate(RIGHT)` with no candidates to the right — focus unchanged
- [ ] 8.4 Unit test: `navigate_tab()` wraps from last to first element
- [ ] 8.5 Unit test: `set_focus("btn-ok")` applies `:focus` class and fires `focus` DOM event
- [ ] 8.6 Unit test: `clear_focus()` fires `blur` event on previously focused element
- [ ] 8.7 Integration test: D-pad DOWN event moves focus from button 1 to button 2 in a vertical list
- [ ] 8.8 Integration test: gamepad A fires `click` event on currently focused `uz-button`

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md` — "Gamepad Navigation" section with `data-focusable` attributes and CSS variables
- [ ] 9.2 Update `docs/guides/editor-workflow.md` — note on testing gamepad UI without physical controller (keyboard fallback)
