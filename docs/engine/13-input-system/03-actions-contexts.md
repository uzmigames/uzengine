# Input Actions & Mapping Contexts

> **Status**: Planned (Phase 2)
> **Source**: `engine/input/input_action.h`, `engine/input/mapping_context.h`

## InputAction — Definition vs Instance

An **InputAction** is a named, typed definition — authored once, shared across all players. The **InputActionInstance** is the per-player runtime state cloned from the definition.

```cpp
/// Static definition of a logical input action.
/// Define in code or load from .uzinput config file.
/// Shared — never holds per-player state.
struct InputAction {
    const char*    name;           // unique ID, e.g. "IA_Jump", "IA_Move"
    InputValueType value_type;     // Boolean for jump, Axis2D for movement, etc.

    /// How values from multiple keys mapping to the same action combine.
    /// - HighestAbsValue: take the key with the largest absolute value (default)
    /// - Cumulative: sum all key values (for WASD → Axis2D cancellation)
    enum class Accumulation { HighestAbsValue, Cumulative };
    Accumulation accumulation = Accumulation::HighestAbsValue;

    /// Action-level modifiers/triggers (applied AFTER mapping-level, on accumulated value).
    Span<InputModifier*> modifiers;
    Span<InputTrigger*>  triggers;

    bool consume_input     = true;   // prevent lower-priority contexts from seeing this key
    bool trigger_when_paused = false;
};

/// Per-player runtime state for an action. Created during context rebuild.
/// Holds per-instance copies of triggers/modifiers (so HeldDuration etc. are per-player).
struct InputActionInstance {
    const InputAction* source;

    InputValue    value;                   // accumulated this frame
    ETriggerEvent trigger_event;           // the event fired this frame
    TriggerState  last_trigger_state;      // from previous frame

    float elapsed_processed_time = 0.0f;  // time in Started/Ongoing/Triggered
    float elapsed_triggered_time = 0.0f;  // time in Triggered specifically

    // Per-instance clones (state like HeldDuration is per-player, not shared)
    std::vector<std::unique_ptr<InputModifier>> modifiers;
    std::vector<std::unique_ptr<InputTrigger>>  triggers;
};
```

## Mapping Context — Priority-Based Context Stack

An **InputMappingContext** groups key→action bindings into a named "mode" (Gameplay, UI, Vehicle, Spectator). Multiple contexts can be active simultaneously with different priorities.

```cpp
/// One physical key → one logical action, with per-mapping modifiers and triggers.
struct ActionKeyMapping {
    const InputAction* action;
    KeyCode            key;
    std::vector<std::unique_ptr<InputModifier>> modifiers;  // mapping-level (run first)
    std::vector<std::unique_ptr<InputTrigger>>  triggers;   // mapping-level

    bool should_ignore = false;  // key was down during context switch — skip until released
};

/// A named collection of key mappings representing one input "mode."
/// Authored as a data asset (.uzinput) or defined in code.
struct InputMappingContext {
    const char*                    name;       // e.g. "IMC_Gameplay", "IMC_UI"
    std::vector<ActionKeyMapping>  mappings;
};
```

### Context Stack Operations

```cpp
/// Add a context at the given priority. Higher priority = processed first.
/// Deferred to end-of-frame unless force_immediately is true.
///
/// # Arguments
/// * `priority` — Integer priority (higher wins).
/// * `ignore_pressed_until_release` — If true, keys currently held are skipped
///   until released. Prevents "instant trigger" when swapping contexts.
void AddMappingContext(const InputMappingContext* context, int32_t priority,
                       bool ignore_pressed_until_release = true);

/// Remove a context. Reference-counted — if added twice, must be removed twice.
void RemoveMappingContext(const InputMappingContext* context);
```

### Typical Context Setup

```cpp
// In a gameplay Behavior:
void OnBeginPlay() override {
    auto& input = GetWorld().GetSubsystem<InputSubsystem>();
    input.AddMappingContext(&IMC_Gameplay, /*priority=*/ 0);
}

// When opening a menu:
void OpenMenu() {
    auto& input = GetWorld().GetSubsystem<InputSubsystem>();
    input.AddMappingContext(&IMC_UI, /*priority=*/ 10);  // higher = processed first
    // IMC_UI consumes movement keys → gameplay context doesn't see them
}

// When closing the menu:
void CloseMenu() {
    auto& input = GetWorld().GetSubsystem<InputSubsystem>();
    input.RemoveMappingContext(&IMC_UI);
    // Gameplay context resumes, ignore_pressed_until_release prevents stale triggers
}
```

