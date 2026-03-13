# Platform — Input System

> **Status**: Done (Milestone 4.2.0)
> **Source**: `platform/windows/input.h/.cpp`
> **Depends on**: PlatformWindow (message dispatch), XInput (gamepad polling)

## Purpose

The `InputSystem` tracks keyboard, mouse, gamepad, and raw input state with per-frame transition detection. It provides low-level queries (`IsKeyDown(Key::W)`), action mappings (`IsActionJustPressed("jump")`), raw mouse input for FPS camera control, XInput gamepad polling with deadzone processing, cursor capture, and gamepad connection callbacks..

## Input Flow

```
Keyboard/Mouse/Cursor:
  OS (WM_KEYDOWN, WM_MOUSEMOVE, WM_INPUT, WM_SETCURSOR)
    → PlatformWindow::WndProc()
      → InputSystem::OnKeyDown/Up(vk), OnMouse*/OnRawMouseInput()
      → Accumulate state and raw deltas

Gamepad:
  Per frame:
    InputSystem::BeginFrame()
      → Snapshot keyboard/mouse state (for transition detection)
      → PollGamepads() via XInputGetState() for 4 players
      → Fire OnGamepadConnected/Disconnected callbacks
      → Reset accumulators (scroll, raw mouse delta)

Application frame:
  input.BeginFrame()                      // 1. Snapshot state, poll gamepad
  window.PumpMessages()                   // 2. OS messages → callbacks
  if (input.IsKeyJustPressed(Key::Space))  // 3. Game code queries input
  if (input.IsActionDown("move_forward"))
  float rx = input.GetGamepadAxis(0, GamepadAxis::RightX);
```

## Key and Button Enums

### Keyboard (`Key`)

Maps directly to Windows virtual key codes (VK_*) for zero-cost conversion:

```cpp
enum class Key : uint16_t {
    A = 'A', B = 'B', ..., Z = 'Z',        // Letters (ASCII)
    Num0 = '0', ..., Num9 = '9',            // Number row
    F1 = 0x70, ..., F12 = 0x7B,             // Function keys
    Escape = 0x1B, Tab = 0x09, Space = 0x20, Enter = 0x0D,
    LeftShift = 0xA0, RightShift = 0xA1,
    LeftCtrl = 0xA2, RightCtrl = 0xA3,
    LeftAlt = 0xA4, RightAlt = 0xA5,
    Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28,
    // ...
};
```

### Mouse (`MouseButton`)

```cpp
enum class MouseButton : uint8_t {
    Left = 0,    // LMB (primary)
    Right = 1,   // RMB (context)
    Middle = 2,  // MMB (scroll click)
    X1 = 3,      // Side button (back)
    X2 = 4,      // Side button (forward)
    Count = 5,
};
```

### Cursor (`CursorShape`)

Standard Win32 cursor shapes for UI feedback:

```cpp
enum class CursorShape : uint8_t {
    Arrow,     // IDC_ARROW (default pointer)
    Hand,      // IDC_HAND (clickable items)
    IBeam,     // IDC_IBEAM (text insertion)
    Crosshair, // IDC_CROSS (precision)
    SizeNS,    // IDC_SIZENS (vertical resize)
    SizeWE,    // IDC_SIZEWE (horizontal resize)
    SizeNWSE,  // IDC_SIZENWSE (diagonal NW-SE)
    SizeNESW,  // IDC_SIZENESW (diagonal NE-SW)
    Wait,      // IDC_WAIT (busy/hourglass)
    No,        // IDC_NO (not allowed)
};
```

### Gamepad (`GamepadButton`)

XInput gamepad buttons (4 players × 14 buttons):

```cpp
enum class GamepadButton : uint8_t {
    A, B, X, Y,              // Face buttons
    LB, RB,                  // Shoulder bumpers
    Start, Back,             // Menu buttons
    LThumb, RThumb,          // Thumbstick clicks
    DPadUp, DPadDown,        // D-pad
    DPadLeft, DPadRight,
    Count = 14,              // Sentinel
};

static constexpr int kMaxGamepads = 4;  // XInput limit
```

### Gamepad Axes (`GamepadAxis`)

Stick and trigger analog inputs:

```cpp
enum class GamepadAxis : uint8_t {
    LeftX,       // Left stick horizontal (-1 = left, +1 = right)
    LeftY,       // Left stick vertical (-1 = down, +1 = up)
    RightX,      // Right stick horizontal
    RightY,      // Right stick vertical
    LeftTrigger, // Left trigger (0 = released, 1 = fully pressed)
    RightTrigger,// Right trigger (0 = released, 1 = fully pressed)
    Count = 6,   // Sentinel
};
```

## State Queries

### Keyboard

