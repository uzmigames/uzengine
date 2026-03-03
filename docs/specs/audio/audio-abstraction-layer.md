# Audio Abstraction Layer Spec

> **Status**: Planned (Phase 2 — design in Phase 1)
> **Priority**: P0 — Architectural Blocker
> **Rationale**: XAudio2 is hard-coded throughout the audio system. Prevents unit testing (COM device required), platform expansion, middleware integration (FMOD/Wwise), and CI/headless execution. Must be designed before Phase 2 audio implementation begins.

---

## Overview

The **IAudioEngine** abstraction provides a platform-independent interface between the audio ECS layer and the underlying audio API. The concrete backend is selected at engine startup from `.uzproj`.

Backends:
- `XAudio2AudioEngine` — Windows production backend
- `NullAudioEngine` — headless, CI, unit testing (all no-ops)
- `FMODAudioEngine` — optional middleware (Phase 4)
- `WwiseAudioEngine` — optional middleware (Phase 4)

---

## IAudioEngine Interface

```cpp
// src/engine/audio/i_audio_engine.h

namespace uz {

using AudioSourceHandle = Handle<struct AudioSourceTag>;
using BusHandle         = Handle<struct AudioBusTag>;

struct AudioSourceDesc {
    AssetPath   clip_path;
    std::string bus_name    = "Master";
    float       volume      = 1.0f;
    bool        loop        = false;
    bool        spatial     = false;
    int         priority    = 128;      // 0-255 (higher = less stealable)
};

struct AudioPlayParams {
    float  volume      = 1.0f;
    float  pitch       = 1.0f;
    bool   loop        = false;
    float  fade_in_ms  = 0.0f;
    float  start_pos_s = 0.0f;         // Start offset in seconds
};

struct AudioProfilerStats {
    uint32_t active_voices;            // Currently playing
    uint32_t virtual_voices;           // Playing but inaudible (beyond pool)
    uint32_t max_voices;               // Pool capacity
    float    dsp_cpu_ms;               // CPU time for audio processing
    float    steal_events_per_sec;     // Voice steal rate
    float    bus_peak_levels[8];       // -1.0..1.0 per bus (indexed)
    float    master_peak_level;        // Overall peak
    uint64_t total_samples_played;     // Cumulative counter
    uint32_t error_count;              // Allocation failures this frame
};

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    // ── Source lifecycle ──────────────────────────────────────
    virtual AudioSourceHandle create_source(const AudioSourceDesc& desc)     = 0;
    virtual void              destroy_source(AudioSourceHandle handle)        = 0;

    // ── Playback ──────────────────────────────────────────────
    virtual void play(AudioSourceHandle handle, const AudioPlayParams& p)    = 0;
    virtual void stop(AudioSourceHandle handle)                               = 0;
    virtual void pause(AudioSourceHandle handle)                              = 0;
    virtual void resume(AudioSourceHandle handle)                             = 0;
    virtual bool is_playing(AudioSourceHandle handle) const                  = 0;

    // ── Parameters ────────────────────────────────────────────
    virtual void  set_volume(AudioSourceHandle handle, float volume)         = 0;
    virtual void  set_pitch(AudioSourceHandle handle, float pitch)           = 0;
    virtual float get_volume(AudioSourceHandle handle) const                 = 0;

    // ── Spatial audio ─────────────────────────────────────────
    virtual void set_position(AudioSourceHandle handle, Vec3 pos)            = 0;
    virtual void set_velocity(AudioSourceHandle handle, Vec3 vel)            = 0;   // Doppler
    virtual void set_listener(Vec3 pos, Vec3 forward, Vec3 up)               = 0;

    // ── Bus control ───────────────────────────────────────────
    virtual void  set_bus_volume(const std::string& bus_name, float vol)     = 0;
    virtual float get_bus_volume(const std::string& bus_name) const          = 0;
    virtual void  set_bus_mute(const std::string& bus_name, bool muted)      = 0;

    // ── Profiling ─────────────────────────────────────────────
    virtual AudioProfilerStats get_stats() const                             = 0;

    // ── Config ────────────────────────────────────────────────
    virtual void  set_master_volume(float vol)                               = 0;
    virtual float get_master_volume() const                                  = 0;

    /// Reload bus graph from project config (no voice restart)
    virtual void reload_config(const AudioBusConfig& cfg)                    = 0;

    // ── Lifecycle ─────────────────────────────────────────────
    virtual bool init()                                                       = 0;
    virtual void update(float dt)                                             = 0;
    virtual void shutdown()                                                   = 0;
};

} // namespace uz
```

---

## NullAudioEngine (Headless / Testing)

```cpp
class NullAudioEngine final : public IAudioEngine {
    struct NullSource { AudioSourceDesc desc; bool playing = false; };
    std::unordered_map<uint32_t, NullSource> sources_;
    uint32_t next_id_ = 1;

public:
    AudioSourceHandle create_source(const AudioSourceDesc& desc) override {
        sources_[next_id_] = { desc };
        return make_handle(next_id_++);
    }
    void destroy_source(AudioSourceHandle h) override { sources_.erase(h.id()); }
    void play(AudioSourceHandle h, const AudioPlayParams&) override {
        if (auto it = sources_.find(h.id()); it != sources_.end())
            it->second.playing = true;
    }
    void stop(AudioSourceHandle h) override {
        if (auto it = sources_.find(h.id()); it != sources_.end())
            it->second.playing = false;
    }
    bool is_playing(AudioSourceHandle h) const override {
        auto it = sources_.find(h.id());
        return it != sources_.end() && it->second.playing;
    }
    // All other methods: no-ops returning defaults
    AudioProfilerStats get_stats() const override { return {}; }
    bool init() override { return true; }
    void update(float) override {}
    void shutdown() override {}
    // ... (all pure virtual no-op implementations)
};
```

