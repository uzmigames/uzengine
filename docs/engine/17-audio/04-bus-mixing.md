# Audio System — Bus Hierarchy & Mixing

> **Status**: Planned (Phase 2)
> **Source**: `engine/audio/audio_bus.h/.cpp`, `engine/audio/audio_effect.h`
> **Depends on**: Audio engine, audio effects (compressor, EQ, reverb)

## Purpose

Audio buses organize sounds into hierarchical channels (like a mixing console). Each bus has:
- **Volume & mute** controls
- **Solo** (mute everything else temporarily)
- **Effect chain** (compressor, EQ, reverb)
- **Sends** (route signal to other buses, e.g., SFX → reverb)
- **Monitoring** (for UI, debugging)

This enables game designers to mix audio without touching code — adjust SFX volume, mute music, add reverb to voice.

## Bus Hierarchy

Default tree structure (configured in `.uzproject`):

```
Master (output to device)
├── SFX (environmental sounds)
│   └── Weapons (gunshots, impacts)
│
├── Music (background music, stingers)
│
├── Voice (dialogue, VO)
│   └── Dialogue (dialogue-specific effects)
│
└── Ambient (wind, rain, environment loops)
```

### Architecture

```cpp
struct AudioBusNode {
    std::string name;
    AudioBusNode* parent = nullptr;
    std::vector<AudioBusNode*> children;

    // Mix state
    float volume = 1.0f;           // 0.0-1.0 (decibels: 20*log10(v))
    float mute = false;
    float solo = false;            // solo this bus (mutes others)

    // Effects chain
    std::vector<AudioEffect*> effects;  // applied in order

    // Send buses (routing)
    struct SendRoute {
        AudioBusNode* target;
        float send_level = 0.5f;   // 0.0-1.0
        bool pre_fader = false;    // pre- or post-fader send
    };
    std::vector<SendRoute> sends;
};
```

## Bus API

```cpp
class AudioBus {
public:
    StringView GetName() const { return name; }

    // ─── Volume & Muting ───────────────────────────────────────────
    void SetVolume(float linear);           // 0.0-1.0
    float GetVolume() const;
    void SetVolumeDb(float db);             // -80..0 dB
    float GetVolumeDb() const;

    void SetMuted(bool muted);
    bool IsMuted() const;

    void SetSolo(bool solo);                // solo this bus (mutes siblings)
    bool IsSolo() const;

    // ─── Hierarchy ──────────────────────────────────────────────────
    AudioBus* GetParent() const { return parent; }
    std::vector<AudioBus*> GetChildren() const { return children; }

    void SetParent(AudioBus* new_parent);   // reparent dynamically
    void AddChild(AudioBus* child);
    void RemoveChild(AudioBus* child);

    // ─── Effects ───────────────────────────────────────────────────
    void AddEffect(AudioEffect* effect, int insert_position = -1);
    void RemoveEffect(AudioEffect* effect);
    std::vector<AudioEffect*> GetEffects() const;

    // ─── Sends (routing to other buses) ─────────────────────────────
    void AddSend(AudioBus* target, float level, bool pre_fader = false);
    void SetSendLevel(AudioBus* target, float level);
    void RemoveSend(AudioBus* target);

    // ─── Monitoring (stats for UI) ──────────────────────────────────
    float GetPeakLevel() const;             // 0.0-1.0 (for VU meter)
    float GetRmsLevel() const;              // 0.0-1.0 (audio loudness)
};
```

## Mixing Process

Each audio frame, voices are mixed down the bus hierarchy:

```cpp
void AudioEngine::MixFrame(float* out_buffer, size_t frame_count) {
    // 1. Mix all voices into per-bus intermediate buffers
    std::map<AudioBus*, float*> bus_buffers;  // allocated once
    for (auto& bus : all_buses) {
        bus_buffers[bus] = ClearBuffer(frame_count);
    }

    // 2. Each voice outputs to its assigned bus
    for (auto& voice : voice_pool) {
        if (voice.state == VoicePlayState::Idle) continue;

        AudioBus* bus = GetBusNamed(voice.bus_name);
        float* bus_buffer = bus_buffers[bus];

        for (size_t frame = 0; frame < frame_count; ++frame) {
            float sample = voice.GetSample(frame);
            sample *= voice.volume;  // voice volume
            bus_buffer[frame] += sample;
        }
    }

    // 3. Process bus tree bottom-up (children → parents)
    MixBusesBottomUp(bus_buffers);

    // 4. Output master bus to device
    std::memcpy(out_buffer, bus_buffers[master_bus], frame_count * sizeof(float) * 2);
}

void AudioEngine::MixBusesBottomUp(std::map<AudioBus*, float*>& bus_buffers) {
    // Visit buses in reverse topological order (children first)
    std::function<void(AudioBus*)> visit = [&](AudioBus* bus) {
        for (auto& child : bus->children) {
            visit(child);
        }

        // Process this bus
        float* buffer = bus_buffers[bus];

        // Apply mute/solo
        if (bus->mute || (AnyOtherBusSolo() && !bus->solo)) {
            ClearBuffer(buffer, frame_count);
            return;
        }

        // Apply volume
        float volume_linear = bus->volume;
        for (size_t frame = 0; frame < frame_count; ++frame) {
            buffer[frame] *= volume_linear;
        }

        // Apply effects in chain
        for (auto& effect : bus->effects) {
            effect->Process(buffer, frame_count);
        }

        // Apply sends (routing to other buses)
        for (auto& send : bus->sends) {
            float send_level = send.pre_fader ? 1.0f : bus->volume;  // pre vs post
            send_level *= send.send_level;

            float* target_buffer = bus_buffers[send.target];
            for (size_t frame = 0; frame < frame_count; ++frame) {
                target_buffer[frame] += buffer[frame] * send_level;
            }
        }

        // Mix up to parent
        if (bus->parent) {
            float* parent_buffer = bus_buffers[bus->parent];
            for (size_t frame = 0; frame < frame_count; ++frame) {
                parent_buffer[frame] += buffer[frame];
            }
        }
    };

    visit(master_bus);
}
```

## Sends — Routing Between Buses

### Pre-Fader vs Post-Fader

**Post-fader send** (default): affected by bus volume knob.
- Use for: reverb sends, parallel compression, effect returns
- If music is quiet, its reverb is also quiet (natural)

**Pre-fader send**: ignores bus volume.
- Use for: cue mixes (monitoring), headphone sends
- Operator hears full signal regardless of playback level

### Configuration

In `.uzproject`:

```json
{
  "audio": {
    "buses": [
      {
        "name": "Master",
        "volume": 1.0,
        "effects": ["master_compressor"],
        "sends": []
      },
      {
        "name": "SFX",
        "parent": "Master",
        "volume": 0.8,
        "sends": [
          {
            "target": "Reverb_Return",
            "level": 0.3,
            "pre_fader": false
          }
        ]
      },
      {
        "name": "Reverb_Return",
        "parent": "Master",
        "volume": 1.0,
        "effects": ["cathedral_reverb"]
      }
    ]
  }
}
```

At runtime:

```cpp
// Get buses
AudioBus* sfx = audio.GetBus("SFX");
AudioBus* reverb_return = audio.GetBus("Reverb_Return");

// Configure send
sfx->AddSend(reverb_return, 0.3f, false);  // post-fader

// Voice plays, outputs to SFX, gets routed to Reverb_Return via send
voice->SetBus("SFX");
```

## DSP Effects Chain

### Base Effect Interface

```cpp
class AudioEffect {
public:
    virtual ~AudioEffect() = default;

    /// Process audio in-place. Called once per audio frame.
    /// buffer: float array, stereo interleaved [L, R, L, R, ...]
    /// frame_count: number of stereo frames
    virtual void Process(float* buffer, size_t frame_count) = 0;

    /// Get effect parameters for UI/debugging
    virtual std::vector<Parameter> GetParameters() const { return {}; }

    /// Set effect parameter by index (live, real-time)
    virtual void SetParameter(int index, float value) {}

    /// Reset effect state (clear delays, filters, etc.)
    virtual void Reset() {}
};
```

### Built-In Effects

#### Compressor

Reduce dynamic range (squeeze loud parts).

```cpp
class CompressorEffect : public AudioEffect {
private:
    float threshold = -20.0f;      // dB above which compression starts
    float ratio = 4.0f;            // 4:1 compression (4dB input → 1dB output)
    float attack = 0.005f;         // seconds to reach target
    float release = 0.1f;          // seconds to recover
    float makeup_gain = 12.0f;     // dB boost to compensate

public:
    void Process(float* buffer, size_t frame_count) override {
        for (size_t frame = 0; frame < frame_count; ++frame) {
            float sample = buffer[frame];

            // Measure input level
            float input_db = 20.0f * std::log10(std::abs(sample) + 1e-6f);

            // Calculate gain reduction
            float gain_reduction_db = 0.0f;
            if (input_db > threshold) {
                float excess = input_db - threshold;
                gain_reduction_db = -excess * (1.0f - 1.0f / ratio);
            }

            // Smooth gain reduction (attack/release)
            float target_gain = DbToLinear(gain_reduction_db + makeup_gain);
            envelope = Lerp(envelope, target_gain,
                           gain_reduction_db < current_gain_db ? attack : release);

            buffer[frame] = sample * envelope;
        }
    }
};
```

