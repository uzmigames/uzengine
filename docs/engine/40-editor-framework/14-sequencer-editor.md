# Sequencer Editor — Timeline & Cinematic Editor

> **Status**: Planned (Phase 3–4)
> **Source**: `extension/editor/sequencer.ts`, `engine/runtime/sequencer/sequence.h`
> **References**: Node Graph Framework (§40.09), Camera System (§30), Animation System (§21), UE5 Sequencer, Godot AnimationPlayer, Unity Timeline

## Purpose

The Sequencer Editor provides a full-featured timeline for cinematic sequencing, camera cuts, skeletal animations, audio timing, particle effects, and material parameter animation. Sequences consist of tracks with keyframes and can be composed into complex cinematics. The editor supports possessables (references to scene entities) and spawnables (sequence-owned entities), camera cuts, sub-sequences, and precise synchronization with audio.

## Architecture

```
Sequencer Editor (TypeScript/Webview)
    │
    ├─ Timeline Panel (scrollable, keyframe grid)
    │   ├─ Track List (left sidebar, collapsible)
    │   │   ├─ Transform Track (position, rotation, scale)
    │   │   ├─ Camera Track (active camera, fov, near/far)
    │   │   ├─ Skeletal Animation Track
    │   │   ├─ Audio Track (with waveform visualization)
    │   │   ├─ Particle Track (spawn/kill particles)
    │   │   ├─ Material Parameter Track (color, roughness, metallic)
    │   │   ├─ Event Track (fire custom events at time)
    │   │   ├─ Fade Track (black in/out)
    │   │   └─ Subtitle Track (localized text)
    │   │
    │   ├─ Ruler (time, frames, seconds)
    │   │
    │   ├─ Track Content (center)
    │   │   ├─ Keyframe blocks (boxes with values)
    │   │   ├─ Bezier curves (if in curve view)
    │   │   └─ Scrubber line (playhead at current time)
    │   │
    │   └─ Viewport Preview
    │       ├─ Live camera and entities update
    │       └─ Audio playback synchronized
    │
    ├─ Playback Controls (top)
    │   ├─ Play / Pause / Stop
    │   ├─ Scrub slider (drag to seek)
    │   ├─ Current Time display (00:00.000)
    │   ├─ Sequence Duration (00:10.000)
    │   ├─ Play Rate slider (0.25x - 2.0x)
    │   ├─ Loop toggle
    │   └─ In/Out points (mark region to loop)
    │
    ├─ Properties Panel (right)
    │   ├─ Selected track properties
    │   ├─ Keyframe properties (time, value, interpolation mode)
    │   └─ Track options (solo, mute, visibility)
    │
    └─ Possessables / Spawnables Panel
        ├─ List of referenced scene entities (Possessables)
        ├─ List of sequence-owned entities (Spawnables)
        └─ Bind/Unbind entity references

Runtime: Sequence (.uzsequence JSON) → Load → Play tracks → Sample animations/audio/positions/etc.
```

## Track Types

### Transform Track

Animate position, rotation, and scale:

```cpp
/// Animates entity transform over time.
struct TransformTrack {
    EntityHandle possessable;  // Which entity to animate

    /// Keyframes for each component.
    struct KeyframeSet {
        std::vector<Keyframe<Vec3>> position_keys;
        std::vector<Keyframe<Quat>> rotation_keys;
        std::vector<Keyframe<Vec3>> scale_keys;
    } keyframes;

    /// Sample transform at time t.
    Transform Sample(float time) const {
        Transform result;
        result.position = SampleCurve(keyframes.position_keys, time);
        result.rotation = SampleCurve(keyframes.rotation_keys, time);
        result.scale = SampleCurve(keyframes.scale_keys, time);
        return result;
    }
};
```

### Camera Track

Switch between cameras and animate camera parameters:

```cpp
/// Animates camera state and cuts between cameras.
struct CameraTrack {
    struct CameraCut {
        float time;
        EntityHandle camera_entity;  // Which camera is active
        float duration;              // How long it's active
    };
    std::vector<CameraCut> cuts;

    /// Animate camera FOV, near/far planes.
    struct KeyframeSet {
        std::vector<Keyframe<float>> fov_keys;
        std::vector<Keyframe<float>> near_plane_keys;
        std::vector<Keyframe<float>> far_plane_keys;
    } keyframes;

    /// Which camera is active at time t.
    EntityHandle GetActiveCameraAt(float time) const {
        for (const auto& cut : cuts) {
            if (time >= cut.time && time < cut.time + cut.duration) {
                return cut.camera_entity;
            }
        }
        return EntityHandle::Invalid;
    }
};
```