### Context Rebuild

End-of-frame, only when contexts changed:

1. Sort active contexts by priority (descending).
2. Flatten all mappings into a single array.
3. Re-order: chord sources first, then chorded actions, then everything else.
4. Mark currently-held keys as `should_ignore` on new mappings (if `ignore_pressed_until_release`).
5. Clone modifiers/triggers per-player into fresh `InputActionInstance` objects.

## Binding in Game Code

Input is always routed through the **PlayerController**, never through a standalone Behavior. When the controller possesses a new pawn, `OnSetupInput()` is called — this is where mapping contexts are added and actions are bound. On `Unpossess()`, all bindings and contexts are automatically cleared.

This makes input **interchangeable by possession**: same player, different pawn = different keys, different actions, different behavior.

```cpp
/// The PlayerController owns all input binding.
/// OnSetupInput() is called on every Possess() — bind actions here.
/// Previous bindings are auto-cleared on Unpossess().
class FPSController : public uz::PlayerController {
    UZ_BEHAVIOR(FPSController);

public:
    void OnSetupInput(uz::InputComponent& input) override {
        auto& sub = GetWorld().GetSubsystem<InputSubsystem>();
        sub.AddMappingContext(&IMC_OnFoot, 0);

        input.BindAction(&IA_Move, ETriggerEvent::Triggered,
            [this](const InputActionInstance& ctx) {
                Vec2 move = ctx.value.AsAxis2D();
                GetWorld().Get<CharacterMovementComponent>(GetPawn()).AddInput({move.x, 0, move.y});
            });

        input.BindAction(&IA_Jump, ETriggerEvent::Triggered,
            [this](const InputActionInstance& ctx) {
                GetWorld().Get<CharacterMovementComponent>(GetPawn()).Jump();
            });
    }
};
```

### Why PlayerController, Not Behavior?

| Scenario | With Behavior (wrong) | With PlayerController (correct) |
|----------|----------------------|-------------------------------|
| Player dies + respawns | Input bindings lost, must re-bind | Controller persists, `Possess(new_pawn)` re-triggers `OnSetupInput()` |
| Player mounts horse | Must manually swap bindings | `Possess(horse)` → auto-clears, `OnSetupInput()` sets up mount bindings |
| UI menu opens | Behavior doesn't know about context priority | Controller adds `IMC_UI` at higher priority |

## Chord Actions

Chords (Shift+E, Ctrl+Shift+S) are implemented entirely as triggers — no special engine code path.

```cpp
InputAction IA_Shift { "IA_Shift", InputValueType::Boolean };

InputMappingContext IMC_Gameplay = {
    "IMC_Gameplay",
    {
        // Shift key → IA_Shift (always evaluates, consumed by chord system)
        { &IA_Shift, Key::LeftShift, {}, {} },

        // E → IA_Interact (normal, but auto-blocked when Shift is held)
        { &IA_Interact, Key::E, {}, {new TriggerPressed()} },

        // Shift+E → IA_PickUp (ChordAction makes IA_Shift an Implicit requirement)
        { &IA_PickUp, Key::E, {}, {new TriggerPressed(), new ChordAction(&IA_Shift)} },
    }
};
```

**How it works internally:**

1. During context rebuild, the system detects `ChordAction(&IA_Shift)` on `IA_PickUp`.
2. It injects a `ChordBlocker(&IA_Shift)` on `IA_Interact` automatically — so plain E is blocked while Shift is held.
3. Evaluation order is sorted: `IA_Shift` first (chord source), then `IA_PickUp` (chorded), then `IA_Interact` (blocked).
4. When Shift is held + E pressed: `IA_Shift` = Triggered → `ChordAction` Implicit satisfied → `IA_PickUp` fires.
5. When only E pressed: `IA_Shift` = None → `ChordAction` fails → `IA_Interact` fires.