#### Low-Pass Filter (EQ)

Remove high frequencies (muffling).

```cpp
class LowPassEffect : public AudioEffect {
private:
    float cutoff_hz = 10000.0f;

public:
    void Process(float* buffer, size_t frame_count) override {
        float fc = cutoff_hz / 48000.0f;  // normalized
        float a = 2.0f * fc / (1.0f + fc);

        for (size_t frame = 0; frame < frame_count; ++frame) {
            filter_state = a * buffer[frame] + (1.0f - a) * filter_state;
            buffer[frame] = filter_state;
        }
    }

    float filter_state = 0.0f;
};
```

#### Reverb (Convolution-Based)

Simulate room acoustics. For now, simple Schroeder reverberator:

```cpp
class ReverbEffect : public AudioEffect {
public:
    enum class Preset { Small, Medium, Large, Cathedral };

    explicit ReverbEffect(Preset preset = Preset::Medium);

private:
    std::vector<float> comb_buffer;    // multiple comb delays at different lengths
    std::vector<float> allpass_buffer; // allpass diffusers
    float wet_level = 0.5f;
    float dry_level = 0.5f;

public:
    void Process(float* buffer, size_t frame_count) override {
        for (size_t frame = 0; frame < frame_count; ++frame) {
            float sample = buffer[frame];

            // Process through parallel combs
            float comb_out = 0.0f;
            for (auto& comb : combs) {
                comb_out += comb.Process(sample);
            }

            // Process through allpass diffusers
            float reverb_out = comb_out;
            for (auto& allpass : allpasses) {
                reverb_out = allpass.Process(reverb_out);
            }

            // Mix dry + wet
            buffer[frame] = sample * dry_level + reverb_out * wet_level;
        }
    }
};
```

## Bus Monitoring (UI/Debugging)

### Peak & RMS Metering

For VU meters in the audio mixer panel:

```cpp
class MeterBuffer {
private:
    float peak_level = 0.0f;
    float rms_accumulator = 0.0f;
    size_t sample_count = 0;

public:
    void Sample(float* buffer, size_t frame_count) {
        for (size_t frame = 0; frame < frame_count; ++frame) {
            float sample = std::abs(buffer[frame]);
            peak_level = std::max(peak_level, sample);
            rms_accumulator += sample * sample;
            sample_count++;
        }
    }

    float GetPeakLevel() const { return peak_level; }
    float GetRmsLevel() const {
        if (sample_count == 0) return 0.0f;
        return std::sqrt(rms_accumulator / sample_count);
    }

    void Reset() {
        peak_level = 0.0f;
        rms_accumulator = 0.0f;
        sample_count = 0;
    }
};
```

Usage in editor IPC:

```cpp
// In audio.get_bus_meters IPC handler:
auto bus = audio.GetBus(bus_name);
MeterData meter = {
    peak_db: 20.0f * std::log10(bus.GetPeakLevel()),
    rms_db: 20.0f * std::log10(bus.GetRmsLevel()),
    clipping: bus.GetPeakLevel() > 1.0f
};
```

## Dynamic Bus Creation

Buses can be created at runtime for dynamic mixing:

```cpp
// Create a temporary bus (e.g., for a specific dialogue line)
AudioBus* dialogue_bus = audio.CreateBus("dialogue_temp", master_bus);
dialogue_bus->AddEffect(new CompressorEffect());
dialogue_bus->AddSend(reverb_return, 0.2f, false);

voice->SetBus("dialogue_temp");
voice->Play();

// After dialogue ends, bus is destroyed
audio.DestroyBus("dialogue_temp");
```

## Rules

1. **ALWAYS** organize buses hierarchically (master at root, at least 3 sub-buses)
2. **NEVER** create circular send routes — infinite loops cause feedback howl
3. **ALWAYS** apply effects post-mute (check mute before processing effects)
4. **NEVER** exceed ~4 serial effects per bus — CPU/latency concern
5. **ALWAYS** meter bus levels for debugging — enable in-game mixer UI