| Method | Returns `true` when |
|--------|-------------------|
| `IsKeyDown(key)` | Key is currently held |
| `IsKeyJustPressed(key)` | Key went from up → down this frame |
| `IsKeyJustReleased(key)` | Key went from down → up this frame |

### Mouse Position & Scroll

| Method | Description |
|--------|-------------|
| `GetMouseX/Y()` | Window-relative position (pixels) |
| `GetMouseDeltaX/Y()` | Movement since last frame (pixels, from WM_MOUSEMOVE) |
| `GetScrollDelta()` | Accumulated scroll wheel delta; positive = up, negative = down; resets each BeginFrame() |

### Mouse Buttons

| Method | Description |
|--------|-------------|
| `IsMouseDown(btn)` | Button currently held |
| `IsMouseJustPressed(btn)` | Button pressed this frame |
| `IsMouseJustReleased(btn)` | Button released this frame |

## Action Mapping

Actions decouple game logic from physical keys:

```cpp
InputSystem input;
input.Init();

// Bind named actions to keys
input.RegisterAction("jump", Key::Space);
input.RegisterAction("move_forward", Key::W);
input.RegisterAction("fire", Key::LeftCtrl);

// Game code uses action names, not keys
if (input.IsActionJustPressed("jump")) {
    player.Jump();
}
if (input.IsActionDown("move_forward")) {
    player.MoveForward(dt);
}
```

### Why Action Mapping?

1. **Rebinding** — changing "jump" from Space to X requires one `RegisterAction` call, not hunting through game code
2. **Platform abstraction** — "jump" can map to Space (keyboard), A (gamepad), or Tap (touch) transparently
3. **Context switching** — menu navigation and gameplay use the same keys for different actions; swap the mapping set at runtime

### Current Limitations

The current action system is minimal — one key per action, no input contexts. For multi-binding and contexts, see the full input system (section 13).

## Raw Mouse Input

For precision FPS camera control, `EnableRawInput()` registers for `WM_INPUT` messages, which provide hardware-level mouse deltas without OS acceleration.

### Usage

```cpp
InputSystem input;
input.Init();
input.EnableRawInput(hwnd);  // Call once after window creation

// Per frame:
input.BeginFrame();
window.PumpMessages();  // Triggers OnRawMouseInput() callbacks
float rawDX = input.GetRawMouseDeltaX();
float rawDY = input.GetRawMouseDeltaY();

// Typical FPS camera code:
camera.RotateYaw(-rawDX * 0.005f);
camera.RotatePitch(rawDY * 0.005f);
```

### How It Works

- `EnableRawInput(hwnd)` calls `RegisterRawInputDevices()` to request `WM_INPUT` messages
- `WndProc` delivers raw input via `WM_INPUT` messages (RAWINPUT structure)
- `OnRawMouseInput(lParam)` extracts the mouse delta from RAWINPUT and accumulates it
- `GetRawMouseDeltaX/Y()` returns the accumulated delta since last `BeginFrame()`
- Deltas reset to 0 each frame, regardless of whether movement occurred

### Key Differences from Cooked Mouse

| Aspect | Cooked `WM_MOUSEMOVE` | Raw `WM_INPUT` |
|--------|----------------------|----------------|
| Acceleration | OS-accelerated (respects mouse sensitivity) | **None** — hardware delta only |
| Precision | Limited by OS acceleration curve | Full precision (counts subpixel) |
| Use case | General UI, slow camera | FPS camera, precision tools |
| API | `GetMouseDeltaX/Y()` | `GetRawMouseDeltaX/Y()` |

## Mouse Capture & Cursor Control

### Capture (Confine Cursor)

`SetMouseCapture(capture, hwnd)` uses `ClipCursor()` to confine the mouse to the window's client area:

```cpp
input.SetMouseCapture(true, hwnd);   // Cursor trapped in window
input.SetMouseCapture(false, hwnd);  // Cursor released

if (input.IsMouseCaptured()) {
    // Cursor is confined
}
```

When the window moves or resizes, call `UpdateMouseCapture(hwnd)` from `WM_MOVE`/`WM_SIZE` handlers to re-clip the cursor:

```cpp
// In WndProc:
case WM_MOVE:
case WM_SIZE:
    input.UpdateMouseCapture(hwnd);
    break;
```

### Cursor Visibility & Shape

```cpp
// Show/hide cursor
input.SetCursorVisible(true);    // Show
input.SetCursorVisible(false);   // Hide (for first-person games)

// Set cursor shape (arrow, hand, crosshair, etc.)
input.SetCursorShape(CursorShape::Hand);
input.SetCursorShape(CursorShape::SizeNS);

// Query state
bool visible = input.IsCursorVisible();
CursorShape shape = input.GetCursorShape();
```

