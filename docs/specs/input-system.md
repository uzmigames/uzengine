# Input System Spec

## Overview

UzEngine's input system is inspired by **Unreal Engine's Enhanced Input**. Input is data-driven: `InputActions` (typed, named, device-agnostic) are defined in files; `InputMappingContexts` bind physical keys/axes to actions with modifiers and triggers. Contexts can be added/removed at runtime (e.g., vehicle, menu, dialogue).

---

## 1. Core Concepts

| Concept | Description |
|---------|-------------|
| **InputAction** | A named, typed gameplay input: `IA_Jump` (bool), `IA_Move` (vec2), `IA_Look` (vec2) |
| **InputMappingContext** | A set of key→action mappings with modifiers/triggers. Active/inactive at runtime |
| **InputModifier** | Transforms raw input: dead zone, scale, negate, swizzle axes, smooth |
| **InputTrigger** | When the action fires: Pressed, Released, Held, Pulse, Chord |

---

## 2. Data Types

```cpp
enum class ActionValueType { Bool, Axis1D, Axis2D, Axis3D };

enum class TriggerType {
    Pressed,    // fires once on key-down
    Released,   // fires once on key-up
    Held,       // fires every frame while held
    Pulse,      // fires at interval while held
    Chord       // fires only when all chord members are held simultaneously
};

enum class ModifierType {
    DeadZone,   // lower/upper thresholds
    Scale,      // multiply by factor(s)
    Negate,     // flip sign
    SwizzleYXZ, // swap XY (for WASD → vec2)
    Smooth,     // exponential smoothing
    Clamp,      // clamp to [-1..1]
    Normalize   // normalize vec2/vec3 to unit length
};

struct InputModifier {
    ModifierType type;
    float params[3] = {0};  // param0..param2 (e.g., dead zone lo/hi, scale xy)
};

struct InputAction {
    std::string     name;
    ActionValueType value_type;
    bool            consume;  // true = block lower-priority contexts from seeing this key
};

struct KeyMapping {
    std::string                key;         // "Space", "W", "Gamepad_LeftStick"
    std::string                action_name;
    TriggerType                trigger   = TriggerType::Held;
    float                      hold_time = 0.5f;       // for Held/Pulse
    float                      pulse_interval = 0.1f;  // for Pulse
    std::vector<InputModifier> modifiers;
    std::vector<std::string>   chord_keys;  // for Chord trigger
};

struct InputMappingContext {
    std::string             name;
    int                     priority = 0;   // higher = evaluated first
    std::vector<KeyMapping> mappings;
};
```

---

## 3. File Format (`.uzinput`)

```ini
; input/player_on_foot.uzinput

[action IA_Jump]
type    = bool
consume = true

[action IA_Move]
type    = axis2d
consume = false

[action IA_Look]
type    = axis2d
consume = false

[action IA_Fire]
type    = bool
consume = true

[action IA_Sprint]
type    = bool
consume = false

; --- Keyboard mappings ---
[mapping jump_kb]
action  = IA_Jump
key     = Space
trigger = pressed

[mapping move_forward]
action  = IA_Move
key     = W
trigger = held
modifier = SwizzleYXZ   ; maps to +Y of vec2

[mapping move_back]
action  = IA_Move
key     = S
trigger = held
modifier = SwizzleYXZ, Negate

[mapping move_right]
action  = IA_Move
key     = D
trigger = held

[mapping move_left]
action  = IA_Move
key     = A
trigger = held
modifier = Negate

[mapping sprint_kb]
action  = IA_Sprint
key     = LeftShift
trigger = held

[mapping fire_mouse]
action  = IA_Fire
key     = Mouse_Left
trigger = pressed

; --- Gamepad mappings ---
[mapping jump_gp]
action  = IA_Jump
key     = Gamepad_FaceButton_Bottom
trigger = pressed

[mapping move_gp]
action   = IA_Move
key      = Gamepad_LeftStick
trigger  = held
modifier = DeadZone(0.15, 0.95)

[mapping look_gp]
action   = IA_Look
key      = Gamepad_RightStick
trigger  = held
modifier = DeadZone(0.15, 0.95), Scale(2.0, 2.0)

[mapping fire_gp]
action  = IA_Fire
key     = Gamepad_RightTrigger
trigger = held

; --- Mouse look ---
[mapping look_mouse]
action   = IA_Look
key      = Mouse_XY
trigger  = held
modifier = Scale(0.1, 0.1)
```

### User Rebinding File (`saves/bindings.uzinput`)

Contains only overridden mappings. Merged over the default `.uzinput` at load:

```ini
; User overrides — merged on top of player_on_foot.uzinput
[mapping jump_kb]
key = E   ; user rebound Jump from Space to E
```

---

## 4. Runtime Implementation

### Input System Class

