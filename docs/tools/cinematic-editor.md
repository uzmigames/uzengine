# Cinematic Sequencer — Tool Specification

> **Status**: Planned (Phase 4+)
> **Owner**: `game.dll` (CinematicPlayer runtime) + VSCode extension (Sequencer WebView panel)
> **File format**: `.uzcinematic` (INI-like, AI-friendly)
> **Dependencies**: Animation System, Camera System, Audio System, EventBus, UI System, Scene System

---

## 1. Overview & Goals

The Cinematic Sequencer is a non-linear, timeline-based authoring tool for creating in-engine cutscenes, dialogue sequences, camera flybys, and game trailers. It integrates deeply with every runtime subsystem: cameras, skeletal animation, audio, post-processing, UI overlays, and Lua scripting.

### Design Principles

| Principle | Description |
|-----------|-------------|
| **Non-destructive** | Cinematics override scene state temporarily; the scene returns to its original state when playback ends |
| **Track-based** | Every animatable property lives on a named track with keyframes; no hidden state |
| **AI-friendly** | `.uzcinematic` files are plain-text INI that AI agents can generate, read, and modify |
| **In-engine playback** | Cinematics play in the game runtime without external tools or video files |
| **Composable** | Master sequences reference sub-sequences (shots); shots can be reordered non-destructively |

### Target Users

| Persona | Workflow |
|---------|----------|
| **Cinematographer** | Camera placement, lens selection, shot composition, camera rails |
| **Narrative designer** | Dialogue timing, subtitle placement, event triggers for branching |
| **Game director** | Shot ordering, pacing review, final render output |
| **AI agent** | Generate cutscenes from natural language via MCP (`cinematic.generate`) |

### Output

- `.uzcinematic` files stored under `res://cinematics/`
- Plays in-engine via `CinematicPlayer` (C++) or `ctx.cinematic:play()` (Lua)
- Exportable as PNG/JPEG frame sequences for offline editing

---

## 2. Editor Layout

The cinematic editor occupies the full VSCode panel area as two stacked WebView panels.

### ASCII Mockup — Full Editor

```
+===========================================================================+
|  [File] [Edit] [View] [Playback]                    [Pilot Cam] [Record] |
+===========================================================================+
|                                                                           |
|                         3D VIEWPORT  (60%)                                |
|                                                                           |
|   +---------------------------------------------------------------+      |
|   |                                                               |      |
|   |    +-------------------------------------------------+        |      |
|   |    |                                                 |        |      |
|   |    |         SAFE FRAME (16:9 letterbox)             |        |      |
|   |    |                                                 |        |      |
|   |    |    [Cam1]  frustum wireframe                    |        |      |
|   |    |            \                                    |        |      |
|   |    |             \   Actor: "Hero"                   |        |      |
|   |    |              \_/                                |        |      |
|   |    |                                                 |        |      |
|   |    |    [Cam2] frustum wireframe (inactive, gray)    |        |      |
|   |    |                                                 |        |      |
|   |    +-------------------------------------------------+        |      |
|   |                                                               |      |
|   +---------------------------------------------------------------+      |
|                                                                           |
+===========================================================================+
|                       SEQUENCER TIMELINE  (40%)                           |
|                                                                           |
|  TRACKS (left)            | KEYFRAME AREA (right)             ZOOM [+][-]|
|  +-----------------------++-----------------------------------------+     |
|  | > Camera Cut          || [Cam1      ][  Cam2  ][ Cam1           ]|     |
|  |   > Cam1 (pos/rot)   || --o----o--------o-----------o-----------o|     |
|  |     . FOV             || ----o--------------o----------------------|     |
|  |     . DOF Focus       || -------o--------------------------------------|     |
|  |   > Cam2 (pos/rot)   || ----------------o--------o------------------|     |
|  | > Hero (actor)        ||                                          |     |
|  |   . Transform         || --o---------o-----------o---------------|     |
|  |   . Animation         || [idle   ][ walk_to_mark  ][ dialogue  ]|     |
|  |   . Visibility        || =========================================|     |
|  | > Villain (actor)     ||                                          |     |
|  |   . Transform         || --------o-----------o-------------------|     |
|  |   . Animation         || [villain_entrance   ][ menace_pose    ]|     |
|  | > Audio: Music        || [====== boss_intro.ogg ================]|     |
|  | > Audio: SFX          || ........[boom].....[footstep]...........|     |
|  | > Events              || ........X..........X....................|     |
|  | > Fade                || [##BLK>]..............................[<BLK##]|     |
|  | > Subtitles           || ...["Hero: ..."]["Villain: ..."]........|     |
|  | > Post-Process        ||                                          |     |
|  |   . DOF Strength      || --o---------o-----------o---------------|     |
|  |   . Vignette          || ----o--------------------------------------|     |
|  +-----------------------++-----------------------------------------+     |
|                                                                           |
|  [|<] [<] [>] [||] [>|]  0:00.00 / 0:10.00   [1x]   In:[0:00] Out:[0:10]|
+===========================================================================+
```

### 3D Viewport (Top 60%)

The viewport shows the cinematic preview from the perspective of the currently active camera in the Camera Cut Track.

| Feature | Description |
|---------|-------------|
| **Safe frame overlay** | 16:9 letterbox lines (configurable: 2.39:1, 1.85:1, 16:9, 4:3) drawn as semi-transparent black bars |
| **Pilot camera mode** | Possess a camera track's camera — WASD + mouse moves the camera, keyframes record position |
| **Director's view** | Toggle to show ALL camera frustums in the scene as colored wireframes (active = green, inactive = gray) |
| **Actor highlight** | Selected actor track highlights the corresponding entity with a yellow outline |
| **Playhead sync** | Viewport updates in real-time as the playhead scrubs across the timeline |
| **Grid overlay** | Rule-of-thirds grid overlay (toggle) |
| **Focal point indicator** | When DOF is active, shows the focus distance as a plane in the viewport |

### Sequencer Timeline (Bottom 40%)

| Element | Behavior |
|---------|----------|
| **Track list** (left column, ~200px) | Hierarchical track names; expand/collapse with `>` arrows; drag to reorder |
| **Keyframe area** (right) | Horizontal timeline; keyframe diamonds, clip rectangles, curve segments |
| **Playhead** | Red vertical line; click to place, drag to scrub; snaps to keyframes when Shift held |
| **In/Out markers** | Green (In) and red (Out) vertical markers; define render range and loop range |
| **Time ruler** | Top edge shows time in `M:SS.FF` (minutes:seconds.frames); tick marks at frame intervals |
| **Zoom** | Mouse scroll on timeline zooms horizontally; `Ctrl+Scroll` zooms vertically (curve height); `F` fits all content |

---

## 3. Track Types

### 3.1 Camera Cut Track

The topmost track. Contains a sequence of **camera segments** — each segment references a Camera Track by name. The active segment at the current playhead time determines which camera renders the viewport.

```
Camera Cut Track:
|[  Cam1: 0.0-3.5s  ][  Cam2: 3.5-6.0s  ][  Cam1: 6.0-10.0s  ]|
```

