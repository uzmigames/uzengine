## 1. Implementation
- [x] 1.1 Register Raw Input devices for mouse (`RIDEV_INPUTSINK` + `RIM_TYPEMOUSE`) <!-- EnableRawInput() in input.cpp -->
- [x] 1.2 Handle `WM_INPUT` for mouse deltas (separate from WM_MOUSEMOVE position) <!-- OnRawMouseInput() + WndProc handler -->
- [x] 1.3 Implement `SetMouseCapture(bool)` — `ClipCursor` to window rect, re-clip on resize/move <!-- input.cpp + WM_MOVE/WM_SIZE handlers -->
- [x] 1.4 Implement `SetCursorVisible(bool)` — tracked state, applied in WM_SETCURSOR <!-- ApplyCursorState() -->
- [x] 1.5 Implement `SetCursorShape(CursorShape)` — enum with Arrow, Hand, IBeam, Crosshair, etc. <!-- CursorShape enum + LoadCursorW -->
- [ ] 1.6 Implement `SetCustomCursor(imagePath, hotspotX, hotspotY)` via `LoadImage` + `SetCursor` <!-- deferred: needs asset loading -->
- [x] 1.7 Implement XInput gamepad polling (4 players, `XInputGetState` per frame) <!-- PollGamepads() in BeginFrame -->
- [x] 1.8 Add gamepad deadzone processing (radial deadzone for thumbsticks, linear for triggers) <!-- ApplyRadialDeadzone/ApplyLinearDeadzone -->
- [x] 1.9 Implement `SetVibration(playerIndex, leftMotor, rightMotor)` via `XInputSetState`
- [x] 1.10 Add `GamepadButton` enum and `IsGamepadButtonDown/JustPressed/JustReleased` queries
- [x] 1.11 Add `GetGamepadAxis(playerIndex, axis)` for thumbsticks and triggers (-1..1 / 0..1)
- [x] 1.12 Implement device hot-plug detection via XInput connect state delta in PollGamepads
- [x] 1.13 Add `OnGamepadConnected`/`OnGamepadDisconnected` callbacks
- [x] 1.14 Key enum already has left/right modifier distinction (LeftShift/RightShift etc.)

## 2. Testing
- [x] 2.1 Test mouse delta computed from OnMouseMove across frame boundary <!-- test_input.cpp -->
- [x] 2.2 Test mouse capture state tracking (SetMouseCapture/IsMouseCaptured)
- [x] 2.3 Test cursor visibility toggling (default true, toggle cycles)
- [x] 2.4 Test gamepad deadzone: mirrored radial/trigger algorithms with full range coverage
- [x] 2.5 Test gamepad button state transitions (just pressed, held, just released)
- [x] 2.6 Test gamepad axis: disconnected returns 0, invalid player no crash
- [x] 2.7 Test null gamepad callbacks don't crash on BeginFrame
- [x] 2.8 Test all 10 CursorShape enum values set/get without crash
- [ ] 2.9 Verify 95%+ coverage on all new code
- [x] 2.10 Build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Update `docs/engine/07-platform/02-input-system.md` with Raw Input and gamepad sections
- [x] 3.2 Cursor management API documented in 3.1 <!-- CursorShape enum, SetCursorVisible, SetMouseCapture -->
