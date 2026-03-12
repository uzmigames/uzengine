# Audio Components & Playback API

> **Status**: Planned (Phase 2)
> **Source**: `engine/components/audio/`, `engine/audio/`

## Fire-and-Forget API

For one-shot sounds that game code doesn't need to control after triggering.

```cpp
namespace uz::Audio {
    /// Play a 2D sound (non-spatialized). UI clicks, menu sounds.
    void Play2D(ResourceHandle<SoundAsset> sound,
                float volume = 1.0f, float pitch = 1.0f);

    /// Play a 3D sound at a fixed world position. Explosions, impacts.
    /// The sound does NOT follow any entity — it stays at the position.
    void PlayAt(World& world, ResourceHandle<SoundAsset> sound, Vec3 position,
                float volume = 1.0f, float pitch = 1.0f);

    /// Play a 3D sound attached to an entity. Follows the entity's transform.
    void PlayAttached(World& world, ResourceHandle<SoundAsset> sound, Entity entity,
                      float volume = 1.0f, float pitch = 1.0f,
                      bool stop_on_destroy = true);
}
```

**No handle returned.** Game code cannot stop, fade, or mutate these sounds. If you need control, use `AudioSourceComponent`.

## AudioSourceComponent — Persistent Emitter

```cpp
/// 3D positional audio emitter. Attached to an entity, follows its transform.
class AudioSourceComponent : public SceneComponent {
public:
    ResourceHandle<SoundAsset> sound;
    bool auto_play = false;
    bool stop_on_destroy = true;

    // ─── Playback Control ──────────────────────────────────
    void Play(float start_time = 0.0f);
    void Stop();
    void FadeIn(float duration, float target_volume = 1.0f, FadeCurve curve = FadeCurve::Linear);
    void FadeOut(float duration, float target_volume = 0.0f, FadeCurve curve = FadeCurve::Linear);
    void Pause();
    void Resume();

    // ─── Runtime Parameters ────────────────────────────────
    void SetVolume(float linear);
    void SetPitch(float scale);
    void SetLowPassFilter(float cutoff_hz);
    void SetHighPassFilter(float cutoff_hz);

    // ─── Routing ───────────────────────────────────────────
    void SetBus(StringView bus_name);
    void SetBusSend(StringView bus_name, float send_level);

    // ─── State ─────────────────────────────────────────────
    enum class PlayState { Stopped, Playing, Paused, FadingIn, FadingOut };
    PlayState GetPlayState() const;
    bool IsPlaying() const;
    float GetPlaybackPosition() const;

    // ─── Callbacks ─────────────────────────────────────────
    MulticastDelegate<void()> OnFinished;
    MulticastDelegate<void(float)> OnPlaybackPercent;
};

enum class FadeCurve : uint8_t {
    Linear,         // constant rate
    Logarithmic,    // fast start, slow end
    SCurve,         // smooth acceleration/deceleration
    EqualPower,     // perceptually correct crossfade (sin/cos)
};
```

## AudioListenerComponent

```cpp
/// Defines the listener position for 3D spatialization.
/// Usually attached to the camera entity. Only one active listener per world.
class AudioListenerComponent : public SceneComponent {
public:
    bool is_active = false;
};
```

## Submix Bus Tree

Hierarchical mixing with per-bus volume, mute, and effect chain.

```cpp
class AudioBus {
public:
    StringView name;

    void SetVolume(float linear);
    void SetMuted(bool muted);
    float GetVolume() const;
    bool IsMuted() const;

    void AddEffect(AudioEffect* effect);
    void RemoveEffect(AudioEffect* effect);
    void SetParent(AudioBus* new_parent);
};
```

### Default Bus Topology

Configured in `.uzproject`:

```
Master (output to device)
├── SFX                    ← gunshots, footsteps, impacts
│   └── Weapons            ← sub-bus for weapon-specific mixing
├── Music                  ← background music, stingers
├── Voice                  ← dialogue, VO
│   └── Dialogue           ← sub-bus for subtitle-synced VO
└── Ambient                ← environment loops, wind, rain
```

### Usage