```cpp
class InputSystem {
public:
    void load_context(const std::string& path);
    void push_context(const std::string& name);   // activate
    void pop_context(const std::string& name);    // deactivate

    void update(float dt);  // call once per frame before game update

    // Query action values (game code uses these, never raw keys)
    bool      get_bool(std::string_view action) const;
    float     get_axis1d(std::string_view action) const;
    glm::vec2 get_axis2d(std::string_view action) const;

    // Rebind a key
    void rebind(const std::string& context, const std::string& action,
                const std::string& new_key);
    void save_bindings(const std::string& path) const;
    void load_bindings(const std::string& path);

private:
    void poll_win32_keyboard();
    void poll_win32_mouse();
    void poll_xinput();
    float apply_modifiers(float raw, const std::vector<InputModifier>&, float dt);
    glm::vec3 resolve_trigger(const KeyMapping&, float val, float dt);

    std::unordered_map<std::string, float>    raw_values;    // key → value this frame
    std::unordered_map<std::string, float>    prev_values;   // key → value last frame
    std::unordered_map<std::string, float>    held_durations;
    std::unordered_map<std::string, glm::vec3> action_values; // action → final value

    std::vector<InputMappingContext*>         active_contexts; // sorted by priority
    std::unordered_map<std::string, InputMappingContext> loaded_contexts;
};
```

### Key Name Reference

| Key String | Device |
|-----------|--------|
| `A`..`Z`, `0`..`9` | Keyboard |
| `Space`, `Enter`, `Escape`, `Tab` | Keyboard |
| `LeftShift`, `RightShift`, `LeftCtrl`, `LeftAlt` | Keyboard |
| `F1`..`F12` | Keyboard |
| `Up`, `Down`, `Left`, `Right` | Keyboard arrow keys |
| `Mouse_Left`, `Mouse_Right`, `Mouse_Middle` | Mouse buttons |
| `Mouse_X`, `Mouse_Y`, `Mouse_XY` | Mouse delta |
| `Mouse_Scroll` | Mouse wheel |
| `Gamepad_FaceButton_Bottom` / `_Right` / `_Left` / `_Top` | A/B/X/Y |
| `Gamepad_LeftShoulder`, `Gamepad_RightShoulder` | LB/RB |
| `Gamepad_LeftTrigger`, `Gamepad_RightTrigger` | LT/RT (0..1) |
| `Gamepad_LeftStick`, `Gamepad_RightStick` | Sticks (vec2, -1..1) |
| `Gamepad_DPad_Up/Down/Left/Right` | D-Pad |

---

## 5. Platform Backends

### Windows — XInput (Gamepad)

```cpp
#include <Xinput.h>
void poll_xinput()
{
    XINPUT_STATE state;
    if (XInputGetState(0, &state) != ERROR_SUCCESS) return;
    auto& gp = state.Gamepad;
    raw_values["Gamepad_LeftStick"]  = {gp.sThumbLX / 32767.f, gp.sThumbLY / 32767.f};
    raw_values["Gamepad_RightStick"] = {gp.sThumbRX / 32767.f, gp.sThumbRY / 32767.f};
    raw_values["Gamepad_LeftTrigger"]  = gp.bLeftTrigger  / 255.f;
    raw_values["Gamepad_RightTrigger"] = gp.bRightTrigger / 255.f;
    raw_values["Gamepad_FaceButton_Bottom"] = (gp.wButtons & XINPUT_GAMEPAD_A) ? 1.f : 0.f;
    // ... etc.
}
```

### Future Platform Backends

| Platform | Gamepad API | Keyboard/Mouse |
|---------|------------|---------------|
| Linux | SDL2 or evdev | X11 / Wayland |
| macOS | Game Controller framework | AppKit events |
| Android | Android NDK input events | Touch as mouse emulation |

The abstraction layer (`key_name → float value`) isolates all platform backends from game code.

---

## 6. Editor Integration

### Input Debug Panel (VSCode)

Shows live input state — useful for debugging mappings:

```
Input Debug (Live)
─────────────────────────────────────────
IA_Move   → vec2(0.00, 0.73)   [Gamepad_LeftStick]
IA_Look   → vec2(-0.15, 0.00)  [Gamepad_RightStick]
IA_Jump   → false
IA_Sprint → true               [LeftShift]
─────────────────────────────────────────
Raw keys active: W, LeftShift
Gamepad connected: yes (controller 0)
```

### Input Rebinding UI

A Webview panel listing all actions with current bindings, allowing drag-and-drop or click-to-rebind. Saves to `saves/bindings.uzinput`.

### IPC Handlers (input.*)

```
input.getActionValue   { name }         → { value, type }
input.listActions      {}               → [{ name, type, current_binding }]
input.rebind           { context, action, key }
input.getDebugState    {}               → all action values + raw keys
input.pushContext      { name }
input.popContext       { name }
```