Cursor state is applied in the `WM_SETCURSOR` handler via `ApplyCursorState()`, which returns `true` if handled (caller should return `TRUE` from `WndProc`):

```cpp
// In WndProc:
case WM_SETCURSOR:
    if (input.ApplyCursorState()) {
        return TRUE;
    }
    break;
```

## Gamepad (XInput)

UzEngine supports up to 4 XInput gamepads (Xbox 360, Xbox One, compatible controllers) via the XInput API. Gamepads are polled each frame, with automatic dead zone processing and connection callbacks.

### Basic Queries

```cpp
// Connection state
if (input.IsGamepadConnected(0)) {
    // Player 0 controller is plugged in
}

// Button state
if (input.IsGamepadButtonDown(0, GamepadButton::A)) {
    // A button is held
}
if (input.IsGamepadButtonJustPressed(0, GamepadButton::Start)) {
    // Start button pressed this frame
}
if (input.IsGamepadButtonJustReleased(0, GamepadButton::B)) {
    // B button released this frame
}

// Axis state (with deadzone)
float leftX = input.GetGamepadAxis(0, GamepadAxis::LeftX);   // -1 to +1
float rightX = input.GetGamepadAxis(0, GamepadAxis::RightX); // -1 to +1
float trigger = input.GetGamepadAxis(0, GamepadAxis::LeftTrigger); // 0 to 1

// Typical game code:
if (input.IsGamepadButtonJustPressed(0, GamepadButton::A)) {
    player.Jump();
}
float moveX = input.GetGamepadAxis(0, GamepadAxis::LeftX);
float moveY = input.GetGamepadAxis(0, GamepadAxis::LeftY);
player.Move(Vec2(moveX, moveY));
```

### Deadzone Processing

Deadzones prevent stick drift and trigger accidental input. UzEngine applies two types:

#### Thumbstick Deadzone (Radial)

Radius-based deadzone on left/right sticks:

```
Input range: -32768 to +32767 (16-bit signed)
Deadzone: |value| < 7849 (~24%)
Output range: -1.0 to +1.0 (after scaling)

Example: For input (-5000, 2000):
  Magnitude = sqrt(5000² + 2000²) ≈ 5385
  If magnitude < deadzone → (0, 0)
  Else → scale to unit circle
```

#### Trigger Deadzone (Linear)

Threshold-based deadzone on triggers:

```
Input range: 0 to 255 (8-bit unsigned)
Deadzone: value < ~30 (depends on calibration)
Output range: 0.0 to 1.0 (after scaling)

Example: For input 100:
  If 100 < deadzone → 0.0
  Else → (100 - deadzone) / (255 - deadzone)
```

### Vibration (Rumble)

```cpp
input.SetVibration(0, 1.0f, 0.5f);  // Player 0: left motor 100%, right motor 50%
input.SetVibration(0, 0.0f, 0.0f);  // Stop vibration

// Typical impact code:
float impactForce = 0.8f;
float duration = 0.2f;  // seconds (handle timer in game code)
input.SetVibration(0, impactForce, impactForce);
```

The motors are:
- **Left motor**: Low-frequency rumble (0.0 to 1.0)
- **Right motor**: High-frequency rumble (0.0 to 1.0)

### Connection Callbacks

Register callbacks to respond when gamepads connect/disconnect:

```cpp
// Define callback
void OnGamepadConnected(int playerIndex, void* userData) {
    printf("Player %d connected\n", playerIndex);
    // Play "player joined" sound, update UI, etc.
}

void OnGamepadDisconnected(int playerIndex, void* userData) {
    printf("Player %d disconnected\n", playerIndex);
    // Pause game, show "reconnect" prompt, etc.
}

// Register
input.SetGamepadConnectedCallback(OnGamepadConnected, nullptr);
input.SetGamepadDisconnectedCallback(OnGamepadDisconnected, nullptr);
```

Callbacks are fired in `BeginFrame()` when connection state changes (from polling `XInputGetState()`).

### XInput Polling Details

`BeginFrame()` calls `PollGamepads()`, which:
1. Calls `XInputGetState()` for each of 4 players
2. Extracts button state (XINPUT_GAMEPAD buttons)
3. Extracts analog state (thumbstick X/Y, trigger Z)
4. Applies deadzones
5. Compares with previous frame to detect press/release transitions
6. Fires connection callbacks if state changed

This happens **every frame** — XInput is polled, not event-based.

### Known Limitations & Workarounds

| Issue | Workaround |
|-------|-----------|
| XInput only supports Xbox-compatible buttons (no DualSense features) | Use Raw Input API or Steam Input SDK for full controller support (future) |
| No battery level or device-specific info | Log at startup only; assume batteries are OK |
| Vibration has latency (~40ms) | Accept as-is; XInput is not designed for sub-frame haptics |

