# Animation System — State Machine & Blend Trees

> **Status**: Planned (Phase 4)
> **Source**: `engine/animation/anim_state_machine.h/.cpp`, `engine/animation/blend_tree.h`
> **Depends on**: Animation sequences (§19.2), GPU skinning (§19.4)

## Purpose

An animation state machine (ASM) controls which animation plays, with transitions between states based on parameters. For example:

- **Idle state** (default)
  - Transition to Walk when `speed > 0.5`
  - Transition to Jump when `jump_pressed`
- **Walk state**
  - Transition to Idle when `speed <= 0.1`
  - Transition to Run when `speed > 2.0`
- **Run state**
  - Transition to Walk when `speed < 1.5`

State machines are data-driven (JSON files) and can be edited in the editor.

## State Machine Structure

### Core Components

```cpp
/// Parameters that drive state transitions (speed, direction, health, etc.).
struct AnimParameter {
    std::string name;
    enum class Type { Float, Bool, Int, Trigger };
    Type type;

    union {
        float float_value = 0.0f;
        bool bool_value;
        int int_value;
    };
};

/// A single animation state.
struct AnimState {
    std::string name;                          // "Idle", "Walk", "Run"
    ResourceHandle<AnimSequence> animation;   // which animation plays?
    float playback_speed = 1.0f;               // speed scalar

    enum class LoopMode { Once, Loop, PingPong };
    LoopMode loop_mode = LoopMode::Once;

    // Optional: blend tree for locomotion variations
    ResourceHandle<BlendTree> blend_tree;     // if set, animation is ignored

    std::vector<struct AnimTransition*> transitions;  // outgoing transitions
};

/// Transition from one state to another.
struct AnimTransition {
    AnimState* from_state;
    AnimState* to_state;
    float blend_time = 0.2f;                   // crossfade duration

    struct Condition {
        std::string parameter_name;
        enum class Op { Equals, NotEquals, Greater, Less, GreaterOrEqual, LessOrEqual };
        Op operation;
        float threshold;
    };
    std::vector<Condition> conditions;         // ALL must be true to transition

    // Transition priority (earlier = higher priority if multiple valid)
    int priority = 0;
};

/// Complete animation state machine.
class AnimationController {
private:
    std::vector<AnimState> states;
    AnimState* current_state = nullptr;
    std::map<std::string, AnimParameter> parameters;

    float transition_blend_time = 0.0f;   // blend progress (0.0-1.0)
    AnimSequence* blending_from = nullptr; // previous animation during blend
    AnimSequence* blending_to = nullptr;   // target animation during blend

    std::vector<AnimationLayer> layers;    // multiple animation layers

public:
    // ─── State Machine Setup ────────────────────────────────────────
    AnimState* AddState(StringView name, ResourceHandle<AnimSequence> animation);
    void AddTransition(StringView from, StringView to, float blend_time,
                       const std::vector<AnimTransition::Condition>& conditions);
    void SetEntryState(StringView state_name);

    // ─── Parameter Control ──────────────────────────────────────────
    void SetFloat(StringView name, float value);
    void SetBool(StringView name, bool value);
    void SetInt(StringView name, int value);
    void SetTrigger(StringView name);  // auto-reset next frame

    // ─── Playback ───────────────────────────────────────────────────
    void Update(float delta_time);
    void Play();
    void Stop();
    void Pause();

    // ─── Queries ────────────────────────────────────────────────────
    StringView GetCurrentStateName() const { return current_state->name; }
    float GetPlaybackPosition() const;
    float GetPlaybackPercent() const;

    // ─── Callbacks ──────────────────────────────────────────────────
    MulticastDelegate<void(StringView)> OnStateChanged;
    MulticastDelegate<void()> OnStateFinished;
};
```

## State Machine JSON Format

