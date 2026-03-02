# Module: Audio

**Location:** `src/engine/audio/`
**Output:** Part of `uz_engine.lib`
**Language:** C++23
**Role:** XAudio2 real-time audio engine with 3D spatial audio and multi-bus mixing.

---

## Responsibility

The Audio module manages all real-time audio playback, 3D spatial positioning, and mixing. It wraps the Windows XAudio2 API into a simple, game-friendly interface that survives game.dll hot-reload.

Key responsibilities:
- **Voice pool management**: Pre-allocated voice pool with priority-based eviction
- **Bus mixing**: Four SubmixVoices (SFX, Music, UI, Voice) routed to MasteringVoice
- **3D spatial audio**: X3DAudio DSP for listener-relative positioning and attenuation
- **Format import**: WAV, MP3, OGG → PCM16 for playback or streaming
- **Graceful degradation**: If XAudio2 initialization fails, all calls become silent no-ops

---

## Why in uz_engine.lib (Not game.dll)

The audio engine lives in uz_engine.lib for critical stability reasons:

1. **Survivor across game.dll reload**: Audio engine state persists; game.dll unload/reload does not disrupt playback.
2. **Stable XAudio2 lifetime**: IXAudio2 and MasteringVoice are singleton resources that must outlive game.dll.
3. **Driven via GameContext callbacks**: game.dll accesses audio only through `GameContext->audio_play_2d()`, `audio_play_3d()`, etc. — never touches IXAudio2 directly.

This architecture allows game logic to be reloaded without dropping audio.

---

## Source Structure

```
src/engine/audio/
├── audio_engine.h/cpp       — XAudio2 init, MasteringVoice (44.1kHz), bus setup
├── audio_voice_pool.h/cpp   — 64-voice pool, acquire/release, priority eviction
├── audio_bus.h/cpp          — Bus enum, SetVolume() on SubmixVoice
├── audio_listener.h/cpp     — X3DAudio listener position/orientation tracking
└── audio_importer.h/cpp     — WAV/MP3/OGG decode → .uzaudio binary format
```

### Dependency Graph

```
game.dll (on_update)
   └─ GameContext::audio_play_2d(path, vol)
   └─ AudioEngine::play_2d()
   │   ├─ ResourceManager::load<AudioClip>(path)
   │   ├─ VoicePool::acquire()
   │   └─ Voice.SubmitSourceBuffer() → SFX SubmixVoice
   │
game.dll (audio_step)
   └─ GameContext::set_listener_position(pos, forward, up)
   └─ AudioListener::update(pos, ...)
   └─ X3DAudio::Calculate(listener, emitter, flags, ...)
   └─ IXAudio2Voice::SetOutputMatrix() [spatial DSP]
```

---

## Voice Pool Management

**Capacity**: 64 simultaneous voices
**Allocation**: Pre-allocated at engine init; never allocates after startup
**Eviction**: LRU priority-based; playing looping sounds get priority > one-shot SFX

```cpp
struct VoiceSlot {
    IXAudio2SourceVoice* voice;
    bool active;
    int priority = (is_looping ? 10 : 1) * volume;
    float duration;  // in seconds
    AudioBus bus;    // SFX, Music, UI, Voice
};

// Acquire voice
VoiceSlot* slot = voice_pool.acquire(bus, is_looping, volume);
if (!slot) {
    // Pool full — evict lowest priority
    slot = voice_pool.evict_lowest_priority();
}
```

---

## Bus Volume Control

Each bus can have its volume controlled independently via SetVolume on the SubmixVoice:

```cpp
enum class AudioBus {
    SFX = 0,
    Music = 1,
    UI = 2,
    Voice = 3,
};

// In GameContext callback:
audio_engine->set_bus_volume(AudioBus::SFX, 0.8f);
// Immediately reduces all SFX voices by 20%
```

The four buses are mixed into a single MasteringVoice at 44.1 kHz.

---

## 3D Spatial Audio Flow

When game.dll plays a 3D audio source:

```
1. game.dll calls:
   GameContext::audio_play_3d(path, pos, vel, min_dist, max_dist, volume)

2. AudioEngine acquires voice, loads AudioClip, submits buffer

3. Every frame (audio_step):
   - Listener position updates via set_listener_position()
   - AudioListener::update(listener_pos, listener_forward, listener_up)
   - For each playing 3D source:
       - Compute relative position/distance from listener
       - X3DAudio::Calculate(listener, emitter, flags, dsp)
       - IXAudio2Voice::SetOutputMatrix(src, dst, channels, matrix)
       - Matrix encodes: volume attenuation + pan (L/R) based on position

4. Result: Full 3D spatial audio (HRTF-style positioning)
```

---

## Audio System Diagram (ASCII)

```
┌─────────────────────────────────────────────────────────┐
│ game.dll                                                │
│  ├─ on_update(): audio_play_2d("sfx/footstep.wav")     │
│  └─ audio_step(): set_listener_position(player_pos)    │
└──────────────────────┬──────────────────────────────────┘
                       │
        ┌──────────────▼──────────────┐
        │   AudioEngine (uz_engine)   │
        │                             │
        │  ┌──────────────────────┐   │
        │  │   VoicePool (64)     │   │
        │  │ [▮ SFX voice 1]      │   │
        │  │ [▮ Music voice]      │   │
        │  │ [  empty]            │   │
        │  └──────────────────────┘   │
        │           │                 │
        │  ┌────────▼────────────┐    │
        │  │  SubmixVoices       │    │
        │  │ SFX   → Vol 1.0     │    │
        │  │ Music → Vol 0.8     │    │
        │  │ UI    → Vol 1.0     │    │
        │  │ Voice → Vol 0.9     │    │
        │  └────────┬────────────┘    │
        │           │                 │
        │  ┌────────▼────────────┐    │
        │  │ MasteringVoice      │    │
        │  │ 44.1 kHz PCM16      │    │
        │  └────────┬────────────┘    │
        └───────────┼──────────────────┘
                    │
            ┌───────▼──────────┐
            │  Windows Audio    │
            │  (speakers/headphones)
            └──────────────────┘
```

---

## Graceful Degradation

If `XAudio2Create()` fails at engine init (e.g., missing DirectX Runtime), the audio engine silently disables:

```cpp
AudioEngine::AudioEngine() {
    if (FAILED(XAudio2Create(&m_audio2, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        m_enabled = false;
        // All subsequent calls become no-ops
        return;
    }
    // ... create MasteringVoice, SubmixVoices, etc.
}

void AudioEngine::play_2d(const AudioClip& clip, float vol) {
    if (!m_enabled) return;  // Silent no-op
    // ... actual playback
}
```

This ensures games don't crash on systems without audio hardware.

---

## Audio Format Support

The audio importer converts source formats to a standard engine format:

| Format | Input | Output | Streaming |
|--------|-------|--------|-----------|
| WAV | PCM16, 44.1/48 kHz | `.uzaudio` binary | No (< 2MB loaded) |
| MP3 | MPEG-1 Layer III | `.uzaudio` PCM16 | Yes (> 2MB) |
| OGG | Vorbis | `.uzaudio` PCM16 | Yes (> 2MB) |

Small clips (< 2MB) load entirely into memory. Large files stream from disk via `.uzaudio` metadata.

---

## Phase Roadmap

**Phase 2**:
- XAudio2 engine init and shutdown
- 64-voice pool management
- Four-bus mixing (SFX, Music, UI, Voice)
- WAV/MP3/OGG import to `.uzaudio` binary
- GameContext callbacks for play_2d, play_3d, stop_all

**Phase 3**:
- 3D spatial audio via X3DAudio
- Audio streaming for large files
- Reverb/echo zones
- Audio snapshots (preset volume states)

**Phase 4**:
- Procedural audio generation
- Audio middleware integration (FMOD, Wwise)
- Voice chat integration

---

## Related Docs

- [Game Module](../game/README.md) — GameContext audio callbacks
- [Audio System Spec](../../specs/audio-system.md) — Full audio design, Lua API, IPC handlers
- [Engine Lifecycle](../../engine/lifecycle-and-concurrency.md) — Thread safety for audio engine
- [Guides: Audio Integration](../../guides/ipc-mcp.md) — Audio IPC commands for debugging