| Property | Type | Description |
|----------|------|-------------|
| `camera` | `string` | Name of the Camera Track to activate |
| `start` | `float` | Start time in seconds |
| `end` | `float` | End time in seconds |
| `blend_frames` | `int` | Number of frames to cross-dissolve between cuts (0 = hard cut) |

Only one Camera Cut Track per sequence. Camera segments must not overlap (validated on save).

### 3.2 Camera Track

Per-camera animation curves for position, rotation, and lens properties.

| Sub-track | Value Type | Interpolation |
|-----------|-----------|---------------|
| `position` | `Vec3` | Catmull-Rom spline (smooth) or linear |
| `rotation` | `Quat` | SLERP between keyframes |
| `fov` | `float` | Linear interpolation |
| `dof_focus_dist` | `float` | Linear interpolation |
| `dof_focus_range` | `float` | Linear interpolation |
| `dof_far_blur` | `float` | Linear interpolation |
| `near_clip` | `float` | Step (no interpolation) |
| `far_clip` | `float` | Step (no interpolation) |

Camera tracks can optionally reference a **Camera Rail** (spline path) — see Section 4.

### 3.3 Actor Track

Per-entity track group. Each actor track is identified by the node name (resolved via `ctx.scene:find_node(name)` at playback start).

| Sub-track | Value Type | Description |
|-----------|-----------|-------------|
| `transform` | `Vec3 + Quat + Vec3` | Position, rotation, scale keyframes |
| `visibility` | `bool` | Show/hide entity at specific times |
| `material_param` | `float` | Animate material parameters (e.g., emissive intensity, opacity) |

### 3.4 Animation Track

Assigns animation clips to a skinned mesh entity. Clips are displayed as horizontal rectangles on the timeline.

```
Animation Track (Hero):
|[  idle: 0.0-1.5s  ][  walk_to_mark: 1.5-4.0s  ][  dialogue: 4.0-10.0s  ]|
```

| Property | Type | Description |
|----------|------|-------------|
| `clip` | `string` | `res://` path to `.uzanim` clip |
| `start` | `float` | Start time in sequence |
| `end` | `float` | End time in sequence |
| `clip_offset` | `float` | Offset into the clip (for trimming the start) |
| `blend_in` | `float` | Cross-fade duration from previous clip (seconds) |
| `blend_out` | `float` | Cross-fade duration into next clip (seconds) |
| `playback_rate` | `float` | Speed multiplier (1.0 = normal, 0.5 = half-speed) |
| `weight` | `float` | Blend weight 0.0-1.0 (for layered animation) |

Animation clips override the entity's normal AnimStateMachine during cinematic playback.

### 3.5 Audio Track

Audio clips displayed as waveform rectangles. Multiple audio tracks allowed (one per bus recommended).

```
Audio Track (Music):
|[====== boss_intro.ogg: 0.0-10.0s ======]|

Audio Track (SFX):
|........[boom: 2.0s].....[footstep: 5.5s].........|
```

| Property | Type | Description |
|----------|------|-------------|
| `clip` | `string` | `res://` path to audio asset |
| `start` | `float` | Start time in sequence |
| `clip_offset` | `float` | Offset into audio clip |
| `duration` | `float` | Playback duration (clamped to clip length) |
| `bus` | `string` | Audio bus: `Master`, `SFX`, `Music`, `Voice` |
| `volume` | `float[]` | Volume keyframes (0.0-1.0) |
| `pitch` | `float[]` | Pitch keyframes (0.5-2.0) |
| `fade_in` | `float` | Fade-in duration from silence (seconds) |
| `fade_out` | `float` | Fade-out duration to silence (seconds) |
| `spatial` | `bool` | If true, positioned at the parent actor's location |

### 3.6 Event Track

Fires Lua callbacks or EventBus events at specific frames. Events appear as `X` markers on the timeline.

```
Event Track:
|........X..........X....................|
         ^          ^
    "door_open"  "spawn_particles"
```

| Property | Type | Description |
|----------|------|-------------|
| `time` | `float` | Trigger time in seconds |
| `event_name` | `string` | Event name (dispatched via EventBus string layer) |
| `params` | `string` | JSON payload passed to handlers |

Events fire exactly once when the playhead crosses the event time. On scrub (non-play), events do not fire. On reverse playback, events fire in reverse order.

### 3.7 Fade Track

Screen overlay for fade-to-black, fade-from-white, and custom color fades. Rendered as a full-screen quad in the UIPass.

```
Fade Track:
|[##BLK>  0.0-1.0s  ]...........................[  9.0-10.0s  <BLK##]|
  fade-in from black                              fade-out to black
```

| Property | Type | Description |
|----------|------|-------------|
| `start` | `float` | Fade start time |
| `end` | `float` | Fade end time |
| `color` | `Vec3` | Fade color RGB (0,0,0 = black; 1,1,1 = white) |
| `alpha_curve` | `float[]` | Alpha keyframes from 0.0 (transparent) to 1.0 (opaque) |

### 3.8 Subtitle Track

Timed text display for dialogue, narration, or on-screen information. Rendered via the UI system (`uz_ui_html.dll`).

```
Subtitle Track:
|...["Hero: We need to move."]["Villain: You're too late."]........|
```

| Property | Type | Description |
|----------|------|-------------|
| `start` | `float` | Display start time |
| `end` | `float` | Display end time |
| `text` | `string` | Subtitle text (supports `LOC:key` for localization) |
| `speaker` | `string` | Speaker name (displayed above subtitle) |
| `position` | `string` | `bottom`, `top`, `center` (default: `bottom`) |
| `style` | `string` | CSS class name in `res://ui/subtitles.html` |

Localization: when `text` starts with `LOC:`, the LocaleManager resolves the key at runtime:
```ini
text = "LOC:cinematic_intro_line_01"
; Resolved via locale CSV: "We need to move." (en) / "Precisamos ir." (pt-BR)
```

### 3.9 Post-Process Track

Animate post-processing parameters over the cinematic duration.

| Sub-track | Value Type | Range | Linked System |
|-----------|-----------|-------|---------------|
| `dof_strength` | `float` | 0.0-1.0 | Camera DOF |
| `dof_focus_dist` | `float` | 0.0-10000.0 | Camera DOF |
| `vignette_intensity` | `float` | 0.0-1.0 | Screen-Space Effects |
| `vignette_smoothness` | `float` | 0.0-1.0 | Screen-Space Effects |
| `color_temperature` | `float` | 1000-15000 K | Color Grading |
| `exposure_bias` | `float` | -5.0 to 5.0 EV | Tone Mapping |
| `saturation` | `float` | 0.0-2.0 | Color Grading |
| `film_grain` | `float` | 0.0-1.0 | Cinematic Camera |
| `chromatic_aberration` | `float` | 0.0-1.0 | Cinematic Camera |
| `lens_flare_intensity` | `float` | 0.0-1.0 | Cinematic Camera |
| `bloom_intensity` | `float` | 0.0-5.0 | HDR Pipeline |
| `ssao_intensity` | `float` | 0.0-2.0 | HBAO+ |

All sub-tracks use linear interpolation between keyframes. Curves are evaluated per-frame and applied before tone mapping.

### Track Type Summary

