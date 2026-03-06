# Animation Editor Tool Spec

## 1. Overview & Goals

The Animation Editor is a professional-grade toolset embedded inside the UzEngine VSCode extension. It provides a complete animation authoring workflow for game characters, including keyframe editing, state machine design, blend tree construction, montage authoring, inverse kinematics setup, and retargeting.

### Target Users

| Persona | Needs |
|---------|-------|
| **Animator** | Keyframe timeline, curve editor, playback preview, notify placement |
| **Technical Animator** | State machine graph, blend trees, IK rig setup, retarget mapping |
| **Gameplay Programmer** | Montage sections, Lua event notifies, root motion tuning |
| **AI Agent** | All file formats are plain-text INI -- readable and writable by LLMs |

### Design Principles

1. **VSCode-native** -- all panels are WebviewPanels inside VSCode, no external windows
2. **Engine-rendered preview** -- the 3D viewport streams JPEG frames from `render.exe --editor`, identical to the scene viewport
3. **Text-first file formats** -- `.uzanim`, `.uzfsm`, `.uzblend`, `.uzmontage` are INI-like plain text, version-controllable and AI-friendly
4. **Non-destructive** -- original imported animation data is never modified; edits layer on top
5. **game.dll integration** -- the animation runtime lives in `game.dll`; the editor communicates via IPC to preview state machines and blends in real time

### System Dependencies

| System | Spec | Role |
|--------|------|------|
| Skeletal Mesh | `docs/specs/skeletal-mesh.md` | `.uzskel` skeleton, `SkinnedVertex`, bone palette |
| Animation System | `docs/specs/animation-system.md` | `AnimationClip`, `AnimStateMachine`, `Pose`, blending |
| Scene System | `docs/specs/scene-system.md` | `SkinnedMesh` node type, `res://` paths |
| Editor Panels | `docs/specs/editor-panels.md` | VSCode panel architecture, IPC patterns |
| Editor Gizmos | `docs/specs/editor-gizmos.md` | 3D handle rendering, bone manipulation |
| Event Bus | `docs/specs/event-bus.md` | Notify dispatch to audio/VFX/scripts |

---

## 2. Editor Layout

The Animation Editor opens as a dedicated editor group layout in VSCode. When a user opens a `.uzanim`, `.uzfsm`, `.uzblend`, or `.uzmontage` file, the extension activates the Animation Editor layout.

### Full Layout Diagram

```
+---------------------------+-------------------------------------------+---------------------+
|   BONE HIERARCHY TREE     |              3D VIEWPORT                  |    PROPERTIES       |
|   (left sidebar)          |         (center, WebviewPanel)            |    INSPECTOR        |
|                           |                                           |   (right sidebar)   |
|  v root                   |  +-------------------------------------+ |                     |
|    v pelvis               |  |                                     | |  -- Keyframe --     |
|      v spine_01           |  |     Animated character preview      | |  Time: 0.500s      |
|        v spine_02         |  |     with skeleton debug overlay     | |  Bone: upperarm_l  |
|          v spine_03       |  |                                     | |  Channel: Rotation  |
|            v neck         |  |     Grid floor, bone axes,          | |  Value:             |
|              head [eye]   |  |     joint spheres                   | |    X: -15.2 deg    |
|            clavicle_l     |  |                                     | |    Y:   3.0 deg    |
|              upperarm_l   |  |                                     | |    Z:  45.8 deg    |
|                lowerarm_l |  +-------------------------------------+ |                     |
|                  hand_l   |  | [|<] [<] [>Play] [>|] [Loop] [Rec] | |  Interpolation:     |
|            clavicle_r     |  | Frame: 15 / 60   Time: 0.500s      | |  [v Bezier      ]   |
|              upperarm_r   |  +-------------------------------------+ |                     |
|      v thigh_l            |                                           |  In Tangent:        |
|        calf_l             |                                           |    Weight: 0.33     |
|          foot_l           |                                           |    Angle: -12.0     |
|      v thigh_r            |                                           |                     |
|        calf_r             |                                           |  Out Tangent:       |
|          foot_r           |                                           |    Weight: 0.33     |
|                           |                                           |    Angle:   5.0     |
+---------------------------+-------------------------------------------+---------------------+
|                           TIMELINE PANEL (bottom)                                           |
|                                                                                             |
|  Asset: idle_breathe.uzanim   Duration: 2.000s   FPS: 30   Frames: 60                      |
|                                                                                             |
|  Time Ruler:  0.0   0.5   1.0   1.5   2.0                                                  |
|               |-----|-----|-----|-----|                                                      |
|                        ^ scrubber (draggable)                                               |
|                                                                                             |
|  Track                 |  Keyframes on timeline                                            |
|  ------------------------------------------------------------------                        |
|  v pelvis                                                                                   |
|    > Translation      |  o-----------o-----------o-----------o                              |
|    > Rotation         |  o-----o-----o-----o-----o-----o-----o                              |
|  v upperarm_l                                                                               |
|    > Rotation         |  o-----------o-----------o                                          |
|  v hand_l                                                                                   |
|    > Rotation         |  o-----------o---o-------o                                          |
|  -- Morph Targets --                                                                        |
|    > smile            |  o-----------o                                                      |
|    > brow_raise_l     |  o-------o---o                                                      |
|  -- Events --                                                                               |
|    > Notify Track 1   |  [footstep_L]     [footstep_R]    [footstep_L]                      |
|    > Notify Track 2   |       [play_sound:whoosh]                                           |
|  -- Properties --                                                                           |
|    > custom_float     |  o-----------o-----------o                                          |
|                                                                                             |
+---------------------------------------------+-------------------------------------------+  |
|            CURVE EDITOR (bottom-right)       |                                           |  |
|                                              |                                           |  |
|   1.0 |          .--*                        |                                           |  |
|       |        /     \                       |                                           |  |
|   0.5 |      *        \                      |                                           |  |
|       |     /           *---.                |                                           |  |
|   0.0 |---*                  *---            |                                           |  |
|       +----+----+----+----+----              |                                           |  |
|       0.0  0.5  1.0  1.5  2.0               |                                           |  |
|                                              |                                           |  |
|   [Fit All] [Normalize] [Snap Grid]         |                                           |  |
+----------------------------------------------+-------------------------------------------+
```

### Panel Responsibilities

| Panel | VSCode API | Purpose |
|-------|-----------|---------|
| 3D Viewport | `WebviewPanel` | DX12-rendered character preview, skeleton overlay, playback |
| Bone Hierarchy Tree | `TreeDataProvider` | Skeleton bone tree, track visibility toggles, bone selection |
| Timeline Panel | `WebviewPanel` | Track lanes, keyframe diamonds, scrubber, time ruler |
| Curve Editor | `WebviewPanel` (embedded in Timeline) | Bezier curve editing per selected channel |
| Properties Inspector | `WebviewPanel` | Selected keyframe/notify/state properties |
| Toolbar | Status bar items + Webview toolbar row | Transport controls, frame counter, mode switches |

### Toolbar Controls

```
[|<]  [<]  [> Play]  [>|]  [Loop]  [Record]  |  Frame: [15] / 60  |  FPS: [30]  |  Speed: [1.0x]
```

| Button | Action | Hotkey |
|--------|--------|--------|
| `\|<` | Jump to first frame | Home |
| `<` | Step back one frame | , (comma) |
| `> Play` / `\|\| Pause` | Play / pause playback | Space |
| `>\|` | Step forward one frame | . (period) |
| Loop | Toggle loop mode (once / loop / ping-pong) | L |
| Record | Toggle record mode (auto-keyframe on transform) | K |
| Frame input | Jump to specific frame | -- |
| FPS | Set playback framerate | -- |
| Speed | Playback speed multiplier (0.1x to 4.0x) | +/- |

