## 1. Input Actions & Types

- [ ] 1.1 Create `runtime/engine/input/input_action.h` — InputAction: name (string), type enum (Bool, Axis1D, Axis2D, Axis3D), consume_input (bool, default true). ActionValue union: bool, float, Vec2, Vec3. GetBoolValue(), GetAxis1D(), GetAxis2D().
- [ ] 1.2 Create `runtime/engine/input/input_action.cpp` — ActionValue normalization: Axis2D auto-normalizes if magnitude > 1.0. Type conversion helpers.

## 2. Input Mapping Context

- [ ] 2.1 Create `runtime/engine/input/input_mapping_context.h` — InputMappingContext: name, priority (int, higher = first). Vector<KeyMapping>. KeyMapping: action_name, key (enum), trigger_type, vector<InputModifier*>.
- [ ] 2.2 Create `runtime/engine/input/input_mapping_context.cpp` — Evaluate(raw_input_state) → map of action_name → ActionValue. Process in priority order, consuming inputs that are mapped.

## 3. Input Modifiers

- [ ] 3.1 Create `runtime/engine/input/modifiers/input_modifier.h` — InputModifier base: virtual Modify(ActionValue) → ActionValue.
- [ ] 3.2 Create DeadZone modifier — DeadZone(inner=0.15, outer=0.95). Remaps axis range, kills values inside inner.
- [ ] 3.3 Create Scale modifier — Scale(factor). Multiplies value by factor.
- [ ] 3.4 Create Negate modifier — Negates value (×-1).
- [ ] 3.5 Create Swizzle modifier — Swizzle(order). Reorders axis components (e.g., XY→YX).
- [ ] 3.6 Create Smooth modifier — Smooth(speed). Lerps toward target value over time.

## 4. Input Triggers

- [ ] 4.1 Create `runtime/engine/input/triggers/input_trigger.h` — InputTrigger base: virtual Evaluate(current, previous, dt) → TriggerState { None, Started, Ongoing, Completed, Canceled }.
- [ ] 4.2 Create Pressed trigger — fires on transition from 0→1 (one frame only).
- [ ] 4.3 Create Released trigger — fires on transition from 1→0.
- [ ] 4.4 Create Held trigger — fires after key held for hold_time seconds.
- [ ] 4.5 Create Pulse trigger — fires repeatedly every interval while held.
- [ ] 4.6 Create Chord trigger — requires all chord keys held simultaneously.

## 5. Input Subsystem

- [ ] 5.1 Create `runtime/engine/input/input_subsystem.h/.cpp` — InputSubsystem (WorldSubsystem): TickGroup::PrePhysics. Owns context stack (vector<InputMappingContext*> sorted by priority). PushContext/PopContext/RemoveContext. OnTick: poll raw input → evaluate contexts in priority order → update action values.
- [ ] 5.2 Add raw input collection — Read keyboard state (Win32 GetAsyncKeyState or WM_INPUT), mouse delta (WM_INPUT), XInput gamepad state (XInputGetState for 4 pads). Store as RawInputState.
- [ ] 5.3 Add XInput gamepad support — Poll up to 4 gamepads. Map buttons (A/B/X/Y/LB/RB/Start/Back), sticks (LeftStick/RightStick as Vec2), triggers (LT/RT as float). Apply dead zones.

## 6. .uzinput File Format

- [ ] 6.1 Create `runtime/engine/input/input_config.h/.cpp` — Load/Save .uzinput JSON format: { "actions": [ { "name": "IA_Jump", "type": "bool" } ], "mappings": [ { "action": "IA_Jump", "key": "Space", "trigger": "pressed" } ] }. Default config + user override merging.

## 7. Runtime Rebinding

- [ ] 7.1 Create `runtime/engine/input/input_rebinding.h/.cpp` — StartRebindCapture(action_name): enters capture mode, next key press becomes new binding. CancelRebind(). SaveBindings(path) → saves/bindings.uzinput. LoadBindings(path) → merges on top of defaults.

## 8. Lua Input API

- [ ] 8.1 Extend `runtime/engine/scripting/api/lua_input.h/.cpp` — `uz.input.action_value(name)` → bool/float/Vec2. `uz.input.is_action_pressed(name)` → true on Started frame. `uz.input.is_action_held(name)`. `uz.input.push_context(name)`, `uz.input.pop_context(name)`.
- [ ] 8.2 Add rebinding Lua API — `uz.input.start_rebind(action_name, callback)`. `uz.input.save_bindings()`.
- [ ] 8.3 Add gamepad Lua API — `uz.input.gamepad_connected(index)`, `uz.input.set_vibration(index, low, high, duration)`.

## 9. Testing

- [ ] 9.1 Unit tests for InputAction — type conversion, normalization.
- [ ] 9.2 Unit tests for modifiers — dead zone remapping, negate, scale, smooth over time.
- [ ] 9.3 Unit tests for triggers — pressed/released edge detection, held timing, chord.
- [ ] 9.4 Unit tests for context priority — higher priority consumes, lower blocked.
- [ ] 9.5 Unit tests for .uzinput — load/save round-trip, merge override.
- [ ] 9.6 Unit tests for rebinding — capture key, save, reload.

## 10. Documentation

- [ ] 10.1 Update `docs/engine/13-input-system/` with Enhanced Input architecture.
- [ ] 10.2 Add `uz.input` enhanced API to `docs/lua-api-reference.md`.
- [ ] 10.3 Document .uzinput format and rebinding workflow.

## 11. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 11.1 Update or create documentation covering the implementation
- [ ] 11.2 Write tests covering the new behavior
- [ ] 11.3 Run tests and confirm they pass
