# Audio System — miniaudio Backend

> **Status**: Planned (Phase 2)
> **Source**: `engine/audio/miniaudio/`, `engine/audio/audio_engine.h/.cpp`
> **Backend**: miniaudio (public domain audio abstraction layer)
> **Depends on**: Core subsystem, threading, memory allocators

## Purpose

miniaudio is a lightweight, single-header audio library that abstracts away platform-specific audio device details (WASAPI on Windows, CoreAudio on macOS, ALSA/PulseAudio on Linux). The audio engine wraps miniaudio to provide:

- **Device enumeration & selection** — list audio outputs, hot-swap detection
- **Format conversion** — PCM resampling, channel expansion/contraction, bit depth conversion
- **Voice pool management** — pre-allocated voices with recycling, zero allocation on hot path
- **Dedicated audio thread** — all mixing runs off the game thread, never blocks

## Architecture

```
Game Thread                      Audio Thread (miniaudio)
─────────────────────────        ──────────────────────────────
Audio API calls:                 │
  Play()                         │ miniaudio_engine_process_pcm_frames()
  Stop()                         │   ├─ Poll all voice sources
  SetVolume()                    │   ├─ Blend samples to intermediate mix buffer
  SetPitch() ───Commands─────►   │   ├─ Apply per-voice attenuation + effects
  Fade()                         │   ├─ Mix down to master bus
                                 │   ├─ Apply master effects
                                 │   └─ Output to device (WASAPI/CoreAudio/ALSA)
                                 │
                                 └─ Callbacks:
                                    - OnVoiceFinished() → game thread queue
                                    - OnDeviceChange() → reinit if needed
```

The **miniaudio context** (`ma_engine*`) is initialized once at startup and managed by the AudioEngine. All voice allocation happens from a pre-warmed pool — no dynamic allocations during playback.

## Initialization

### Device Selection

```cpp
/// Audio engine — wrapper around miniaudio.
class AudioEngine {
public:
    /// Initialize with default device. Non-blocking. Safe to call multiple times.
    /// Returns false on WASAPI init failure (still falls back to PCM silence).
    bool Initialize();

    /// Enumerate all audio output devices (speaker devices only).
    struct AudioDevice {
        std::string name;           // e.g., "Stereo Mix", "NVIDIA HDA"
        bool is_default = false;    // system default device
        uint32_t sample_rate = 0;   // device capability
        uint32_t channel_count = 0; // 2 for stereo
    };

    std::vector<AudioDevice> EnumerateDevices() const;

    /// Select a specific device. If not found, falls back to default.
    /// Safe to call during playback (triggers device switch after current frame).
    void SetDevice(const std::string& device_name);

    /// Get currently active device name (for UI, save to preferences).
    std::string GetActiveDeviceName() const;
};
```

### Format Negotiation

miniaudio handles format conversion automatically. The device might support:
- Sample rate: 48 kHz (most common on Windows WASAPI), 44.1 kHz, 96 kHz
- Channels: Stereo (2), 5.1 (6), 7.1 (8)
- Bit depth: 16-bit PCM (legacy), 24-bit, 32-bit float

**Engine choice:** Internally, audio engine normalizes all samples to **32-bit float, 48 kHz, stereo**. miniaudio resamples/downmixes as needed for the actual device. Game code never sees device format differences.

```cpp
// In audio_engine.cpp:
ma_engine_config config = ma_engine_config_init();
config.channels = 2;           // Always stereo internally
config.sampleRate = 48000;     // Always 48 kHz internally
config.pLog = &ma_log;
config.allocationCallbacks = uz_alloc_callbacks;  // Use engine allocator

result = ma_engine_init(&config, &m_engine);
if (result != MA_SUCCESS) {
    UZ_LOG(Audio, Error, "Failed to init audio engine: code={}", (int)result);
    // Silent playback mode — voices play but produce no device output
    m_silent_mode = true;
}
```

## Voice Pool

### Pre-Allocation

Voices are allocated upfront (not on-demand) to avoid stutters. Typical pool size: **64 voices** (customizable via config).

```cpp
class AudioVoicePool {
private:
    std::vector<AudioVoice> voices;
    std::queue<size_t> free_voices;  // indices of available voices

public:
    explicit AudioVoicePool(size_t capacity = 64) : voices(capacity) {
        for (size_t i = 0; i < capacity; ++i) {
            free_voices.push(i);
        }
    }

    /// Allocate a voice. Returns nullptr if pool exhausted (concurrency rules apply).
    AudioVoice* AllocateVoice() {
        if (free_voices.empty()) return nullptr;
        size_t idx = free_voices.front();
        free_voices.pop();
        voices[idx].Reset();  // clear state
        return &voices[idx];
    }

    /// Return voice to pool after playback finishes.
    void ReleaseVoice(AudioVoice* voice) {
        size_t idx = voice - voices.data();
        UZ_ENSURE(idx < voices.size(), return);
        free_voices.push(idx);
    }

    /// Query pool state (for UI, stats).
    size_t GetFreeVoiceCount() const { return free_voices.size(); }
    size_t GetTotalVoiceCount() const { return voices.size(); }
};
```