| Track Type | Max Per Sequence | Keyframe Type | Icon |
|------------|-----------------|---------------|------|
| Camera Cut | 1 | Segment (start/end) | filmstrip |
| Camera | Unlimited | Vec3/Quat/float | camera |
| Actor | 1 per entity | Vec3/Quat/float/bool | person |
| Animation | 1 per entity per layer | Clip segment | running |
| Audio | Unlimited | Clip + float curves | speaker |
| Event | Unlimited | Point trigger | lightning |
| Fade | 1 | Segment + alpha curve | square |
| Subtitle | 1 | Text segment | text |
| Post-Process | 1 | Float curves | sliders |

---

## 4. Camera System for Cinematics

### 4.1 Camera Rail (Spline Path)

A camera rail defines a smooth Bezier spline path through 3D space. The camera follows this path, parameterized by a normalized `t` value (0.0 = start, 1.0 = end).

```cpp
struct CameraRailSpline {
    struct ControlPoint {
        Vec3  position;       // World-space position
        Vec3  tangent_in;     // Incoming tangent (cubic Bezier)
        Vec3  tangent_out;    // Outgoing tangent (cubic Bezier)
        float ease;           // 0.0 = linear, 1.0 = ease-in-out
    };

    std::vector<ControlPoint> points;

    // Evaluate position at parameter t [0, 1]
    Vec3 evaluate_position(float t) const;

    // Evaluate forward direction (tangent) at parameter t
    Vec3 evaluate_tangent(float t) const;

    // Arc-length reparameterization for constant-speed travel
    float arc_length_to_param(float distance) const;

    // Total arc length of the spline
    float total_length() const;
};
```

Rails are visualized as colored lines in the 3D viewport with control point handles for interactive editing.

```
 3D Viewport — Camera Rail Visualization:

     P0 ----__                    __---- P3
               \__              __/
                  \__  P1--P2__/
                     \_______/

   Green dots = control points (draggable)
   Yellow handles = tangent in/out (draggable)
   Blue line = spline path
   White diamond = current camera position on rail
```

### 4.2 Camera Rig Presets

Pre-configured camera behaviors that simulate physical camera equipment.

| Rig | Description | Parameters |
|-----|-------------|------------|
| **Dolly** | Camera moves along a linear or spline track | `rail`, `speed`, `ease_type` |
| **Crane / Jib** | Boom arm: pivot at base, camera at tip; sweeps vertically | `arm_length`, `pivot_height`, `sweep_angle` |
| **Orbit** | Camera orbits a target point at fixed distance | `target`, `radius`, `speed`, `elevation` |
| **Handheld** | Adds Perlin-noise shake to simulate handheld footage | `magnitude`, `frequency`, `seed` |
| **Steadicam** | Smoothed follow with configurable lag | `follow_target`, `lag_speed`, `height_offset` |

Rig parameters are animatable via the Camera Track's sub-tracks.

### 4.3 Look-At Constraint

A camera track can have a look-at constraint that overrides its rotation to always face a target.

```ini
; In .uzcinematic
[camera name="Cam1"]
look_at_mode = track_actor
look_at_target = "Hero"
look_at_offset = [0.0, 1.5, 0.0]   ; aim at head, not feet
look_at_damping = 0.1               ; smooth tracking (0 = instant)
```

| Look-At Mode | Description |
|--------------|-------------|
| `none` | Rotation is fully keyframed |
| `track_actor` | Always face a named entity (with optional offset) |
| `track_spline` | Face a second spline path (look-at spline) |
| `track_point` | Face a fixed world-space point |

### 4.4 FOV Animation

FOV keyframes on the Camera Track enable cinematic lens effects.

| Effect | FOV Curve | Description |
|--------|----------|-------------|
| **Rack focus** | FOV holds, DOF focus_dist animates | Shift focus between foreground/background |
| **Dolly zoom** | FOV widens as camera moves forward | Vertigo / Hitchcock effect |
| **Drone swoop** | FOV narrows on approach | Telephoto compression on fast approach |

### 4.5 Cinematic Camera Properties

Per-keyframe visual properties that enhance the cinematic look.

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `film_grain` | `float` | 0.0-1.0 | Simulates analog film noise |
| `lens_flare_intensity` | `float` | 0.0-1.0 | Anamorphic lens flare streaks |
| `chromatic_aberration` | `float` | 0.0-1.0 | RGB channel separation at edges |
| `barrel_distortion` | `float` | -0.5 to 0.5 | Lens distortion (negative = pincushion) |
| `motion_blur_scale` | `float` | 0.0-2.0 | Per-camera motion blur strength override |

These properties override the global post-process settings while the camera is active.

### 4.6 Multi-Camera Setup

The Camera Cut Track manages switching between cameras. Each camera is an independent Camera Track with its own position, rotation, FOV, and lens properties.

```
Camera Cut Track:
|[ Cam1: wide establishing shot ][ Cam2: close-up ][ Cam1: wide reaction ]|

Camera Track Cam1:
|--o------o----------------------------o------o------|
  position keyframes                  position keyframes

Camera Track Cam2:
|------------------o------o-----------------------------|
                  position keyframes (only active during cut)
```

Cameras that are not active in the Camera Cut Track are still evaluated (their keyframes tick) so that switching back produces continuous motion.

---

## 5. Actor Animation in Sequencer

### 5.1 Override Gameplay Animation

During cinematic playback, the `CinematicPlayer` sets a flag on each participating entity:

```cpp
// ECS component added during cinematic playback
struct CCinematicOverride {
    uint32_t sequence_id;     // Which cinematic controls this entity
    bool     override_anim;   // true = sequencer drives animation
    bool     override_transform; // true = sequencer drives position/rotation
    bool     override_physics;   // true = suspend physics simulation
};
```

When `override_anim` is true, the entity's `AnimStateMachine` is paused and the sequencer's Animation Track directly sets the pose. When the cinematic ends, `CCinematicOverride` is removed and normal gameplay resumes.

### 5.2 Per-Track Clip Assignment

Animation clips are dragged from the Asset Browser onto the Animation Track. Each clip becomes a rectangle on the timeline.

```
Animation Track:
                     blend_in
                     |<->|
|[  idle: 0.0-1.5s  ][  walk_to_mark: 1.3-4.0s  ][  dialogue: 3.8-10.0s  ]|
                  ^  ^                           ^  ^
                  overlap = cross-fade region
```

Overlapping regions produce cross-fades using the same `blend_poses()` function from the animation system.

### 5.3 Facial Animation Track

A specialized sub-track for morph target animation (blend shapes). Each morph target is a named float curve.

| Morph Target | Range | Description |
|-------------|-------|-------------|
| `jaw_open` | 0.0-1.0 | Jaw opening for speech |
| `lip_pucker` | 0.0-1.0 | Lip rounding (O, U sounds) |
| `brow_raise_l` | 0.0-1.0 | Left eyebrow raise |
| `brow_raise_r` | 0.0-1.0 | Right eyebrow raise |
| `smile_l` | 0.0-1.0 | Left mouth corner smile |
| `smile_r` | 0.0-1.0 | Right mouth corner smile |
| `blink` | 0.0-1.0 | Eyelid closure |

Morph targets are set via `scene.setMorphWeight` (existing IPC handler from the animation system spec).

### 5.4 Approximate Lip Sync

