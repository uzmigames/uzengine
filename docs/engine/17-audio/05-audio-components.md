# Audio System — ECS Components & Playback

> **Status**: Planned (Phase 2)
> **Source**: `engine/components/audio/`, `engine/lua/bindings/audio_binding.cpp`
> **Depends on**: ECS (§10), Audio engine (§17)

## Purpose

Audio in the game is controlled via two main components:
- **AudioSourceComponent** — persistent emitters (loops, controlled playback)
- **AudioListenerComponent** — the viewpoint for 3D spatialization

Additionally, `uz::Audio` namespace provides fire-and-forget APIs for one-shot sounds (explosions, footsteps, impacts).

## AudioSourceComponent

### Definition

```cpp
/// Audio emitter — attaches to entity, follows transform, plays sounds.
class AudioSourceComponent : public SceneComponent {
public:
    // ─── Audio Asset ────────────────────────────────────────────────
    ResourceHandle<SoundAsset> sound;       // the sound to play
    bool auto_play = false;                 // play on entity spawn

    // ─── Playback ───────────────────────────────────────────────────
    void Play(float start_time = 0.0f);
    void Stop();
    void Pause();
    void Resume();

    void FadeIn(float duration, float target_volume = 1.0f,
                FadeCurve curve = FadeCurve::Linear);
    void FadeOut(float duration, float target_volume = 0.0f,
                 FadeCurve curve = FadeCurve::Linear);

    // ─── Parameters ─────────────────────────────────────────────────
    void SetVolume(float linear);           // 0.0-1.0
    float GetVolume() const;

    void SetPitch(float scale);             // 1.0 = normal, 2.0 = double speed
    float GetPitch() const;

    void SetBus(StringView bus_name);       // route to "SFX", "Music", etc.
    StringView GetBus() const;

    // ─── Filters ────────────────────────────────────────────────────
    void SetLowPassFilter(float cutoff_hz);  // 20000 Hz = no filter
    float GetLowPassFilterCutoff() const;

    void SetHighPassFilter(float cutoff_hz); // 20 Hz = no filter
    float GetHighPassFilterCutoff() const;

    // ─── State Query ────────────────────────────────────────────────
    enum class PlayState {
        Stopped,
        Playing,
        Paused,
        FadingIn,
        FadingOut,
    };
    PlayState GetPlayState() const;
    bool IsPlaying() const { return GetPlayState() == PlayState::Playing; }
    float GetDuration() const;
    float GetPlaybackPosition() const;
    float GetPlaybackPercent() const { return GetPlaybackPosition() / GetDuration(); }

    // ─── Callbacks ──────────────────────────────────────────────────
    MulticastDelegate<void()> OnFinished;           // fires when playback ends
    MulticastDelegate<void(float)> OnPlaybackPercent;  // fires every 10% progress
    MulticastDelegate<void()> OnLoopStart;          // fires at loop boundary
};
```

### C++ Usage

```cpp
// In entity initialization
class CharacterController : public Entity {
private:
    AudioSourceComponent* footstep_source = nullptr;

public:
    void OnConstruct() {
        // Attach audio source
        footstep_source = AddComponent<AudioSourceComponent>();
        footstep_source->sound = AssetManager::Load<SoundAsset>("res://audio/footsteps.uzsound");
        footstep_source->SetBus("SFX");

        // Bind callbacks
        footstep_source->OnFinished += [this]() {
            UZ_LOG(Character, Debug, "Footstep finished");
        };
    }

    void OnFootstep() {
        // Fire sound one-shot
        footstep_source->SetVolume(0.8f);
        footstep_source->Play();
    }

    void OnDeath() {
        // Fade out over 2 seconds
        footstep_source->FadeOut(2.0f);
    }
};
```

### One-Shot Playback

For sounds that don't need manual control (impacts, pickups):

```cpp
// Fire-and-forget 2D sound (no 3D positioning)
uz::Audio::Play2D(bullet_impact_sound, 0.9f, 1.0f);

// 3D sound at fixed location (stays at world position, doesn't follow entity)
uz::Audio::PlayAt(world, explosion_sound, {100, 50, -200}, 1.0f, 1.0f);

// 3D sound attached to entity (follows entity's transform)
uz::Audio::PlayAttached(world, sword_slash_sound, player_entity, 0.8f, 1.1f);
```

