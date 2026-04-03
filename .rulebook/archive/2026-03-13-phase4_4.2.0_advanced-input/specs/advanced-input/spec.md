# Advanced Input — Spec Delta

## ADDED Requirements

### Requirement: Raw Input Mouse
The input system MUST use the Windows Raw Input API (`WM_INPUT`) for mouse deltas instead of `WM_MOUSEMOVE`, providing acceleration-free, high-precision movement data.

#### Scenario: FPS Camera Mouse Delta
Given Raw Input is registered for mouse
When the user moves the mouse 50 hardware units right
Then `GetMouseDeltaX()` returns exactly 50 (no OS acceleration applied)

### Requirement: Mouse Capture and Confinement
The input system MUST support confining the mouse cursor to the window client area via `ClipCursor`.

#### Scenario: Enable Mouse Capture
Given a window is focused
When `SetMouseCapture(true)` is called
Then the cursor cannot leave the window client area
And the clip rect updates automatically on window resize

#### Scenario: Disable Mouse Capture
Given mouse capture is active
When `SetMouseCapture(false)` is called or the window loses focus
Then the cursor moves freely across the desktop

### Requirement: XInput Gamepad Support
The input system MUST poll up to 4 XInput gamepads per frame with deadzone processing, button state transitions, and vibration control.

#### Scenario: Thumbstick With Deadzone
Given a gamepad with left thumbstick at 15% deflection and deadzone set to 20%
When `GetGamepadAxis(0, GamepadAxis::LeftStickX)` is queried
Then 0.0 is returned (inside deadzone)

#### Scenario: Gamepad Button Press
Given gamepad 0 has button A released
When the player presses button A
Then `IsGamepadButtonJustPressed(0, GamepadButton::A)` returns true for one frame
And subsequent frames return false for JustPressed, true for Down

### Requirement: Device Hot-Plug Detection
The input system MUST detect gamepad connect/disconnect events and fire callbacks.

#### Scenario: Gamepad Connected
Given no gamepads are connected
When the user plugs in an XInput controller
Then `OnGamepadConnected(playerIndex)` callback fires
And `IsGamepadConnected(playerIndex)` returns true

### Requirement: Cursor Management
The input system MUST support showing/hiding the cursor and setting cursor shapes.

#### Scenario: Hide Cursor For FPS
Given the cursor is visible
When `SetCursorVisible(false)` is called
Then the cursor is hidden within the window
And `SetCursorVisible(true)` restores it
