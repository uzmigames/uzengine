# UzEngine — Audio System

> **Audience**: Engine developers and AI agents generating game code
> **Scope**: XAudio2 architecture, audio pipeline, asset format, spatial audio, Lua/C++ API
> **Language**: C++23, HLSL (for effects in Phase 3)

---

## 1. Architecture Overview

The audio system is built on **XAudio2** (Windows native), with a 4-bus mixing hierarchy and voice pooling for deterministic performance.

### 1.1 Design Goals

| Goal | Implementation |
|------|-----------------|
| **No allocations at playback** | Voice pool pre-allocated; zero malloc during play() |
| **Spatial audio support** | X3DAudio DSP per-frame (Phase 3) |
| **Bus-based mixing** | 4 submix buses (SFX, Music, Voice, Ambient) with independent volume control |
| **Hot-reload safe** | Audio state survives game.dll reload |
| **Graceful degradation** | XAudio2 init failure → audio becomes no-op (no crash) |
| **Format agnostic** | Support WAV, MP3, OGG via importers; decode to PCM at cook time |

### 1.2 Module Boundary

```
┌──────────────────────────────────────────────────────┐
│ uz_engine.lib (Stable)                               │
│  - AudioEngine singleton                             │
│  - Voice pool (64 IXAudio2SourceVoice)               │
│  - Submix buses (Master, SFX, Music, Voice, Ambient) │
│  - Voice callback for streaming (Phase 3)            │
│  - No game logic; only low-level XAudio2 wrapper     │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ game.dll (Hot-Reloadable)                            │
│  - AudioManager (play requests, instance lifecycle)  │
│  - CAudioSource component instantiation              │
│  - Spatial audio emitter/listener setup              │
│  - Lua bindings (ctx.audio.*)                        │
└──────────────────────────────────────────────────────┘
```

---

## 2. XAudio2 Voice Hierarchy

### 2.1 Voice Graph

```
Master Voice (output to speaker/headphones)
    ├── Submix Voice: "SFX" (index 1)
    │       ├── Source Voice 0 (playing footstep)
    │       └── Source Voice 1 (playing impact)
    ├── Submix Voice: "Music" (index 2)
    │       └── Source Voice 2 (playing background theme)
    ├── Submix Voice: "Voice" (index 3)
    │       ├── Source Voice 3 (NPC dialogue)
    │       └── Source Voice 4 (UI click)
    └── Submix Voice: "Ambient" (index 4)
            └── Source Voice 5 (wind, rain loop)
```

### 2.2 Bus Configuration

Each submix voice:
- Receives audio from zero or more source voices
- Routes to master voice (or another submix for effects chains in Phase 3)
- Has independent volume/mute control

| Bus | Purpose | Submix Index | Typical Volume | Streaming |
|-----|---------|--------------|-----------------|-----------|
| **SFX** | Sound effects, impacts | 1 | Dynamic (ducking) | Never |
| **Music** | Background music, tracks | 2 | 0.7 (background) | Yes (Phase 3) |
| **Voice** | Dialog, narration, UI | 3 | 1.0 (critical) | No (< 30s) |
| **Ambient** | Environmental (wind, rain) | 4 | 0.5 (subtle) | Yes (Phase 3) |

### 2.3 Initialization

