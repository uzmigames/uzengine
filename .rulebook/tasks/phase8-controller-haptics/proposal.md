# Proposal: phase3-controller-haptics

## Why
XInput controllers expose two independent rumble motors (low-frequency and high-frequency) via `XInputSetState()`, but the engine provides no API to drive them. Games that use haptic feedback — weapon fire, environmental rumble, impact confirmation — feel significantly more immersive and responsive than games without it. Player testing consistently rates haptic-enabled builds higher on "game feel" metrics. Without this feature, every game shipping with controller support delivers a worse experience than it should, and there is no way to expose the hardware capability already present in every Xbox controller.

## What Changes
- Add `IInputDevice::rumble(float low_freq, float high_freq, uint32_t duration_ms)` to the input device interface; `low_freq` and `high_freq` are [0.0, 1.0] normalized motor strengths
- Implement `XInputDevice::rumble()` calling `XInputSetState()` with scaled `XINPUT_VIBRATION` values
- Add `RumbleEnvelope` struct: `peak_strength`, `duration_ms`, `fade_curve` (Linear / EaseOut / EaseIn); `InputSystem::update()` advances active envelopes each frame and calls `rumble()` with the interpolated value
- Built-in haptic profile presets registered by name: `"gunshot"` (sharp high-freq spike 80ms), `"explosion"` (heavy low-freq 400ms ease-out), `"footstep"` (light low-freq pulse 60ms), `"engine_idle"` (continuous low 0.1 loop)
- Lua API: `ctx.input:rumble(low, high, duration_ms)` for direct control; `ctx.input:rumble_preset("gunshot")` for named presets
- Auto-stop: rumble state is cleared when the game pauses (GameMode enters PauseMode) and when the application loses focus, preventing stuck-on vibration
- Debug console command `input.rumble_test <preset>` for in-editor haptic testing without running game code

## Impact
- Affected specs: `docs/specs/input-system.md`
- Affected code: `src/input/input_system.cpp`, `src/input/input_system.h`, `src/input/xinput_device.cpp`, `src/input/xinput_device.h`, `src/scripting/lua_input.cpp`
- Breaking change: NO
- User benefit: Controller players get tactile feedback for all game events that invoke the API. The preset library means a developer can add weapon fire haptics in one Lua line with no motor tuning required.