---

## 3. Keyframe System

### 3.1 Track Types

Each animated property is represented as a **track** in the timeline. Tracks are grouped by bone, then by transform channel.

| Track Type | Data per Key | Storage |
|-----------|-------------|---------|
| Bone Translation | `{ time: float, value: vec3 }` | `BoneChannel.pos_keys` |
| Bone Rotation | `{ time: float, value: quat }` | `BoneChannel.rot_keys` |
| Bone Scale | `{ time: float, value: vec3 }` | `BoneChannel.scl_keys` |
| Morph Target Weight | `{ time: float, value: float }` | `MorphTrack.keys` |
| Custom Float Property | `{ time: float, value: float }` | `PropertyTrack.keys` |
| Event Notify | `{ time: float, event: string, params: string }` | `AnimNotify` |

### 3.2 Keyframe Data Structure

```cpp
enum class InterpMode : uint8_t {
    Constant,     // step function -- hold value until next key
    Linear,       // linear interpolation
    Bezier,       // cubic bezier with tangent handles
    Hermite       // hermite spline with in/out tangent vectors
};

struct KeyframeMeta {
    InterpMode    interp = InterpMode::Bezier;
    float         in_tangent_weight  = 0.333f;   // normalized [0..1]
    float         in_tangent_angle   = 0.0f;     // degrees
    float         out_tangent_weight = 0.333f;
    float         out_tangent_angle  = 0.0f;
    bool          tangent_broken = false;         // true = free handles, false = aligned
};

// Extended key for editor use (runtime strips metadata)
struct EditorKeyVec3 {
    float         time;
    glm::vec3     value;
    KeyframeMeta  meta[3];    // one per channel (X, Y, Z)
};

struct EditorKeyQuat {
    float         time;
    glm::quat     value;
    KeyframeMeta  meta[3];    // Euler XYZ decomposition for curve editing
};

struct EditorKeyFloat {
    float         time;
    float         value;
    KeyframeMeta  meta;
};
```

### 3.3 Interpolation Modes

| Mode | Behavior | Use Case |
|------|----------|----------|
| **Constant** | Value jumps instantly at key time | On/off states, weapon swap |
| **Linear** | Straight-line interpolation between keys | Mechanical motion, simple paths |
| **Bezier** | Cubic bezier curve with weighted tangent handles | Organic motion, easing |
| **Hermite** | Hermite spline with in/out tangent vectors | Smooth looping, auto-tangent |

### 3.4 Auto-Tangent Computation

When a keyframe is inserted or moved, the editor can automatically compute tangent angles using the Catmull-Rom method:

```cpp
// For key[i] with neighbors key[i-1] and key[i+1]:
float slope = (key[i+1].value - key[i-1].value) / (key[i+1].time - key[i-1].time);
key[i].meta.out_tangent_angle = atan2(slope * dt_next, dt_next) * RAD_TO_DEG;
key[i].meta.in_tangent_angle  = atan2(slope * dt_prev, dt_prev) * RAD_TO_DEG;

// Boundary keys (first/last) use flat tangent (angle = 0)
```

Auto-tangent is the default mode. The user can switch to manual tangent editing at any time by selecting a key and modifying tangent handles in the curve editor.

### 3.5 Keyframe Operations

| Operation | Hotkey | Description |
|-----------|--------|-------------|
| Insert key | I | Insert keyframe at scrubber position for selected tracks |
| Delete key | Delete | Remove selected keyframes |
| Copy keys | Ctrl+C | Copy selected keyframes to clipboard |
| Paste keys | Ctrl+V | Paste keyframes at scrubber position |
| Duplicate keys | Ctrl+D | Duplicate and offset in time |
| Select all keys | Ctrl+A | Select all keyframes in visible tracks |
| Box select | Left-click drag (empty area) | Rubber-band selection of keyframes |
| Move keys | Left-click drag (selected key) | Slide keyframe(s) in time and/or value |
| Snap to frame | Hold Ctrl while dragging | Snap key time to nearest frame boundary |

### 3.6 Record Mode

When Record mode is active (K key):

1. The scrubber position defines the current time
2. Any transform change made in the 3D viewport (via gizmos or inspector) automatically creates or updates a keyframe at the current time
3. The affected track channel (translation/rotation/scale) is auto-created if it does not exist
4. A red circle indicator appears in the viewport and timeline to show recording is active

---

## 4. Curve Editor

The Curve Editor provides per-channel bezier curve visualization and editing. It occupies the bottom-right portion of the Animation Editor layout, sharing space with the timeline.

### 4.1 Curve Display

```
Value
 1.0 |          .--*                   Channels visible:
     |        /     \                  [x] X (red)
 0.5 |      *        \                 [x] Y (green)
     |     / |         *---.           [x] Z (blue)
 0.0 |---*   |              *---
     +----+--|-+----+----+----
     0.0  0.5 |1.0  1.5  2.0  Time
              ^
          scrubber
```

- Each channel (X, Y, Z) is drawn in its axis color (red, green, blue)
- Selected keys show tangent handles as lines extending from the key point
- The scrubber position is synchronized with the timeline scrubber
- Value axis auto-scales to fit visible data, or can be manually zoomed

### 4.2 Tangent Handle Modes

| Mode | Behavior | Visual |
|------|----------|--------|
| **Free** | In and out tangents move independently | Two separate handle lines |
| **Aligned** | In and out tangents are co-linear but can have different weights | Single line through key |
| **Flat** | Both tangents forced horizontal (angle = 0) | Horizontal handles |
| **Stepped** | Equivalent to Constant interpolation | No visible curve between keys |
| **Auto** | Catmull-Rom auto-computed tangents | Small handle indicators |

Switching tangent mode: right-click a key in the curve editor and select from the context menu.

### 4.3 Curve Editor Operations

| Operation | Input | Description |
|-----------|-------|-------------|
| Pan view | Middle-click drag | Pan the curve editor viewport |
| Zoom | Scroll wheel | Zoom both axes uniformly |
| Zoom horizontal | Ctrl+Scroll | Zoom time axis only |
| Zoom vertical | Shift+Scroll | Zoom value axis only |
| Fit all | A | Fit all visible curves into the viewport |
| Fit selected | F | Fit selected keys into the viewport |
| Box select keys | Left-click drag (empty area) | Select keys within rectangle |
| Move key | Left-click drag (key point) | Move key in time and value |
| Move tangent | Left-click drag (tangent handle) | Adjust tangent angle and weight |
| Scale keys (time) | Alt+drag selection | Scale selected keys along time axis |
| Scale keys (value) | Alt+Shift+drag selection | Scale selected keys along value axis |
| Normalize view | N | Scale value axis to [0, 1] range |
| Snap to grid | Hold Ctrl while dragging key | Snap to visible grid lines |

### 4.4 Rotation Curve Special Handling

Quaternion rotation keys are internally stored as quaternions but **displayed in the curve editor as Euler angles** (XYZ decomposition) for intuitive editing. The editor must:

1. Decompose quaternion to Euler on display
2. Detect and correct gimbal lock discontinuities (unwrap angles across 180-degree boundaries)
3. Recompose Euler edits back to quaternion on commit
4. Warn the user with a yellow indicator when Euler angles approach gimbal lock regions (pitch near +/-90)

---

## 5. Animation State Machine Editor

The Animation State Machine (ASM) editor is a visual node graph for designing character animation controllers. It opens when the user edits a `.uzfsm` file.

### 5.1 State Machine Layout