The sequencer provides an approximate lip sync generator that analyzes an audio waveform and produces phoneme keyframes.

```
Input:  res://audio/dialogue/hero_line_01.ogg
Output: Morph target keyframes on the Facial Animation Track

Pipeline:
  1. Decode audio to PCM
  2. Extract amplitude envelope (16ms windows)
  3. Map amplitude to jaw_open (simple energy-based mapping)
  4. Detect sibilants (high-frequency energy) -> lip_pucker
  5. Generate blink keyframes at natural intervals (~4s random)
  6. Artist refines auto-generated keyframes manually
```

This is intentionally approximate (no neural speech-to-phoneme). The goal is a 70% starting point that an artist or AI agent refines.

---

## 6. Shot System

### 6.1 Master Sequence and Sub-Sequences

A `.uzcinematic` file can contain a **master sequence** that references **sub-sequences** (shots). Each shot is self-contained: its own camera, actors, audio.

```
Master Sequence: "intro_cutscene"
|[ Shot 1: Establishing ][ Shot 2: Dialogue ][ Shot 3: Action ][ Shot 4: Ending ]|
     0:00 - 0:03            0:03 - 0:07        0:07 - 0:09       0:09 - 0:10

Shot 1 → res://cinematics/intro/shot_establishing.uzcinematic
Shot 2 → res://cinematics/intro/shot_dialogue.uzcinematic
Shot 3 → res://cinematics/intro/shot_action.uzcinematic
Shot 4 → res://cinematics/intro/shot_ending.uzcinematic
```

### 6.2 Shot Properties

| Property | Type | Description |
|----------|------|-------------|
| `path` | `string` | `res://` path to sub-sequence `.uzcinematic` |
| `start` | `float` | Start time in master sequence |
| `duration` | `float` | Duration (can be shorter than sub-sequence = trimmed) |
| `offset` | `float` | Start offset into the sub-sequence |

### 6.3 Shot Thumbnails

The Camera Cut Track displays a thumbnail image for each shot. Thumbnails are captured automatically:

- On shot creation: render the first frame of the shot's active camera
- On shot modification: re-render on next save
- Stored as: `.cache/cinematics/<sequence_name>/<shot_index>.jpg` (128x72, JPEG quality 80)

### 6.4 Shot Lock

A shot can be **locked** to prevent accidental editing. Locked shots display a padlock icon and reject all edit operations. Lock state is stored in the `.uzcinematic` file.

```ini
[shot index=2 name="Dialogue"]
path = "res://cinematics/intro/shot_dialogue.uzcinematic"
locked = true
```

### 6.5 Shot Rendering

Individual shots (or the full sequence) can be rendered to disk.

| Output | Format | Resolution | Use Case |
|--------|--------|------------|----------|
| Frame sequence | PNG (lossless) | Full resolution | Offline compositing |
| Frame sequence | JPEG (quality 95) | Full resolution | Quick preview |
| Single frame | PNG | Full resolution | Thumbnail, reference |

Rendering bypasses real-time constraints: each frame is rendered at full quality, saved to disk, then the next frame advances. Frame output path: `export/cinematics/<sequence_name>/frame_%05d.png`.

---

## 7. Playback Controls

### Transport Bar

```
[|<] [<] [>] [||] [>|]  0:03.12 / 0:10.00   [1x v]   In:[0:00.00] Out:[0:10.00]  [LOOP]
 ^    ^   ^   ^    ^                            ^
 |    |   |   |    |                            Playback rate dropdown
 |    |   |   |    Jump to end
 |    |   |   Pause
 |    |   Play
 |    Frame step backward
 Jump to start
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Space` | Play / Pause toggle |
| `Left Arrow` | Step backward 1 frame |
| `Right Arrow` | Step forward 1 frame |
| `Shift+Left` | Step backward 10 frames |
| `Shift+Right` | Step forward 10 frames |
| `Home` | Jump to In marker (or start) |
| `End` | Jump to Out marker (or end) |
| `I` | Set In marker at playhead |
| `O` | Set Out marker at playhead |
| `L` | Toggle loop between In/Out |
| `J` | Decrease playback rate (0.25x steps) |
| `K` | Pause |
| `Shift+L` | Increase playback rate (0.25x steps) |

### Playback Rates

| Rate | Use Case |
|------|----------|
| 0.25x | Frame-by-frame timing review |
| 0.5x | Animation quality check |
| 1.0x | Normal speed (default) |
| 2.0x | Quick review |
| 4.0x | Fast scrub through long sequences |

### Record Mode

Record mode captures live gameplay input and writes it as keyframes on the selected track.

1. Select a Camera Track
2. Press `R` to arm recording
3. Press `Space` to play — WASD + mouse input is captured
4. Press `Space` to stop — captured input is converted to position/rotation keyframes
5. Keyframes are smoothed with a configurable filter (Gaussian, window = 5 frames)

Record mode also works for Actor Tracks: possess an entity and record its movement.

---

## 8. Editing Tools

### 8.1 Clip Operations

| Tool | Shortcut | Description |
|------|----------|-------------|
| **Select** | `V` | Click to select clips, keyframes; box-select with drag |
| **Trim** | Drag clip edge | Extend or shorten a clip by dragging its left/right handle |
| **Slip** | `Y` + drag | Shift the clip's internal content without moving its boundaries |
| **Blade** | `B` + click | Cut a clip at the click position (splits into two clips) |
| **Move** | Drag clip body | Drag clips horizontally along the timeline |
| **Duplicate** | `Ctrl+D` | Duplicate selected clips in place |
| **Delete** | `Del` | Remove selected clips or keyframes |

### 8.2 Keyframe Operations

| Operation | Shortcut | Description |
|-----------|----------|-------------|
| **Add keyframe** | `K` (with track selected) | Insert keyframe at playhead with current value |
| **Delete keyframe** | `Del` | Remove selected keyframes |
| **Move keyframe** | Drag | Move keyframes in time (horizontal) or value (vertical in curve editor) |
| **Multi-select** | `Ctrl+Click` | Select multiple keyframes; edit values in batch |
| **Copy/Paste** | `Ctrl+C` / `Ctrl+V` | Copy keyframes and paste at playhead |

### 8.3 Snapping

| Snap Target | Description |
|-------------|-------------|
| Other clips | Clip edges snap to adjacent clip edges |
| Keyframes | Clips and keyframes snap to keyframes on other tracks |
| Markers | Snap to In/Out markers |
| Frame grid | Snap to frame boundaries (at current zoom level) |
| Beat grid | Snap to audio beat markers (if audio track has BPM set) |

Snapping tolerance: 5 pixels at current zoom level. Hold `Ctrl` to temporarily disable snapping.

### 8.4 Curve Editor

Expanding a numeric sub-track reveals a curve editor inline in the keyframe area.

```
Curve Editor (FOV sub-track):
                              o (90.0)
                            /   \
 (75.0) o------------------       --------o (75.0)
        0:00                                0:10

  Tangent handles: drag to adjust ease-in/ease-out
  Curve types: [Linear] [Bezier] [Step] [Auto]
```

| Curve Type | Description |
|------------|-------------|
| **Linear** | Straight line between keyframes |
| **Bezier** | Smooth curve with adjustable tangent handles |
| **Step** | Instant jump at keyframe time (no interpolation) |
| **Auto** | Engine computes smooth tangents automatically (Catmull-Rom) |