```json
{
  "format": "uzanimcontroller",
  "version": 1,
  "parameters": [
    {"name": "speed", "type": "float", "default": 0.0},
    {"name": "is_jumping", "type": "bool", "default": false},
    {"name": "direction", "type": "float", "default": 0.0},
    {"name": "health", "type": "float", "default": 100.0}
  ],
  "states": [
    {
      "name": "Idle",
      "animation": "res://anims/idle.uzanim",
      "playback_speed": 1.0,
      "loop_mode": "loop"
    },
    {
      "name": "Walk",
      "animation": "res://anims/walk.uzanim",
      "playback_speed": 1.0,
      "loop_mode": "loop",
      "blend_tree": "res://anims/walk_blend_tree.uzblend"
    },
    {
      "name": "Run",
      "animation": "res://anims/run.uzanim",
      "playback_speed": 1.0,
      "loop_mode": "loop"
    },
    {
      "name": "Jump",
      "animation": "res://anims/jump.uzanim",
      "playback_speed": 1.0,
      "loop_mode": "once"
    },
    {
      "name": "Death",
      "animation": "res://anims/death.uzanim",
      "playback_speed": 1.0,
      "loop_mode": "once"
    }
  ],
  "transitions": [
    {
      "from": "Idle",
      "to": "Walk",
      "blend_time": 0.2,
      "conditions": [
        {"parameter": "speed", "operation": ">", "threshold": 0.5}
      ],
      "priority": 0
    },
    {
      "from": "Idle",
      "to": "Jump",
      "blend_time": 0.1,
      "conditions": [
        {"parameter": "is_jumping", "operation": "==", "threshold": 1.0}
      ],
      "priority": 1
    },
    {
      "from": "Walk",
      "to": "Idle",
      "blend_time": 0.3,
      "conditions": [
        {"parameter": "speed", "operation": "<=", "threshold": 0.1}
      ],
      "priority": 0
    },
    {
      "from": "Walk",
      "to": "Run",
      "blend_time": 0.2,
      "conditions": [
        {"parameter": "speed", "operation": ">", "threshold": 2.0}
      ],
      "priority": 0
    },
    {
      "from": "Run",
      "to": "Walk",
      "blend_time": 0.3,
      "conditions": [
        {"parameter": "speed", "operation": "<=", "threshold": 1.5}
      ],
      "priority": 0
    },
    {
      "from": "Any",
      "to": "Death",
      "blend_time": 0.5,
      "conditions": [
        {"parameter": "health", "operation": "<=", "threshold": 0.0}
      ],
      "priority": 100
    }
  ],
  "entry_state": "Idle"
}
```

## Update Logic

### Frame Update

```cpp
void AnimationController::Update(float delta_time) {
    // 1. Evaluate all transition conditions
    AnimState* next_state = nullptr;
    for (auto& transition : current_state->transitions) {
        if (EvaluateConditions(transition)) {
            next_state = transition.to_state;
            break;  // take highest-priority transition
        }
    }

    // 2. If transitioning, start blend
    if (next_state && next_state != current_state) {
        transition_blend_time = 0.0f;
        blending_from = GetCurrentAnimation();
        blending_to = GetNextAnimation(next_state);
        current_state = next_state;
        OnStateChanged.Broadcast(current_state->name);
    }

    // 3. Update blending
    if (transition_blend_time < 1.0f) {
        transition_blend_time += delta_time / current_state->transitions[0].blend_time;
        transition_blend_time = std::min(transition_blend_time, 1.0f);
    }

    // 4. Sample animations
    SampleAnimation(blending_from, playback_position);
    if (transition_blend_time < 1.0f) {
        SampleAnimation(blending_to, playback_position, 1.0f - transition_blend_time);
    }

    // 5. Update playback position
    playback_position += delta_time * current_state->playback_speed;
    if (playback_position >= current_state->animation->GetDuration()) {
        if (current_state->loop_mode == LoopMode::Loop) {
            playback_position = 0.0f;
        } else {
            OnStateFinished.Broadcast();
        }
    }

    // 6. Reset triggers
    for (auto& [name, param] : parameters) {
        if (param.type == AnimParameter::Type::Trigger) {
            param.bool_value = false;
        }
    }
}
```

## Blend Trees

Blend trees interpolate between multiple animations based on parameters. Used for locomotion (walk in any direction).

### 1D Blend Tree (Speed-based)

```cpp
struct BlendTree1D {
    std::string parameter_name;  // "speed"

    struct BlendPoint {
        float threshold;                        // when speed < 0.5, use idle
        ResourceHandle<AnimSequence> animation;
    };
    std::vector<BlendPoint> points;  // sorted by threshold

    AnimSequence* Sample(float parameter_value) const {
        // Find two surrounding points
        size_t left_idx = 0;
        for (size_t i = 1; i < points.size(); ++i) {
            if (points[i].threshold > parameter_value) {
                left_idx = i - 1;
                break;
            }
        }

        // Interpolate
        float t = (parameter_value - points[left_idx].threshold) /
                  (points[left_idx + 1].threshold - points[left_idx].threshold);
        t = std::clamp(t, 0.0f, 1.0f);

        // For now, just return the closest point
        // (true blending would sample both and blend joint rotations)
        return (t < 0.5f) ? points[left_idx].animation : points[left_idx + 1].animation;
    }
};
```