```
+-----------------------------------------------------------------------+
|  ANIMATION STATE MACHINE: HumanoidLocomotion.uzfsm                    |
|  [+ Add State]  [+ Add Transition]  [Compile]  [Preview]             |
+-----------------------------------------------------------------------+
|                                                                       |
|  +-----------+          +-----------+          +-----------+          |
|  |           |  speed   |           |  speed   |           |          |
|  |   IDLE    |--------->|   WALK    |--------->|    RUN    |          |
|  |           |  > 0.5   |           |  > 3.5   |           |          |
|  | idle.uzanim|<---------|walk.uzanim|<---------|run.uzanim |          |
|  |  loop=true|  < 0.1   | loop=true |  < 2.0   | loop=true |          |
|  +-----------+          +-----------+          +-----------+          |
|       ^                      |                      |                 |
|       |                      |                      |                 |
|       |        is_jumping    |   is_jumping          |  is_jumping    |
|       |        == true       |   == true             |  == true       |
|       |                      v                       v                |
|       |                 +-----------+                                  |
|       |                 |  JUMP_UP  |                                  |
|       |    (on_end)     |           |                                  |
|       |<----------------|jump.uzanim|                                  |
|       |                 |  loop=no  |                                  |
|       |                 +-----+-----+                                  |
|       |                       |                                        |
|       |                       | on_end                                 |
|       |                       v                                        |
|       |                 +-----------+                                  |
|       |                 |   LAND    |                                  |
|       +<----------------|           |                                  |
|            on_end       |land.uzanim|                                  |
|                         |  loop=no  |                                  |
|                         +-----------+                                  |
|                                                                       |
|  Parameters:                                                          |
|  +-------------------+--------+------------------+                    |
|  | Name              | Type   | Default          |                    |
|  +-------------------+--------+------------------+                    |
|  | speed             | float  | 0.0              |                    |
|  | is_jumping        | bool   | false             |                    |
|  | is_grounded       | bool   | true              |                    |
|  +-------------------+--------+------------------+                    |
+-----------------------------------------------------------------------+
```

### 5.2 State Properties

Each state node in the graph has the following configurable properties:

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `name` | string | required | Unique state identifier |
| `clip` | resource path | required | `res://` path to `.uzanim` clip |
| `looping` | bool | `true` | Whether the clip loops |
| `speed` | float | `1.0` | Playback speed multiplier |
| `blend_in` | float | `0.2` | Default blend-in time (seconds) when entering this state |
| `on_end` | enum | `hold` | What happens when a non-looping clip ends: `hold`, `transition:<name>`, `state:<name>` |
| `blend_tree` | resource path | -- | Optional `.uzblend` file instead of a single clip |
| `root_motion` | bool | `false` | Extract root motion from this state's clip |

### 5.3 Transition Conditions

Transitions connect two states and fire when their condition evaluates to `true`.

| Condition Syntax | Meaning | Example |
|-----------------|---------|---------|
| `param > value` | Float greater than | `speed > 0.5` |
| `param < value` | Float less than | `speed < 0.1` |
| `param >= value` | Float greater or equal | `speed >= 3.5` |
| `param <= value` | Float less or equal | `speed <= 2.0` |
| `param == value` | Exact match (bool or float) | `is_jumping == true` |
| `param != value` | Not equal | `is_grounded != false` |
| `AND(cond, cond)` | Logical AND | `AND(speed > 3.0, is_grounded == true)` |
| `OR(cond, cond)` | Logical OR | `OR(is_jumping == true, is_falling == true)` |
| `exit_time >= value` | State has played for N seconds | `exit_time >= 0.8` |
| `clip_finished` | Non-looping clip reached end | `clip_finished` |

### 5.4 Transition Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `name` | string | auto-generated | Unique transition identifier |
| `from` | string | required | Source state name (`*` = Any State) |
| `to` | string | required | Target state name |
| `blend_time` | float | `0.2` | Cross-fade duration in seconds |
| `condition` | string | required | Condition expression (see table above) |
| `priority` | int | `0` | Higher priority transitions are evaluated first |
| `can_interrupt` | bool | `true` | Whether this transition can be interrupted by a higher-priority one |

### 5.5 Any State Transitions

A transition with `from = *` can fire from any state. Used for global reactions (jump, death, hit reaction). Any State transitions are evaluated after state-specific transitions, unless they have higher priority.

### 5.6 Sub-State Machines

A state can reference another `.uzfsm` file instead of a clip, creating a nested FSM:

```ini
[state name=locomotion]
sub_fsm = res://anim/locomotion.uzfsm
```

The parent FSM treats the sub-FSM as a single state. Entry goes to the sub-FSM's default state; the sub-FSM can signal "exit" to return to the parent.

### 5.7 State Machine Graph Interaction

| Action | Input | Description |
|--------|-------|-------------|
| Add state | Double-click empty area / toolbar button | Create new state node |
| Add transition | Right-click drag from state to state | Create transition arrow |
| Select node | Left-click | Select state or transition for inspector |
| Move node | Left-click drag | Reposition state node in graph |
| Delete | Delete key | Remove selected state or transition |
| Pan graph | Middle-click drag / Scroll | Pan the node graph viewport |
| Zoom graph | Ctrl+Scroll | Zoom in/out |
| Set default state | Right-click state, "Set as Entry" | Mark state as the initial state (green border) |
| Preview | Click "Preview" toolbar button | Play the FSM in the 3D viewport with live parameter sliders |

---

## 6. Blend Tree Editor

Blend trees define how multiple animation clips are blended based on runtime parameters. They are referenced from state machine states via the `blend_tree` property.

### 6.1 Blend Tree Types

#### 6.1.1 1D Blend Space

Blends between clips along a single float parameter axis.

```
Blend Parameter: speed [0.0 --------*------------ 6.0]
                          |    |         |
                        idle  walk      run
                        0.0   1.5       5.0

Output pose = lerp(walk, run, remap(speed, 1.5, 5.0))
              (when speed is between 1.5 and 5.0)
```

```ini
[uzblend version=1 name="LocomotionBlend" type=blend_1d]
parameter = speed

[sample name=idle]
clip  = res://anim/idle.uzanim
value = 0.0

[sample name=walk]
clip  = res://anim/walk.uzanim
value = 1.5

[sample name=run]
clip  = res://anim/run.uzanim
value = 5.0
```

#### 6.1.2 2D Blend Space

Blends clips on a 2D parameter plane. Uses Delaunay triangulation to determine blend weights.

```
         Forward (+Y)
            |
     jog_FL | jog_F  | jog_FR
       *----+----*----+----*
            |         |
     jog_L  | idle    | jog_R
       *----+----*----+----*
            |         |
     jog_BL | jog_B   | jog_BR
       *----+----*----+----*
            |
        Backward (-Y)
    Left (-X)         Right (+X)
```

```ini
[uzblend version=1 name="DirectionalJog" type=blend_2d]
parameter_x = move_x
parameter_y = move_y

[sample name=idle]
clip  = res://anim/idle.uzanim
pos_x = 0.0
pos_y = 0.0

[sample name=jog_f]
clip  = res://anim/jog_forward.uzanim
pos_x = 0.0
pos_y = 1.0

[sample name=jog_r]
clip  = res://anim/jog_right.uzanim
pos_x = 1.0
pos_y = 0.0

; ... remaining directional samples
```

#### 6.1.3 Additive Layer Blend

Layers an additive animation on top of a base pose:

```ini
[uzblend version=1 name="BreathingOverlay" type=additive]
base_clip = res://anim/idle.uzanim
additive_clip = res://anim/breathing_additive.uzanim
weight_param = breathing_weight
```

Runtime formula:
```
final_pose[i].rotation = base_pose[i].rotation * additive_delta[i].rotation
final_pose[i].position = base_pose[i].position + additive_delta[i].position * weight
```

### 6.2 Blend Tree Graph Editor

The blend tree graph is a directed acyclic graph (DAG) of blend nodes rendered in a WebviewPanel.