### Skeletal Animation Track

Play skeletal animations with blending:

```cpp
/// Plays skeletal animation sections on a skeletal mesh.
struct SkeletalAnimationTrack {
    EntityHandle possessable;  // Skeletal mesh entity

    struct AnimationSegment {
        float start_time;
        float duration;
        AssetRef animation;      // .uzanim animation asset
        float play_rate = 1.0f;  // Speed multiplier
        bool loop = false;       // Loop within segment
    };
    std::vector<AnimationSegment> segments;

    /// Blend between overlapping animations.
    struct BlendConfig {
        float blend_in_time = 0.2f;   // Fade in from previous
        float blend_out_time = 0.2f;  // Fade out to next
    } blend_config;

    /// Sample skeletal pose at time t.
    SkeletalPose Sample(float time, const SkeletalMesh& mesh) const;
};
```

### Audio Track

Synchronize audio with visual timeline:

```cpp
/// Plays audio clips synchronized with timeline.
struct AudioTrack {
    struct AudioSegment {
        float start_time;        // When to start playing
        AssetRef audio_clip;     // .uzaudio clip
        float volume = 1.0f;
        float pitch = 1.0f;
    };
    std::vector<AudioSegment> segments;

    /// Waveform for visual feedback (cached).
    struct Waveform {
        std::vector<float> samples;  // Mono mixed-down samples
        int sample_rate = 48000;
    } waveform;

    /// Which audio clip is playing at time t.
    const AudioSegment* GetActiveSegmentAt(float time) const {
        for (const auto& seg : segments) {
            if (time >= seg.start_time && time < seg.start_time + seg.audio_clip.duration) {
                return &seg;
            }
        }
        return nullptr;
    }
};
```

### Material Parameter Track

Animate material properties (color, roughness, metallic, etc.):

```cpp
/// Animates material parameters (scalar and color).
struct MaterialParameterTrack {
    EntityHandle possessable;      // Entity with material
    int material_index = 0;        // Which material slot
    std::string parameter_name;    // Parameter name in material

    enum class ParamType { Scalar, Color, Vector };
    ParamType param_type;

    std::vector<Keyframe<float>> scalar_keys;      // For scalar params
    std::vector<Keyframe<Vec3>> color_keys;        // For color params

    /// Sample parameter value at time t.
    float SampleScalar(float time) const {
        // Interpolate between keyframes
    }

    Vec3 SampleColor(float time) const {
        // Interpolate between keyframes
    }
};
```

### Particle & VFX Track

Spawn and despawn particle effects:

```cpp
/// Controls particle emitter lifecycle on timeline.
struct ParticleTrack {
    struct ParticleSegment {
        float start_time;
        float duration;
        AssetRef vfx_graph;       // .uzvfx emitter
        Vec3 spawn_position;
        Quat spawn_rotation;
    };
    std::vector<ParticleSegment> segments;

    /// Spawn/despawn effects as playhead moves.
    void Update(float time, const PreviousFrame& prev_time);
};
```

### Event Track

Fire custom events at specific times:

```cpp
/// Fires custom events for game logic (cutscene callbacks).
struct EventTrack {
    struct Event {
        float time;
        std::string event_name;    // "on_dialog_start", "on_door_open", etc.
        std::string parameter;     // Optional data
    };
    std::vector<Event> events;

    /// Events that fired between prev_time and current time.
    std::vector<const Event*> GetFiredEventsSince(float prev_time, float current_time) const;
};
```

### Fade & Subtitle Tracks

Simple but essential:

```cpp
/// Black fade in/out (letterbox style).
struct FadeTrack {
    struct FadeSegment {
        float start_time;
        float fade_duration;
        bool fade_in = true;  // true = from black, false = to black
    };
    std::vector<FadeSegment> fades;

    float GetFadeAlpha(float time) const;
};

/// Display subtitles (localized).
struct SubtitleTrack {
    struct Subtitle {
        float start_time;
        float duration;
        std::string text_key;    // "npc.dialog.greeting" (localized)
        std::string speaker;     // Optional speaker name
    };
    std::vector<Subtitle> subtitles;
};
```