### Voice State Machine

Each voice tracks playback state:

```cpp
enum class VoicePlayState {
    Idle,           // not in use
    Playing,        // actively producing samples
    Paused,         // paused by user
    Stopping,       // stopping with fade-out
    FadingIn,       // during fade-in ramp
    Fading,         // during arbitrary fade (cross-fade, duck)
};

struct AudioVoice {
    VoicePlayState state = VoicePlayState::Idle;
    ResourceHandle<SoundAsset> sound;
    float volume = 1.0f;
    float pitch = 1.0f;
    float playback_position = 0.0f;  // seconds
    float duration = 0.0f;           // seconds (from sound asset)

    // Fade state
    float fade_target = 1.0f;
    float fade_duration = 0.0f;
    float fade_elapsed = 0.0f;
    FadeCurve fade_curve = FadeCurve::Linear;

    // 3D spatialization
    Vec3 position = {0, 0, 0};
    bool follow_entity = false;
    Entity entity;
    float world_distance = 0.0f;
    float spatial_pan = 0.0f;         // -1.0 (left) to 1.0 (right)
    float spatial_volume = 1.0f;      // distance attenuation

    // Bus routing
    StringView bus_name = "SFX";
};
```

### Voice Update Loop

Called every audio frame (typically every 512 samples at 48 kHz ≈ 10.7 ms):

```cpp
void AudioEngine::UpdateVoices(float delta_time) {
    for (auto& voice : voice_pool.voices) {
        if (voice.state == VoicePlayState::Idle) continue;

        // Update fade curves
        if (voice.state == VoicePlayState::FadingIn ||
            voice.state == VoicePlayState::Fading) {
            voice.fade_elapsed += delta_time;
            if (voice.fade_elapsed >= voice.fade_duration) {
                voice.volume = voice.fade_target;
                voice.state = (voice.fade_target == 0.0f) ?
                    VoicePlayState::Idle : VoicePlayState::Playing;
            } else {
                float t = voice.fade_elapsed / voice.fade_duration;
                voice.volume = blend_curve(voice.volume, voice.fade_target, t, voice.fade_curve);
            }
        }

        // Update playback position
        if (voice.state == VoicePlayState::Playing ||
            voice.state == VoicePlayState::FadingIn ||
            voice.state == VoicePlayState::Fading) {
            voice.playback_position += delta_time * voice.pitch;
            if (voice.playback_position >= voice.duration) {
                voice.state = VoicePlayState::Idle;
                voice_pool.ReleaseVoice(&voice);
                if (voice.OnFinished) voice.OnFinished();
            }
        }

        // Update 3D spatialization
        if (voice.follow_entity) {
            if (auto transform = voice.entity.GetComponent<TransformComponent>()) {
                voice.position = transform->GetWorldPosition();
            }
        }
    }
}
```

## Format Conversion

### PCM Resampling

Sound assets are stored in various formats (MP3, OGG, WAV). At import time, they're decoded to canonical **PCM 48 kHz 16-bit stereo** in the `.uzpak` archive.

At runtime, if a sound is used at a different pitch, miniaudio resamples:

```cpp
// Inside voice mix callback:
for (size_t frame = 0; frame < frame_count; ++frame) {
    if (voice.state == VoicePlayState::Playing) {
        // Resample from voice.playback_position
        float sample_index = voice.playback_position * 48000.0f;  // in samples
        int frame_idx = (int)sample_index;
        float frac = sample_index - frame_idx;

        // Linear interpolation (cheap; cubic available for higher quality)
        float s0 = pcm_buffer[frame_idx];
        float s1 = pcm_buffer[frame_idx + 1];
        float sample = s0 * (1.0f - frac) + s1 * frac;

        // Apply pitch scaling
        sample *= voice.pitch;

        // Output
        out_buffer[frame * 2 + 0] = sample;  // left
        out_buffer[frame * 2 + 1] = sample;  // right
    }
}
```

### Channel Expansion

Mono sounds (single-channel) are expanded to stereo by duplicating the channel:

```cpp
// Import-time: 1 channel → 2 channel
for (size_t i = 0; i < mono_sample_count; ++i) {
    stereo_buffer[i * 2 + 0] = mono_buffer[i];
    stereo_buffer[i * 2 + 1] = mono_buffer[i];
}
```

Surround-sound assets (5.1, 7.1) are mixed down to stereo during import.

## Threading Model

### Command Queue (Lock-Free)

Voice control commands (play, stop, fade) are enqueued from the game thread and processed by the audio thread:

```cpp
struct AudioCommand {
    enum Type {
        VoicePlay,
        VoiceStop,
        VoiceSetVolume,
        VoiceSetPitch,
        VoiceFade,
    } type;

    AudioVoice* voice;      // target voice
    union {
        float float_param;
        FadeCurve fade_curve;
    };
};

class LockFreeCmdQueue {
    std::queue<AudioCommand> commands;  // single-producer, single-consumer
    mutable std::atomic<size_t> producer_idx = 0;
    mutable std::atomic<size_t> consumer_idx = 0;

    void Enqueue(const AudioCommand& cmd) {
        // Game thread
        commands.push(cmd);
        producer_idx.store(producer_idx + 1, std::memory_order_release);
    }

    std::optional<AudioCommand> Dequeue() {
        // Audio thread
        if (consumer_idx == producer_idx.load(std::memory_order_acquire)) {
            return std::nullopt;  // queue empty
        }
        AudioCommand cmd = commands.front();
        commands.pop();
        consumer_idx.fetch_add(1, std::memory_order_release);
        return cmd;
    }
};
```

### Audio Thread Lifecycle

```cpp
class AudioSubsystem {
private:
    std::thread audio_thread;
    std::atomic<bool> running = false;

public:
    void Initialize() {
        running = true;
        audio_thread = std::thread(&AudioSubsystem::AudioLoop, this);
        // Set thread affinity to avoid hopping cores (audio quality matters)
        SetThreadAffinity(audio_thread, 1);  // dedicated core
    }

private:
    void AudioLoop() {
        while (running) {
            // Process command queue
            while (auto cmd = command_queue.Dequeue()) {
                ProcessCommand(*cmd);
            }

            // Update all voices, mix, output
            audio_engine.MixAndOutput();

            // Frame-rate independent: wait for next batch
            // miniaudio calls us at fixed intervals (e.g., every 512 samples)
            std::this_thread::sleep_for(std::chrono::milliseconds(11));
        }
    }
};
```

## Error Handling

### WASAPI Failures

WASAPI is Windows-only and can fail if:
- No audio device detected (headless server)
- Device disconnected (USB headphones unplugged)
- Exclusive mode already in use

**Fallback behavior:**

```cpp
if (!Initialize()) {
    UZ_LOG(Audio, Warning, "Audio device init failed, running in silent mode");
    m_silent_mode = true;
}

// During playback:
void AudioEngine::MixAndOutput() {
    // Always mix voices (they're playing, just not outputting)
    MixVoices();

    if (!m_silent_mode) {
        // Send to device
        int result = ma_engine_read_pcm_frames(&m_engine, mix_buffer, frame_count);
        if (result != MA_SUCCESS) {
            UZ_LOG(Audio, Error, "Device output error, entering silent mode");
            m_silent_mode = true;
        }
    }
}
```

Games continue running even if audio is unavailable — UI still plays feedback, music still drives animations (just silently), no user impact.

## Comparison

| Aspect | Unreal (Custom) | Godot (Custom) | FMOD | miniaudio |
|--------|-----------------|-----------------|------|-----------|
| **Code size** | ~50k LOC | ~15k LOC | ~2MB binary (proprietary) | ~2k LOC (single header) |
| **License** | UE EULA | MIT | Proprietary (free for indie) | CC0 (public domain) |
| **Platform coverage** | Windows, Mac, Linux, consoles | Windows, Mac, Linux, Web, iOS | Yes (premium) | Windows, Mac, Linux, Web, iOS, Android |
| **Latency** | WASAPI exclusive (low) | PulseAudio (medium) | Professional (ultra-low) | WASAPI standard (medium) |
| **Device hot-swap** | Yes | No | Yes | Yes |
| **Format conversion** | Custom resampler | libsamplerate | Built-in | Built-in (fast quality) |

## Rules

1. **NEVER** call miniaudio from game thread — all mixing on dedicated audio thread only
2. **ALWAYS** use voice pool allocation — zero allocs during playback (no stutter)
3. **NEVER** exceed pre-allocated voice count — concurrency manager evicts voices silently
4. **ALWAYS** initialize audio engine at startup — silent mode fallback if device unavailable
5. **NEVER** assume device format — always resample/convert to 48 kHz stereo internally