```
+------------------------------------------------------------------+
|  BLEND TREE: LocomotionBlend.uzblend                             |
+------------------------------------------------------------------+
|                                                                  |
|  +------------+                                                  |
|  | Parameter  |                                                  |
|  |  "speed"   |---+                                              |
|  +------------+   |                                              |
|                    v                                              |
|              +-------------+       +--------+                    |
|              |  1D Blend   |------>| OUTPUT |                    |
|              |             |       +--------+                    |
|              | idle  walk  run                                   |
|              | 0.0   1.5   5.0                                   |
|              +-------------+                                     |
|                ^   ^   ^                                         |
|                |   |   |                                         |
|  +---------+ +---------+ +---------+                             |
|  | idle    | | walk    | | run     |                             |
|  | .uzanim | | .uzanim | | .uzanim |                             |
|  +---------+ +---------+ +---------+                             |
|                                                                  |
+------------------------------------------------------------------+
```

### 6.3 IK Pass Integration

Blend trees can include an IK pass node that runs after blending:

```ini
[ik_pass name=foot_ik]
type = two_bone
chain_tip = foot_l
chain_root = thigh_l
pole_bone = calf_l
target_param = left_foot_ik_target
weight_param = left_foot_ik_weight
```

The IK pass modifies the blended pose in-place before it is sent to the GPU skinning pipeline.

---

## 7. Animation Montage Editor

Animation montages enable layered, scripted animation playback with precise event timing. They are used for attack combos, cinematic sequences, and complex multi-part animations.

### 7.1 Montage Structure

```
+-----------------------------------------------------------------------+
|  MONTAGE: SwordCombo.uzmontage                                       |
|  [Play] [Stop]  Slot: UpperBody                                      |
+-----------------------------------------------------------------------+
|                                                                       |
|  Sections:                                                            |
|  +---------+---------+---------+---------+                            |
|  | WindUp  | Slash1  | Slash2  | Recover |                            |
|  | 0.0-0.3 | 0.3-0.8 | 0.8-1.4 | 1.4-2.0 |                         |
|  +---------+---------+---------+---------+                            |
|                                                                       |
|  Clip Track:                                                          |
|  |====WindUp====|=====Slash1=====|=====Slash2=====|===Recover===|     |
|  0.0           0.3              0.8              1.4           2.0    |
|                                                                       |
|  Notify Track 1 (Gameplay):                                          |
|  |              [attack_start] [attack_end]    [attack_start]   |     |
|  |              0.35           0.75            0.85             |     |
|                                                                       |
|  Notify Track 2 (Audio):                                             |
|  |     [whoosh]        [whoosh]         [whoosh]                |     |
|  |     0.32            0.78             0.83                    |     |
|                                                                       |
|  Notify Track 3 (VFX):                                               |
|  |          [sword_trail_start]  [sword_trail_end]              |     |
|  |          0.33                 0.76                           |     |
|                                                                       |
|  Blend Mask: UpperBody (spine_02 and above)                          |
+-----------------------------------------------------------------------+
```

### 7.2 Montage Components

| Component | Description |
|-----------|-------------|
| **Section** | Named time range within the montage (e.g., "WindUp", "Slash1") |
| **Clip** | The animation clip data that drives the montage |
| **Notify** | Event fired at a specific time (point notify) |
| **Notify State** | Event with duration (start time + end time), e.g., `sword_trail` |
| **Slot** | Blend mask slot name that determines which bones this montage affects |
| **Blend In/Out** | Cross-fade durations when the montage starts/ends |

### 7.3 Section Navigation

Sections enable runtime branching within a montage:

```cpp
// In Lua game script:
ctx.anim:play_montage(entity, "res://anim/sword_combo.uzmontage")
-- After detecting player input during Slash1:
ctx.anim:montage_jump_to_section(entity, "Slash2")
-- Or, if no input, montage continues linearly through all sections
```

Section links define the default next section (linear by default, but can be overridden):

```ini
[section name=WindUp]
start = 0.0
end   = 0.3
next  = Slash1

[section name=Slash1]
start = 0.3
end   = 0.8
next  = Slash2    ; default next
; game code can call montage_jump_to_section("Recover") to skip Slash2

[section name=Slash2]
start = 0.8
end   = 1.4
next  = Recover

[section name=Recover]
start = 1.4
end   = 2.0
next  = _end      ; _end = montage finishes
```

### 7.4 Montage Slots and Layering

Montages play on named **slots**, each associated with a blend mask:

| Slot | Blend Mask | Use Case |
|------|-----------|----------|
| `FullBody` | All bones (weight 1.0) | Dodge rolls, death animations |
| `UpperBody` | `spine_02` and above | Attacks while running |
| `RightArm` | `clavicle_r` subtree | One-handed weapon actions |
| `Face` | `head`, `jaw`, eye bones | Facial expressions |

Multiple montages can play simultaneously on different slots. When slots overlap (e.g., `UpperBody` and `RightArm` both affect `upperarm_r`), the higher-priority slot wins.

### 7.5 Montage Notify Types

| Notify Type | Visual in Editor | Runtime Behavior |
|-------------|-----------------|------------------|
| **Point Notify** | Diamond marker at a single frame | Fires once when playback crosses the time |
| **Notify State** | Colored bar spanning a time range | Fires `on_begin` at start, ticks each frame, fires `on_end` at end |
| **Branching Point** | Triangle marker (yellow) | Like point notify but evaluated every frame (not just on crossing) for precise branching |

---

## 8. Inverse Kinematics Editor

The IK editor provides visual setup for IK chains and constraints, integrated into the 3D viewport with gizmo-style handles.

### 8.1 IK Types

#### 8.1.1 Two-Bone IK

Used for arms (reach/grab) and legs (foot placement).

```
          *  pole vector (knee direction hint)
         /
        /
  root *--------* mid (elbow/knee)
       |         \
       |          \
       |           * end (hand/foot)
       |           |
       |           v
       |        target (IK goal position)
```

Setup in the IK editor:
1. Select chain: root bone, mid bone, end bone
2. Place pole vector gizmo (draggable sphere in viewport)
3. Set weight parameter (0 = FK only, 1 = full IK)

```ini
[ik_chain name=left_leg_ik type=two_bone]
root_bone   = thigh_l
mid_bone    = calf_l
end_bone    = foot_l
pole_bone   = calf_l          ; used as initial pole direction
weight      = 1.0
```

#### 8.1.2 FABRIK Chain IK

Used for spines, tails, tentacles -- arbitrary-length bone chains.

```ini
[ik_chain name=spine_ik type=fabrik]
bones       = spine_01, spine_02, spine_03, neck, head
target_bone = head
max_iterations = 10
tolerance   = 0.001           ; meters
weight      = 0.5
```

#### 8.1.3 Look-At IK

Used for head/eye tracking.

```ini
[ik_chain name=head_look_at type=look_at]
bone        = head
forward_axis = z              ; which local axis points "forward"
up_axis      = y
clamp_angle  = 80.0           ; degrees, max rotation from rest pose
speed        = 5.0            ; degrees per second (smooth tracking)
weight       = 1.0
```

### 8.2 IK Rig File Format (`.uzik`)

```ini
[uzik version=1 name="HumanoidIK"]
skeleton = res://skeletons/humanoid.uzskel

[ik_chain name=left_leg_ik type=two_bone]
root_bone   = thigh_l
mid_bone    = calf_l
end_bone    = foot_l
pole_bone   = calf_l
weight      = 1.0

[ik_chain name=right_leg_ik type=two_bone]
root_bone   = thigh_r
mid_bone    = calf_r
end_bone    = foot_r
pole_bone   = calf_r
weight      = 1.0

[ik_chain name=head_look_at type=look_at]
bone        = head
forward_axis = z
up_axis      = y
clamp_angle  = 80.0
speed        = 5.0
weight       = 1.0

[ik_chain name=spine_ik type=fabrik]
bones       = spine_01, spine_02, spine_03
target_bone = spine_03
max_iterations = 10
tolerance   = 0.001
weight      = 0.0
```