### 2D Blend Tree (Directional)

Blend between walk-forward, walk-backward, walk-left, walk-right based on speed + direction:

```cpp
struct BlendTree2D {
    std::string parameter_x;  // "speed" (0 = backward, 1 = forward)
    std::string parameter_y;  // "direction" (0 = left, 1 = right)

    struct BlendPoint2D {
        float x, y;  // parameter space coordinates
        ResourceHandle<AnimSequence> animation;
    };
    std::vector<BlendPoint2D> points;

    AnimSequence* Sample(float param_x, float param_y) const {
        // Shepard interpolation (distance-weighted blend)
        float total_weight = 0.0f;
        Vec3 result_rotation = {0, 0, 0};

        for (const auto& point : points) {
            float dx = param_x - point.x;
            float dy = param_y - point.y;
            float dist_sq = dx * dx + dy * dy;

            if (dist_sq < 0.001f) {
                // Very close to this point, use it exclusively
                return point.animation;
            }

            float weight = 1.0f / dist_sq;
            total_weight += weight;

            // Would blend animation data here
        }

        // In practice, still return closest for now
        return points[0].animation;
    }
};
```

### Blend Tree JSON

```json
{
  "format": "uzblendtree",
  "version": 1,
  "type": "1d",
  "parameter": "speed",
  "points": [
    {"threshold": 0.0, "animation": "res://anims/idle.uzanim"},
    {"threshold": 1.0, "animation": "res://anims/walk.uzanim"},
    {"threshold": 3.0, "animation": "res://anims/run.uzanim"}
  ]
}
```

## Animation Layers

Stack multiple animations on top of each other (base locomotion + upper body aim):

```cpp
struct AnimationLayer {
    std::string name;  // "Base", "UpperBody", "Facial"
    float blend_weight = 1.0f;  // 0.0-1.0

    AnimationController layer_controller;  // independent state machine per layer
    enum class BlendMode { Override, Additive, Multiply };
    BlendMode blend_mode = BlendMode::Override;
};

// Usage
auto base_layer = anim_controller->GetLayer("Base");
base_layer->SetFloat("speed", current_speed);

auto upper_body_layer = anim_controller->GetLayer("UpperBody");
upper_body_layer->GetState("AimUp")->Play();  // upper body aims while legs walk
```

## Animation Notify Events

Fire callbacks at specific animation frames:

```cpp
struct AnimNotify {
    float time;  // seconds into animation
    std::string event_name;
    std::map<std::string, std::string> payload;
};

// In animation file:
// frame 30/60: emit footstep sound
// frame 45/60: emit impact dust

// In code
anim_controller->OnAnimNotify += [game](StringView event_name, const std::map<std::string, std::string>& data) {
    if (event_name == "footstep") {
        game->PlayFootstepSound(data.at("surface"));
    } else if (event_name == "impact") {
        game->SpawnDustParticles(game->GetPlayer()->GetPosition());
    }
};
```

## Comparison

| Feature | UE5 | Godot | Unity | **UzEngine** |
|---------|-----|-------|-------|------------|
| **State machine** | AnimGraph (visual) | AnimationTree | Animator Controller | **AnimationController (JSON)** |
| **Blend trees** | BlendSpace 1D/2D | BlendTree | Blend Tree | **BlendTree1D/2D** |
| **Layers** | Full support | Limited | Layers | **AnimationLayer** |
| **Transitions** | Condition-based | Condition-based | Condition-based | **Condition-based** |
| **Editing** | Visual in editor | Visual in editor | Visual in inspector | **JSON (or visual editor later)** |

## Rules

1. **ALWAYS** use triggers for one-frame events — reset auto at end of frame
2. **NEVER** transition between incompatible states — path finding prevents impossible transitions
3. **ALWAYS** prioritize transitions by priority field — prevent ambiguous conditions
4. **NEVER** create circular transitions without conditions — infinite loop
5. **ALWAYS** blend animations during transitions — avoid snapping between poses
