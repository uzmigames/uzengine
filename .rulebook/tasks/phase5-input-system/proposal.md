# Proposal: phase3-input-system

## Why

The engine has no input handling layer. Characters, menus, and gameplay systems cannot respond to keyboard, mouse, or gamepad input because there is no API to query device state or bind actions to physical buttons. Hard-coding `GetAsyncKeyState` calls in game scripts bypasses rebinding, gamepad support, and any future input abstraction. Without a data-driven Enhanced Input layer — InputActions defined in `.uzinput` files with modifiers and trigger conditions — shipping a game with rebindable controls or gamepad support is not feasible.

## What Changes

- **`src/input/input_system.h/cpp`** — `InputSystem` singleton: polls Win32 `GetKeyState`/`GetCursorPos` each frame for keyboard and mouse; polls `XInputGetState` for up to 4 XInput gamepads; dispatches `InputAction` events via the EventBus
- **`.uzinput` file format** — INI-like data file defining `InputAction` entries (name, value type: bool/axis1d/axis2d) and `InputMappingContext` entries that bind physical keys/buttons to actions with optional modifiers (Negate, Scale, DeadZone) and trigger conditions (Pressed, Released, Hold with duration)
- **`InputMappingContext` hot-swap API** — `ctx.input:push_context("driving")` / `ctx.input:pop_context()` for context stacks; menu input context pushed on pause, popped on resume
- **Runtime rebinding** — player-configured bindings serialized to `saves/bindings.uzinput`; loaded at startup and merged over the default mapping context
- **Lua API** — `ctx.input:bind("Jump", handler)`, `ctx.input:get_axis("MoveForward")`, `ctx.input:get_axis2d("Look")`, `ctx.input:is_pressed("Fire")`, `ctx.input:rumble(low, high, ms)`
- **VSCode debug panel** — live input state panel showing per-action current value, active mapping context, and raw device state; updated via IPC `input.debug_state` message at 10 Hz
- **Tests** — mock device state injection, action trigger/hold/release coverage, rebinding round-trip, context stack push/pop, axis modifier chain (Negate + Scale + DeadZone)

## Impact

- Affected specs: `docs/specs/input-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/editor-panels.md`
- Affected code: `src/input/input_system.cpp`, `src/input/input_system.h`, `src/input/xinput_device.cpp`, `src/scripting/lua_input.cpp`, `src/ipc/ipc_handlers.cpp`, `vscode-extension/src/panels/inputDebugPanel.ts`
- Breaking change: NO — no existing input API exists; this is a new additive module
- User benefit: Game scripts can bind to named actions with a single `ctx.input:bind` call, pick up controller input automatically, and allow players to rebind keys at runtime with no additional code. The VSCode debug panel makes input problems trivially diagnosable during development.