This enables:
- **Unit tests**: Run without XAudio2 COM initialization
- **CI pipelines**: Headless containers with no audio device
- **Deterministic tests**: No voice stealing, no timing variance

---

## Bus Configuration

### `.uzproj` Format

```toml
[audio]
master_volume = 1.0
backend = "xaudio2"   # xaudio2 | null | fmod | wwise

[[audio.buses]]
name     = "Master"
volume   = 1.0
priority = 10

[[audio.buses]]
name     = "Music"
volume   = 0.8
priority = 5

[[audio.buses]]
name     = "SFX"
volume   = 1.0
priority = 15

[[audio.buses]]
name     = "Voice"
volume   = 1.0
priority = 20

[[audio.buses]]
name     = "UI"
volume   = 0.7
priority = 8

[[audio.buses]]
name     = "Ambient"
volume   = 0.6
priority = 3
```

### AudioBusConfig

```cpp
struct AudioBusInfo {
    std::string name;
    float       volume   = 1.0f;
    bool        muted    = false;
    int         priority = 0;
};

struct AudioBusConfig {
    std::vector<AudioBusInfo> buses;

    const AudioBusInfo* find(const std::string& name) const;
    static AudioBusConfig load(const ProjectConfig& cfg);
};
```

---

## CAudioSource ECS Component

```cpp
struct CAudioSource {
    AssetPath         clip_path;
    std::string       bus_name     = "SFX";     // Resolved at play time
    float             volume       = 1.0f;
    bool              loop         = false;
    bool              spatial      = true;
    int               priority     = 128;
    float             falloff_dist = 20.0f;
    float             min_dist     = 0.5f;

    // Runtime state (not serialized)
    AudioSourceHandle _handle      = {};
    bool              _is_playing  = false;
};
```

---

## Voice Steal Policy

When the 64-voice pool is exhausted, the lowest-score playing source is stolen:

```
score = (priority × 1000)
      + (distance_to_listener / max_dist × 500)
      - (age_ms / 10)
      + (bus_importance × 200)
```

Lower score = higher steal priority. Configurable weights in `.uzproj [audio.steal]`.

---

## AudioSystem (ECS)

```cpp
class AudioSystem final : public ISystem {
    IAudioEngine& engine_;
public:
    void on_configure(SystemInfo& info) override {
        info.name = "AudioSystem";
        info.tick_group = TickGroup::PostPhysics;
        info.parallelizable = false;  // IAudioEngine is not thread-safe
    }

    void execute() override {
        update_listener_from_camera();
        update_source_positions();
        engine_.update(delta_time_);
    }

private:
    void update_source_positions() {
        auto view = reg_.view<CTransform, CAudioSource>();
        for (auto [entity, xform, src] : view.each()) {
            if (src.spatial && src._handle.is_valid()) {
                engine_.set_position(src._handle, xform.world_position());
            }
        }
    }
};
```

---

## IPC Handlers

| Handler | Input | Output |
|---------|-------|--------|
| `audio.get_profiler_stats` | `{}` | `{ active, virtual, dsp_ms, steal_rate, bus_peaks }` |
| `audio.list_buses` | `{}` | `{ buses: [{ name, volume, muted }] }` |
| `audio.set_bus_volume` | `{ bus, volume }` | `{ ok }` |
| `audio.reload_config` | `{}` | `{ ok, bus_count }` |
| `audio.play_oneshot` | `{ path, bus, volume }` | `{ handle }` |

---

## Unit Testing

```cpp
// tests/audio_system_tests.cpp

TEST(AudioSystem, PlayStopsOnDestroy) {
    NullAudioEngine engine;
    AudioSystem sys(engine);

    auto entity = reg.create();
    reg.emplace<CAudioSource>(entity, AudioSourceDesc{ .clip_path = "test.uzaudio" });

    sys.execute();
    EXPECT_TRUE(engine.is_playing(reg.get<CAudioSource>(entity)._handle));

    reg.destroy(entity);
    sys.execute();
    // Handle should be invalid after entity destroyed
}

TEST(AudioSystem, BusVolumeConfigured) {
    NullAudioEngine engine;
    AudioBusConfig cfg;
    cfg.buses = { {"SFX", 0.5f}, {"Music", 0.8f} };
    engine.reload_config(cfg);

    EXPECT_FLOAT_EQ(engine.get_bus_volume("SFX"), 0.5f);
    EXPECT_FLOAT_EQ(engine.get_bus_volume("Music"), 0.8f);
}
```

---

## Implementation Plan

| Week | Deliverable |
|------|-------------|
| 1 | `IAudioEngine` + `NullAudioEngine` + unit tests |
| 2 | `XAudio2AudioEngine` wrapping existing code |
| 3 | `AudioBusConfig` + `.uzproj` parsing |
| 4 | `AudioProfilerStats` + IPC handlers |

---

## Dependencies

- ECS Phase 1 (`CTransform`, `CCamera`)
- IPC protocol (Phase 0)
- Project config parser

## Related Specs

- [Audio System](audio-system.md)
- [Audio Snapshots](audio-snapshots.md)
- [ECS and Scene](../../engine/ecs-and-scene.md)