## Frame Lifecycle

```cpp
while (window.IsOpen()) {
    input.BeginFrame();        // 1. Snapshot state, poll gamepads, reset accumulators
    window.PumpMessages();     // 2. OS messages → OnKey*/OnMouse*/OnRawMouseInput callbacks
    // 3. Game code queries input
    if (input.IsKeyJustPressed(Key::Escape)) break;
    if (input.IsGamepadButtonJustPressed(0, GamepadButton::Start)) break;
}
input.Shutdown();
```

**Order matters**: `BeginFrame()` must come before `PumpMessages()`. If reversed, the first frame's transitions would be lost because the snapshot happens after new events arrive.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine 4.2.0 |
|--------|-----|-------|----------|
| Input model | Enhanced Input (`UInputAction`, `UInputMappingContext`) | `InputEvent` class hierarchy + `InputMap` | `InputSystem` with action map |
| Key enum | `EKeys` (200+ entries, platform-unified) | `Key` enum (300+ entries) | `Key` enum (60+ entries, VK-mapped) |
| Action system | `UInputAction` + modifiers + triggers | `InputMap::add_action` + events | `RegisterAction(name, key)` (basic) |
| Gamepad | Full support (XInput, DualSense, Steam Input) | Full support (SDL2 backend) | XInput (4 players, full button/axis support) |
| Raw mouse | `FMouseEvent::IsRawMouseDelta()` | `InputEventMouseMotion::relative` | `GetRawMouseDeltaX/Y()` via `WM_INPUT` |
| Cursor control | `FSlateApplication::SetCursorShape()` + capture | `Input::set_mouse_mode()` | `SetCursorShape()` + `SetMouseCapture()` |
| Deadzone | Enhanced Input modifiers | Built-in deadzone processing | Radial (sticks) + linear (triggers) |
| Vibration | `FGamepadVibrationReplicate` | `Input::start_joy_vibration()` | `SetVibration(player, left, right)` |
| Transition detection | `WasInputKeyJustPressed()` / per-frame delta | `is_action_just_pressed()` | `IsKeyJustPressed()` (current && !previous) |
| Input contexts | `UInputMappingContext` (priority-ordered) | None (global action map) | **Planned** (section 13) |

### UE5's Enhanced Input — Lessons for UzEngine

UE5's Enhanced Input system (introduced in UE 5.1) replaced the legacy input system with a more powerful model:

- **Input Actions** are typed (bool, float, Vec2, Vec3) — a "Move" action outputs a Vec2, not individual key presses
- **Modifiers** transform raw input (dead zones, curves, negate, swizzle) before it reaches gameplay code
- **Triggers** define when an action fires (pressed, released, held for N seconds, chord)
- **Mapping Contexts** are priority-ordered stacks — push "Vehicle" context to override "OnFoot" bindings

UzEngine's current (4.2.0) implementation provides:
- **Basic action mapping** via `RegisterAction(name, key)` (typed for future expansion)
- **Gamepad support** with 4 players, 14 buttons, 6 axes, deadzone processing
- **Raw input** for FPS camera control (`WM_INPUT` unaccelerated deltas)
- **Cursor control** with shape and capture APIs

The full input system (section 13) will add:
- Typed actions (bool, float, Vec2) with axis mapping
- Modifiers (deadzone curves, negate, swizzle) on actions
- Input contexts for gameplay/menu/vehicle switching
- Multi-binding (keyboard + gamepad for same action)
- Serialized rebinding (save/load from config)

## Rules

1. **ALWAYS call `BeginFrame()` before `PumpMessages()`** — snapshot must precede new events, and gamepad polling happens in `BeginFrame()`
2. **ALWAYS use action names in game code** — never hardcode `Key::W` in gameplay systems
3. **NEVER query input from threads other than main** — state arrays are not synchronized; all input queries must be from the message-pump thread
4. **ALWAYS call `Shutdown()` before destruction** — clears action map allocations and releases raw input registration
5. **Call `EnableRawInput()` once after window creation** — if FPS camera control is needed; enables `WM_INPUT` processing
6. **Call `UpdateMouseCapture()` on `WM_MOVE`/`WM_SIZE`** — keeps the cursor clip rect in sync when window moves/resizes
7. **Gamepad callbacks are optional** — if not registered, connection/disconnection is silent; register only if UI feedback needed
8. **Deadzone thresholds are fixed** — calibration is not exposed; if custom deadzones are needed, post-process `GetGamepadAxis()` results
9. **Use per-player logic for multiplayer** — query each gamepad separately with player index (0..3); no global "current player" concept