These don't return a handle — fire and forget. Ideal for game events.

## AudioListenerComponent

### Definition

```cpp
/// Listener position for 3D audio spatialization.
/// Only one active listener per world. Usually on the camera entity.
class AudioListenerComponent : public SceneComponent {
public:
    void SetActive();
    bool IsActive() const;

    // ─── Velocity (for Doppler shift, future feature) ──────────────
    Vec3 GetVelocity() const { return velocity; }
    void SetVelocity(Vec3 v) { velocity = v; }

private:
    Vec3 velocity = {0, 0, 0};
    friend class AudioSubsystem;
};
```

### Setup

In the camera entity or player controller:

```cpp
// Find or create listener
auto listener = player_camera->GetOrAddComponent<AudioListenerComponent>();
listener->SetActive();

// Audio engine now knows the listening position
// All 3D sounds will be spatialized relative to listener
```

The audio engine searches for an active listener. If none exists, 3D spatialization is disabled (all sounds play in stereo, no panning).

## Lua Bindings

### Audio API (ctx.audio)

```lua
-- Play fire-and-forget sounds from Lua
ctx.audio:play2d("res://audio/pickup.wav", volume, pitch)
ctx.audio:play_at(position_vec3, "res://audio/impact.wav", volume, pitch)

-- Access entity audio source
local sfx = entity:get_component("AudioSource")
if sfx then
    sfx:play()
    sfx:fade_in(2.0, 1.0)  -- 2 second fade-in to full volume
    sfx:set_volume(0.8)
    sfx:on_finished(function()
        print("Sound finished!")
    end)
end

-- Query playback state
if sfx:is_playing() then
    print("Progress: " .. (sfx:get_playback_percent() * 100) .. "%")
end

-- Bus routing
sfx:set_bus("Music")

-- Filter effects
sfx:set_lpf_cutoff(8000)  -- muffled
```

### Listener

```lua
-- Set listener on camera entity
local listener = camera:get_or_add_component("AudioListener")
listener:set_active()

-- Now all 3D audio is spatialized relative to camera position
```

## Sound Looping

### One-Shot vs Looping

Sounds can loop indefinitely:

```cpp
// Load a looping ambient sound
ResourceHandle<SoundAsset> wind_sound = AssetManager::Load<SoundAsset>("res://audio/wind.uzsound");

auto wind_source = entity->AddComponent<AudioSourceComponent>();
wind_source->sound = wind_sound;
wind_source->auto_play = true;  // starts on spawn

// If sound asset has looping=true, it loops forever
// Otherwise, plays once
```

In the sound asset file (`.uzsound` JSON):

```json
{
  "format": "uzsound",
  "version": 1,
  "waves": ["content/audio/wind_loop.wav"],
  "looping": true,
  "default_bus": "Ambient",
  "attenuation": "content/audio/attenuation_ambient.uzatten"
}
```

### Loop Events

```cpp
// Get callback when loop restarts
audio_source->OnLoopStart += []() {
    UZ_LOG(Game, Info, "Sound looped");
};

// Stop looping
audio_source->Stop();  // stops immediately
audio_source->FadeOut(1.0f);  // fade before stopping
```

## SoundCue (Behavioral Variation)

For sounds with random variation (footsteps with pitch/volume randomization):

```cpp
// SoundCue picks randomly from waves
ResourceHandle<SoundCue> footstep_cue = AssetManager::Load<SoundCue>("res://audio/footsteps.uzcue");

// Each play picks a random variant
uz::Audio::Play2D(footstep_cue, 0.8f, 1.0f);  // plays one of 3 footstep samples
uz::Audio::Play2D(footstep_cue, 0.8f, 1.0f);  // might play a different one
```

### Cue Definition (`.uzcue` JSON)

```json
{
  "format": "uzcue",
  "version": 1,
  "name": "footsteps_concrete",
  "mode": "random",
  "waves": [
    "content/audio/footstep_concrete_01.wav",
    "content/audio/footstep_concrete_02.wav",
    "content/audio/footstep_concrete_03.wav"
  ],
  "volume_variation": [0.9, 1.1],
  "pitch_variation": [0.95, 1.05],
  "looping": false,
  "default_bus": "SFX",
  "attenuation": "content/audio/attenuation_footstep.uzatten"
}
```

