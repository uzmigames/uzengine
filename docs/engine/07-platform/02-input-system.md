# Platform — Input System

> **Status**: Done (Milestone 1.2)
> **Source**: `platform/windows/input.h/.cpp`
> **Depends on**: PlatformWindow (message dispatch)

## Purpose

The `InputSystem` tracks keyboard, mouse, and (future) gamepad state with per-frame transition detection. It provides both low-level queries (`IsKeyDown(Key::W)`) and high-level action mappings (`IsActionJustPressed("jump")`).

## Input Flow

```
OS (WM_KEYDOWN)
  → PlatformWindow::WndProc()
    → InputSystem::OnKeyDown(vk)
      → m_keyCurrent[vk] = true

Per frame:
  InputSystem::BeginFrame()
    → m_keyPrevious[] = m_keyCurrent[]   (snapshot for transition detection)
    → m_mouseDX = m_mouseX - m_prevMouseX
    → m_scrollDelta = 0                   (reset accumulator)

Game code:
  if (input.IsKeyJustPressed(Key::Space))  // current && !previous
  if (input.IsActionDown("move_forward"))  // action → key → state
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

## State Queries

### Keyboard

| Method | Returns `true` when |
|--------|-------------------|
| `IsKeyDown(key)` | Key is currently held |
| `IsKeyJustPressed(key)` | Key went from up → down this frame |
| `IsKeyJustReleased(key)` | Key went from down → up this frame |

### Mouse

| Method | Description |
|--------|-------------|
| `IsMouseDown(btn)` | Button currently held |
| `IsMouseJustPressed(btn)` | Button pressed this frame |
| `IsMouseJustReleased(btn)` | Button released this frame |
| `GetMouseX/Y()` | Window-relative position (pixels) |
| `GetMouseDeltaX/Y()` | Movement since last frame (pixels) |
| `GetScrollDelta()` | Scroll wheel delta (positive = up) |

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

The current action system is minimal — one key per action, no input contexts, no gamepad. The full input system (section 13) will add:
- Multiple bindings per action (keyboard + gamepad)
- Input contexts (gameplay, menu, vehicle, spectator)
- Axis mappings (WASD → normalized Vec2)
- Dead zones, sensitivity curves, input smoothing
- Serialized rebinding (save/load from config)

## Frame Lifecycle

```cpp
while (window.IsOpen()) {
    input.BeginFrame();        // 1. Snapshot state, compute deltas
    window.PumpMessages();     // 2. OS messages → OnKey*/OnMouse*
    // 3. Game code queries input
    if (input.IsKeyJustPressed(Key::Escape)) break;
}
input.Shutdown();
```

**Order matters**: `BeginFrame()` must come before `PumpMessages()`. If reversed, the first frame's transitions would be lost because the snapshot happens after new events arrive.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Input model | Enhanced Input (`UInputAction`, `UInputMappingContext`) | `InputEvent` class hierarchy + `InputMap` | `InputSystem` with action map |
| Key enum | `EKeys` (200+ entries, platform-unified) | `Key` enum (300+ entries) | `Key` enum (60+ entries, VK-mapped) |
| Action system | `UInputAction` + modifiers + triggers | `InputMap::add_action` + events | `RegisterAction(name, key)` |
| Gamepad | Full support (XInput, DualSense, Steam Input) | Full support (SDL2 backend) | **Planned** (XInput) |
| Transition detection | `WasInputKeyJustPressed()` / per-frame delta | `is_action_just_pressed()` | `IsKeyJustPressed()` (current && !previous) |
| Input contexts | `UInputMappingContext` (priority-ordered) | None (global action map) | **Planned** (section 13) |

### UE5's Enhanced Input — Lessons for UzEngine

UE5's Enhanced Input system (introduced in UE 5.1) replaced the legacy input system with a more powerful model:

- **Input Actions** are typed (bool, float, Vec2, Vec3) — a "Move" action outputs a Vec2, not individual key presses
- **Modifiers** transform raw input (dead zones, curves, negate, swizzle) before it reaches gameplay code
- **Triggers** define when an action fires (pressed, released, held for N seconds, chord)
- **Mapping Contexts** are priority-ordered stacks — push "Vehicle" context to override "OnFoot" bindings

UzEngine will adopt similar concepts in its full input system (section 13), but with a simpler implementation — UE5's Enhanced Input has features that only matter for complex editor tooling.

## Rules

1. **ALWAYS call `BeginFrame()` before `PumpMessages()`** — snapshot must precede new events
2. **ALWAYS use action names in game code** — never hardcode `Key::W` in gameplay systems
3. **NEVER query input from threads other than main** — state arrays are not synchronized
4. **ALWAYS call `Shutdown()` before destruction** — clears action map allocations
