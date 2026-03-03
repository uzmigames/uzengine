# Proposal: phase2-ui-text-input-fields

## Why
The UI system has no text input capability. Players cannot type character names at game start, send chat messages, enter console commands, or configure keybind names. Any game requiring player-typed text must implement its own character-by-character input handling on top of the raw input system, which is error-prone, misses platform edge cases, and must be rebuilt for every project. A native text input widget is required for the engine to support a broad class of games.

## What Changes
- Add `<uz-input>` custom element to uz_ui_html.dll that renders as a styled text field; attributes: `type="text|password|number"`, `maxlength`, `placeholder`, `id`
- Route keyboard events from the engine's input system through an IPC channel to the active `<uz-input>` element, bypassing the browser's normal focus model which cannot capture game-mode keyboard input
- Implement cursor rendering and text selection using a canvas overlay within the custom element, since the embedded browser's native caret is not controllable when the window has no OS focus
- Add IME (Input Method Editor) support for CJK text entry: integrate Windows IMM32 API to handle composition strings and commit events, forwarding the composed text to the active input element
- Add Lua API: `ctx.ui:get_input_value(id)` returns the current string, `ctx.ui:set_input_value(id, str)` sets it programmatically, `ctx.ui:focus_input(id)` activates the field, and an `on_submit` callback registered via `ctx.ui:on_input_submit(id, fn)` fires when the player presses Enter
- Add validation hooks: `ctx.ui:set_input_validator(id, fn)` receives each keystroke result and returns true/false to accept or reject the character, enabling numeric-only fields or profanity filtering
- Style the `<uz-input>` element with CSS variables (`--uz-input-bg`, `--uz-input-border`, `--uz-input-text`) that integrate with the theme system

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/input-system.md`
- Affected code: `src/ui/uz_ui_html.dll`, `src/input/InputSystem.cpp`, `src/ipc/IpcServer.cpp`, `src/platform/ImeHandler.cpp`
- Breaking change: NO
- User benefit: Games can ship name entry screens, chat panels, and in-game console fields without any custom input handling code — a complete text input widget with IME, validation, and Lua callbacks is available as a single HTML element.