Modes:
- **Random**: pick any wave
- **RoundRobin**: cycle through waves in order (ensures variety)
- **Shuffle**: random permutation, then cycle

## Concurrency Control

When too many voices play simultaneously, concurrency rules decide which ones to stop:

```cpp
// Define concurrency rules (in sound asset)
struct ConcurrencySettings {
    int32_t max_count = 8;              // max simultaneous instances
    bool limit_to_owner = false;        // per-entity limit?
    ConcurrencyRule rule = ConcurrencyRule::StopFarthestThenOldest;
    float retrigger_time = 0.0f;        // prevent rapid re-triggering
    float voice_steal_fade = 0.05f;     // fade-out time when evicted
};

enum class ConcurrencyRule {
    PreventNew,                  // don't play if limit reached
    StopOldest,                  // evict oldest instance
    StopFarthest,                // evict farthest instance (3D only)
    StopFarthestThenOldest,      // farthest, tie-break with oldest
    StopLowestPriority,          // evict lowest priority voice
    StopQuietest,                // evict quietest voice
};
```

Example: footsteps never exceed 4 simultaneous sounds, farthest evicted first:

```json
{
  "format": "uzsound",
  "concurrency": {
    "max_count": 4,
    "rule": "StopFarthestThenOldest",
    "voice_steal_fade": 0.1
  }
}
```

## Fade Curves

Different fade shapes for different use cases:

```cpp
enum class FadeCurve : uint8_t {
    Linear,         // constant rate: y = t
    Logarithmic,    // fast start, slow end: y = log(t + 1)
    SCurve,         // smooth acceleration: y = t*t*(3 - 2*t)
    EqualPower,     // perceptually uniform crossfade: y = sqrt(t)
};

// Example: crossfade music with equal-power curve
old_music->FadeOut(1.0f, 0.0f, FadeCurve::EqualPower);
new_music->FadeIn(1.0f, 1.0f, FadeCurve::EqualPower);
```

## Component Lifecycle

### On Spawn

```cpp
void OnConstruct() override {
    if (auto_play && sound) {
        Play();
    }
}
```

### On Destroy

```cpp
void OnDestruct() override {
    // Stop playback, release voice
    Stop();
}
```

If `stop_on_destroy = true` (default), sound stops immediately. Otherwise, it continues playing.

## Integration Example

Complete character footstep system:

```cpp
class CharacterController : public Entity {
private:
    AudioSourceComponent* footstep_sfx = nullptr;
    float step_timer = 0.0f;
    float step_interval = 0.5f;  // time between steps

    Vec3 last_position = {0, 0, 0};
    float movement_speed = 0.0f;

public:
    void OnConstruct() {
        footstep_sfx = AddComponent<AudioSourceComponent>();
        footstep_sfx->sound = AssetManager::Load<SoundAsset>("res://audio/footsteps.uzcue");
        footstep_sfx->SetBus("SFX");
        footstep_sfx->OnFinished += [this]() {
            // Footstep finished, ready for next
        };
    }

    void OnUpdate(float delta_time) {
        // Calculate movement speed
        Vec3 current_pos = GetComponent<TransformComponent>()->GetPosition();
        movement_speed = (current_pos - last_position).Length() / delta_time;
        last_position = current_pos;

        // Scale footstep interval by movement speed
        if (movement_speed > 1.0f) {  // only play if walking
            step_timer += delta_time;
            float adjusted_interval = step_interval / (movement_speed / 5.0f);  // scale by speed

            if (step_timer >= adjusted_interval) {
                // Volume based on speed
                float vol = Clamp(movement_speed / 10.0f, 0.3f, 1.0f);
                footstep_sfx->SetVolume(vol);

                // Pitch variation based on surface (would come from raycast)
                float pitch = 0.95f + (float)rand() / RAND_MAX * 0.1f;
                footstep_sfx->SetPitch(pitch);

                footstep_sfx->Play();
                step_timer = 0.0f;
            }
        }
    }
};
```

## Rules

1. **ALWAYS** set up an AudioListenerComponent on the camera or player
2. **NEVER** play audio without a sound asset — lazy-load during initialization
3. **ALWAYS** use Lua bindings for game logic audio control (not direct C++)
4. **NEVER** exceed voice pool size — concurrency rules will evict sounds
5. **ALWAYS** set appropriate buses — organize by sound type (SFX, Music, Voice)