## Keyframe Management

Flexible interpolation modes:

```typescript
/// Keyframe with interpolation metadata.
export interface Keyframe<T> {
    time: number;           // Seconds in sequence
    value: T;
    interpolation: 'Linear' | 'Cubic' | 'Step';
    tangent_in?: number;    // For Cubic mode
    tangent_out?: number;
}

/// Right-click keyframe → context menu.
export class KeyframeContextMenu {
    constructor(private keyframe: Keyframe<any>) {}

    show(): HTMLElement {
        const menu = document.createElement('div');
        menu.className = 'context-menu';

        const options = [
            { label: 'Linear', callback: () => this.keyframe.interpolation = 'Linear' },
            { label: 'Cubic', callback: () => this.keyframe.interpolation = 'Cubic' },
            { label: 'Step', callback: () => this.keyframe.interpolation = 'Step' },
            { label: 'Delete', callback: () => this.deleteKeyframe() },
            { label: 'Copy Value', callback: () => this.copyKeyframeValue() }
        ];

        for (const opt of options) {
            const item = document.createElement('div');
            item.textContent = opt.label;
            item.onclick = opt.callback;
            menu.appendChild(item);
        }

        return menu;
    }

    private deleteKeyframe() {
        this.track.removeKeyframe(this.keyframe.time);
    }

    private copyKeyframeValue() {
        navigator.clipboard.writeText(JSON.stringify(this.keyframe.value));
    }
}
```

## .uzsequence File Format

Sequences save as human-readable JSON:

```json
{
  "name": "intro_cinematic",
  "duration": 30.5,
  "framerate": 30,
  "tracks": [
    {
      "type": "Camera",
      "name": "Cameras",
      "cuts": [
        {
          "time": 0.0,
          "camera_entity": "camera_wide",
          "duration": 5.0
        },
        {
          "time": 5.0,
          "camera_entity": "camera_closeup",
          "duration": 10.0
        }
      ],
      "keyframes": {
        "fov": [
          {"time": 0.0, "value": 50.0, "interpolation": "Linear"},
          {"time": 15.0, "value": 35.0, "interpolation": "Cubic"}
        ]
      }
    },
    {
      "type": "Transform",
      "name": "Hero Movement",
      "possessable": "player_char",
      "position": [
        {"time": 0.0, "value": [0.0, 0.0, 0.0], "interpolation": "Cubic"},
        {"time": 10.0, "value": [10.0, 0.0, 5.0], "interpolation": "Cubic"}
      ],
      "rotation": [
        {"time": 0.0, "value": [0.0, 0.0, 0.0, 1.0]},
        {"time": 10.0, "value": [0.0, 0.7071, 0.0, 0.7071]}
      ]
    },
    {
      "type": "SkeletalAnimation",
      "name": "Hero Animation",
      "possessable": "player_char",
      "segments": [
        {
          "start_time": 0.0,
          "duration": 5.0,
          "animation": "res://animations/hero_walk.uzanim",
          "play_rate": 1.0,
          "loop": true
        },
        {
          "start_time": 5.0,
          "duration": 8.0,
          "animation": "res://animations/hero_talk.uzanim",
          "play_rate": 1.0,
          "loop": false
        }
      ]
    },
    {
      "type": "Audio",
      "name": "Dialog",
      "segments": [
        {
          "start_time": 2.0,
          "audio_clip": "res://audio/dialog_intro.uzaudio",
          "volume": 1.0
        }
      ]
    },
    {
      "type": "Event",
      "name": "Gameplay Events",
      "events": [
        {
          "time": 8.0,
          "name": "on_player_arrives",
          "parameter": "{}"},
        {
          "time": 15.0,
          "name": "on_dialog_start",
          "parameter": "{\"speaker\": \"npc_guard\"}"
        }
      ]
    }
  ],
  "possessables": [
    {
      "id": "player_char",
      "scene_entity": "player"
    },
    {
      "id": "camera_wide",
      "scene_entity": "cam_wide"
    }
  ],
  "spawnables": [
    {
      "id": "spawned_enemy",
      "prefab": "res://prefabs/enemy_basic.uzprefab"
    }
  ]
}
```