### 8.3 IK Viewport Visualization

When the IK editor is active, the 3D viewport renders:

| Visual | Color | Description |
|--------|-------|-------------|
| Chain bones | Yellow highlights | Bones participating in the IK chain |
| End effector | Green sphere gizmo | Draggable target position |
| Pole vector | Blue sphere gizmo | Draggable pole hint direction |
| Clamp cone | Semi-transparent orange | Look-at constraint cone visualization |
| Weight slider | In-viewport overlay | Real-time weight adjustment (0 to 1) |

---

## 9. Root Motion

### 9.1 Root Motion Preview

The 3D viewport provides a dedicated root motion preview mode:

```
[Root Motion: ON]  [In-Place: OFF]  [Show Path: ON]  [Loop Seam: --|--]
```

| Mode | Behavior |
|------|----------|
| **Root Motion ON** | Character moves through world space as dictated by hip bone delta |
| **In-Place** | Character stays at origin, root motion delta displayed as path line on ground |
| **Show Path** | Draw the accumulated root motion path as a colored line on the ground grid |

### 9.2 Root Motion Extraction

Root motion is extracted from bone index 0 (root/hip) of the animation clip:

```cpp
RootMotionDelta extract_root_motion(
    const AnimationClip& clip, float prev_t, float cur_t,
    const Skeleton& sk)
{
    BoneTransform t0 = sample_bone(clip, 0, prev_t);
    BoneTransform t1 = sample_bone(clip, 0, cur_t);

    // Handle loop boundary
    if (cur_t < prev_t && clip.looping) {
        BoneTransform t_end   = sample_bone(clip, 0, clip.duration);
        BoneTransform t_start = sample_bone(clip, 0, 0.0f);
        return {
            .translation = (t_end.position - t0.position)
                         + (t1.position - t_start.position),
            .rotation    = (t_end.rotation * glm::inverse(t0.rotation))
                         * (t1.rotation * glm::inverse(t_start.rotation))
        };
    }

    return {
        .translation = t1.position - t0.position,
        .rotation    = t1.rotation * glm::inverse(t0.rotation)
    };
}
```

### 9.3 Loop Seam Tool

When a looping animation's last-frame root position does not match the first-frame position, a visible "pop" occurs at the loop boundary. The Loop Seam tool fixes this:

1. **Detect seam**: compute `delta = root_pos(duration) - root_pos(0.0)`
2. **Distribute correction**: spread `delta` linearly across the entire clip duration
3. **Preview**: show before/after loop playback in the viewport
4. **Apply**: modify the root bone channel keys to eliminate the seam

```
Loop Seam Analysis:
  Root position at t=0.0:    [0.000, 0.000, 0.000]
  Root position at t=2.0:    [0.012, 0.003, -0.005]
  Seam delta:                [0.012, 0.003, -0.005]
  Status: SEAM DETECTED (threshold: 0.001)

  [Fix Seam]  [Preview Fixed]  [Ignore]
```

---

## 10. File Formats

All animation editor file formats are plain-text INI-like formats, consistent with UzEngine's AI-friendly design philosophy.

### 10.1 `.uzanim` -- Animation Clip

```ini
[uzanim version=1 name="walk_cycle"]
duration   = 1.000
fps        = 30
looping    = true
root_motion = true
additive   = false

; Bone channels -- only animated bones are listed (sparse)
[channel bone=pelvis]
position = [
    { t=0.000, v=[0.0, 95.0, 0.0] },
    { t=0.500, v=[0.0, 94.5, 0.5] },
    { t=1.000, v=[0.0, 95.0, 0.0] }
]
rotation = [
    { t=0.000, v=[0.0, 0.0, 0.0, 1.0], interp=bezier, out_w=0.33, out_a=5.0 },
    { t=0.500, v=[0.02, 0.0, -0.01, 0.9998], interp=bezier, in_w=0.33, in_a=-3.0, out_w=0.33, out_a=3.0 },
    { t=1.000, v=[0.0, 0.0, 0.0, 1.0], interp=bezier, in_w=0.33, in_a=-5.0 }
]

[channel bone=thigh_l]
rotation = [
    { t=0.000, v=[0.38, 0.0, 0.0, 0.92], interp=bezier },
    { t=0.250, v=[-0.26, 0.0, 0.0, 0.97], interp=bezier },
    { t=0.500, v=[0.17, 0.0, 0.0, 0.98], interp=bezier },
    { t=0.750, v=[0.38, 0.02, 0.0, 0.92], interp=bezier },
    { t=1.000, v=[0.38, 0.0, 0.0, 0.92], interp=bezier }
]

[channel bone=thigh_r]
rotation = [
    { t=0.000, v=[-0.26, 0.0, 0.0, 0.97], interp=bezier },
    { t=0.250, v=[0.17, 0.0, 0.0, 0.98], interp=bezier },
    { t=0.500, v=[0.38, 0.0, 0.0, 0.92], interp=bezier },
    { t=0.750, v=[-0.26, -0.02, 0.0, 0.97], interp=bezier },
    { t=1.000, v=[-0.26, 0.0, 0.0, 0.97], interp=bezier }
]

; Morph target tracks (optional)
[morph name=smile]
keys = [
    { t=0.0, v=0.0 },
    { t=1.0, v=0.0 }
]

; Notify events
[notify]
events = [
    { t=0.150, event="footstep_left",  params={"surface":"any","volume":0.8} },
    { t=0.650, event="footstep_right", params={"surface":"any","volume":0.8} }
]

; Custom property tracks (for Lua script events)
[property name=attack_power]
keys = [
    { t=0.0, v=0.0 },
    { t=0.5, v=1.0 },
    { t=1.0, v=0.0 }
]
```

### 10.2 `.uzfsm` -- Animation State Machine

```ini
[uzfsm version=1 name="HumanoidLocomotion"]
default_state = idle

; Parameters
[param name=speed type=float default=0.0]
[param name=is_jumping type=bool default=false]
[param name=is_grounded type=bool default=true]

; States
[state name=idle]
clip       = res://anim/idle.uzanim
looping    = true
speed      = 1.0
blend_in   = 0.2

[state name=walk]
clip       = res://anim/walk.uzanim
looping    = true
speed      = 1.0
root_motion = true

[state name=run]
clip       = res://anim/run.uzanim
looping    = true
speed      = 1.0
root_motion = true

[state name=jump_up]
clip       = res://anim/jump.uzanim
looping    = false
on_end     = transition:jump_to_land

[state name=land]
clip       = res://anim/land.uzanim
looping    = false
on_end     = state:idle

; Blend tree state (references external .uzblend)
[state name=locomotion_blend]
blend_tree = res://anim/locomotion_blend.uzblend
looping    = true
root_motion = true

; Transitions
[transition name=idle_to_walk]
from       = idle
to         = walk
blend_time = 0.2
condition  = speed > 0.5

[transition name=walk_to_run]
from       = walk
to         = run
blend_time = 0.15
condition  = speed > 3.5

[transition name=run_to_walk]
from       = run
to         = walk
blend_time = 0.2
condition  = speed < 2.0

[transition name=walk_to_idle]
from       = walk
to         = idle
blend_time = 0.3
condition  = speed < 0.1

[transition name=any_to_jump]
from       = *
to         = jump_up
blend_time = 0.1
condition  = is_jumping == true
priority   = 10

[transition name=jump_to_land]
from       = jump_up
to         = land
blend_time = 0.1
condition  = clip_finished
```