```cpp
auto& audio = GetWorld().GetSubsystem<AudioSubsystem>();
audio.GetBus("Music")->SetMuted(true);
audio.GetBus("SFX")->SetVolume(0.5f);

auto* reverb = audio.CreateEffect<ReverbEffect>(ReverbPreset::Cathedral);
audio.GetBus("Ambient")->AddEffect(reverb);
```

## Concurrency — Voice Budget Management

```cpp
enum class ConcurrencyRule : uint8_t {
    PreventNew,
    StopOldest,
    StopFarthest,
    StopFarthestThenOldest,   // default
    StopLowestPriority,
    StopQuietest,
};

struct ConcurrencySettings {
    int32_t         max_count = 8;
    bool            limit_to_owner = false;
    ConcurrencyRule rule = ConcurrencyRule::StopFarthestThenOldest;
    float           retrigger_time = 0.0f;
    float           voice_steal_fade = 0.05f;
};
```

**Voice virtualization:** When the global voice pool (~64 voices) is exhausted, low-priority voices are virtualized — tracked silently. When a slot opens, the sound resumes at the correct playback position.

## Attenuation — 3D Spatial Falloff

```cpp
struct AttenuationSettings {
    bool  enable_distance = true;
    float min_distance = 100.0f;
    float max_distance = 5000.0f;
    enum class FalloffModel { Linear, InverseSquare, Logarithmic };
    FalloffModel falloff = FalloffModel::InverseSquare;

    bool  enable_spatialization = true;
    float non_spatialized_radius = 50.0f;

    bool  enable_air_absorption = false;
    float lpf_min_distance = 500.0f, lpf_max_distance = 5000.0f;
    float lpf_cutoff_near = 20000.0f, lpf_cutoff_far = 2000.0f;

    bool  enable_occlusion = false;
    float occlusion_check_interval = 0.1f;
    float occlusion_lpf_cutoff = 1000.0f;
    float occlusion_volume_scale = 0.3f;
    float occlusion_interp_time = 0.2f;

    bool  enable_reverb_send = false;
    float reverb_send_near = 0.0f, reverb_send_far = 0.6f;
};
```

## Audio Zones & Music

```cpp
/// World-space audio zone. Modifies audio when listener is inside.
class AudioZone : public SceneComponent {
public:
    float exterior_volume = 0.3f;
    float exterior_lpf_cutoff = 1500.0f;
    float interior_volume = 0.5f;
    float interior_lpf_cutoff = 2000.0f;
    float crossfade_time = 0.5f;

    StringView bus_override_target;
    std::vector<AudioEffect*> bus_override_effects;
};

namespace uz::Music {
    void Play(ResourceHandle<SoundAsset> music, float crossfade_duration = 1.0f);
    void Stop(float fade_duration = 1.0f);
    void PlayStinger(ResourceHandle<SoundAsset> stinger, float duck_amount = 0.3f);
    void SetIntensity(float intensity);  // for layered music stems
}
```

## Thread Model

```
Game Thread                          Audio Thread
───────────────────                  ────────────────────────────
AudioSourceComponent.Play()          │
    │ enqueue command ──────────────►│ Voice allocation
                                     │ Concurrency check
AudioSourceComponent.SetVolume()     │ Mix active voices
    │ enqueue command ──────────────►│ Apply attenuation per voice
                                     │ Apply bus effects → master
                                     │ Output to miniaudio device
```

Parameter changes are **command-queued** and applied between mix frames — no locks on the hot path.

## Directory Structure

```
engine/components/audio/
│   ├── audio_source_component.h/.cpp
│   └── audio_listener_component.h

engine/subsystem/
│   └── audio_subsystem.h/.cpp

engine/audio/
├── audio_bus.h/.cpp
├── audio_voice.h/.cpp
├── audio_concurrency.h/.cpp
├── audio_attenuation.h/.cpp
├── audio_zone.h/.cpp
├── audio_music.h/.cpp
├── audio_effect.h
├── sound_asset.h
├── fade_curve.h
└── miniaudio/
    ├── ma_engine_wrapper.cpp
    ├── ma_bus_node.cpp
    └── ma_spatial_processor.cpp
```
