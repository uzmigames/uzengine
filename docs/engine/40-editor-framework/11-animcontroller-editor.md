# Animation Controller Editor — State Machine & Blend Space Visual Editor

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/animcontroller-editor.ts`, `engine/anim/anim-controller.h`
> **References**: Node-Graph Framework (§9), Animation System (§32), UE5 AnimGraph/AnimBP, Godot AnimationTree, Unity Animator

## Purpose

The Animation Controller Editor allows animators and gameplay programmers to create state machines that control skeletal animations. Rather than writing code to switch between animation clips, artists visually connect states (idle, walk, run) with transitions, conditions (is_moving, speed > 5), and blend spaces (smooth interpolation between animations based on continuous parameters).

The editor combines state machine visualization with embedded blend trees, making it the primary tool for animation authoring alongside the Skeleton and Animation Timeline editors.

## Architecture

```
Animation Controller Editor (TypeScript)
    ├─ State Machine Canvas
    │  ├─ States (boxes with animation clips)
    │  ├─ Transitions (arrows with conditions)
    │  ├─ Blend Spaces (1D/2D parameter blending)
    │  └─ Default state indicator (blue entry arrow)
    │
    ├─ Preview Viewport
    │  ├─ Skeletal mesh with animations
    │  ├─ Parameter sliders (speed, direction)
    │  ├─ Timeline scrubber
    │  └─ Skeleton/bone visualization
    │
    ├─ Parameters Panel
    │  ├─ Float, Int, Bool, Trigger parameters
    │  └─ Add/remove/edit (shared with Lua scripts)
    │
    ├─ Layers Panel
    │  ├─ Base layer (full body)
    │  ├─ Upper body override
    │  ├─ Additive layers
    │  └─ Layer masks (which bones each layer affects)
    │
    └─ AnimNotify Editor
       ├─ Timeline with markers
       ├─ Event triggers (sound, particle, gameplay)
       └─ Real-time preview in viewport
```

## State Types

### Single Animation

Plays a single animation clip from start to end:

```typescript
interface SingleAnimationState {
    type: "SingleAnimation";
    animationClip: string;      // Path to .uzanim file
    looping: boolean;           // Auto-restart when finished
    playRate: number;           // 1.0 = normal speed
    startFrame: number;         // Start at frame N (for looping)
}
```

### Blend Space 1D

Interpolates between animations based on a single parameter (e.g., speed):

```typescript
interface BlendSpace1D {
    type: "BlendSpace1D";
    parameter: string;          // Parameter name (e.g., "speed")
    parameterRange: [number, number];  // [0, 10] for speed parameter

    // Array of (parameter_value, animation_clip, play_rate) triples
    samples: Array<{
        parameterValue: number;
        clip: string;
        playRate: number;
    }>;

    // Example: walk/run blending
    // samples = [
    //   { parameterValue: 0, clip: "idle", playRate: 1.0 },
    //   { parameterValue: 3, clip: "walk", playRate: 1.0 },
    //   { parameterValue: 7, clip: "run", playRate: 1.2 },
    // ];
}
```

### Blend Space 2D

Interpolates based on two parameters (e.g., direction + speed):

```typescript
interface BlendSpace2D {
    type: "BlendSpace2D";
    parameterX: string;
    parameterY: string;
    parameterRangeX: [number, number];
    parameterRangeY: [number, number];

    // 2D grid of animations
    samples: Array<{
        x: number;
        y: number;
        clip: string;
    }>;

    // Example: directional locomotion
    // X = horizontal direction (left -1 to right +1)
    // Y = forward speed (0 to 1)
    // Samples: idle (0,0), strafe left (−1, 0.5), walk forward (0, 0.5), run diagonal (−0.5, 1.0), etc.
}
```

### Random Selection

Randomly picks one animation from a list:

```typescript
interface RandomState {
    type: "Random";
    clips: Array<{
        clip: string;
        weight: number;  // Higher = more likely
    }>;
}
```

### Sequence

Plays clips in order:

```typescript
interface SequenceState {
    type: "Sequence";
    clips: Array<{
        clip: string;
        looping: boolean;
    }>;
}
```

## Transitions

Transitions connect states and specify when to switch:

```typescript
interface Transition {
    fromState: string;
    toState: string;

    /// Conditions that must be true to take this transition
    conditions: TransitionCondition[];

    /// Blend duration (crossfade between animations)
    blendDuration: number;  // In seconds, typically 0.1-0.3

    /// Blend curve (linear, ease-in, ease-out, custom)
    blendCurve: "Linear" | "EaseIn" | "EaseOut" | "Custom";

    /// If true, can interrupt current animation mid-play
    canInterruptSelf: boolean;

    /// If true, can be interrupted by other transitions
    canBeInterrupted: boolean;
}

