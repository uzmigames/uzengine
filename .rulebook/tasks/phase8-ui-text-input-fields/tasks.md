## 1. uz-input Custom Element

- [ ] 1.1 Register `<uz-input>` as a custom HTML element in `uz_ui_html.dll` with attributes: `type` ("text", "password", "number"), `maxlength` (int), `placeholder` (string), `id` (string)
- [ ] 1.2 Render the input field as a styled rectangle with a flashing text cursor; draw current text content left-aligned with one character-width left padding
- [ ] 1.3 In `type="password"` mode, render all characters as `•` bullet glyphs while storing the actual string internally
- [ ] 1.4 In `type="number"` mode, reject any character that is not a digit, `.`, or `-` at the validation layer
- [ ] 1.5 Apply CSS variables `--uz-input-bg`, `--uz-input-border`, `--uz-input-text` for theme-compatible styling

## 2. Keyboard Event Routing

- [ ] 2.1 Route keyboard events from `InputSystem` through an IPC channel to the active `<uz-input>` element each frame when `FocusManager` reports an input element is focused
- [ ] 2.2 Handle printable character keys by appending to the internal buffer (respecting `maxlength`)
- [ ] 2.3 Handle `Backspace`: remove the character before the cursor position
- [ ] 2.4 Handle `Delete`: remove the character after the cursor position
- [ ] 2.5 Handle `Left`/`Right` arrow keys to move the cursor; `Home`/`End` to jump to buffer start/end

## 3. Cursor and Text Selection

- [ ] 3.1 Implement a canvas overlay within the `<uz-input>` element that renders a blinking cursor bar at the current cursor position
- [ ] 3.2 Implement text selection: track selection start and end positions; render selected range with a highlight background
- [ ] 3.3 Implement `Ctrl+A` to select all; `Ctrl+C` to copy selected text to clipboard; `Ctrl+V` to paste from clipboard

## 4. IME Support

- [ ] 4.1 Create `src/platform/ImeHandler.cpp` integrating the Windows `IMM32` API to intercept `WM_IME_COMPOSITION` and `WM_IME_CHAR` window messages
- [ ] 4.2 Forward composition string characters to the active `<uz-input>` as a temporary underlined preview string
- [ ] 4.3 On `WM_IME_COMPOSITION` with `GCS_RESULTSTR`, commit the finalized composed string into the input buffer, replacing the composition preview
- [ ] 4.4 Expose IME composition candidate window position near the active `<uz-input>` element by calling `ImmSetCompositionWindow()`

## 5. Lua API

- [ ] 5.1 Add `ctx.ui:get_input_value(id)` Lua binding returning the current string content of the `<uz-input>` element with `id`
- [ ] 5.2 Add `ctx.ui:set_input_value(id, str)` Lua binding replacing the buffer with `str`; reposition cursor to end of buffer
- [ ] 5.3 Add `ctx.ui:focus_input(id)` Lua binding activating the field and showing the cursor
- [ ] 5.4 Add `ctx.ui:on_input_submit(id, fn)` Lua binding registering a callback function fired when the player presses `Enter` on the field
- [ ] 5.5 Add `ctx.ui:set_input_validator(id, fn)` Lua binding registering a per-keystroke validator; the function receives the proposed new string and returns `true` to accept or `false` to reject the character

## 6. Testing

- [ ] 6.1 Create `tests/ui/test_text_input.cpp` testing that typing printable characters appends them to the buffer in order
- [ ] 6.2 Test `maxlength` enforcement: typing beyond `maxlength` characters does not modify the buffer
- [ ] 6.3 Test `Backspace` at cursor position 3 removes the character at index 2 and decrements cursor position
- [ ] 6.4 Test `Backspace` at position 0 is a no-op (no crash, buffer unchanged)
- [ ] 6.5 Test `type="password"` renders bullets while the internal buffer stores actual characters
- [ ] 6.6 Test `type="number"` rejects alphabetic characters; accepts digits, `.`, and `-`
- [ ] 6.7 Test `ctx.ui:set_input_value()` replaces the buffer and positions cursor at end
- [ ] 6.8 Test `ctx.ui:on_input_submit()` callback fires exactly once when `Enter` is pressed
- [ ] 6.9 Test `ctx.ui:set_input_validator()` with a rejection function prevents disallowed characters from appearing in the buffer
- [ ] 6.10 Test IME commit: simulate `WM_IME_COMPOSITION` with `GCS_RESULTSTR`; verify the result string is appended to the buffer
- [ ] 6.11 Achieve >= 95% line and branch coverage across `ImeHandler.cpp` and the `<uz-input>` event handling code paths

## 7. Documentation

- [ ] 7.1 Update `docs/specs/ui-system.md` documenting `<uz-input>` attributes, `type` values, CSS variables, and Lua API
- [ ] 7.2 Update `docs/specs/input-system.md` documenting keyboard event routing to focused `<uz-input>` elements and IME integration