## Playback Control

Real-time viewport preview:

```typescript
/// Sequencer playback controller (webview).
export class SequencerPlayback {
    private currentTime: number = 0;
    private duration: number = 0;
    private isPlaying: boolean = false;
    private playRate: number = 1.0;
    private loopStart: number = 0;
    private loopEnd: number = 0;
    private loopEnabled: boolean = false;

    async play() {
        this.isPlaying = true;
        await this.ipc.call("sequencer.play", [this.sequenceId]);
        this.startPlaybackLoop();
    }

    pause() {
        this.isPlaying = false;
        this.ipc.call("sequencer.pause", [this.sequenceId]);
    }

    stop() {
        this.isPlaying = false;
        this.currentTime = 0;
        this.ipc.call("sequencer.stop", [this.sequenceId]);
    }

    seek(time: number) {
        this.currentTime = Math.max(0, Math.min(time, this.duration));
        this.ipc.call("sequencer.seek", [this.sequenceId, this.currentTime]);
    }

    /// Called by playback loop to update time.
    private startPlaybackLoop() {
        const tick = () => {
            if (!this.isPlaying) return;

            const delta = 1 / 60;  // Assume 60 FPS
            this.currentTime += delta * this.playRate;

            // Handle looping
            if (this.loopEnabled && this.currentTime > this.loopEnd) {
                this.currentTime = this.loopStart;
            } else if (this.currentTime > this.duration) {
                this.stop();
                return;
            }

            this.updateTimelineUI();
            requestAnimationFrame(tick);
        };

        tick();
    }

    private updateTimelineUI() {
        const timeStr = this.formatTime(this.currentTime);
        const scrubber = document.querySelector('.scrubber') as HTMLElement;
        const position = (this.currentTime / this.duration) * 100;
        scrubber.style.left = `${position}%`;
    }

    private formatTime(seconds: number): string {
        const mins = Math.floor(seconds / 60);
        const secs = (seconds % 60).toFixed(2);
        return `${mins}:${secs.padStart(5, '0')}`;
    }

    setPlayRate(rate: number) {
        this.playRate = Math.max(0.25, Math.min(rate, 2.0));
        this.ipc.call("sequencer.set_play_rate", [this.sequenceId, this.playRate]);
    }

    setLoopRegion(start: number, end: number) {
        this.loopStart = Math.max(0, start);
        this.loopEnd = Math.min(this.duration, end);
        this.loopEnabled = true;
    }

    clearLoopRegion() {
        this.loopEnabled = false;
    }
}
```

## Curve Editor

Toggle curve view per track:

```typescript
/// Visual curve editor for keyframe interpolation.
export class CurveEditor {
    private canvas: HTMLCanvasElement;
    private keyframes: Keyframe<number>[] = [];
    private selectedKeyframe: Keyframe<number> | null = null;

    constructor(private track: Track, container: HTMLElement) {
        this.canvas = document.createElement('canvas');
        this.canvas.width = 800;
        this.canvas.height = 200;
        container.appendChild(this.canvas);
        this.setupInput();
    }

    render() {
        const ctx = this.canvas.getContext('2d')!;
        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        // Draw grid
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 1;
        for (let i = 0; i <= 10; i++) {
            const x = (i / 10) * this.canvas.width;
            const y = (i / 10) * this.canvas.height;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, this.canvas.height);
            ctx.stroke();
        }

        // Draw curve
        this.drawCurve();

        // Draw keyframes
        for (const kf of this.keyframes) {
            const x = (kf.time / this.track.duration) * this.canvas.width;
            const y = (1 - kf.value) * this.canvas.height;

            ctx.fillStyle = kf === this.selectedKeyframe ? '#f00' : '#0f0';
            ctx.fillRect(x - 4, y - 4, 8, 8);

            // Draw tangent lines if Cubic
            if (kf.interpolation === 'Cubic' && kf.tangent_in !== undefined) {
                ctx.strokeStyle = '#f0f';
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(x - 10, y - kf.tangent_in * 20);
                ctx.lineTo(x, y);
                ctx.stroke();
            }
        }
    }

    private drawCurve() {
        const ctx = this.canvas.getContext('2d')!;
        ctx.strokeStyle = '#0f0';
        ctx.lineWidth = 2;
        ctx.beginPath();

        for (let i = 0; i <= 200; i++) {
            const time = (i / 200) * this.track.duration;
            const value = this.sampleCurve(time);
            const x = (time / this.track.duration) * this.canvas.width;
            const y = (1 - value) * this.canvas.height;

            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }

        ctx.stroke();
    }

    private sampleCurve(time: number): number {
        const lower = this.keyframes.find(k => k.time <= time);
        const upper = this.keyframes.find(k => k.time > time);

        if (!lower || !upper) {
            return lower?.value ?? upper?.value ?? 0;
        }

        const t = (time - lower.time) / (upper.time - lower.time);

        if (lower.interpolation === 'Linear') {
            return lower.value + (upper.value - lower.value) * t;
        } else if (lower.interpolation === 'Cubic') {
            // Hermite interpolation
            const h00 = 2*t*t*t - 3*t*t + 1;
            const h10 = t*t*t - 2*t*t + t;
            const h01 = -2*t*t*t + 3*t*t;
            const h11 = t*t*t - t*t;

            return h00 * lower.value + h10 * (lower.tangent_out || 0) + h01 * upper.value + h11 * (upper.tangent_in || 0);
        } else {  // Step
            return lower.value;
        }
    }

    private setupInput() {
        this.canvas.onmousedown = (e) => {
            const rect = this.canvas.getBoundingClientRect();
            const x = e.clientX - rect.left;
            const y = e.clientY - rect.top;

            // Hit-test keyframes
            for (const kf of this.keyframes) {
                const kfx = (kf.time / this.track.duration) * this.canvas.width;
                const kfy = (1 - kf.value) * this.canvas.height;

                if (Math.abs(x - kfx) < 8 && Math.abs(y - kfy) < 8) {
                    this.selectedKeyframe = kf;
                    this.startDragKeyframe(e);
                    return;
                }
            }

            // Click to add new keyframe
            const time = (x / this.canvas.width) * this.track.duration;
            const value = 1 - (y / this.canvas.height);
            this.track.addKeyframe({time, value, interpolation: 'Cubic'});
            this.render();
        };
    }

    private startDragKeyframe(mouseDown: MouseEvent) {
        const kf = this.selectedKeyframe!;
        const startTime = kf.time;

        const onMove = (e: MouseEvent) => {
            const rect = this.canvas.getBoundingClientRect();
            const x = e.clientX - rect.left;
            const newTime = (x / this.canvas.width) * this.track.duration;
            kf.time = Math.max(0, Math.min(newTime, this.track.duration));
            this.render();
        };

        const onUp = () => {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
        };

        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
    }
}
```

## Comparison

| Feature | UE5 (Sequencer) | Godot (AnimationPlayer) | Unity (Timeline) | **UzEngine** |
|---|---|---|---|---|
| **Track types** | 15+ (Transform, Anim, Camera, Audio, Event, etc.) | Simple animation tracks | Playable tracks | **9 types (all essentials)** |
| **Camera cuts** | Yes, with blending | No | Yes | **Yes, with FOV animation** |
| **Possessables** | Yes | Via Node references | Yes | **Yes + Spawnables** |
| **Audio sync** | Yes, with playback | Limited | Yes, with waveform vis | **Yes + waveform overlay** |
| **Curve editing** | Full per-track curves | Limited to AnimationTrack | Full curve editor | **Bezier curves with tangents** |
| **File format** | Binary .uasset | .tres text | Binary (C# codegen) | **.uzsequence (JSON)** |
| **Sub-sequences** | Limited | No | Nested PlayableGraph | **Yes (compose sequences)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Basic timeline, 3 tracks (Transform, Camera, Audio), playback controls (play/pause/stop/scrub), .uzsequence JSON serialization, viewport preview |
| **Phase 4** | All 9 track types, curve editor, sub-sequences, camera cuts, waveform visualization, event firing, fade/subtitle tracks, loop regions |
| **Phase 4+** | Possessables/Spawnables binding, multi-track editing, keyframe editing UI refinement, performance profiling (GPU cost per track), export to video (frame capture) |

