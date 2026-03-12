# Input Modifiers & Triggers

> **Status**: Planned (Phase 2)
> **Source**: `engine/input/modifier.h`, `engine/input/trigger.h`

## Input Modifiers — Pure Function Chain

A modifier transforms a value. Modifiers chain: output of one feeds input of the next. Some are stateless (Negate, Scalar), some carry per-instance state (SmoothDelta, ResponseCurve).

```cpp
/// Base class for all input modifiers.
/// Override Modify() to transform the input value.
/// Modifiers are instanced per-player (state like SmoothDelta::old_value is per-player).
class InputModifier {
public:
    virtual ~InputModifier() = default;

    /// Transform the input value. Called once per frame per mapping or action.
    ///
    /// # Arguments
    /// * `value` — Output of the previous modifier (or raw key value for the first).
    /// * `dt` — Frame delta time (non-dilated).
    ///
    /// # Returns
    /// Modified value. Type tag must be preserved unless explicitly promoted.
    virtual InputValue Modify(InputValue value, float dt) = 0;
};
```

### Built-in Modifiers

| Modifier | Description | Stateful? |
|----------|-------------|-----------|
| `DeadZone` | Axial or radial dead zone with lower/upper threshold | No |
| `Scalar` | Per-axis multiply (`Vec3 scale`) | No |
| `Negate` | Flip sign on selected axes | No |
| `SwizzleAxis` | Remap axes (e.g. swap X↔Y for different stick conventions) | No |
| `SmoothDelta` | Frame-to-frame smoothing (stores previous value) | **Yes** |
| `ResponseCurve` | Exponential/custom curve remapping | No |
| `FOVScaling` | Scale sensitivity by current camera FOV | No |

### Chain Execution

```cpp
/// Apply a chain of modifiers sequentially. Returns the final value.
InputValue ApplyModifiers(Span<InputModifier*> chain, InputValue value, float dt) {
    for (auto* mod : chain) {
        value = mod->Modify(value, dt);
    }
    return value;
}
```

## Input Triggers — State Machine

A trigger evaluates whether an action should fire. Triggers return a **state** (`None`, `Ongoing`, `Triggered`), not an **event**. The event (`Started`, `Completed`, `Canceled`) is derived by the system by comparing previous state to current state.

```cpp
/// What role this trigger plays in the voting system.
enum class TriggerType : uint8_t {
    Explicit,  // At least one Explicit must be Triggered for the action to fire
    Implicit,  // ALL Implicits must be Triggered (used for chords)
    Blocker,   // If ANY Blocker is Triggered, the action is suppressed
};

/// Current state of a trigger (NOT the event — see trigger events below).
enum class TriggerState : uint8_t {
    None,       // not active
    Ongoing,    // in progress (e.g. hold timer counting)
    Triggered,  // condition met
};

/// Base class for all input triggers.
class InputTrigger {
public:
    virtual ~InputTrigger() = default;

    float actuation_threshold = 0.5f;

    /// What type of trigger this is (for voting).
    virtual TriggerType GetType() const { return TriggerType::Explicit; }

    /// Evaluate the trigger this frame. Returns the current state.
    ///
    /// # Arguments
    /// * `value` — The modified input value (after modifier chain).
    /// * `dt` — Frame delta time.
    virtual TriggerState Update(InputValue value, float dt) = 0;

protected:
    InputValue last_value;  // auto-updated by the system after Update()

    /// Helper: is the value above the actuation threshold?
    bool IsActuated(const InputValue& v) const {
        return v.MagnitudeSq() >= actuation_threshold * actuation_threshold;
    }
};
```

### Built-in Triggers

| Trigger | Fires when | Type |
|---------|-----------|------|
| `Down` | Value is non-zero (default if no triggers specified) | Explicit |
| `Pressed` | Transition from zero → non-zero (single frame) | Explicit |
| `Released` | Transition from non-zero → zero (single frame) | Explicit |
| `Hold` | Held for N seconds continuously | Explicit |
| `HoldAndRelease` | Held for N seconds, fires on release | Explicit |
| `Tap` | Pressed and released within N seconds | Explicit |
| `Pulse` | Fires repeatedly every N seconds while held | Explicit |
| `ChordAction` | Another action is currently Triggered | **Implicit** |
| `ChordBlocker` | Blocks the action while a chord key is active | **Blocker** |

### Trigger Voting

When multiple triggers are on one mapping/action:

- No triggers defined → fire if value is non-zero (implicit `Down`).
- Explicit only → at least one Explicit must be `Triggered`.
- Implicit present → ALL Implicits must be `Triggered` (AND logic).
- Any Blocker `Triggered` → action is suppressed regardless.

## Trigger Events — State Transition Table

Triggers return `TriggerState`. The system converts state transitions into `ETriggerEvent` — this is what game code binds to.

```cpp
/// The public events that game code can bind to.
enum class ETriggerEvent : uint8_t {
    None       = 0,
    Started    = 1 << 0,  // None/Triggered → Ongoing (or instant None → Triggered)
    Ongoing    = 1 << 1,  // Ongoing → Ongoing (still in progress)
    Triggered  = 1 << 2,  // any → Triggered (condition met)
    Completed  = 1 << 3,  // Triggered → None (cleanly finished)
    Canceled   = 1 << 4,  // Ongoing → None (interrupted before Triggered)
};
```

### Full State Transition Table

```
Last State  →  New State   →  Event
─────────────────────────────────────────
None        →  Ongoing     →  Started
None        →  Triggered   →  Started + Triggered (both fire)
Ongoing     →  Ongoing     →  Ongoing
Ongoing     →  Triggered   →  Triggered
Ongoing     →  None        →  Canceled
Triggered   →  Triggered   →  Triggered
Triggered   →  Ongoing     →  Ongoing
Triggered   →  None        →  Completed
```

The `None → Triggered` case fires **both** `Started` and `Triggered` callbacks in the same frame. This handles single-frame triggers (like `Pressed`) that actuate and resolve immediately.