---

## 9. Output / Export

### 9.1 Render Sequence

Render the cinematic as an image sequence to disk.

| Setting | Options | Default |
|---------|---------|---------|
| **Format** | PNG, JPEG | PNG |
| **Resolution** | 100%, 50%, 25% of viewport | 100% |
| **Frame rate** | 24, 30, 60 fps | 30 |
| **Range** | Full sequence, In/Out markers | Full |
| **Output path** | User-specified directory | `export/cinematics/<name>/` |
| **Naming** | `frame_%05d.png` | `frame_%05d.png` |

Rendering is performed offline: the engine advances one frame at a time, renders at full quality (no frame drops), and writes each frame to disk before advancing.

### 9.2 Preview Resolution

During interactive editing, the viewport can render at reduced resolution for better scrub performance.

| Preview | Resolution | Use Case |
|---------|------------|----------|
| Full (100%) | Native viewport | Final quality check |
| Half (50%) | 50% width/height | Interactive editing |
| Quarter (25%) | 25% width/height | Fast scrubbing on complex scenes |

### 9.3 In-Game Render via Lua

Trigger a render export from game code (for automated trailer generation or AI-driven rendering).

```lua
ctx.cinematic:render("res://cinematics/intro.uzcinematic", {
    output_dir = "export/trailer/",
    format     = "png",
    resolution = 1.0,    -- 100%
    fps        = 30,
})
```

### 9.4 Cinematic Mode During Play

When a cinematic plays during gameplay, the engine enters **cinematic mode**:

1. HUD is hidden (`ctx.ui:hide_all()`)
2. Player input is locked (input actions return false/zero)
3. Physics simulation is paused for cinematic-controlled entities
4. Letterbox overlay appears (animated slide-in, 0.3s)
5. On completion, letterbox slides out and gameplay resumes

The `skip_enabled` flag allows players to skip cinematics (press `Escape` + hold for 1 second).

---

## 10. File Format (.uzcinematic)

### Format Specification

`.uzcinematic` files use the same INI-like format as `.uzscene` — plain text, AI-readable, version-controlled.

### Full Example: 10-Second Cutscene

```ini
; res://cinematics/intro.uzcinematic
; A 10-second cutscene: camera flies in, hero delivers a line,
; camera cuts to villain, fade to black.

[uzcinematic version=1 name="IntroCutscene"]
duration     = 10.0
framerate    = 30
author       = "AI-generated"

; ─── CAMERA CUT TRACK ────────────────────────────────────────────

[camera_cut]
; segment: camera_name, start_time, end_time, blend_frames
segment = "FlyInCam",      0.0,  3.5,  0
segment = "HeroCloseUp",   3.5,  7.0,  4
segment = "VillainCam",    7.0, 10.0,  2

; ─── CAMERA TRACKS ───────────────────────────────────────────────

[camera name="FlyInCam"]
look_at_mode   = track_actor
look_at_target = "Hero"
look_at_offset = [0.0, 1.5, 0.0]

[camera.FlyInCam.position]
; time, x, y, z, interpolation
key = 0.0,   50.0, 20.0, -30.0, bezier
key = 1.5,   20.0, 10.0, -15.0, bezier
key = 3.5,    5.0,  3.0,  -5.0, bezier

[camera.FlyInCam.fov]
key = 0.0,  40.0, linear
key = 3.5,  65.0, linear

[camera name="HeroCloseUp"]
look_at_mode = none

[camera.HeroCloseUp.position]
key = 3.5,  1.5, 1.7, -2.0, linear

[camera.HeroCloseUp.rotation]
; time, pitch, yaw, roll (Euler degrees)
key = 3.5,  -5.0, 180.0, 0.0, linear

[camera.HeroCloseUp.fov]
key = 3.5,  50.0, linear

[camera.HeroCloseUp.dof_focus_dist]
key = 3.5,  200.0, linear
key = 5.0,  150.0, bezier

[camera name="VillainCam"]
look_at_mode   = track_actor
look_at_target = "Villain"
look_at_offset = [0.0, 1.6, 0.0]

[camera.VillainCam.position]
key = 7.0,  -3.0, 1.8, -4.0, linear
key = 10.0, -3.5, 1.8, -3.5, bezier

[camera.VillainCam.fov]
key = 7.0,  55.0, linear

; ─── ACTOR TRACKS ────────────────────────────────────────────────

[actor name="Hero"]

[actor.Hero.transform.position]
key = 0.0,  0.0, 0.0, 0.0, linear

[actor.Hero.animation]
; clip_path, start_time, end_time, clip_offset, blend_in, playback_rate
clip = "res://anim/hero_idle.uzanim",       0.0,  3.5,  0.0,  0.0,  1.0
clip = "res://anim/hero_gesture.uzanim",    3.3,  7.0,  0.0,  0.2,  1.0
clip = "res://anim/hero_look_away.uzanim",  6.8, 10.0,  0.0,  0.2,  1.0

[actor.Hero.facial]
; morph_target, time, value, interpolation
key = jaw_open,    3.8,  0.0, linear
key = jaw_open,    4.0,  0.6, bezier
key = jaw_open,    4.5,  0.1, bezier
key = jaw_open,    5.0,  0.5, bezier
key = jaw_open,    5.5,  0.0, linear
key = brow_raise_l, 4.2, 0.3, bezier
key = brow_raise_l, 5.0, 0.0, bezier

[actor name="Villain"]

[actor.Villain.transform.position]
key = 0.0,  10.0, 0.0, -8.0, linear
key = 7.0,   4.0, 0.0, -4.0, bezier

[actor.Villain.visibility]
key = 0.0, false
key = 6.5, true

[actor.Villain.animation]
clip = "res://anim/villain_walk.uzanim",    6.5, 8.0,  0.0,  0.0,  1.0
clip = "res://anim/villain_menace.uzanim",  7.8, 10.0, 0.0,  0.2,  1.0

; ─── AUDIO TRACKS ────────────────────────────────────────────────

[audio name="Music" bus="Music"]
clip      = "res://audio/music/tension_theme.ogg"
start     = 0.0
duration  = 10.0
volume    = 0.7
fade_in   = 1.0
fade_out  = 2.0

[audio name="Dialogue_Hero" bus="Voice"]
clip      = "res://audio/dialogue/hero_line_01.ogg"
start     = 3.8
duration  = 2.0
volume    = 1.0
fade_in   = 0.05
fade_out  = 0.1

[audio name="SFX_Footsteps" bus="SFX"]
clip      = "res://audio/sfx/villain_footsteps.ogg"
start     = 6.5
duration  = 1.5
volume    = 0.6
spatial   = true
; spatial source = actor "Villain" position

; ─── EVENT TRACK ─────────────────────────────────────────────────

[events]
; time, event_name, params_json
event = 0.0,  "cinematic_started", {"name": "intro"}
event = 3.5,  "camera_cut",        {"from": "FlyInCam", "to": "HeroCloseUp"}
event = 6.5,  "villain_reveal",    {"spawn_vfx": "res://vfx/smoke.uzparticles"}
event = 10.0, "cinematic_ended",   {"name": "intro"}

; ─── FADE TRACK ──────────────────────────────────────────────────

[fade]
; start, end, color_r, color_g, color_b
; alpha keyframes: time, alpha
segment = 0.0, 1.0, 0.0, 0.0, 0.0
segment.alpha = 0.0:1.0, 1.0:0.0

segment = 9.0, 10.0, 0.0, 0.0, 0.0
segment.alpha = 9.0:0.0, 10.0:1.0

; ─── SUBTITLE TRACK ──────────────────────────────────────────────

[subtitles]
; start, end, speaker, text, position
sub = 3.8, 5.8, "Hero", "LOC:intro_hero_line_01", bottom
sub = 7.5, 9.5, "Villain", "LOC:intro_villain_line_01", bottom

; ─── POST-PROCESS TRACK ─────────────────────────────────────────

[postprocess.vignette_intensity]
key = 0.0,  0.0, linear
key = 1.0,  0.3, bezier
key = 9.0,  0.3, linear
key = 10.0, 0.8, bezier

[postprocess.film_grain]
key = 0.0,  0.0, linear
key = 0.5,  0.15, bezier
key = 9.5,  0.15, linear
key = 10.0, 0.3, bezier

[postprocess.color_temperature]
key = 0.0,  6500, linear
key = 7.0,  5500, bezier
key = 10.0, 4500, bezier

; ─── SHOT REFERENCES (optional — master sequence) ────────────────

; [shots]
; shot = "res://cinematics/intro/shot_flyin.uzcinematic",   0.0,  3.5, 0.0
; shot = "res://cinematics/intro/shot_dialogue.uzcinematic", 3.5, 7.0, 0.0
; shot = "res://cinematics/intro/shot_villain.uzcinematic",  7.0, 10.0, 0.0
```

