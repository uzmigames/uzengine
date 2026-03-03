## 1. ARIA Role Attribute Support

- [ ] 1.1 Create `src/ui/accessibility.h` declaring `UIAccessibility` with methods to parse and expose `role`, `aria-label`, `aria-live`, `aria-disabled` attributes from HTML elements
- [ ] 1.2 Create `src/ui/accessibility.cpp` implementing Windows UI Automation (UIA) tree population: for each HTML element with an ARIA role, create or update a corresponding UIA provider node
- [ ] 1.3 Map HTML ARIA roles to UIA control types: `role="button"` → `UIA_ButtonControlTypeId`, `role="dialog"` → `UIA_WindowControlTypeId`, `role="alert"` → `UIA_TextControlTypeId` with live region
- [ ] 1.4 Call `UIAccessibility::sync_uia_tree()` from `ui_pass.cpp` each frame when the DOM changes

## 2. Text-to-Speech IPC Bridge

- [ ] 2.1 In `src/ui/accessibility.cpp`, implement `speak(const wchar_t* text, bool interrupt)` using the Windows SAPI `ISpVoice` COM interface via a named pipe worker thread
- [ ] 2.2 When `interrupt == true`, call `ISpVoice::Speak()` with `SPF_ASYNC | SPF_PURGEBEFORESPEAK` to cancel current utterance before starting the new one
- [ ] 2.3 Add `ctx.ui:speak(text, interrupt)` Lua binding in `src/lua/lua_ui_api.cpp`
- [ ] 2.4 Expose TTS rate and volume as configurable settings readable from `AccessibilitySettings`

## 3. High Contrast Theme

- [ ] 3.1 Implement `ctx.ui:set_high_contrast(bool)` Lua binding that injects or removes CSS class `.uz-high-contrast` on the `<body>` element of all active HTML documents
- [ ] 3.2 On startup, detect the Windows system `prefers-contrast: more` setting via `SystemParametersInfo(SPI_GETHIGHCONTRAST, ...)` and auto-apply the high contrast class
- [ ] 3.3 Create `assets/ui/themes/high_contrast.css` with black background, white/yellow text, minimum 4.5:1 contrast ratio; apply to all `button`, `input`, and `uz-*` elements

## 4. Visible Focus Ring

- [ ] 4.1 In `src/ui/focus_manager.cpp`, inject CSS `outline: 2px solid yellow` on the currently focused element when focus was set via keyboard or gamepad
- [ ] 4.2 Suppress the focus ring CSS outline when the last focus action was a mouse click (implement `:focus-visible` semantics)
- [ ] 4.3 Apply the focus ring on `<button>`, `<input>`, `[tabindex]`, and all `<uz-*>` custom elements

## 5. Tab Order and Focus Navigation

- [ ] 5.1 Create `src/ui/focus_manager.h` declaring `FocusManager` with methods `focus_next()`, `focus_prev()`, `focus(element_id)`, and `get_focused_element()`
- [ ] 5.2 Create `src/ui/focus_manager.cpp` implementing `focus_next()` and `focus_prev()` by traversing focusable elements in `tabindex` order
- [ ] 5.3 Add `ctx.ui:focus_next()`, `ctx.ui:focus_prev()`, and `ctx.ui:focus(element_id)` Lua bindings
- [ ] 5.4 Wire gamepad D-pad navigation: call `FocusManager::focus_next()` on D-pad right/down and `focus_prev()` on D-pad left/up

## 6. Reduced Motion Support

- [ ] 6.1 In `uz_ui_html.dll`, evaluate `prefers-reduced-motion: reduce` CSS media query as true when `AccessibilitySettings::reduced_motion == true` or the Windows system accessibility setting is active
- [ ] 6.2 Add `ctx.ui:set_reduced_motion(bool)` Lua binding that sets `AccessibilitySettings::reduced_motion` and triggers a CSS media query re-evaluation in all active HTML documents

## 7. Settings Integration

- [ ] 7.1 Expose TTS rate, TTS volume, high contrast, and reduced motion as standard `ctx.settings` entries with string keys `"accessibility.tts_rate"`, `"accessibility.tts_volume"`, `"accessibility.high_contrast"`, `"accessibility.reduced_motion"`

## 8. Testing

- [ ] 8.1 Create `tests/ui/test_ui_accessibility.cpp` testing ARIA attribute parsing: verify `role="button"` maps to `UIA_ButtonControlTypeId`
- [ ] 8.2 Test `speak()` with `interrupt=false` does not cancel the current utterance (SAPI mock verifies no purge flag)
- [ ] 8.3 Test `speak()` with `interrupt=true` sends `SPF_PURGEBEFORESPEAK` to SAPI mock
- [ ] 8.4 Test `set_high_contrast(true)` injects `.uz-high-contrast` class; `set_high_contrast(false)` removes it
- [ ] 8.5 Test focus ring is applied on keyboard focus and removed on mouse click
- [ ] 8.6 Test `FocusManager::focus_next()` cycles through elements in ascending `tabindex` order; wraps from last to first
- [ ] 8.7 Test `FocusManager::focus_prev()` cycles in descending `tabindex` order; wraps from first to last
- [ ] 8.8 Test `set_reduced_motion(true)` causes the `prefers-reduced-motion: reduce` media query to evaluate as true in the HTML renderer
- [ ] 8.9 Test `ctx.settings` entries for TTS rate and volume persist across save/load
- [ ] 8.10 Achieve >= 95% line and branch coverage across `accessibility.cpp`, `focus_manager.cpp`, and Lua accessibility bindings

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md` documenting ARIA attributes, TTS bridge, focus ring behavior, tab order, and high contrast CSS class
- [ ] 9.2 Update `docs/specs/input-system.md` documenting gamepad D-pad focus navigation integration
- [ ] 9.3 Update `docs/specs/lua-scripting.md` documenting all `ctx.ui` accessibility methods and `ctx.settings` accessibility keys
- [ ] 9.4 Update `docs/specs/localization.md` documenting `ctx.ui:speak()` for screen reader announcements during locale changes