```cpp
// uz_engine.lib/audio/audio_engine.h
class AudioEngine {
public:
    static AudioEngine* create();
    static void destroy(AudioEngine* engine);

    // Playback control
    void play(const AudioClip& clip, uint8_t bus_index, float volume = 1.0f,
              bool spatial = false, const Vec3* position = nullptr);
    void stop_bus(uint8_t bus_index);
    void set_bus_volume(uint8_t bus_index, float volume);

    // Listener/emitter setup (spatial audio)
    void set_listener(const Vec3& position, const Vec3& forward, const Vec3& up);
    X3DAUDIO_DSP_SETTINGS calc_spatial_dsp(const Vec3& emitter_pos);

private:
    IXAudio2* device;
    IXAudio2MasteringVoice* master;
    IXAudio2SubmixVoice* submixes[4];  // SFX, Music, Voice, Ambient
    array<IXAudio2SourceVoice*> voice_pool;
    uint32_t next_voice_index;
};

// uz_engine.lib/audio/audio_engine.cpp
AudioEngine* AudioEngine::create() {
    AudioEngine* engine = new AudioEngine();

    // 1. Initialize COM and XAudio2
    HRESULT hr = XAudio2Create(&engine->device, 0);
    if (FAILED(hr)) {
        log_error("[audio] XAudio2Create failed: 0x%08X", hr);
        engine->device = nullptr;
        return engine;  // Degraded mode: audio operations are no-ops
    }

    // 2. Create mastering voice (output to default speaker)
    hr = engine->device->CreateMasteringVoice(&engine->master);
    if (FAILED(hr)) {
        engine->device->Release();
        engine->device = nullptr;
        return engine;
    }

    // 3. Get audio format from mastering voice
    WAVEFORMATEX format{};
    engine->master->GetVoiceDetails(&format);

    // 4. Create 4 submix buses
    for (uint32_t i = 0; i < 4; ++i) {
        hr = engine->device->CreateSubmixVoice(
            &engine->submixes[i],
            format.nChannels,
            format.nSamplesPerSec,
            0,  // Flags
            i,  // ProcessingStage (for effects ordering, Phase 3)
            nullptr  // SendList (routes to master automatically)
        );
        if (FAILED(hr)) {
            log_warn("[audio] Failed to create submix %d", i);
        }
    }

    // 5. Pre-allocate voice pool (64 voices)
    WAVEFORMATEX voice_format = pcm_16bit_stereo_44khz();
    for (uint32_t i = 0; i < 64; ++i) {
        IXAudio2SourceVoice* voice = nullptr;
        hr = engine->device->CreateSourceVoice(&voice, &voice_format,
            XAUDIO2_VOICE_USEFILTER,
            XAUDIO2_MAX_FREQ_RATIO,
            nullptr);  // No callback (blocking playback)
        if (SUCCEEDED(hr)) {
            engine->voice_pool.push(voice);
        }
    }

    engine->next_voice_index = 0;
    return engine;
}
```

### 2.4 Voice Pool Allocation

```cpp
// Get next available voice (round-robin with eviction)
IXAudio2SourceVoice* AudioEngine::get_voice() {
    if (next_voice_index < voice_pool.size()) {
        return voice_pool[next_voice_index++];
    }

    // All voices busy; evict lowest-priority (oldest) voice
    IXAudio2SourceVoice* voice = voice_pool[0];
    voice->Stop();
    voice->FlushSourceBuffers();

    log_warn("[audio] Voice pool exhausted; evicting oldest voice");
    next_voice_index = 1;
    return voice;
}

// Return voice to pool (no deallocation; reuse)
void AudioEngine::release_voice(IXAudio2SourceVoice* voice) {
    voice->Stop();
    voice->FlushSourceBuffers();
    // Voice stays in pool for next allocation
}
```

---

## 3. Audio Asset Pipeline

### 3.1 .uzaudio Format

Audio clips are stored in a binary format optimized for runtime loading:

```
.uzaudio file layout:

[Header] 16 bytes
├─ magic: "UZAU" (4 bytes)
├─ version: uint16_t (2 bytes) = 1
├─ is_compressed: bool (1 byte)  // 0 = PCM16, 1 = zstd
├─ is_streaming: bool (1 byte)   // 0 = load to RAM, 1 = stream from disk
├─ sample_rate: uint16_t (2 bytes)
├─ channels: uint8_t (1 byte)
└─ padding (2 bytes)

[Metadata] 32 bytes
├─ duration_ms: uint32_t (4 bytes)
├─ num_samples: uint32_t (4 bytes)
├─ data_size: uint32_t (4 bytes)
├─ loop_point_samples: uint32_t (4 bytes)
├─ author_offset: uint32_t (4 bytes)
├─ copyright_offset: uint32_t (4 bytes)
├─ padding (4 bytes)

[Optional Strings] Variable
├─ author (null-terminated)
└─ copyright (null-terminated)

[Audio Data] num_samples * channels * 2 bytes (PCM16 or zstd-compressed)
```

### 3.2 Format Rules

| Constraint | Reason |
|-----------|--------|
| **< 2 MB** | Decode to PCM16 and embed in .uzaudio (instant playback) |
| **≥ 2 MB** | Store compressed reference; stream from disk during playback (Phase 3) |
| **Mono or Stereo** | Mono converted to stereo at cook time (match submix format) |
| **16-bit PCM** | XAudio2 native format; no runtime conversion needed |
| **44.1 or 48 kHz** | Resample at cook time (no quality loss) |

### 3.3 Import Pipeline