### 10.3 `.uzblend` -- Blend Tree

```ini
[uzblend version=1 name="LocomotionBlend" type=blend_1d]
parameter = speed

[sample name=idle]
clip  = res://anim/idle.uzanim
value = 0.0

[sample name=walk]
clip  = res://anim/walk.uzanim
value = 1.5

[sample name=run]
clip  = res://anim/run.uzanim
value = 5.0
```

### 10.4 `.uzmontage` -- Animation Montage

```ini
[uzmontage version=1 name="SwordCombo"]
clip      = res://anim/sword_combo_full.uzanim
slot      = UpperBody
blend_in  = 0.1
blend_out = 0.2

; Sections
[section name=WindUp]
start = 0.0
end   = 0.3
next  = Slash1

[section name=Slash1]
start = 0.3
end   = 0.8
next  = Slash2

[section name=Slash2]
start = 0.8
end   = 1.4
next  = Recover

[section name=Recover]
start = 1.4
end   = 2.0
next  = _end

; Notify tracks
[notify_track name=Gameplay]
notifies = [
    { t=0.35, event="attack_start",  params={"damage":25,"type":"slash"} },
    { t=0.75, event="attack_end",    params={} },
    { t=0.85, event="attack_start",  params={"damage":35,"type":"slash"} },
    { t=1.35, event="attack_end",    params={} }
]

[notify_track name=Audio]
notifies = [
    { t=0.32, event="play_sound", params={"asset":"res://audio/whoosh_light.ogg","volume":0.8} },
    { t=0.78, event="play_sound", params={"asset":"res://audio/whoosh_heavy.ogg","volume":1.0} },
    { t=0.83, event="play_sound", params={"asset":"res://audio/whoosh_heavy.ogg","volume":1.0} }
]

[notify_track name=VFX]
notify_states = [
    { t_start=0.33, t_end=0.76, event="sword_trail", params={"color":[1.0,0.8,0.2],"width":0.05} }
]
```

---

## 11. Retargeting

Animation retargeting allows clips authored for one skeleton to be played on a different skeleton with different proportions.

### 11.1 Retarget Map File (`.uzretarget`)

```ini
[uzretarget version=1 name="MixamoToUzHumanoid"]
source_skeleton = res://skeletons/mixamo_humanoid.uzskel
target_skeleton = res://skeletons/uz_humanoid.uzskel

; Per-bone mapping and retarget mode
; Modes:
;   animation  -- use the source animation rotation directly (relative to bind pose)
;   skeleton   -- use the target skeleton's bind pose (ignore source animation)
;   reference  -- blend between source animation and target reference pose

[bone_map]
; source_bone = target_bone, mode
mixamorig:Hips       = pelvis,     animation
mixamorig:Spine      = spine_01,   animation
mixamorig:Spine1     = spine_02,   animation
mixamorig:Spine2     = spine_03,   animation
mixamorig:Neck       = neck,       animation
mixamorig:Head       = head,       animation
mixamorig:LeftShoulder  = clavicle_l,  animation
mixamorig:LeftArm       = upperarm_l,  animation
mixamorig:LeftForeArm   = lowerarm_l,  animation
mixamorig:LeftHand      = hand_l,      animation
mixamorig:RightShoulder = clavicle_r,  animation
mixamorig:RightArm      = upperarm_r,  animation
mixamorig:RightForeArm  = lowerarm_r,  animation
mixamorig:RightHand     = hand_r,      animation
mixamorig:LeftUpLeg     = thigh_l,     animation
mixamorig:LeftLeg       = calf_l,      animation
mixamorig:LeftFoot      = foot_l,      animation
mixamorig:RightUpLeg    = thigh_r,     animation
mixamorig:RightLeg      = calf_r,      animation
mixamorig:RightFoot     = foot_r,      animation
; Bones not listed are ignored (not retargeted)
```

### 11.2 Retarget Modes

| Mode | Translation | Rotation | Use Case |
|------|------------|----------|----------|
| **Animation** | Scale by bone length ratio | Copy source rotation relative to bind pose | Most bones: arms, legs, spine |
| **Skeleton** | Use target bind pose | Use target bind pose | Bones that should not animate (e.g., twist bones) |
| **Reference** | Lerp(source, target, weight) | Lerp(source, target, weight) | Partial retarget for fine-tuning |

### 11.3 Retarget Runtime

```cpp
struct RetargetContext {
    const Skeleton*   source_skeleton;
    const Skeleton*   target_skeleton;
    const RetargetMap* bone_map;

    void retarget_pose(const Pose& source_pose, Pose& target_pose) const
    {
        for (auto& mapping : bone_map->entries) {
            int src_idx = source_skeleton->find_bone(mapping.source_bone);
            int tgt_idx = target_skeleton->find_bone(mapping.target_bone);
            if (src_idx < 0 || tgt_idx < 0) continue;

            const auto& src_bind = source_skeleton->bones[src_idx];
            const auto& tgt_bind = target_skeleton->bones[tgt_idx];

            switch (mapping.mode) {
            case RetargetMode::Animation: {
                // Rotation: source animation relative to source bind, applied to target bind
                glm::quat src_local = source_pose.bones[src_idx].rotation;
                glm::quat delta = src_local * glm::inverse(src_bind.bind_rotation);
                target_pose.bones[tgt_idx].rotation = delta * tgt_bind.bind_rotation;

                // Translation: scale by bone length ratio
                float ratio = bone_length(target_skeleton, tgt_idx)
                            / bone_length(source_skeleton, src_idx);
                glm::vec3 src_delta = source_pose.bones[src_idx].position
                                    - src_bind.bind_translation;
                target_pose.bones[tgt_idx].position =
                    tgt_bind.bind_translation + src_delta * ratio;
                break;
            }
            case RetargetMode::Skeleton:
                target_pose.bones[tgt_idx].rotation  = tgt_bind.bind_rotation;
                target_pose.bones[tgt_idx].position  = tgt_bind.bind_translation;
                break;
            case RetargetMode::Reference:
                // Blend handled by weight parameter
                break;
            }
        }
    }
};
```

### 11.4 Retarget Editor UI

The retarget editor is a split-panel WebviewPanel:

```
+-----------------------------------+-----------------------------------+
|       SOURCE SKELETON             |        TARGET SKELETON            |
|       (left viewport)             |        (right viewport)           |
|                                   |                                   |
|    Mixamo character in T-pose     |    UzEngine character in T-pose   |
|    with bone names labeled        |    with bone names labeled        |
|                                   |                                   |
|    Selected bones highlighted     |    Mapped bones highlighted       |
|    in yellow                      |    in green                       |
+-----------------------------------+-----------------------------------+
|                    BONE MAPPING TABLE                                 |
|                                                                       |
|  Source Bone             | Target Bone          | Mode      | Status  |
|  -----------------------|---------------------|-----------|---------|
|  mixamorig:Hips          | pelvis               | Animation | OK      |
|  mixamorig:Spine         | spine_01             | Animation | OK      |
|  mixamorig:Spine1        | spine_02             | Animation | OK      |
|  mixamorig:LeftShoulder  | clavicle_l           | Animation | OK      |
|  mixamorig:LeftArm       | upperarm_l           | Animation | OK      |
|  (unmapped)              | twist_upperarm_l     | Skeleton  | --      |
|  ...                                                                  |
|                                                                       |
|  [Auto-Map (by name)]  [Auto-Map (by hierarchy)]  [Clear All]       |
+-----------------------------------------------------------------------+
|                    PREVIEW                                            |
|  [Play Source]  [Play Retargeted]  [Side-by-Side]                    |
+-----------------------------------------------------------------------+
```

Auto-mapping strategies:

