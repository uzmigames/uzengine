# Input System — Enhanced Input

> **Status**: Planned (Phase 2)
> **Source**: `engine/input/`
> **Depends on**: Platform input capture (§07), WorldSubsystem (§10), InputComponent (ActorComponent §10)
> **References**: UE5 Enhanced Input Plugin (clean-room). Adapted for UzEngine's C++ component model.

## Purpose

Value-typed, modifier/trigger-driven input with context stacking, runtime rebinding, and zero-boilerplate binding in Behaviors. Replaces traditional `if (IsKeyPressed(Key::Space)) Jump()` patterns with a composable, data-driven pipeline.

## The Problem

Traditional input systems map keys directly to callbacks. This breaks down quickly:

- **No value types** — a joystick axis and a button press use completely different code paths
- **No composability** — dead zones, smoothing, negation require per-action if-chains
- **No context switching** — UI vs gameplay vs vehicle all fight over the same key bindings
- **No runtime rebinding** — key mappings hardcoded in C++, unmodifiable by players
- **No chords** — Shift+E requires manual state tracking

## Architecture

```
OS Key Event (WM_KEYDOWN, XInput, etc.)
    │
    ▼
Platform Layer (platform/windows/) → stores raw KeyState per key
    │
    ▼
InputSystem::Tick() — per player, per frame:
    │
    │  For each ActionKeyMapping (sorted by context priority):
    │    1. Read raw key value
    │    2. Apply mapping-level Modifiers (DeadZone, Negate, Scalar...)
    │    3. Evaluate mapping-level Triggers (Down, Hold, Tap...)
    │    4. Accumulate into InputActionInstance (highest abs value or cumulative)
    │
    │  For each InputActionInstance (post-accumulation):
    │    5. Apply action-level Modifiers
    │    6. Evaluate action-level Triggers
    │    7. Compute ETriggerEvent from state transition (None→Ongoing = Started, etc.)
    │
    │  Walk InputComponent binding stack:
    │    8. For each binding where action.trigger_event matches → fire callback
    │
    ▼
Game Code receives (InputActionInstance&) with typed value + event
```

**Two-pass design** (critical): mapping-level modifiers/triggers run first per-key, then action-level modifiers/triggers run on the accumulated value. A `Negate` on a mapping negates only that key's contribution; a `Negate` on the action negates the combined input from all keys.

## InputValue — Unified Value Type

All input flows through a single value type. No branching on "is this a button or an axis?"

```cpp
/// The type tag describes which components of the Vec3 are meaningful.
/// The data is always Vec3 — modifiers operate on 3 floats without branching.
enum class InputValueType : uint8_t {
    Boolean,  // value.x is 0.0 or 1.0, y/z = 0
    Axis1D,   // value.x is the scalar, y/z = 0
    Axis2D,   // value.x + value.y active, z = 0
    Axis3D,   // all components active
};

/// Unified input value. 16 bytes, no heap, no virtuals.
/// Modifiers receive and return this — they never need to know the semantic type.
struct InputValue {
    Vec3           value = {};
    InputValueType type  = InputValueType::Boolean;

    bool  AsBool()   const;   // magnitude² >= 0.25
    float AsAxis1D() const;   // value.x
    Vec2  AsAxis2D() const;   // {value.x, value.y}
    Vec3  AsAxis3D() const;   // value

    float MagnitudeSq() const; // type-aware: bool→x², 2D→x²+y², 3D→full
    bool  IsNonZero(float tolerance = 0.01f) const;

    InputValue& operator+=(const InputValue& rhs); // promotes type, adds vector
    InputValue& operator*=(float scalar);
};
```

## Input Stack (Priority)

When running inside VSCode, all input is captured and processed by the runtime — keyboard, mouse, gamepad. The extension does NOT intercept keyboard/mouse for viewport interaction.

```
1. Editor shortcuts (when in editor mode)  — gizmo manipulation, camera orbit
2. Game input (when in play mode)          — player controls, game actions
3. System input                            — screenshot, profiler toggle, console
```

## Comparison

| Feature | Legacy (UE4/Unity) | UE5 Enhanced Input | **UzEngine** |
|---------|--------------------|--------------------|--------------|
| Value types | bool/float separate | Unified `FInputActionValue` | Unified `InputValue` (Vec3 + type tag) |
| Modifiers | Hardcoded per-action | Chainable `UInputModifier` | Chainable `InputModifier` (virtual) |
| Triggers | Pressed/Released only | `UInputTrigger` state machine | `InputTrigger` state machine |
| Context switching | Manual enable/disable | Priority stack + `bConsumeInput` | Priority stack + `consume_input` |
| Chords | Manual state tracking | `ChordAction` trigger (Implicit) | `ChordAction` trigger (Implicit) |
| Rebinding | Platform-specific | `UPlayerMappableKeySettings` | `RebindKey()` + `.uzinput` JSON |
| Config format | C++ only | Data assets (UObject) | **JSON `.uzinput`** (LLM-friendly) |

## Implementation Phase

**Phase 2** — requires platform/ input capture (§07) and WorldSubsystem (§10). InputComponent is an ActorComponent (§10).

## Rules

1. **NEVER** poll raw keys in game code — always go through `InputAction` + `InputComponent`
2. **ALWAYS** route input through `PlayerController`, never through standalone Behaviors
3. **ALWAYS** use the two-pass modifier/trigger pipeline — mapping-level first, then action-level
4. **NEVER** hardcode key bindings in C++ — use `InputMappingContext` (code or `.uzinput` JSON)