```cpp
// uz_engine.lib/assets/audio_import.cpp

Result<AudioClip> import_audio(StringView source_path) {
    // 1. Detect format (WAV, MP3, OGG)
    AudioFormat fmt = detect_format(source_path);

    // 2. Decode to PCM16
    array<int16_t> samples;
    uint32_t sample_rate, channels;

    if (fmt == AUDIO_WAV) {
        // Hand-written or stb_vorbis for .wav
        Result<WavFile> wav = parse_wav(source_path);
        samples = wav->data;
        sample_rate = wav->sample_rate;
        channels = wav->channels;
    } else if (fmt == AUDIO_MP3) {
        // libmpg123
        Result<Mp3File> mp3 = decode_mp3(source_path);
        samples = mp3->data;
        sample_rate = mp3->sample_rate;
        channels = mp3->channels;
    } else if (fmt == AUDIO_OGG) {
        // stb_vorbis
        Result<OggFile> ogg = decode_ogg(source_path);
        samples = ogg->data;
        sample_rate = ogg->sample_rate;
        channels = ogg->channels;
    } else {
        return Error("Unsupported audio format");
    }

    // 3. Normalize channels (mono → stereo)
    if (channels == 1) {
        samples = mono_to_stereo(samples);
        channels = 2;
    }

    // 4. Resample if needed
    if (sample_rate != 44100) {
        samples = resample(samples, sample_rate, 44100);
        sample_rate = 44100;
    }

    // 5. Determine encoding (PCM vs. zstd)
    size_t uncompressed_size = samples.size() * sizeof(int16_t);
    bool compress = uncompressed_size >= 2 * 1024 * 1024;  // 2 MB threshold

    // 6. Write .uzaudio
    return write_uzaudio(source_path, samples, sample_rate, channels, compress);
}

// Parallel example: import_audio("res://audio/footstep.wav") → "res://audio/footstep.uzaudio"
```

---

## 4. Playback API

### 4.1 C++ Interface (game.dll)

```cpp
// game.dll/audio_manager.h
class AudioManager {
public:
    void play(StringId clip_id, uint8_t bus_index = BUS_SFX,
              float volume = 1.0f, float pitch = 1.0f,
              bool loop = false);

    void play_at(StringId clip_id, const Vec3& position,
                 uint8_t bus_index = BUS_SFX,
                 float volume = 1.0f, float min_distance = 1.0f, float max_distance = 100.0f);

    void play_attached(StringId clip_id, entt::entity source_entity,
                       uint8_t bus_index = BUS_SFX, float volume = 1.0f);

    void stop_all(uint8_t bus_index = 0xFF);  // 0xFF = all buses
    void set_bus_volume(uint8_t bus_index, float volume);

    // Listener setup (from CCamera or CAudioListener)
    void set_listener(const Vec3& position, const Vec3& forward, const Vec3& up);
};

// Example usage (Lua bridge in game.dll):
// ctx.audio.play_2d("res://audio/click.uzaudio", volume=0.8, bus="UI")
// ctx.audio.play_at("res://audio/explosion.uzaudio", position, volume=1.0, bus="SFX")
```

### 4.2 ECS Integration

Each CAudioSource entity has a backing audio instance:

```cpp
struct CAudioSource {
    StringId clip_id;
    float volume;
    float pitch;
    float min_distance;
    float max_distance;
    uint8_t bus_index;
    bool loop;
    bool spatial;
    bool is_playing;
    uint32_t voice_id;  // Index into active_voices
    int32_t fade_out_ms;  // -1 = no fade
};

// System update:
void update_audio_sources(entt::registry& world, AudioManager& audio_mgr, float dt) {
    auto view = world.view<CAudioSource, CTransform>();
    for (auto entity : view) {
        auto [src, tf] = view.get<CAudioSource, CTransform>(entity);

        if (src.is_playing && src.spatial) {
            // Update emitter position
            audio_mgr.update_emitter(src.voice_id, tf.position);
        }

        if (src.fade_out_ms > 0) {
            src.fade_out_ms -= (int32_t)(dt * 1000);
            if (src.fade_out_ms <= 0) {
                audio_mgr.stop(src.voice_id);
                src.is_playing = false;
            }
        }
    }
}
```

---

## 5. Spatial Audio (Phase 3)

### 5.1 X3DAudio Integration