### Format Rules

| Rule | Description |
|------|-------------|
| **Version** | `version=1` — must match parser version |
| **Duration** | Explicit duration in seconds; tracks beyond this are ignored |
| **Time format** | Seconds as float (0.0, 3.5, 10.0) — not timecode |
| **Paths** | All `res://` relative paths |
| **Comments** | Lines starting with `;` are ignored |
| **Key format** | `key = time, value(s), interpolation` |
| **Interpolation** | `linear`, `bezier`, `step`, `auto` |
| **Localization** | Text values starting with `LOC:` are resolved via LocaleManager |
| **Backward compatible** | Unknown sections/keys are ignored (forward compatibility) |

---

## 11. Lua Integration

### Playback API

```lua
-- ─── ctx.cinematic API ──────────────────────────────────────

-- Play a cinematic sequence (non-blocking)
-- Returns a CinematicHandle for stop/query
local handle = ctx.cinematic:play("res://cinematics/intro.uzcinematic")

-- Play and wait (coroutine-based, blocks the calling script)
-- Must be called from a coroutine (ctx:schedule_after or similar)
ctx.cinematic:play_and_wait("res://cinematics/intro.uzcinematic")
-- Execution resumes here after cinematic finishes or is skipped

-- Stop a playing cinematic
ctx.cinematic:stop(handle)
-- or stop all cinematics
ctx.cinematic:stop_all()

-- Query state
local is_playing = ctx.cinematic:is_playing(handle)  -- boolean
local current_time = ctx.cinematic:get_time(handle)   -- float (seconds)
local duration = ctx.cinematic:get_duration(handle)    -- float (seconds)

-- Seek to a specific time (useful for branching cinematics)
ctx.cinematic:seek(handle, 5.0)  -- jump to 5 seconds

-- Set playback rate
ctx.cinematic:set_rate(handle, 0.5)  -- half speed

-- Skip cinematic immediately (triggers on_end callbacks)
ctx.cinematic:skip(handle)
```

### Options Table

```lua
local handle = ctx.cinematic:play("res://cinematics/intro.uzcinematic", {
    skip_enabled  = true,     -- allow player to skip (default: true)
    letterbox     = true,     -- show 16:9 letterbox bars (default: true)
    hide_hud      = true,     -- hide game UI during playback (default: true)
    lock_input    = true,     -- lock player input (default: true)
    pause_physics = false,    -- pause physics for non-cinematic entities (default: false)
    start_time    = 0.0,      -- start playback at this time (default: 0.0)
    on_end        = function()
        ctx:log("Cinematic finished!")
        ctx.scene:load("res://scenes/level_02.uzscene")
    end,
})
```

### Event Track Callbacks

Event Track markers dispatch through the EventBus. Lua scripts subscribe to receive them.

```lua
-- Subscribe to cinematic events
local sub = ctx.events:subscribe("villain_reveal", function(data)
    ctx:log("Villain revealed! Spawning VFX...")
    local villain = ctx.scene:find_node("Villain")
    ctx.audio:play_at("res://audio/sfx/dramatic_sting.ogg", {
        position = villain.position,
        volume = 1.0,
        spatial = true,
    })
end)

-- Clean up when script stops
function M:on_stop(ctx)
    ctx.events:unsubscribe(sub)
end
```

### Branching Cinematics

Use events and Lua logic to create interactive/branching cinematics.

```lua
function M:on_start(ctx)
    local handle = ctx.cinematic:play("res://cinematics/dialogue.uzcinematic", {
        skip_enabled = false,
        lock_input   = false,  -- allow input during cinematic
    })

    -- Listen for the choice event from the Event Track
    self.choice_sub = ctx.events:subscribe("dialogue_choice", function(data)
        local choice = data.choice_id
        ctx.cinematic:stop(handle)

        if choice == "accept" then
            ctx.cinematic:play("res://cinematics/dialogue_accept.uzcinematic")
        else
            ctx.cinematic:play("res://cinematics/dialogue_reject.uzcinematic")
        end
    end)
end
```

---

## 12. Implementation Notes

### 12.1 C++ Class Hierarchy

```
src/engine/cinematic/
+-- cinematic_player.h/cpp          -- CinematicPlayer: load, play, stop, seek
+-- cinematic_sequence.h/cpp        -- CinematicSequence: parsed .uzcinematic data
+-- cinematic_loader.h/cpp          -- Parse .uzcinematic INI to CinematicSequence
+-- track_evaluator.h/cpp           -- SequenceTrackEvaluator: evaluate all tracks at time t
+-- camera_rail_spline.h/cpp        -- CameraRailSpline: Bezier spline evaluation
+-- cinematic_camera.h/cpp          -- CinematicCamera: rig presets, look-at, lens properties
+-- cinematic_fade.h/cpp            -- FadeOverlay: full-screen color quad rendering

src/engine/ecs/
+-- components.h                    -- CCinematicOverride component (added to section)

src/game/
+-- cinematic_bindings.h/cpp        -- Lua bindings for ctx.cinematic

src/editor/
+-- sequencer_panel.h/cpp           -- VSCode WebView panel: timeline, track list, controls
+-- shot_thumbnail.h/cpp            -- Shot thumbnail capture and caching
```

### 12.2 Core C++ Structures

