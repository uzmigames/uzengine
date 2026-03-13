# Audio System — miniaudio + Submix Hierarchy

> **Status**: Planned (Phase 2–4)
> **Source**: `engine/audio/`, `engine/subsystem/audio_subsystem.h`
> **Backend**: miniaudio (public domain, cross-platform device abstraction) + custom submix mixer
> **References**: UE5 AudioMixer + Submix hierarchy (clean-room), Godot AudioServer bus system

## Purpose

Gameplay-facing audio API — play/stop/fade, 3D spatialization, bus hierarchy, concurrency, attenuation, and audio zones. All audio runs on a dedicated thread — never blocks the game thread.

## Architecture

```
Game Code
    │
    ├── Fire-and-forget: uz::Audio::PlayAt(sound, position)
    │   └── Creates internal voice, auto-destroys on completion
    │
    ├── Persistent: AudioSourceComponent (attached to entity)
    │   └── Play, FadeIn, FadeOut, Stop, SetVolume, SetPitch
    │
    ▼
Audio Engine (dedicated audio thread)
    │
    ├── Voice Pool (max ~64 simultaneous voices)
    │   ├── Active voices → mixing
    │   ├── Virtual voices → tracked silently (resume when slot opens)
    │   └── Concurrency Manager → decides which voices live/die
    │
    ├── Submix Bus Tree
    │   ├── Master
    │   │   ├── SFX → effects: compressor
    │   │   ├── Music → effects: none (or crossfade)
    │   │   ├── Voice → effects: ducking
    │   │   └── Ambient → effects: reverb
    │   └── Each bus: volume, mute, effect chain
    │
    ├── Spatialization (per-voice)
    │   ├── Distance attenuation (inverse square / logarithmic / linear)
    │   ├── HRTF panning (optional plugin)
    │   ├── Air absorption LPF
    │   └── Occlusion (physics raycast → LPF + volume reduction)
    │
    └── miniaudio Device Output
```

## Audio Assets

```cpp
/// Base audio asset. Game code always uses ResourceHandle<SoundAsset>, never raw PCM.
class SoundAsset {
public:
    float  base_volume   = 1.0f;
    float  base_pitch    = 1.0f;
    float  priority      = 50.0f;      // 0-100, for voice eviction
    float  duration      = 0.0f;       // seconds, computed at import

    ResourceHandle<Attenuation> attenuation;    // spatial falloff (null = 2D)
    ResourceHandle<Concurrency> concurrency;    // voice budget rules (null = unlimited)

    StringView default_bus = "SFX";
};

/// Raw audio data — decoded or streaming PCM.
class SoundWave : public SoundAsset {
    enum class DecompressionType { InMemory, Streaming };
    DecompressionType decompression = DecompressionType::InMemory;
};

/// Behavioral audio descriptor — picks from a set of waves with randomization.
/// JSON-authored (.uzsound).
class SoundCue : public SoundAsset {
    enum class Mode { Random, RoundRobin, Shuffle };
    Mode mode = Mode::Random;

    std::vector<ResourceHandle<SoundWave>> waves;

    float volume_variation_min = 1.0f, volume_variation_max = 1.0f;
    float pitch_variation_min  = 1.0f, pitch_variation_max  = 1.0f;
    bool  looping = false;
};
```

### `.uzsound` Format

```json
{
    "format": "uzsound",
    "version": 1,
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
    "attenuation": "content/audio/attenuation_footstep.uzatten",
    "concurrency": "content/audio/concurrency_footstep.uzconcurrency"
}
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Backend | Custom AudioMixer | Custom AudioServer | FMOD-based | **miniaudio (public domain)** |
| Bus system | USoundSubmix hierarchy | Flat indexed buses | AudioMixer groups | **Named bus tree** |
| Concurrency | USoundConcurrency (6 rules) | `max_polyphony` (oldest only) | None built-in | **6 rules + per-owner** |
| Attenuation | FSoundAttenuationSettings | Per-node params | Per-AudioSource | **Shared asset** |
| Occlusion | Physics trace + LPF | None | None | **Physics raycast + LPF** |
| Voice virtualization | Yes | No | No | **Yes** |
| Fire-and-forget | `PlaySoundAtLocation` | `AudioStreamPlayer.play()` | `PlayOneShot()` | **`Audio::PlayAt()`** |
| Music | Manual crossfade | Manual | Manual | **Auto-crossfade + stingers** |
| Audio zones | `AAudioVolume` | None | Reverb zones | **AudioZone** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | AudioSubsystem, AudioSourceComponent, Play2D/PlayAt, bus tree, basic attenuation |
| **Phase 3** | Concurrency, voice virtualization, SoundCue, air absorption, occlusion |
| **Phase 4** | AudioZone, music system (crossfade/stingers/stems), HRTF, reverb effects |