```cpp
// uz_engine.lib/audio/spatial_audio.h
class SpatialAudio {
private:
    X3DAUDIO_HANDLE handle;
    X3DAUDIO_LISTENER listener;

public:
    void init(IXAudio2MasteringVoice* master) {
        DWORD channel_mask = 0;
        master->GetChannelMask(&channel_mask);
        X3DAudioInitialize(channel_mask, X3DAUDIO_SPEED_OF_SOUND, handle);
    }

    void set_listener(const Vec3& position, const Vec3& forward, const Vec3& up) {
        listener.Position = to_x3daudio(position);
        listener.OrientFront = to_x3daudio(forward);
        listener.OrientTop = to_x3daudio(up);
        listener.Velocity = { 0, 0, 0 };  // Phase 3: add velocity from frame delta
    }

    X3DAUDIO_DSP_SETTINGS calc_dsp(const Vec3& emitter_pos) {
        X3DAUDIO_EMITTER emitter{};
        emitter.Position = to_x3daudio(emitter_pos);
        emitter.OrientFront = { 0, 0, 1 };
        emitter.OrientTop = { 0, 1, 0 };
        emitter.Velocity = { 0, 0, 0 };
        emitter.ChannelCount = 2;  // Stereo emitter

        X3DAUDIO_DSP_SETTINGS dsp{};
        dsp.SrcChannelCount = 2;
        dsp.DstChannelCount = 2;

        X3DAudioCalculate(handle, &listener, &emitter,
            X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER,
            &dsp);

        return dsp;
    }
};

// Usage per-frame:
void update_spatial_audio(const Vec3& listener_pos, const Vec3& listener_forward,
                          CAudioSource& src, const Vec3& emitter_pos,
                          IXAudio2SourceVoice* voice) {
    spatial_audio.set_listener(listener_pos, listener_forward, Vec3::up());
    X3DAUDIO_DSP_SETTINGS dsp = spatial_audio.calc_dsp(emitter_pos);

    // Apply DSP output matrix to voice
    voice->SetOutputMatrix(master_voice, 2, 2, dsp.pMatrixCoefficients);

    // Optional: apply Doppler pitch shift
    float pitch = dsp.DopplerFactor;  // Usually 0.9-1.1
    voice->SetFrequencyRatio(pitch);
}
```

### 5.2 Distance Attenuation

The output matrix computed by X3DAudio includes:
- **Distance scaling** (inverse square law with min/max distance falloff)
- **Panning** (L/R channels based on azimuth angle)
- **Doppler pitch shift** (emitter velocity relative to listener)

---

## 6. Graceful Degradation

If XAudio2 initialization fails:

```cpp
// All audio operations become no-ops:
void AudioEngine::play(...) {
    if (!device) return;  // Silently skip
    // ... play audio ...
}

void AudioEngine::set_bus_volume(uint8_t bus, float vol) {
    if (!device) return;
    // ...
}

// Game continues unaffected; just plays silently
```

---

## 7. Performance Characteristics

| Aspect | Metric | Notes |
|--------|--------|-------|
| **Voice creation latency** | < 0.5 ms | From pool; no allocation |
| **DSP calculation (spatial)** | < 0.1 ms per emitter | X3DAudio is highly optimized |
| **Memory footprint** | ~20 MB | 64 voices + 4 submixes + decoder state |
| **Max voices** | 64 active | Hardcoded; expandable for shipping |
| **Bus mixing latency** | < 1 ms | Hardware (XAudio2) |

---

## 8. Lua Integration

```lua
-- Lua API via ctx.audio.*

-- Play 2D (non-spatial) sound
ctx.audio.play_2d(clip_id, {
    volume = 0.8,
    pitch = 1.0,
    loop = false,
    bus = "SFX"
})

-- Play 3D (spatial) sound at position
ctx.audio.play_at(clip_id, position, {
    volume = 1.0,
    min_distance = 1.0,
    max_distance = 100.0,
    bus = "SFX"
})

-- Play attached to entity (follows its position)
ctx.audio.play_attached(clip_id, entity, {
    volume = 0.9,
    bus = "Voice"
})

-- Stop all audio on a bus
ctx.audio.stop_bus("Music")

-- Set bus volume (for ducking, muting, etc.)
ctx.audio.set_bus_volume("SFX", 0.7)

-- Set listener position (usually from active camera)
ctx.audio.set_listener(camera_pos, camera_forward, camera_up)
```

---

## 9. Related Documentation

- [Audio System Specification](../specs/audio-system.md) — Full XAudio2 API, IPC handlers, import formats
- [ECS and Scene System](ecs-and-scene.md) — CAudioSource component, entity lifecycle
- [Lifecycle and Concurrency](lifecycle-and-concurrency.md) — Audio thread safety, frame synchronization
- [Scripting System](scripting-system.md) — ctx.audio bindings, Lua playback API

---

## 10. Key Takeaways

1. **XAudio2 is the engine** — Native Windows audio; works reliably and requires minimal dependencies
2. **Pre-allocated voice pool** — No runtime allocation; predictable latency
3. **Bus hierarchy for control** — Independent volume, mute, and effects per bus
4. **.uzaudio is self-contained** — Decoded PCM for < 2 MB, compressed ref for ≥ 2 MB
5. **Spatial audio via X3DAudio** — Automatic distance/panning/Doppler in Phase 3
6. **ECS components drive playback** — CAudioSource lifecycle manages voice allocation
7. **Graceful degradation** — Missing XAudio2 → silent playback, no crash