```cpp
// cinematic/cinematic_sequence.h

enum class TrackType : uint8_t {
    CameraCut, Camera, Actor, Animation, Audio,
    Event, Fade, Subtitle, PostProcess
};

enum class InterpMode : uint8_t {
    Linear, Bezier, Step, Auto
};

struct Keyframe {
    float      time;
    float      value[4];    // up to Vec4 (x,y,z,w) — unused channels = 0
    InterpMode interp;
    float      tangent_in[4];   // Bezier tangent (only if interp == Bezier)
    float      tangent_out[4];
};

struct KeyframeTrack {
    std::string              name;       // "position", "fov", "jaw_open", etc.
    uint8_t                  channels;   // 1=float, 3=Vec3, 4=Quat
    std::vector<Keyframe>    keys;       // sorted by time

    // Evaluate at time t (returns interpolated value)
    void evaluate(float t, float* out) const;
};

struct CameraCutSegment {
    std::string camera_name;
    float       start;
    float       end;
    int         blend_frames;    // 0 = hard cut
};

struct AnimClipSegment {
    std::string clip_path;       // res:// path
    float       start;
    float       end;
    float       clip_offset;
    float       blend_in;
    float       playback_rate;
};

struct AudioClipEntry {
    std::string name;
    std::string clip_path;
    std::string bus;
    float       start;
    float       duration;
    float       volume;
    float       fade_in;
    float       fade_out;
    bool        spatial;
};

struct EventEntry {
    float       time;
    std::string event_name;
    std::string params_json;
};

struct SubtitleEntry {
    float       start;
    float       end;
    std::string speaker;
    std::string text;
    std::string position;    // "bottom", "top", "center"
    std::string style;       // CSS class
};

struct FadeSegment {
    float       start;
    float       end;
    float       color[3];          // RGB
    std::vector<Keyframe> alpha;   // alpha curve
};

struct CameraTrackData {
    std::string              name;
    std::string              look_at_mode;    // "none", "track_actor", etc.
    std::string              look_at_target;
    float                    look_at_offset[3];
    float                    look_at_damping;
    std::vector<KeyframeTrack> sub_tracks;    // position, rotation, fov, dof_*
};

struct ActorTrackData {
    std::string                 name;         // node name in scene
    std::vector<KeyframeTrack>  sub_tracks;   // transform, visibility, material params
    std::vector<AnimClipSegment> anim_clips;
    std::vector<KeyframeTrack>  facial_tracks; // morph target curves
};

struct CinematicSequence {
    std::string                   name;
    float                         duration;
    int                           framerate;
    uint32_t                      version;

    // Tracks
    std::vector<CameraCutSegment> camera_cuts;
    std::vector<CameraTrackData>  cameras;
    std::vector<ActorTrackData>   actors;
    std::vector<AudioClipEntry>   audio_clips;
    std::vector<EventEntry>       events;
    std::vector<FadeSegment>      fades;
    std::vector<SubtitleEntry>    subtitles;
    std::vector<KeyframeTrack>    postprocess_tracks;

    // Shot references (master sequence)
    struct ShotRef {
        std::string path;
        float       start;
        float       duration;
        float       offset;
        bool        locked;
    };
    std::vector<ShotRef>          shots;
};
```

### 12.3 CinematicPlayer Runtime

```cpp
// cinematic/cinematic_player.h

using CinematicHandle = uint32_t;

class CinematicPlayer {
public:
    // Load and begin playback
    CinematicHandle play(const std::string& path, const PlayOptions& opts);

    // Stop playback, restore scene state
    void stop(CinematicHandle h);
    void stop_all();

    // Seek to absolute time
    void seek(CinematicHandle h, float time);

    // Set playback rate
    void set_rate(CinematicHandle h, float rate);

    // Skip to end immediately
    void skip(CinematicHandle h);

    // Query
    bool  is_playing(CinematicHandle h) const;
    float get_time(CinematicHandle h) const;
    float get_duration(CinematicHandle h) const;

    // Called each frame from game.dll on_update
    // Evaluates all active cinematics, applies track values to scene
    void update(float dt, entt::registry& world, SceneManager& scene,
                AudioEngine& audio, EventBus_Lua& events);

private:
    struct ActiveCinematic {
        CinematicHandle         handle;
        CinematicSequence       sequence;
        float                   current_time;
        float                   playback_rate;
        PlayOptions             options;
        bool                    playing;

        // Saved state for restoration
        struct SavedEntityState {
            NodeId   node;
            Vec3     position;
            Quat     rotation;
            Vec3     scale;
            bool     visible;
            int      anim_state_index;
        };
        std::vector<SavedEntityState> saved_states;

        // Audio voice handles for stopping
        std::vector<uint32_t>   active_voices;

        // Event firing: tracks which events have already fired
        std::vector<bool>       events_fired;
    };

    std::vector<ActiveCinematic> active_;
    uint32_t next_handle_ = 1;

    // Per-track evaluation
    void evaluate_camera_cuts(ActiveCinematic& ac, SceneManager& scene);
    void evaluate_cameras(ActiveCinematic& ac, float t);
    void evaluate_actors(ActiveCinematic& ac, float t, entt::registry& world);
    void evaluate_audio(ActiveCinematic& ac, float t, AudioEngine& audio);
    void evaluate_events(ActiveCinematic& ac, float t, EventBus_Lua& events);
    void evaluate_fades(ActiveCinematic& ac, float t);
    void evaluate_subtitles(ActiveCinematic& ac, float t);
    void evaluate_postprocess(ActiveCinematic& ac, float t);

    // Scene state save/restore
    void save_entity_states(ActiveCinematic& ac, entt::registry& world);
    void restore_entity_states(ActiveCinematic& ac, entt::registry& world);
};
```

### 12.4 Track Evaluation Order

Each frame, the CinematicPlayer evaluates tracks in a strict order to avoid dependency issues:

```
1. Camera Cut Track    → determine active camera for this frame
2. Camera Tracks       → evaluate position/rotation/FOV/DOF for all cameras
3. Actor Tracks        → evaluate transform, visibility for all actors
4. Animation Tracks    → apply animation clips (overrides AnimStateMachine)
5. Audio Tracks        → start/stop audio clips, evaluate volume/pitch curves
6. Event Track         → fire events whose time was crossed this frame
7. Fade Track          → evaluate fade alpha, submit overlay quad
8. Subtitle Track      → show/hide subtitle UI elements
9. Post-Process Track  → apply post-process parameter overrides
```

This order ensures that camera position is resolved before actors (for look-at constraints), and that animations are applied before events fire (so event handlers see the correct pose).

### 12.5 DX12 Rendering Integration

| Feature | Render Pass | Implementation |
|---------|-------------|----------------|
| **Camera spline debug draw** | Debug Pass (sort_key=900) | Line list of spline segments + control point billboards |
| **Camera frustum visualization** | Debug Pass (sort_key=900) | Wireframe frustum from camera projection matrix |
| **Safe frame overlay** | UI Pass (sort_key=800) | 4 semi-transparent black quads for letterbox |
| **Fade overlay** | UI Pass (sort_key=810) | Full-screen quad with animated alpha |
| **Subtitle rendering** | UI Pass via `uz_ui_html.dll` | HTML element positioned at bottom/top/center |
| **Focal plane indicator** | Debug Pass (sort_key=900) | Semi-transparent disc at DOF focus distance |

### 12.6 Scene State Management