| Strategy | Method |
|----------|--------|
| **By Name** | Fuzzy-match bone names (strip prefixes like `mixamorig:`, match substrings) |
| **By Hierarchy** | Match bones by depth and child index in the skeleton tree |

---

## 12. Implementation Notes

### 12.1 C++ Class Hierarchy

```
src/engine/animation/
  animation_clip.h/cpp          -- AnimationClip, BoneChannel, AnimKeyVec3/Quat
  animation_pose.h/cpp          -- Pose, BoneTransform, blend_poses()
  animation_state_machine.h/cpp -- AnimStateMachine, AnimState, AnimTransition
  animation_blend_tree.h/cpp    -- BlendTree, BlendNode1D, BlendNode2D, BlendNodeAdditive
  animation_montage.h/cpp       -- AnimMontage, MontageSection, MontageSlot
  animation_ik.h/cpp            -- IKChain, TwoBoneIK, FABRIK, LookAtIK
  animation_retarget.h/cpp      -- RetargetMap, RetargetContext
  animation_notify.h/cpp        -- AnimNotify, NotifyState, notify dispatch
  root_motion.h/cpp             -- RootMotionDelta, extract_root_motion()

src/engine/animation/editor/
  animation_editor.h/cpp        -- AnimationEditor class (coordinates all sub-editors)
  clip_editor.h/cpp             -- ClipEditor: timeline, keyframe, curve editing state
  fsm_editor.h/cpp              -- FSMEditor: node graph layout, transition editing
  blend_tree_editor.h/cpp       -- BlendTreeEditor: node graph for blend nodes
  montage_editor.h/cpp          -- MontageEditor: section/notify timeline
  ik_editor.h/cpp               -- IKEditor: chain setup, gizmo integration
  retarget_editor.h/cpp         -- RetargetEditor: bone mapping UI state

src/engine/animation/serialization/
  uzanim_serializer.h/cpp       -- .uzanim text read/write
  uzfsm_serializer.h/cpp        -- .uzfsm text read/write
  uzblend_serializer.h/cpp      -- .uzblend text read/write
  uzmontage_serializer.h/cpp    -- .uzmontage text read/write
  uzik_serializer.h/cpp         -- .uzik text read/write
  uzretarget_serializer.h/cpp   -- .uzretarget text read/write
```

### 12.2 Key C++ Classes

```cpp
// animation_editor.h
class AnimationEditor {
public:
    void open_clip(const std::string& path);
    void open_fsm(const std::string& path);
    void open_blend_tree(const std::string& path);
    void open_montage(const std::string& path);

    // Playback
    void play();
    void pause();
    void stop();
    void set_time(float time);
    void step_forward();
    void step_backward();
    void set_loop_mode(LoopMode mode);
    void set_playback_speed(float speed);

    // Keyframe editing
    void insert_keyframe(int bone_index, KeyChannel channel, float time);
    void delete_keyframes(const std::vector<KeyframeId>& keys);
    void move_keyframes(const std::vector<KeyframeId>& keys, float time_delta);
    void set_interpolation(const std::vector<KeyframeId>& keys, InterpMode mode);

    // State
    float               current_time() const;
    const AnimationClip* current_clip() const;
    const Skeleton*     skeleton() const;
    Pose                current_pose() const;

private:
    std::unique_ptr<ClipEditor>       m_clip_editor;
    std::unique_ptr<FSMEditor>        m_fsm_editor;
    std::unique_ptr<BlendTreeEditor>  m_blend_editor;
    std::unique_ptr<MontageEditor>    m_montage_editor;
    std::unique_ptr<IKEditor>         m_ik_editor;
    std::unique_ptr<RetargetEditor>   m_retarget_editor;

    float       m_current_time = 0.0f;
    float       m_playback_speed = 1.0f;
    LoopMode    m_loop_mode = LoopMode::Loop;
    bool        m_playing = false;
    bool        m_recording = false;
};

// animation_state_machine.h (extended for editor)
struct AnimState {
    std::string  name;
    std::string  clip_path;         // res:// path
    bool         looping = true;
    float        speed = 1.0f;
    float        blend_in = 0.2f;
    std::string  on_end;            // "hold", "transition:name", "state:name"
    std::string  blend_tree_path;   // optional, overrides clip_path
    bool         root_motion = false;

    // Editor-only layout
    glm::vec2    editor_position = {0, 0};  // node position in graph
};

struct AnimTransition {
    std::string  name;
    std::string  from;              // "*" = any state
    std::string  to;
    float        blend_time = 0.2f;
    std::string  condition;         // condition expression
    int          priority = 0;
    bool         can_interrupt = true;
};

struct AnimParam {
    std::string  name;
    enum Type { Float, Bool } type;
    union { float f; bool b; } default_value;
};
```

### 12.3 DX12 Render Pass: Skeleton Debug Draw

The animation editor adds a skeleton overlay pass to the DX12 render pipeline:

```cpp
// Rendered after the main scene pass, before JPEG capture
// Editor-only: compiled with #ifdef UZ_EDITOR

struct SkeletonDebugPass {
    // Draws bone axes as colored lines (R=X, G=Y, B=Z)
    // Draws joint positions as small spheres
    // Draws bone connections as tapered lines (thick at parent, thin at child)
    // Highlights selected bone with yellow overlay

    void render(
        ID3D12GraphicsCommandList* cmd,
        const Skeleton& skeleton,
        const Pose& current_pose,
        const std::vector<glm::mat4>& bone_world_matrices,
        int selected_bone_index,        // -1 = none
        const SkeletonDebugOptions& opts
    );
};

struct SkeletonDebugOptions {
    bool show_bone_axes   = true;    // RGB axis lines per joint
    bool show_joint_spheres = true;  // small spheres at joint positions
    bool show_bone_names  = false;   // text labels (expensive, debug only)
    bool show_ik_targets  = true;    // IK gizmo handles
    float bone_line_width = 2.0f;
    float joint_sphere_radius = 0.01f;
};
```

### 12.4 VSCode Extension: WebView Panel Architecture

The animation editor WebviewPanel uses a React-based UI (same as the Inspector panel) communicating with the engine via IPC.

```
VSCode Extension (TypeScript)
  |
  +-- AnimationEditorProvider (WebviewPanel)
  |     |
  |     +-- TimelineView (React component)
  |     |     +-- TrackLane[]
  |     |     +-- KeyframeDiamond[]
  |     |     +-- Scrubber
  |     |     +-- TimeRuler
  |     |
  |     +-- CurveEditorView (React component)
  |     |     +-- BezierCurveRenderer (Canvas2D)
  |     |     +-- TangentHandles[]
  |     |
  |     +-- FSMGraphView (React component)
  |     |     +-- StateNode[]
  |     |     +-- TransitionArrow[]
  |     |     +-- ParameterPanel
  |     |
  |     +-- MontageView (React component)
  |           +-- SectionBar[]
  |           +-- NotifyTrack[]
  |           +-- NotifyMarker[]
  |
  +-- BoneTreeProvider (TreeDataProvider)
  |     +-- BoneTreeItem[]
  |     +-- TrackVisibilityToggle[]
  |
  +-- AnimPropertiesProvider (WebviewPanel)
        +-- KeyframeInspector
        +-- StateInspector
        +-- TransitionInspector
```

### 12.5 IPC Handlers (Animation Editor)

