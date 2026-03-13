# Proposal: Advanced Input (Phase 4.2.0)

## Why
The current `InputSystem` uses basic `WM_KEYDOWN`/`WM_MOUSEMOVE` messages which suffer from mouse acceleration, no cursor confinement, and zero gamepad support. FPS/TPS games require Raw Input API for pixel-perfect mouse deltas, cursor locking for camera control, and XInput for gamepad. UE5 uses Raw Input + XInput + DirectInput; Godot uses WinTab + XInput + custom cursor management. This is P1 for any interactive demo beyond clear-screen.

## What Changes

### New Systems
1. **Raw Input mouse** — `RegisterRawInputDevices` + `WM_INPUT` for high-precision, acceleration-free mouse deltas
2. **Mouse capture/confinement** — `ClipCursor` for FPS-style camera, `SetCapture`/`ReleaseCapture`
3. **Cursor management** — show/hide (`ShowCursor`), custom cursor shapes (`SetCursor` + `LoadImage`)
4. **XInput gamepad** — polling 4 controllers, thumbstick with configurable deadzone, trigger axes, vibration
5. **Device hot-plug** — `WM_DEVICECHANGE` / XInput polling for connect/disconnect events
6. **Keyboard improvements** — repeat rate detection, scancode-based mapping (not VK), left/right modifier distinction

### Refactoring
- `InputSystem` mouse handling switches from `WM_MOUSEMOVE` to `WM_INPUT` (Raw Input)
- Key enum extended with scancode-based entries

## Impact
- Affected specs: `docs/engine/07-platform/02-input-system.md`
- Affected code: `runtime/platform/windows/input.h/.cpp`
- Breaking change: NO — additive, existing key/mouse queries still work
- User benefit: Pixel-perfect mouse for FPS cameras, gamepad support, custom cursors
- Enables: Enhanced Input system (13-input-system), camera system (14-camera)