interface TransitionCondition {
    parameter: string;
    operator: "===" | "!==" | ">" | "<" | ">=" | "<=";
    value: any;
}

// Example: Walk → Run transition
// condition: { parameter: "speed", operator: ">", value: 5.0 }
```

## Parameters

Parameters are the inputs to the state machine. They're shared with Lua scripts via `ctx.anim`:

```typescript
interface AnimControllerParameter {
    name: string;
    type: "Float" | "Int" | "Bool" | "Trigger";

    // Float/Int constraints
    min?: number;
    max?: number;

    // Default value
    defaultValue: any;
}

// Example parameters for a character
const params = [
    { name: "speed", type: "Float", min: 0, max: 10, defaultValue: 0 },
    { name: "direction", type: "Float", min: -1, max: 1, defaultValue: 0 },
    { name: "is_grounded", type: "Bool", defaultValue: true },
    { name: "jump_pressed", type: "Trigger", defaultValue: false },
    { name: "is_dead", type: "Bool", defaultValue: false },
];

// In Lua, animating a character:
// ctx.anim:set_parameter(entity_id, "speed", current_speed)
// ctx.anim:set_parameter(entity_id, "direction", input_direction)
// ctx.anim:trigger(entity_id, "jump_pressed")
```

## Animation Layers

Layers allow partial animation blending (e.g., upper body plays idle while legs run):

```typescript
interface AnimationLayer {
    name: string;
    blendMode: "Replace" | "Additive" | "Overlay";
    weight: number;  // [0, 1]

    // Bone mask: which bones this layer affects
    boneMask?: {
        mode: "Include" | "Exclude";
        bones: string[];
    };

    // The state machine for this layer
    stateMachine: StateMachine;
}

// Example: two-layer character
const layers = [
    {
        name: "Base",
        blendMode: "Replace",
        weight: 1.0,
        boneMask: undefined,  // Affects all bones
        // State machine for full body
    },
    {
        name: "UpperBody",
        blendMode: "Additive",
        weight: 1.0,
        boneMask: {
            mode: "Include",
            bones: ["Spine", "LeftShoulder", "RightShoulder", "LeftArm", "RightArm", "LeftHand", "RightHand"],
        },
        // State machine for upper body only (e.g., reload anim)
    },
];
```

## AnimNotify Markers

AnimNotify events trigger at specific frames and fire gameplay events:

```typescript
interface AnimNotifyMarker {
    frame: number;          // Frame in animation clip
    time: number;           // Time in seconds
    type: "Sound" | "Particle" | "RootMotion" | "Event" | "Custom";
    data: {
        // Type-specific data
    };
}

// Example notifications in a footstep animation
const notifies = [
    {
        frame: 10,
        time: 0.33,
        type: "Sound",
        data: { sound: "res://sfx/footstep_left.wav", volume: 0.8 }
    },
    {
        frame: 25,
        time: 0.83,
        type: "Sound",
        data: { sound: "res://sfx/footstep_right.wav", volume: 0.8 }
    },
    {
        frame: 15,
        time: 0.5,
        type: "Event",
        data: { event: "on_footstep_left" }
    },
];
```

## Preview Viewport

The preview shows the skeletal mesh with live parameter controls:

```typescript
class AnimControllerPreview {
    private mesh: SkeletalMesh;
    private animator: Animator;
    private parameters: Map<string, number> = new Map();

    /// Render skeletal mesh with current animations
    render() {
        // Update parameters from UI sliders
        for (const [param, value] of this.parameters) {
            this.animator.setParameter(param, value);
        }

        // Update animator (blends layers, evaluates state machine)
        this.animator.update(deltaTime);

        // Draw skeletal mesh
        const pose = this.animator.getPose();
        this.mesh.render(pose);

        // Draw skeleton (debug)
        if (this.showSkeleton) {
            this.drawSkeleton(pose);
        }

        requestAnimationFrame(() => this.render());
    }

