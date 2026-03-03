## 1. Input Device Interface Extension

- [ ] 1.1 Add `virtual void rumble(float low_freq, float high_freq, uint32_t duration_ms) = 0` to `IInputDevice` in `src/input/input_device.h`; `low_freq` and `high_freq` are normalized [0.0, 1.0] motor strengths
- [ ] 1.2 Add default no-op implementation `void rumble(float, float, uint32_t) override {}` to the base stub device so non-XInput devices compile without changes
- [ ] 1.3 Implement `XInputDevice::rumble()` in `src/input/xinput_device.cpp` calling `XInputSetState()` with `XINPUT_VIBRATION` values scaled from the [0.0, 1.0] input to [0, 65535]

## 2. RumbleEnvelope System

- [ ] 2.1 Define `struct RumbleEnvelope` in `src/input/input_system.h` with fields: `peak_low` (float), `peak_high` (float), `duration_ms` (uint32_t), `elapsed_ms` (uint32_t), `fade_curve` (enum: Linear, EaseOut, EaseIn), `device_index` (int)
- [ ] 2.2 Add `std::vector<RumbleEnvelope> active_envelopes_` to `InputSystem` private members
- [ ] 2.3 In `InputSystem::update()`, advance each active envelope by the frame delta time in milliseconds; compute interpolated `low` and `high` values based on `fade_curve` and elapsed ratio
- [ ] 2.4 Call `device->rumble(low, high, 0)` each frame for each active envelope using the interpolated values; pass `duration_ms = 0` to mean "set now, not timed" at the device level
- [ ] 2.5 Remove completed envelopes (where `elapsed_ms >= duration_ms`) from `active_envelopes_` after calling rumble with zero values

## 3. Built-in Haptic Presets

- [ ] 3.1 Define a `HapticPreset` registry in `src/input/haptic_presets.h` as a static map from preset name string to `RumbleEnvelope` parameters
- [ ] 3.2 Register preset `"gunshot"`: high_freq peak 0.9, duration 80ms, EaseOut fade
- [ ] 3.3 Register preset `"explosion"`: low_freq peak 1.0, duration 400ms, EaseOut fade
- [ ] 3.4 Register preset `"footstep"`: low_freq peak 0.3, duration 60ms, Linear fade
- [ ] 3.5 Register preset `"engine_idle"`: low_freq 0.1, high_freq 0.0, duration 0ms (continuous loop marker); loop flag in preset struct
- [ ] 3.6 Add `InputSystem::register_haptic_preset(const char* name, RumbleEnvelope env)` for game code to add custom presets

## 4. Auto-Stop Behavior

- [ ] 4.1 Clear all active envelopes and call `device->rumble(0, 0, 0)` for all devices when `GameMode` transitions to `PauseMode`
- [ ] 4.2 Clear all active envelopes and call `device->rumble(0, 0, 0)` for all devices when the application window loses focus (WM_KILLFOCUS)

## 5. Lua API

- [ ] 5.1 Add `ctx.input:rumble(low, high, duration_ms)` Lua binding in `src/scripting/lua_input.cpp` that calls `InputSystem::rumble_direct()` for the first connected controller
- [ ] 5.2 Add `ctx.input:rumble_preset(name)` Lua binding that looks up the named preset and queues a `RumbleEnvelope` on the first connected controller
- [ ] 5.3 Add `ctx.input:stop_rumble()` Lua binding that clears all active envelopes for all devices

## 6. Debug Console Command

- [ ] 6.1 Register console command `input.rumble_test <preset>` in `src/debug/console_commands.cpp` that calls `InputSystem::rumble_preset()` on the first connected device
- [ ] 6.2 If the named preset does not exist, print an error message listing all available preset names

## 7. Testing

- [ ] 7.1 Create `tests/input/test_controller_haptics.cpp` testing `XInputDevice::rumble()` with mock `XInputSetState`: verify scaled XINPUT_VIBRATION values for low=0.5/high=0.75 inputs
- [ ] 7.2 Test `RumbleEnvelope` linear fade: at 50% elapsed time, interpolated strength equals 50% of peak
- [ ] 7.3 Test `RumbleEnvelope` EaseOut fade: at 50% elapsed time, interpolated strength is > 50% of peak
- [ ] 7.4 Test `RumbleEnvelope` EaseIn fade: at 50% elapsed time, interpolated strength is < 50% of peak
- [ ] 7.5 Test envelope completion: after advancing elapsed past duration, the envelope is removed from `active_envelopes_` and rumble is called with zero values
- [ ] 7.6 Test auto-stop on pause: envelopes cleared and rumble zeroed when `GameMode::set_paused(true)` is called
- [ ] 7.7 Test auto-stop on focus loss: envelopes cleared and rumble zeroed when focus-loss event is fired
- [ ] 7.8 Test all four built-in presets are registered and produce the correct `RumbleEnvelope` parameters when looked up by name
- [ ] 7.9 Test `rumble_preset()` with an unknown preset name returns `false` and does not crash
- [ ] 7.10 Achieve >= 95% line and branch coverage across `input_system.cpp`, `xinput_device.cpp`, and `haptic_presets.h`

## 8. Documentation

- [ ] 8.1 Update `docs/specs/input-system.md` documenting `IInputDevice::rumble()`, `RumbleEnvelope`, preset names, Lua API, and auto-stop behavior
