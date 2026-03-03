## 1. Core Data Structures

- [ ] 1.1 `InputAction`, `InputMappingContext`, `KeyMapping` structs (see spec)
- [ ] 1.2 `InputModifier`: DeadZone, Scale, Negate, SwizzleYXZ, Smooth, Clamp, Normalize
- [ ] 1.3 `TriggerType`: Pressed, Released, Held, Pulse, Chord

## 2. `.uzinput` File Loader

- [ ] 2.1 Parse `[action]` sections: name, type, consume
- [ ] 2.2 Parse `[mapping]` sections: action, key, trigger, modifiers
- [ ] 2.3 Modifiers parsed from inline syntax: `DeadZone(0.15, 0.95)`, `Scale(2.0)`
- [ ] 2.4 Load built-in template: `input/player_on_foot.uzinput`

## 3. Platform Backends

- [ ] 3.1 Win32 keyboard: `GetAsyncKeyState()` or `WM_KEYDOWN/WM_KEYUP` — map to key name strings
- [ ] 3.2 Win32 mouse: `GetCursorPos()` delta per frame + button state
- [ ] 3.3 XInput gamepad: `XInputGetState()` — sticks, triggers, buttons
- [ ] 3.4 Mouse delta: raw input via `RegisterRawInputDevices` (bypasses OS acceleration)
- [ ] 3.5 Key name table: maps VK_SPACE → "Space", etc.

## 4. Input Processing

- [ ] 4.1 `InputSystem::update(dt)` — poll all backends, apply modifiers, resolve triggers
- [ ] 4.2 Context priority: higher priority evaluated first; `consume=true` blocks lower
- [ ] 4.3 `apply_modifiers(raw, mods, dt)` — chain modifiers in order
- [ ] 4.4 `get_bool(action)`, `get_axis1d(action)`, `get_axis2d(action)` query API
- [ ] 4.5 Chord trigger: all chord_keys must be held simultaneously

## 5. Rebinding

- [ ] 5.1 `InputSystem::rebind(context, action, new_key)` — update in-memory mapping
- [ ] 5.2 `save_bindings(path)` — write override file (only changed bindings)
- [ ] 5.3 `load_bindings(path)` — merge overrides on top of defaults

## 6. IPC Handlers (input.*)

- [ ] 6.1 `input.getActionValue(name)` → current frame value
- [ ] 6.2 `input.listActions()` → all actions + current bindings
- [ ] 6.3 `input.rebind(context, action, key)`
- [ ] 6.4 `input.getDebugState()` → all action values + active raw keys
- [ ] 6.5 `input.pushContext(name)` / `input.popContext(name)`

## 7. VSCode Input Panel

- [ ] 7.1 Input debug Webview: shows live action values, refreshes every 100ms via IPC poll
- [ ] 7.2 Rebinding UI: list of actions + current key, click to rebind
- [ ] 7.3 Gamepad indicator: connected/disconnected status

## 8. Testing

- [ ] 8.1 W key → `IA_Move` Y=1.0, S key → Y=-1.0
- [ ] 8.2 Gamepad left stick → `IA_Move` vec2 with dead zone applied
- [ ] 8.3 Rebind Jump from Space to E → jump works with E
- [ ] 8.4 Save/load rebindings: persist across engine restart
- [ ] 8.5 Pop on-foot context → no input processed (e.g., in pause menu)
