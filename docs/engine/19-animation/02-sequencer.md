# Sequencer — Timeline-Based Cinematics & Animation

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/sequencer/`, `engine/subsystem/sequencer_world.h`
> **Depends on**: Scene (§11), Rendering (§12), Scripting (§15), Gameplay (§20)
> **References**: UE5 `ULevelSequence` + `FMovieSceneSequencePlayer`, Godot `AnimationPlayer` timeline concept

## Purpose

Timeline-based cinematic sequencer for in-game cutscenes, camera animations, and choreographed gameplay moments. Supports possessables (existing scene actors) and spawnables (sequence-owned actors), multi-track keyframe animation with interpolation curves, camera cuts, particle/audio/animation playback, and Lua event scripting. All sequences are stored as JSON files (.uzsequence) for easy AI-driven editing and version control.

## Sequence Structure

A `.uzsequence` file describes a complete timeline:

```json
{
  "name": "intro_cutscene",
  "duration": 15.0,
  "frame_rate": 30,
  "master_properties": {
    "autoplay": true,
    "loop": false,
    "blend_in_time": 0.0
  },
  "tracks": [
    {
      "track_name": "hero_camera_cut",
      "track_type": "camera_cut",
      "possessable_guid": "camera_actor_001",
      "sections": [
        {
          "start_time": 0.0,
          "duration": 5.0,
          "payload": {
            "target_camera": "intro_cam_01"
          }
        }
      ]
    },
    {
      "track_name": "hero_transform",
      "track_type": "transform",
      "possessable_guid": "player_001",
      "sections": [
        {
          "start_time": 0.0,
          "duration": 15.0,
          "keyframes": [
            { "time": 0.0, "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "interpolation": "cubic" },
            { "time": 5.0, "position": [10, 2, -5], "rotation": [0, 0.707, 0, 0.707], "interpolation": "cubic" },
            { "time": 15.0, "position": [20, 0, 0], "rotation": [0, 0, 0, 1], "interpolation": "linear" }
          ]
        }
      ]
    },
    {
      "track_name": "hero_skeletal_anim",
      "track_type": "skeletal_animation",
      "possessable_guid": "player_001",
      "sections": [
        { "start_time": 0.0, "duration": 3.0, "animation_asset": "res://anims/hero_talk.uzanim" },
        { "start_time": 3.0, "duration": 2.0, "animation_asset": "res://anims/hero_wave.uzanim" }
      ]
    },
    {
      "track_name": "ambient_music",
      "track_type": "audio",
      "sections": [
        { "start_time": 0.0, "duration": 15.0, "audio_asset": "res://audio/intro_music.uzaudio", "volume": 0.8 }
      ]
    },
    {
      "track_name": "explosion_vfx",
      "track_type": "particle",
      "sections": [
        { "start_time": 5.0, "duration": 0.5, "particle_system": "res://vfx/explosion.uzpart", "location": [15, 5, -10] }
      ]
    },
    {
      "track_name": "gameplay_events",
      "track_type": "event",
      "sections": [
        { "start_time": 3.0, "event": "player_spotted_enemy" },
        { "start_time": 7.0, "event": "combat_begins" }
      ]
    },
    {
      "track_name": "fade_in",
      "track_type": "fade",
      "sections": [
        { "start_time": 0.0, "duration": 1.0, "fade_type": "fade_in", "color": [0, 0, 0] }
      ]
    },
    {
      "track_name": "subtitles",
      "track_type": "subtitles",
      "sections": [
        { "start_time": 2.0, "duration": 3.0, "text": "What's happening?", "locale": "en" }
      ]
    }
  ],
  "possessables": [
    {
      "guid": "camera_actor_001",
      "name": "intro_cam_01",
      "type": "camera_actor",
      "scene_binding": "CinematicCamera_0"  // existing actor in scene
    },
    {
      "guid": "player_001",
      "name": "Player",
      "type": "pawn",
      "scene_binding": "Pawn_Player"
    }
  ],
  "spawnables": [
    {
      "guid": "enemy_spawnable_001",
      "name": "Enemy_01",
      "spawn_template": "res://characters/enemy.uzactor",
      "spawn_time": 2.0,
      "despawn_time": 15.0
    }
  ]
}
```

All timing is in **seconds** (not frames). Sequences are **independent of frame rate** — 30 fps sequences play identically at 60 fps (interpolation handles it).

## Track Types

| Track | Purpose | Payload |
|-------|---------|---------|
| **Transform** | Animate position, rotation, scale | Keyframes with cubic/linear interpolation |
| **Camera Cut** | Switch active camera at specific times | Target camera GUID + optional blend |
| **Skeletal Animation** | Play skeletal animations on characters | Animation asset + playback speed |
| **Audio** | Play music, dialogue, SFX | Audio asset + volume + pan |
| **Particle** | Spawn and play particle systems | VFX asset + spawn location + duration |
| **Material Parameter** | Animate shader parameters per object | Property name + keyframed float/vec4 |
| **Event** | Fire Lua callback at specific times | Event name + optional payload |
| **Fade** | Screen fade in/out | Fade color + duration |
| **Subtitles** | Display localized text | Text key + duration + locale fallback |

## Possessables vs Spawnables

**Possessables** — existing scene actors controlled by the sequence:

```cpp
struct Possessable {
    GUID   guid;              // unique ID in sequence
    string name;              // display name
    string scene_binding;     // which entity in the live scene?
    bool   is_transient;      // reset transform after sequence ends?
};

// Binding at playback:
Entity actor = GetWorld().FindEntityByName(possessable.scene_binding);
if (!actor) {
    UZ_LOG(Sequencer, Warning, "Possessable binding failed: {}", possessable.scene_binding);
    return;  // graceful skip
}
// Sequence now controls actor's transform, animation, etc.
```

**Spawnables** — actors created by the sequence, despawned at end:

```cpp
struct Spawnable {
    GUID   guid;
    string name;
    string spawn_template;    // which .uzactor to instantiate?
    float  spawn_time;        // when to create
    float  despawn_time;      // when to destroy
    Vec3   spawn_location;
    Quat   spawn_rotation;
};

// At runtime:
if (current_time == spawnable.spawn_time) {
    Entity actor = GetWorld().SpawnActor(spawnable.spawn_template, spawn_location, spawn_rotation);
    spawnable_instances[guid] = actor;
}
```

**Why both?** Possessables keep the player alive and let sequences control them (cutscene camera is a possessable). Spawnables create temporary actors (enemies spawned during cutscene, destroyed at end). No cleanup hassles — spawnables are automatically destroyed when sequence ends.

## Keyframe Interpolation

Transform keyframes support multiple interpolation modes:

```cpp
enum class InterpolationMode {
    Linear,        // v(t) = v0 + (v1 - v0) * (t / duration)
    Cubic,         // cubic bezier via control points (smooth curves)
    Step,          // v(t) = v0 until t >= duration, then v1
    CubicInOut,    // easing function (slow at start/end)
};

struct Keyframe {
    float           time;            // seconds within section
    Vec3            position;        // target value
    Quat            rotation;
    Vec3            scale;
    InterpolationMode interpolation; // how to blend toward next keyframe
    Vec2            cubic_in;        // for cubic bezier (tangent handles)
    Vec2            cubic_out;
};
```

**Cubic bezier** keyframes enable smooth camera moves and character animations without artificial linearity.

## Camera Cuts

Switch between cameras at precise times:

```cpp
struct CameraCutTrack {
    std::vector<CameraCut> cuts;
};

struct CameraCut {
    float  start_time;
    float  blend_duration;     // crossfade time (0 = cut, >0 = blend)
    GUID   target_camera;      // which camera to cut to
};

// At runtime:
if (current_time crosses a camera cut boundary) {
    if (blend_duration > 0) {
        // Crossfade: blend between previous and new camera
        camera_blend_timer = blend_duration;
        camera_blend_src = active_camera;
        camera_blend_dst = new_camera;
    } else {
        // Hard cut: instant camera switch
        active_camera = new_camera;
    }
}
```

Used for cinematic cameras, dramatic reveals, and multi-angle dialog scenes.

## Event Track — Triggering Gameplay

Events fire at specific times, executing Lua callbacks:

```cpp
struct SequenceEvent {
    float    time;
    string   event_name;
    json     payload;  // arbitrary data passed to Lua
};

// At runtime:
if (current_time == event.time) {
    lua_state->Call("OnSequenceEvent", event_name, event.payload);
}

// In Lua:
function OnSequenceEvent(event_name, payload)
    if event_name == "player_spotted_enemy" then
        print("Enemy spotted! Starting combat...")
        -- trigger game behavior here
    end
end
```

Events let sequences coordinate with gameplay — trigger dialogue, spawn enemies, activate physics, run scripts. No hardcoding — all data-driven.

## Master Sequences & Nesting

Large cinematics can be split into **sub-sequences**:

```json
{
  "name": "act_2_intro",
  "duration": 60.0,
  "tracks": [
    {
      "track_name": "intro_sequence_call",
      "track_type": "master_sequence",
      "sections": [
        {
          "start_time": 0.0,
          "duration": 15.0,
          "sequence_asset": "res://cinematics/intro_cutscene.uzsequence"
        }
      ]
    },
    {
      "track_name": "dialog_sequence_call",
      "track_type": "master_sequence",
      "sections": [
        {
          "start_time": 15.0,
          "duration": 20.0,
          "sequence_asset": "res://cinematics/dialog_scene.uzsequence"
        }
      ]
    },
    {
      "track_name": "action_sequence_call",
      "track_type": "master_sequence",
      "sections": [
        {
          "start_time": 35.0,
          "duration": 25.0,
          "sequence_asset": "res://cinematics/action_finale.uzsequence"
        }
      ]
    }
  ]
}
```

Master sequences are just timelines of sub-sequences. Enables modular cinematic design — reuse `intro_cutscene.uzsequence` in multiple chapters.

## Runtime Playback API

```cpp
class SequencerSystem : public WorldSubsystem {
public:
    /// Start playing a sequence from the beginning
    SequencePlaybackHandle Play(const std::string& sequence_path);

    /// Pause the active sequence
    void Pause();

    /// Resume from pause
    void Resume();

    /// Jump to a specific time
    void Seek(float time_seconds);

    /// Set playback speed (1.0 = normal)
    void SetPlayRate(float rate);

    /// Stop and clean up (destroy spawnables)
    void Stop();

    /// Is the sequence currently playing?
    bool IsPlaying() const;

    /// Current playback time (0..duration)
    float GetCurrentTime() const;

    /// Callback when sequence ends
    void OnSequenceFinished(SequencePlaybackHandle handle, const std::function<void()>& callback);
};
```

## Lua Integration

Sequences can be triggered and controlled from Lua:

```lua
-- Lua script in scene
function on_start()
    ctx.sequencer:play("intro_cutscene")
    ctx.sequencer:on_finished(function()
        print("Intro finished, starting gameplay")
        UnlockPlayerControl()
    end)
end

function on_update()
    if input:IsKeyPressed("Escape") then
        ctx.sequencer:stop()  -- allow skipping cutscenes
    end
end
```

The `ctx.sequencer` API provides game code access to timeline playback without lower-level C++ APIs.

## Editor Preview

In the editor, sequences are **previewed in-viewport**:

- Timeline scrubber for frame-accurate navigation
- Keyframe graph editor (similar to UE5's Curves panel)
- Real-time preview of skeletal animations, camera cuts, particles
- Possessable binding UI (drag actors from hierarchy onto track)
- Event payload editor (JSON inspector for event data)

All preview changes are **non-destructive** — scrubbing doesn't modify the sequence file.

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Core timeline, keyframe interpolation, transform/skeletal/audio/event tracks, possessables, Lua callback integration. |
| **Phase 4** | Master sequences, spawnables, particle/fade/subtitle tracks, editor UI, camera cuts with blending, playback speed control. |

## Rules

1. **ALWAYS** use `time` in seconds — never frame-based timing (frame rate independent)
2. **NEVER** store absolute actor pointers in sequences — always use possessable GUIDs
3. **ALWAYS** clean up spawnables when sequence ends — no zombie actors
4. **NEVER** modify binding during playback — binding must happen at sequence start only
5. **ALWAYS** validate interpolation tangents in cubic keyframes — prevent NaN/Inf from malformed data
6. **NEVER** skip events if they're within a frame's delta time — events must fire in playback order
7. **ALWAYS** support sequence skipping (via input or API) — never force players to watch cinematics
8. **NEVER** hardcode possessable names — always use scene binding lookups for flexibility
