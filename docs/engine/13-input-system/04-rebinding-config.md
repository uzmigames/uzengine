# Runtime Rebinding & Config Format

> **Status**: Planned (Phase 2)
> **Source**: `engine/input/input_subsystem.h`

## Runtime Rebinding

Players can remap keys at runtime. Rebindings are saved per-player and persist across sessions.

```cpp
/// Rebind a specific mapping in a context. The new key replaces the old one.
/// Persisted to player settings (JSON) automatically.
void InputSubsystem::RebindKey(
    const InputMappingContext* context,
    const InputAction*         action,
    KeyCode                    old_key,
    KeyCode                    new_key);

/// Get current binding for an action (for UI display).
/// Returns all keys mapped to this action across all active contexts.
std::vector<KeyCode> InputSubsystem::GetBoundKeys(const InputAction* action) const;

/// Reset all rebindings to default.
void InputSubsystem::ResetToDefaults(const InputMappingContext* context);
```

### Rebinding Storage

Per-player rebinding sidecar (`.uzinput`):

```json
{
    "rebindings": {
        "IMC_Gameplay": {
            "IA_Jump": { "Space": "GamepadFaceBottom" },
            "IA_Fire": { "MouseLeft": "GamepadRightTrigger" }
        }
    }
}
```

## Input Config File Format (`.uzinput`)

Mapping contexts can be defined in JSON instead of code — enabling designers and LLMs to author/modify input mappings without recompilation.

```json
{
    "name": "IMC_Gameplay",
    "mappings": [
        {
            "action": "IA_Move",
            "key": "W",
            "modifiers": [
                { "type": "Negate", "axes": [false, true, false] },
                { "type": "SwizzleAxis", "order": "YXZ" }
            ],
            "triggers": []
        },
        {
            "action": "IA_Move",
            "key": "Gamepad_LeftStick",
            "modifiers": [
                { "type": "DeadZone", "lower": 0.2, "upper": 1.0, "mode": "Radial" }
            ],
            "triggers": []
        },
        {
            "action": "IA_Jump",
            "key": "Space",
            "modifiers": [],
            "triggers": [{ "type": "Pressed" }]
        },
        {
            "action": "IA_Fire",
            "key": "MouseLeft",
            "modifiers": [],
            "triggers": [{ "type": "Hold", "threshold": 0.0 }]
        }
    ]
}
```

## Directory Structure

```
engine/input/
├── input_value.h                ← InputValue struct + operators
├── input_action.h               ← InputAction (definition) + InputActionInstance (per-player)
├── input_component.h/.cpp       ← InputComponent (ActorComponent) + BindAction API
├── input_subsystem.h/.cpp       ← InputSubsystem (WorldSubsystem) + context stack + tick
├── mapping_context.h            ← InputMappingContext + ActionKeyMapping
├── modifier.h                   ← InputModifier base class + ApplyModifiers()
├── trigger.h                    ← InputTrigger base class + TriggerState + voting
├── trigger_event.h              ← ETriggerEvent enum + state transition table
├── key_codes.h                  ← KeyCode enum (keyboard, mouse, gamepad)
├── modifiers/                   ← built-in modifier implementations
│   ├── deadzone.h
│   ├── scalar.h
│   ├── negate.h
│   ├── swizzle_axis.h
│   ├── smooth_delta.h
│   └── response_curve.h
└── triggers/                    ← built-in trigger implementations
    ├── trigger_down.h
    ├── trigger_pressed.h
    ├── trigger_released.h
    ├── trigger_hold.h
    ├── trigger_tap.h
    ├── trigger_pulse.h
    └── trigger_chord.h
```