The cinematic player is **non-destructive**: it saves affected entity state before playback and restores it after.

```
Before playback:
  1. For each actor in the sequence, save: position, rotation, scale,
     visibility, current animation state
  2. Add CCinematicOverride component to each actor
  3. Hide HUD (if options.hide_hud)
  4. Lock input (if options.lock_input)

During playback:
  - Tracks write directly to entity transforms, anim state, etc.
  - Physics is suspended for entities with CCinematicOverride
  - EventBus events fire normally (game logic can react)

After playback (or on stop/skip):
  1. Restore saved state for each actor
  2. Remove CCinematicOverride components
  3. Restore HUD visibility
  4. Unlock input
  5. Fire "cinematic_ended" event
```

### 12.7 VSCode WebView Panel

The Sequencer Timeline is implemented as a VSCode WebView panel using HTML Canvas for the timeline rendering.

```
VSCode Extension Architecture:

sequencer-panel.ts
  +-- SequencerPanel (implements vscode.WebviewViewProvider)
  |     +-- Creates/manages WebView with timeline HTML
  |     +-- Receives IPC messages from engine (playhead position, track data)
  |     +-- Sends IPC commands (play, stop, seek, add keyframe, edit track)
  |
  +-- sequencer-timeline.ts (runs in WebView)
  |     +-- Canvas-based timeline renderer
  |     +-- Track list with expand/collapse
  |     +-- Keyframe rendering (diamonds, rectangles, curves)
  |     +-- Playhead + scrubbing
  |     +-- Drag/drop clip operations
  |     +-- Keyboard shortcut handling
  |
  +-- shot-thumbnail-strip.ts (runs in WebView)
        +-- Horizontal strip of shot thumbnails in Camera Cut Track
        +-- Thumbnails loaded as base64 JPEG from engine cache
```

### 12.8 IPC Handlers (`cinematic.*`)

| Command | Args | Returns | Description |
|---------|------|---------|-------------|
| `cinematic.load` | `{path}` | `{ok, sequence_data}` | Load and parse a .uzcinematic file |
| `cinematic.save` | `{path, data}` | `{ok}` | Serialize and save a .uzcinematic file |
| `cinematic.play` | `{path, options?}` | `{ok, handle}` | Start playback |
| `cinematic.stop` | `{handle}` | `{ok}` | Stop playback |
| `cinematic.seek` | `{handle, time}` | `{ok}` | Seek to time |
| `cinematic.get_state` | `{handle}` | `{time, playing, rate}` | Query playback state |
| `cinematic.add_keyframe` | `{track, sub_track, time, value, interp}` | `{ok}` | Add keyframe to track |
| `cinematic.remove_keyframe` | `{track, sub_track, index}` | `{ok}` | Remove keyframe |
| `cinematic.add_track` | `{type, name, parent?}` | `{ok, track_id}` | Add a new track |
| `cinematic.remove_track` | `{track_id}` | `{ok}` | Remove a track |
| `cinematic.render_frame` | `{handle, time}` | `{ok, image_base64}` | Render single frame as JPEG |
| `cinematic.render_sequence` | `{path, output_dir, format, fps}` | `{ok, frame_count}` | Render frame sequence to disk |
| `cinematic.capture_thumbnail` | `{handle, shot_index}` | `{ok, image_base64}` | Capture shot thumbnail |

### 12.9 Integration with Existing Systems

| System | Integration Point | Description |
|--------|-------------------|-------------|
| **Animation** | `AnimStateMachine::pause()` / `AnimStateMachine::force_clip()` | Cinematic overrides gameplay animation |
| **Camera** | `CameraSystem::set_cinematic_override(pos, rot, fov, dof)` | Cinematic camera bypasses normal camera modes |
| **Audio** | `AudioEngine::play_2d()` / `play_3d()` / `stop()` | Audio clips start/stop at sequence-specified times |
| **EventBus** | `EventBus<T>::emit()` / Lua `ctx.events:emit()` | Event Track fires events through existing bus |
| **UI** | `ctx.ui:set_property()` / `ctx.ui:show()` / `ctx.ui:hide()` | Subtitle display, HUD hide/show, fade overlay |
| **Scene** | `SceneManager::get()` / `set_property()` | Actor transforms written via SceneCommand queue |
| **Physics** | `PhysicsWorld::set_paused(entity, true)` | Suspend physics for cinematic-controlled entities |
| **Timer** | `drain_timers()` still runs during cinematic | Timers continue; cinematic is a layer on top |
| **Save** | `SaveManager` serializes active cinematic handle + time | Resumable save/load mid-cinematic |
| **Localization** | `LocaleManager::resolve("LOC:key")` | Subtitle text resolved at display time |

### 12.10 Performance Considerations

| Concern | Mitigation |
|---------|------------|
| Keyframe binary search per frame | Keys sorted; use `std::lower_bound`; cache last-evaluated index per track |
| Multiple camera evaluation | Only evaluate cameras referenced in the Camera Cut Track's active segment + 1 (for blend) |
| Audio voice allocation | Pre-scan sequence on load to determine max concurrent voices; reserve from pool |
| Spline evaluation | Arc-length LUT built once on load (256 samples); runtime evaluation is O(1) table lookup + lerp |
| Subtitle rendering | Create/destroy HTML elements only on segment boundaries, not every frame |
| Post-process overrides | Batch all post-process values into a single cbuffer update per frame |

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 4** | `.uzcinematic` loader/serializer, `CinematicPlayer` runtime, Camera Track + Actor Track + Audio Track evaluation, Lua `ctx.cinematic:play/stop`, basic Event Track, fade overlay |
| **Phase 4+** | Camera Rail spline + rig presets, Animation Track with blending, Facial Animation Track, Subtitle Track with localization, Post-Process Track, shot system (master/sub-sequences), VSCode Sequencer Timeline WebView panel |
| **Phase 5** | Record mode, approximate lip sync, shot rendering (PNG/JPEG sequence), curve editor in WebView, AI-assisted cinematic generation via MCP (`cinematic.generate`) |

---

## Related Docs

- [specs/scene-system.md](../specs/scene-system.md) -- Node tree, .uzscene format, SceneManager API
- [specs/animation-system.md](../specs/animation-system.md) -- AnimationClip, AnimStateMachine, blend_poses, AnimNotify
- [specs/camera-system.md](../specs/camera-system.md) -- Camera modes, SpringArm, DoF, Camera Shake, .uzcam presets
- [specs/audio-system.md](../specs/audio-system.md) -- XAudio2, CAudioSource, 4-bus system, Lua audio API
- [specs/event-bus.md](../specs/event-bus.md) -- EventBus<T>, AutoSub<T>, Lua string-dispatch
- [specs/lua-scripting.md](../specs/lua-scripting.md) -- LuaJIT VM, ctx API, script lifecycle
- [specs/ui-system.md](../specs/ui-system.md) -- uz_ui_html.dll, UIPass, subtitle rendering
- [specs/screen-space-effects.md](../specs/screen-space-effects.md) -- SSAO, SSR, TAA, velocity buffer
- [specs/localization.md](../specs/localization.md) -- CSV strings, LocaleManager, LOC: key resolution
- [specs/save-system.md](../specs/save-system.md) -- Save/load during cinematic playback