    /// Create UI slider for each parameter
    createParameterUI(param: AnimControllerParameter) {
        const container = document.createElement('div');
        container.className = 'parameter-control';

        const label = document.createElement('label');
        label.textContent = param.name;
        container.appendChild(label);

        if (param.type === "Float") {
            const slider = document.createElement('input');
            slider.type = 'range';
            slider.min = String(param.min ?? 0);
            slider.max = String(param.max ?? 100);
            slider.step = '0.1';
            slider.value = String(param.defaultValue ?? 0);
            slider.oninput = () => {
                this.parameters.set(param.name, parseFloat(slider.value));
            };
            container.appendChild(slider);
        } else if (param.type === "Bool") {
            const checkbox = document.createElement('input');
            checkbox.type = 'checkbox';
            checkbox.checked = param.defaultValue ?? false;
            checkbox.onchange = () => {
                this.parameters.set(param.name, checkbox.checked ? 1.0 : 0.0);
            };
            container.appendChild(checkbox);
        } else if (param.type === "Trigger") {
            const button = document.createElement('button');
            button.textContent = `Trigger ${param.name}`;
            button.onclick = () => {
                this.animator.trigger(param.name);
            };
            container.appendChild(button);
        }

        return container;
    }
}
```

## File Format

Animation Controllers are saved as `.uzanimcontroller` JSON:

```json
{
  "version": 1,
  "metadata": {
    "title": "CharacterAnimController",
    "skeleton": "res://models/character.uzskeleton"
  },
  "parameters": [
    {
      "name": "speed",
      "type": "Float",
      "min": 0,
      "max": 10,
      "defaultValue": 0
    },
    {
      "name": "is_grounded",
      "type": "Bool",
      "defaultValue": true
    },
    {
      "name": "jump_pressed",
      "type": "Trigger"
    }
  ],
  "layers": [
    {
      "name": "Base",
      "blendMode": "Replace",
      "weight": 1.0,
      "stateMachine": {
        "defaultState": "Idle",
        "states": [
          {
            "name": "Idle",
            "type": "SingleAnimation",
            "animationClip": "res://anims/idle.uzanim",
            "looping": true,
            "playRate": 1.0,
            "position": { "x": 100, "y": 100 }
          },
          {
            "name": "Walk",
            "type": "SingleAnimation",
            "animationClip": "res://anims/walk.uzanim",
            "looping": true,
            "playRate": 1.0,
            "position": { "x": 300, "y": 100 }
          },
          {
            "name": "Run",
            "type": "SingleAnimation",
            "animationClip": "res://anims/run.uzanim",
            "looping": true,
            "playRate": 1.2,
            "position": { "x": 500, "y": 100 }
          }
        ],
        "transitions": [
          {
            "from": "Idle",
            "to": "Walk",
            "conditions": [
              { "parameter": "speed", "operator": ">", "value": 0.5 }
            ],
            "blendDuration": 0.2,
            "blendCurve": "Linear",
            "canInterruptSelf": false,
            "canBeInterrupted": true
          },
          {
            "from": "Walk",
            "to": "Run",
            "conditions": [
              { "parameter": "speed", "operator": ">", "value": 5.0 }
            ],
            "blendDuration": 0.3,
            "blendCurve": "Linear",
            "canInterruptSelf": false,
            "canBeInterrupted": true
          }
        ]
      }
    }
  ]
}
```

## Lua Integration

AnimControllers are accessed in Lua via the animation context:

```lua
-- Get current animation state
local state = ctx.anim:get_state(entity_id)
print("Current state:", state)  -- "Walk", "Run", etc.

-- Set parameters (continuous)
ctx.anim:set_parameter(entity_id, "speed", move_speed)
ctx.anim:set_parameter(entity_id, "direction", input_x)

-- Set bool parameters
ctx.anim:set_bool(entity_id, "is_grounded", is_grounded)

-- Trigger events (one-time)
ctx.anim:trigger(entity_id, "jump_pressed")
ctx.anim:trigger(entity_id, "attack_pressed")

-- Query current animation
local anim_name, anim_time, anim_length = ctx.anim:get_animation(entity_id)
print(anim_name, "at", anim_time, "/", anim_length)

-- AnimNotify callback
ctx.anim:on_notify(entity_id, "on_footstep_left", function()
    print("Left foot planted")
end)
```

## Comparison

| Feature | UE5 AnimBP/AnimGraph | Godot AnimationTree | Unity Animator | **UzEngine** |
|---|---|---|---|---|
| **State machine** | Yes (AnimBP) | Yes (Tree) | Yes (Animator) | **Yes** |
| **Blend spaces** | 1D/2D/3D | Blend points only | Blend trees (similar) | **1D/2D** |
| **Layers** | Yes (blend per-bone) | Limited | Yes (layer masks) | **Yes (with bone masks)** |
| **AnimNotify** | Yes (Montages) | Limited (AnimationPlayer callbacks) | Animation events | **Yes (frame-based)** |
| **Parameters** | Yes (shared with Lua/code) | Variables | Animator parameters | **Yes (shared with Lua)** |
| **Preview** | Real-time with mesh | Real-time | Real-time | **Real-time with parameter sliders** |
| **Blend curves** | Multiple (ease, custom curves) | Linear/ease | Linear | **Linear/ease-in/out/custom** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Single animation + simple transitions, state machine canvas, parameter panel, basic preview |
| **Phase 3** | Blend spaces (1D/2D), animation layers, AnimNotify markers, advanced transition rules, parameter triggers, live preview with sliders |
| **Phase 4** | Blend space 3D, synchronization (keep animations in sync across layers), montage system (complex sequences), animation sharing (multiple characters using same controller with different skeletons) |