| IPC Call | Direction | Description |
|----------|-----------|-------------|
| `anim.openClip` | Ext -> Engine | Load `.uzanim` clip for editing |
| `anim.openFSM` | Ext -> Engine | Load `.uzfsm` state machine for editing |
| `anim.openBlendTree` | Ext -> Engine | Load `.uzblend` for editing |
| `anim.openMontage` | Ext -> Engine | Load `.uzmontage` for editing |
| `anim.setTime` | Ext -> Engine | Set playback scrubber position |
| `anim.play` | Ext -> Engine | Start playback |
| `anim.pause` | Ext -> Engine | Pause playback |
| `anim.stop` | Ext -> Engine | Stop and reset to frame 0 |
| `anim.getClipData` | Ext -> Engine | Fetch full clip data (tracks, keys, notifies) |
| `anim.insertKey` | Ext -> Engine | Insert keyframe at time for bone/channel |
| `anim.deleteKeys` | Ext -> Engine | Delete keyframes by ID list |
| `anim.moveKeys` | Ext -> Engine | Move keyframes in time/value |
| `anim.setKeyInterp` | Ext -> Engine | Set interpolation mode for keys |
| `anim.setKeyTangent` | Ext -> Engine | Set tangent weight/angle for a key |
| `anim.saveClip` | Ext -> Engine | Save modified clip to `.uzanim` |
| `anim.getFSMData` | Ext -> Engine | Fetch state machine graph data |
| `anim.setFSMParam` | Ext -> Engine | Set parameter value for live preview |
| `anim.saveFSM` | Ext -> Engine | Save state machine to `.uzfsm` |
| `anim.getBlendData` | Ext -> Engine | Fetch blend tree data |
| `anim.saveBlend` | Ext -> Engine | Save blend tree to `.uzblend` |
| `anim.getMontageData` | Ext -> Engine | Fetch montage data |
| `anim.saveMontage` | Ext -> Engine | Save montage to `.uzmontage` |
| `anim.setBonePose` | Ext -> Engine | Set a bone's local transform (for record mode / FK posing) |
| `anim.getSkeleton` | Ext -> Engine | Fetch skeleton bone hierarchy |
| `anim.selectBone` | Ext -> Engine | Highlight bone in viewport |
| `anim.onTimeChanged` | Engine -> Ext | Notify extension of playback time update |
| `anim.onPoseChanged` | Engine -> Ext | Notify extension when pose changes (for inspector update) |
| `anim.onNotifyFired` | Engine -> Ext | Notify extension when an AnimNotify fires during preview |

### 12.6 State Machine Compilation

The `.uzfsm` text format is parsed at load time into the `AnimStateMachine` runtime struct. The compilation step:

1. Parse all `[state]` sections into `AnimState` structs
2. Parse all `[transition]` sections into `AnimTransition` structs
3. Parse all `[param]` sections into `AnimParam` structs
4. Validate: all `from`/`to` references resolve to existing state names
5. Validate: all condition expressions reference declared parameters
6. Sort transitions by priority (descending) for evaluation order
7. Pre-load referenced `.uzanim` clips via ResourceManager
8. Bind Lua parameter setters: `ctx.anim:set_float(entity, "speed", 3.0)` maps to `AnimStateMachine::set_float("speed", 3.0)`

Compilation errors are reported as VSCode diagnostics on the `.uzfsm` file (red squiggles with error messages).

### 12.7 Performance Constraints

| Constraint | Value |
|-----------|-------|
| Max bones per skeleton | 256 (uint8_t bone indices) |
| Max keyframes per channel | 65536 |
| Max morph targets per mesh | 64 |
| Max notify tracks per montage | 8 |
| Max notifies per track | 256 |
| Max states per FSM | 128 |
| Max transitions per FSM | 512 |
| Max blend samples per blend space | 32 |
| Skeleton debug draw max verts | 16384 |
| Curve editor max visible keys | 4096 (virtualized beyond) |
| Timeline IPC update rate | 60 Hz max (throttled to frame rate) |
| Retarget computation budget | < 0.5ms per character per frame |

---

## 13. Lua API Integration

The animation editor's runtime systems are exposed to Lua scripts via `game.dll`:

```lua
-- Animation State Machine
ctx.anim:set_float(entity, "speed", 5.0)
ctx.anim:set_bool(entity, "is_jumping", true)
ctx.anim:get_current_state(entity)            --> "run"
ctx.anim:get_float(entity, "speed")           --> 5.0
ctx.anim:get_state_time(entity)               --> 0.35 (seconds into current state)

-- Montage
ctx.anim:play_montage(entity, "res://anim/sword_combo.uzmontage")
ctx.anim:stop_montage(entity)
ctx.anim:montage_jump_to_section(entity, "Slash2")
ctx.anim:is_montage_playing(entity)           --> true/false
ctx.anim:get_montage_section(entity)          --> "Slash1"

-- Direct clip playback (bypasses state machine)
ctx.anim:play_clip(entity, "res://anim/dance.uzanim", { loop = true, speed = 1.0 })
ctx.anim:stop_clip(entity)

-- Morph targets
ctx.anim:set_morph(entity, "smile", 0.8)
ctx.anim:get_morph(entity, "smile")           --> 0.8

-- IK
ctx.anim:set_ik_target(entity, "left_leg_ik", target_world_pos)
ctx.anim:set_ik_weight(entity, "left_leg_ik", 1.0)
ctx.anim:clear_ik_target(entity, "left_leg_ik")

-- Root motion
ctx.anim:enable_root_motion(entity, true)
ctx.anim:get_root_motion_delta(entity)        --> { translation = vec3, rotation = quat }

-- Notify listener (via EventBus)
ctx.events:on("attack_start", function(params)
    -- params.damage, params.type
    local damage = params.damage or 10
    -- apply damage logic
end)
```

---

## 14. Undo/Redo Integration

All animation editor operations integrate with the UndoStack (see `docs/specs/undo-redo.md`):

| Command | Undo Data | Redo Data |
|---------|-----------|-----------|
| `InsertKeyframeCommand` | Key ID, track info | Key data + position |
| `DeleteKeyframesCommand` | Full key data for each deleted key | Key ID list |
| `MoveKeyframesCommand` | Original time/value per key | New time/value per key |
| `SetInterpolationCommand` | Original interp mode per key | New interp mode |
| `SetTangentCommand` | Original tangent data | New tangent data |
| `AddStateCommand` | -- | State data |
| `DeleteStateCommand` | Full state + connected transitions | State name |
| `AddTransitionCommand` | -- | Transition data |
| `DeleteTransitionCommand` | Full transition data | Transition name |
| `ModifyStatePropertyCommand` | Original value | New value |
| `ModifyTransitionPropertyCommand` | Original value | New value |
| `AddSectionCommand` | -- | Section data |
| `DeleteSectionCommand` | Full section data | Section name |
| `AddNotifyCommand` | -- | Notify data |
| `DeleteNotifyCommand` | Full notify data | Notify ID |

All commands follow the `EditorCommand` pattern from the undo-redo spec, with `execute()`, `undo()`, and `description()` methods.

---

## 15. Comparison with Industry Tools

| Feature | Unreal Engine 5 | Unity | Blender | UzEngine |
|---------|----------------|-------|---------|----------|
| Timeline editor | Sequencer + AnimGraph | Animation Window | Dope Sheet + Graph Editor | Timeline Panel + Curve Editor |
| State machine | AnimGraph (Blueprint) | Animator Controller | -- | `.uzfsm` node graph |
| Blend trees | BlendSpace 1D/2D | Blend Tree | -- | `.uzblend` node graph |
| Montages | AnimMontage | -- (custom) | NLA Editor | `.uzmontage` sections + notifies |
| IK | Control Rig + CCDIK/FABRIK | Animation Rigging | Armature constraints | `.uzik` two-bone / FABRIK / look-at |
| Retargeting | IK Retargeter | Humanoid Avatar | -- | `.uzretarget` bone mapping |
| File format | Binary (uasset) | Binary (anim) | Binary (blend) | Plain-text INI (AI-friendly) |
| AI integration | None | None | None | All formats LLM-readable/writable, MCP tools |
| Scripting | C++ / Blueprint | C# | Python | Lua (game.dll) |
