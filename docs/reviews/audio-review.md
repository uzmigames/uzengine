# Audio System — AAA Gap Analysis

**Author**: Audio Engineer
**Date**: 2026-03-02
**Spec reviewed**: `docs/specs/audio-system.md` (Phase 2 planned)
**Scope**: Critical gaps between UzEngine audio-system.md and AAA-grade audio engine requirements as seen in Wwise 2023, FMOD Studio 2.03, and Unreal Audio Engine 5.x

---

## Executive Summary

The current `audio-system.md` spec describes a functional but primitive audio foundation. It covers the minimum viable product: XAudio2 initialization, a 4-bus submix graph, a 64-voice pool, WAV/MP3/OGG import, and a basic Lua API. This is adequate for a tech demo, but it is roughly at the level of a 2004 indie engine.

A game engine targeting mid-tier or AAA production needs audio systems of a fundamentally different complexity class. The gaps identified below are not polish — many of them are architectural, meaning if they are not planned for now, retrofitting them later will require breaking changes to the core audio API, the Lua bindings, the ECS schema, and the `.uzscene` file format.

The most critical architectural gaps are:

1. No data-driven audio event system (everything is hardcoded asset paths)
2. No adaptive music system (vertical layering or horizontal re-sequencing)
3. No DSP effect chain per bus
4. No occlusion/obstruction model
5. No virtual voice system (no graceful degradation beyond the pool limit)
6. No audio snapshot system
7. No dialogue / voice acting pipeline

Each gap is described below with the technical detail needed to inform planning.

---

## Gap 1 — Audio Event System (Data-Driven)

**Criticality**: BLOCKER for any production game

### Why it is critical

Every shipping game engine — Wwise, FMOD, Unreal — separates **what sound to play** from **when and how to trigger it**. In UzEngine today, a Lua script does this:

```lua
ctx.audio:play_2d("res://audio/footstep_concrete.wav", 0.8)
```

This is hardcoded: one clip, one path, one volume. In a real production:

- A footstep is not a single WAV. It is an **event** (`footstep`) that randomly selects from 8 concrete variants, applies pitch randomization ±15%, is tagged as `SFX/Character`, has a cooldown of 80ms, and fades out if the voice pool is full.
- The designer changes it without touching code. The audio director tunes it in a dedicated tool (Wwise/FMOD authoring app). No programmer is in the loop.
- If the concrete changes to wood mid-level (dynamic surface detection), the event switches to the wood variant pool automatically.

Without an event system, every audio decision is hardcoded into Lua scripts. Audio directors cannot work independently. Sound variations require code changes. No data-driven iteration is possible.

### Technical description

An audio event system requires:

**AudioEvent asset** (`.uzevent` — text/INI format, AI-friendly):

```ini
[audio_event name="footstep_concrete"]
  mode = random_no_repeat       ; random_no_repeat | weighted_random | sequential
  cooldown_ms = 80
  bus = SFX

  [[clip]]
    path = "res://audio/fx/footstep_concrete_01.wav"
    weight = 1.0
    pitch_min = 0.9
    pitch_max = 1.1
    volume_min = 0.75
    volume_max = 0.85

  [[clip]]
    path = "res://audio/fx/footstep_concrete_02.wav"
    weight = 1.0
    pitch_min = 0.88
    pitch_max = 1.12
    volume_min = 0.78
    volume_max = 0.82
```

**EventManager** (C++):
- Loads `.uzevent` files into an event registry (StringId-keyed)
- On `trigger(event_id)`: selects clip by mode, applies pitch/volume random range, acquires voice from pool, submits buffer
- Tracks cooldowns per event per emitter (not globally)
- Supports parametric events: `trigger("footstep", {surface="wood", speed=1.4})`

**Lua API change** (backward-compatible extension):

```lua
-- Old API (still works, deprecated):
ctx.audio:play_2d("res://audio/click.wav", 0.8)

-- New event API:
ctx.audio:trigger("ui_click")
ctx.audio:trigger("footstep", { surface = "concrete", speed = self.move_speed })
```

**CAudioSource extension**:

```cpp
struct CAudioSource {
    // existing fields unchanged
    StringId  event_id;   // alternative to clip_id; if set, triggers event on play
};
```

### Impact if absent

Every Lua script that plays audio is a hardcoded asset path. Changing any sound requires finding all Lua scripts that reference it. No random variation. No designer iteration without programmer involvement. Audio quality cap at prototype level.

### Complexity: L
### Dependencies: Asset pipeline (`.uzevent` import), AudioEngine (event registry), Lua bindings

---

## Gap 2 — Adaptive Music System

**Criticality**: HIGH for any story or action game

### Why it is critical

Unreal Engine 5's Quartz system, FMOD's Timeline, and Wwise's Music Switcher/Music Playlist exist because static music loops destroy immersion. In every AAA game:

- Combat music starts when an enemy detects the player, but it cannot abruptly cut in — it must wait for the next musical bar boundary (`quantized transition`)
- Exploration music has an intensity layer (strings) that fades in when the player approaches a point of interest
- Boss fight music has three stems (low/mid/high intensity) that blend based on boss HP
- When the player dies, the music drops to a single piano stem and slows down (tempo automation)

The current spec has a single `Music` bus with volume control. There is no concept of musical time, stems, transitions, or reactive blending.

### Technical description — Vertical Layering (Stems)

**Music Stem Track** (`.uzmusictrack` — INI):

```ini
[music_track name="boss_theme"]
  bpm = 120
  time_signature = 4/4
  beat_callback = true         ; fires Lua on_beat / on_bar events

  [[stem]]
    id = "base"
    path = "res://audio/music/boss_base.wav"
    volume = 1.0
    always_on = true

  [[stem]]
    id = "percussion"
    path = "res://audio/music/boss_drums.wav"
    volume = 0.0
    fade_time_ms = 500

  [[stem]]
    id = "intensity"
    path = "res://audio/music/boss_strings.wav"
    volume = 0.0
    fade_time_ms = 1200
```

All stems are decoded into memory and submitted as looping XAudio2 source voices synchronized to the same sample offset. Stem volume is controlled by game state via Lua:

```lua
-- In boss fight script:
function M:on_update(ctx, dt)
    local hp_ratio = self.boss_hp / self.boss_max_hp
    ctx.audio:set_stem_volume("boss_theme", "percussion", 1.0 - hp_ratio)
    ctx.audio:set_stem_volume("boss_theme", "intensity", 1.0 - hp_ratio)
end
```

**Quantized Transitions** (Horizontal Re-sequencing):

```ini
[music_playlist name="exploration"]
  [[segment]]
    id = "peaceful"
    path = "res://audio/music/explore_peaceful.wav"
    loop = true
    transition_quantize = "bar"    ; bar | beat | immediate

  [[segment]]
    id = "tense"
    path = "res://audio/music/explore_tense.wav"
    loop = true
    transition_quantize = "bar"
    entry_point_ms = 0
```

```lua
-- Switch to tense segment on enemy proximity, quantized to next bar:
ctx.audio:music_switch_segment("exploration", "tense", { quantize = "bar" })
```

The `MusicClock` subsystem tracks the current BPM, current beat, and current bar. Transition requests are queued and executed at the next quantized boundary by an XAudio2 callback scheduled via `IXAudio2SourceVoice::SetSourceSampleRate` and `OnBufferEnd` callback on the current segment voice.

### Technical description — Beat/Bar Callbacks

```lua
-- Lua script receives musical time events:
function M:on_beat(ctx, beat_number, bar_number)
    -- Spawn a visual particle on every beat
    self:spawn_beat_particle(ctx)
end
```

This requires the `MusicClock` to call registered Lua callbacks on the audio thread (or queue to game thread) at precise sample positions using `XAUDIO2_BUFFER.pContext` and the `OnBufferEnd` voice callback.

### Impact if absent

Music must be pre-authored as single static loops. Dynamic combat/exploration music transitions are impossible. Boss fight intensity is expressed only as volume fade of a single loop. No synchronization between music and gameplay events. Any game with more than one music track suffers jarring crossfades.

### Complexity: XL
### Dependencies: Gap 1 (Event System), audio streaming (Phase 3), XAudio2 voice callback thread

---

## Gap 3 — DSP Effect Chains Per Bus

**Criticality**: HIGH — sound quality and mix polish

### Why it is critical

In every shipping game:

- The `Music` bus has a compressor, an EQ (HPF at 80Hz to remove rumble), and a gentle limiter
- The `SFX` bus has a short reverb tail for outdoor scenes
- The `Voice` bus has a noise gate, a de-esser, and a maximizer
- The `Master` bus has a multiband compressor and a brickwall limiter to prevent clipping

The current spec has zero DSP processing on any bus. XAudio2 provides the `XAPO` (Cross-platform Audio Processing Object) interface natively for custom DSP chains. Not using it means the final mix is raw, unprocessed, and potentially clipping.

### Technical description

**XAPO effect chain** (XAudio2 native):

```cpp
// Attach a reverb + EQ chain to the SFX submix voice
IUnknown* reverb_effect = nullptr;
XAudio2CreateReverb(&reverb_effect);

XAUDIO2_EFFECT_DESCRIPTOR fx_chain[] = {
    { reverb_effect, TRUE, 2 },
};

XAUDIO2_EFFECT_CHAIN chain = { 1, fx_chain };
m_buses[BUS_SFX]->SetEffectChain(&chain);

// Configure reverb parameters
XAUDIO2FX_REVERB_PARAMETERS params = {};
params.Density         = 100.0f;
params.RoomSize        = 100.0f;
params.RoomFilterFreq  = 5000.0f;
params.DecayTime       = 1.5f;
XAudio2SetEffectParameters(m_buses[BUS_SFX], 0, &params, sizeof(params));
```

**Per-bus effect manifest** in `.uzproj`:

```toml
[audio.buses.sfx]
volume = 0.8
effects = [
    { type = "reverb",    preset = "small_room" },
    { type = "eq",        low_cut_hz = 100 },
    { type = "compressor", threshold_db = -12, ratio = 4.0 },
]

[audio.buses.music]
volume = 0.8
effects = [
    { type = "eq",        low_cut_hz = 80 },
    { type = "limiter",   ceiling_db = -0.3 },
]

[audio.buses.master]
volume = 1.0
effects = [
    { type = "multiband_compressor" },
    { type = "limiter", ceiling_db = -0.1 },
]
```

Required XAPO implementations (or use `XAudio2FX` built-in where available):
- **Reverb**: `XAudio2CreateReverb()` — built-in
- **EQ**: Custom XAPO — 4-band parametric EQ
- **Compressor**: Custom XAPO — feed-forward RMS compressor
- **Limiter**: Custom XAPO — brickwall peak limiter
- **Chorus/Flanger**: Custom XAPO — modulated delay line
- **Distortion**: Custom XAPO — waveshaper

**Lua runtime control**:

```lua
-- Dynamically adjust reverb wetness based on room type:
ctx.audio:set_bus_effect_param("SFX", "reverb", "wet_mix", 0.4)
ctx.audio:set_bus_effect_param("SFX", "eq", "low_cut_hz", 200)
```

**Snapshot integration** (see Gap 6): DSP parameters are part of audio snapshots, not just volumes.

### Impact if absent

The final audio mix sounds like a raw digital recording with no processing. No reverb tail on SFX. No protection against clipping on the master bus. No EQ to manage frequency buildup. Professional audio directors will reject the engine for any serious project.

### Complexity: L (reverb via XAudio2FX built-in) to XL (full custom XAPO suite)
### Dependencies: Audio engine core, XAudio2 effect chain API, `.uzproj` bus config

---

## Gap 4 — Audio Occlusion and Obstruction

**Criticality**: HIGH for any 3D game

### Why it is critical

In Unreal Engine 5, audio occlusion is a first-class citizen. When a wall is between the player and a sound source, the sound is not silenced — it is filtered (high frequencies attenuated, low frequencies pass through). This is called **obstruction** (partial) and **occlusion** (full). Without it:

- A gunshot heard through a concrete wall sounds identical to one in the open
- A conversation in the next room sounds like it is in the same room
- The player cannot spatially reason about sound sources

The current spec has no occlusion model. X3DAudio provides distance attenuation but no geometry-aware filtering.

### Technical description

**Raycast-based occlusion** (integrated with Jolt Physics, Phase 3+):

```cpp
// AudioOcclusionSystem::update_per_frame()
// For each spatial CAudioSource that is playing:

struct OcclusionState {
    float occlusion;      // 0.0 = open, 1.0 = fully occluded
    float obstruction;    // 0.0 = open, 1.0 = fully obstructed
};

// Single ray from listener to source:
PhysicsRayResult ray = physics.raycast(listener_pos, source_pos, LAYER_SOLID_GEOMETRY);

if (!ray.hit) {
    state.occlusion    = 0.0f;   // line of sight clear
    state.obstruction  = 0.0f;
} else {
    // Estimate occlusion from number of surfaces hit (sphere cast):
    int hit_count = physics.sphere_cast_all(listener_pos, source_pos, 0.1f).size();
    state.occlusion = clamp(hit_count * 0.4f, 0.0f, 1.0f);
    state.obstruction = clamp(hit_count * 0.2f, 0.0f, 1.0f);
}

// Apply to source voice:
// Obstruction = high-frequency cut (LPF via XAPO)
voice->set_filter({ XAUDIO2_LOWPASS_FILTER,
    lerp(1.0f, 0.3f, state.obstruction),   // cutoff (1.0 = no cut)
    1.0f                                    // Q
});

// Occlusion = volume and send level reduction:
float occluded_volume = source_volume * (1.0f - state.occlusion * 0.8f);
voice->SetVolume(occluded_volume);
```

**Smooth interpolation**: Occlusion state is not applied instantly. It is lerped over 100–200ms to avoid audible pops when crossing geometry boundaries.

**ECS extension**:

```cpp
struct CAudioSource {
    // existing fields...
    bool    occlusion_enabled;         // default true for spatial sources
    uint8_t occlusion_ray_count;       // 1=simple, 5=accurate (sphere cast), default 1
    float   occlusion_update_interval; // seconds between raycasts (default 0.05)
    // runtime:
    float   _current_occlusion;
    float   _current_obstruction;
};
```

**Performance**: Raycasts are expensive. The system must:
- Only raycast spatial sources within `max_dist`
- Stagger raycasts across frames (not all sources every frame)
- Use the `occlusion_update_interval` to skip sources that are stable
- Skip sources with `occlusion_enabled = false` (music, UI SFX)

### Impact if absent

All spatial audio sounds like it is in open air regardless of geometry. Players cannot use audio to locate enemies behind walls by their muffled sound. A massive realism and immersion gap that users of Unreal or Unity notice immediately.

### Complexity: M (single-ray) to L (multi-ray + smooth interpolation)
### Dependencies: 3D spatial audio (Phase 3), Jolt Physics (Phase 3), Gap 3 (DSP chain for LPF)

---

## Gap 5 — Virtual Voices (Beyond Pool Limit)

**Criticality**: HIGH — correctness under heavy load

### Why it is critical

The spec defines a 64-voice pool with "evict lowest-priority playing voice" on overflow. This is the minimum viable strategy. Wwise calls the more sophisticated version **virtual voices**. The problem:

In a combat scene with 20 enemies, explosion effects, bullet impacts, footsteps, ambient loops, and music, it is trivial to need 100+ simultaneous voices. A naive pool with eviction will:
- Kill the ambient music loop to play a distant impact SFX (wrong priority)
- Produce audible pops when voices are evicted mid-playback
- Give no feedback to the designer about how close they are to the voice limit

Wwise's virtual voice system **does not evict** — it **virtualizes**. A virtual voice continues tracking playback position on the CPU without consuming an XAudio2 hardware voice. When a real voice becomes available (a higher-priority sound finishes), the virtual voice **resumes from the correct playback position**, creating seamless behavior from the listener's perspective.

### Technical description

**VirtualVoice struct**:

```cpp
struct VirtualVoice {
    StringId   clip_id;
    uint64_t   virtual_start_sample;    // sample position when virtualized
    float      volume;
    float      pitch;
    uint8_t    priority;                // 0=background, 255=cinematic
    uint8_t    bus_index;
    bool       loop;
    bool       spatial;
    Vec3       position;                // for re-acquisition priority re-check
    float      elapsed_sec;            // CPU-tracked playback time
    bool       expired;                // computed from clip length
};
```

**VoicePool enhanced**:

```cpp
// On acquire() when pool is full:
// 1. Compute priority of requesting voice
// 2. Find lowest-priority active voice
// 3. If requesting priority > lowest active priority:
//    - Push lowest-priority active voice into virtual_voices[] queue
//    - Capture its current sample position via voice->GetState().SamplesPlayed
//    - Stop and reclaim its XAudio2 voice
//    - Give the reclaimed voice to the new request
// 4. If requesting priority <= lowest active priority:
//    - Create VirtualVoice (no XAudio2 voice consumed)

// Each frame update:
for (VirtualVoice& vv : virtual_voices) {
    vv.elapsed_sec += dt;
    // Check if a real voice slot is now free:
    if (real_voice_available()) {
        uint64_t resume_sample = vv.virtual_start_sample +
            (uint64_t)(vv.elapsed_sec * clip.sample_rate * vv.pitch);
        if (resume_sample < clip.frame_count || vv.loop) {
            // Re-acquire real voice, seek to resume_sample, continue
            resume_virtual_voice(vv);
        } else {
            vv.expired = true;
        }
    }
}
```

**Priority system**:

```cpp
enum AudioPriority : uint8_t {
    PRIORITY_AMBIENT    = 10,
    PRIORITY_SFX_FAR    = 50,
    PRIORITY_SFX_NEAR   = 100,
    PRIORITY_DIALOGUE   = 180,
    PRIORITY_CINEMATIC  = 255,
};
```

**Distance-based priority scaling**: A nearby explosion is higher priority than a distant footstep even if both have the same declared priority. The system adjusts effective priority as `declared_priority * (1.0 - normalized_distance)`.

**CAudioSource extension**:

```cpp
struct CAudioSource {
    // existing...
    uint8_t priority;              // AudioPriority (default PRIORITY_SFX_NEAR)
    bool    can_virtualize;        // default true; set false for critical one-shots
};
```

**Profiler integration** (see Gap 18): Real-time display of real vs. virtual voice count.

### Impact if absent

Dense audio scenes cause random popping and dropped sounds. Priority inversions where music is killed for distant SFX. No developer visibility into voice budget pressure. Audio degrades non-deterministically under load.

### Complexity: M
### Dependencies: Voice pool (Phase 2), audio streaming (Phase 3 for seek)

---

## Gap 6 — Audio Snapshots (Mixer States)

**Criticality**: HIGH for any game with distinct gameplay states

### Why it is critical

Unreal Engine's `USoundMix` / `FAudioDevice::PushSoundMixModifier` and Wwise's **States** allow the audio system to transition between pre-defined mixer configurations. Examples:

- **Normal gameplay**: SFX 0.8, Music 0.7, reverb = "outdoor"
- **Underwater**: SFX 0.4 + heavy LPF, Music pitch down 5%, reverb = "underwater_body"
- **Paused/menu**: Music 0.3, SFX 0.0, Voice 1.0 (menu VO)
- **Cinematic**: SFX 0.5, Music 0.9, Voice 1.0, ducking = "dialogue_duck"
- **Car interior**: SFX LPF + high attenuation, Music different EQ, engine_rumble bus active

The current spec mentions "Audio snapshots (Pre-defined audio presets)" only as a Phase 3 future item with zero technical detail.

### Technical description

**AudioSnapshot asset** (`.uzsnapshot` — INI):

```ini
[audio_snapshot name="underwater"]
  transition_ms = 500
  transition_curve = ease_in_out

  [bus.master]
    volume = 0.9

  [bus.sfx]
    volume = 0.4
    effect.eq.low_cut_hz = 0          ; no HPF underwater
    effect.eq.high_cut_hz = 800       ; heavy LPF
    effect.reverb.preset = "underwater_body"
    effect.reverb.wet_mix = 0.6

  [bus.music]
    volume = 0.6
    effect.pitch_shift = -0.05        ; slight pitch down

  [bus.voice]
    volume = 0.8
    effect.eq.high_cut_hz = 1200      ; muffled dialogue
```

**SnapshotManager** (C++):

```cpp
class SnapshotManager {
public:
    // Push a snapshot (can stack multiple)
    void push(StringId snapshot_id, float blend_weight = 1.0f);

    // Pop last pushed snapshot (transition back)
    void pop(StringId snapshot_id);

    // Blended stack: multiple snapshots interpolated by weight
    // e.g., push("combat", 0.8) blended with push("rainy", 0.3)

    void update(float dt);  // per-frame interpolation
};
```

**Snapshot stacking**: Unreal allows multiple sound mixes to be active simultaneously with blend weights. This enables "combat + raining + low_health" simultaneously, each contributing to the final bus parameters via weighted blend.

**Lua API**:

```lua
-- Push/pop pattern (like C++ RAII):
ctx.audio:push_snapshot("underwater")
ctx.audio:pop_snapshot("underwater")

-- Blend weight (for gradual transitions):
ctx.audio:push_snapshot("combat", { weight = 0.7 })
```

### Impact if absent

Transitioning between game states requires ad-hoc Lua scripts that manually set bus volumes. No coordination between systems (entering water, low health effects, cutscene). No cross-fade between states. Each new gameplay state requires a programmer, not a sound designer, to wire up the audio changes.

### Complexity: M
### Dependencies: Gap 3 (DSP chains), Gap 1 (event system), audio bus system

---

## Gap 7 — Reverb Zones (Environmental Audio)

**Criticality**: HIGH for immersive 3D games

### Why it is critical

Environmental audio is one of the most impactful audio features for immersion. Walking from an open field into a cave should have the audio character change — more reverb, more low-frequency resonance, narrower stereo image. This is achieved through **reverb zones**: 3D volumes in the scene that define the acoustic properties of a space.

The spec mentions reverb zones in Phase 3 as "Audio effects: Reverb zones, echo, filters" with no technical detail.

Unreal Engine uses `AAudioVolume` with `FAudioEffectsSettings`. FMOD Studio uses `FMOD::Geometry` + Environment zones. Wwise uses **Rooms and Portals** (a more advanced model).

### Technical description

**CReverbZone ECS component** (new):

```cpp
struct CReverbZone {
    // Volume shape (AABB or sphere, checked against listener position)
    enum class Shape : uint8_t { AABB, Sphere } shape;
    Vec3  half_extents;      // AABB half-extents (if shape=AABB)
    float radius;            // sphere radius (if shape=Sphere)

    // Reverb parameters
    float decay_time_sec;    // 0.1 (anechoic) to 20.0 (cathedral)
    float density;           // 0.0 to 1.0
    float diffusion;         // 0.0 to 1.0
    float wet_mix;           // 0.0 to 1.0 (how much reverb is applied)
    float room_hf_level;     // high frequency room attenuation (dB)
    float air_absorption_hf; // HF attenuation per meter of air distance

    // Transition
    float blend_distance;    // meters of crossfade into zone (default 2.0)

    // Presets (convenience, overrides individual params)
    StringId preset_id;      // "cave", "church", "bathroom", "outdoor", etc.
};
```

**ReverbZoneSystem** (per-frame):

```cpp
// For each frame, find which reverb zones the listener is in (can be multiple):
void ReverbZoneSystem::update(Vec3 listener_pos) {
    float total_weight = 0.0f;
    ReverbParams blended = {};

    for (auto& [entity, zone, transform] : scene.view<CReverbZone, CTransform>()) {
        float dist = zone.distance_to_border(listener_pos, transform);
        if (dist < zone.blend_distance) {
            float weight = 1.0f - (dist / zone.blend_distance);
            blended = blend(blended, zone.to_params(), weight);
            total_weight += weight;
        }
    }

    // Apply blended reverb to SFX bus:
    audio_engine.set_bus_reverb(BUS_SFX, blended, total_weight);
}
```

**Preset library** (`.uzproj` or built-in defaults):

```toml
[audio.reverb_presets]
  [audio.reverb_presets.cave]
    decay_time = 3.2
    density = 0.8
    diffusion = 0.6
    wet_mix = 0.7
    room_hf_level = -8.0

  [audio.reverb_presets.outdoor]
    decay_time = 0.3
    density = 0.2
    diffusion = 0.3
    wet_mix = 0.1
    room_hf_level = -1.0
```

**Scene authoring** (`.uzscene`):

```ini
[node id=20 type=Node3D name="CaveReverbZone" parent=1]
reverb_zone = true
reverb_shape = aabb
reverb_half_extents = [15.0, 8.0, 20.0]
reverb_preset = "cave"
reverb_blend_distance = 3.0
```

### Impact if absent

All environments sound acoustically identical regardless of geometry. The cave sounds like the field. The cathedral sounds like the street. One of the most impactful audio features for immersion is absent.

### Complexity: M
### Dependencies: 3D spatial audio (Phase 3), Gap 3 (DSP chains for reverb XAPO), ECS system

---

## Gap 8 — Convolution Reverb (Impulse Response)

**Criticality**: MEDIUM — quality ceiling for linear/horror games

### Why it is critical

Algorithmic reverb (what `XAudio2FX` provides) is an approximation. It sounds "synthetic" to trained ears. Convolution reverb uses a recorded **Impulse Response (IR)** of a real acoustic space — a church in Notre-Dame, an anechoic chamber, a concert hall — and convolves the audio signal with it in the frequency domain. The result is acoustically accurate, indistinguishable from being in that space.

Games like RDR2, Resident Evil Village, and The Last of Us Part II use convolution reverb for specific high-impact acoustic spaces.

### Technical description

**IR asset** (`.uzir` — binary, derived from WAV):

```
[4]  magic: "UZIR"
[4]  version: 1
[4]  channels: 2
[4]  sample_rate: 44100
[4]  frame_count
[N]  pre-computed frequency-domain partition data (FFT partitioned)
```

The IR WAV is imported at build time and pre-transformed into frequency-domain partitions (uniform partitioned convolution) to minimize per-frame CPU cost. A 2-second stereo IR at 44100Hz requires approximately 176,400 samples, which at 1024-sample FFT partitions produces ~176 frequency-domain segments. Each segment is stored pre-multiplied with the IR FFT for fast runtime overlap-add convolution.

**ConvolutionReverb XAPO** (custom):

The CPU cost for realtime convolution of a 2-second IR on a single voice is approximately 2–5% of one core at 44100Hz/1024 buffer size. It is only feasible on the dedicated audio thread, not the game thread.

XAudio2 does not provide convolution reverb built-in. Microsoft's `XAudio2FX` only provides algorithmic reverb. A custom XAPO implementing frequency-domain partitioned convolution (the standard algorithm since Cook 1999) is required.

**Lua API**:

```lua
-- Assign convolution reverb to a specific bus (expensive — use sparingly):
ctx.audio:set_bus_convolution_reverb("SFX", "res://audio/ir/notre_dame_nave.uzir", { wet = 0.4 })
```

**Practical constraint**: Convolution reverb is CPU-expensive. It should only be active on one bus at a time (typically SFX). Switching IRs requires a crossfade to avoid a click (pre-fade out current, load new, fade in).

### Impact if absent

Algorithmic reverb only. High-fidelity acoustic reproduction of real spaces is impossible. This is a quality ceiling gap, not a blocking gap.

### Complexity: XL (custom FFT-based XAPO, pre-processing pipeline)
### Dependencies: Gap 3 (XAPO framework), asset pipeline (IR pre-processing)

---

## Gap 9 — Audio Streaming with Seek Points

**Criticality**: HIGH for music and long-form content

### Why it is critical

The current spec mentions streaming as a Phase 3 item: "Large files (>2 MB) stream from disk instead of full decode." This is correct but insufficient. A complete streaming system needs:

1. **Seek points**: The ability to restart playback from a specific location without streaming from the beginning (required for music loops and branching music)
2. **Pre-roll**: Pre-buffering N seconds ahead of the current playback position to prevent starvation
3. **Seek without audible gap**: Looping back to a loop point in a music file must be gapless (the XAudio2 voice must submit the next buffer before the current one ends)
4. **Streaming with compression**: Streaming raw PCM is wasteful. Streaming compressed OGG/Opus reduces disk bandwidth by 5-10x

### Technical description

**StreamingVoice** (in addition to in-memory voices):

```cpp
class StreamingVoice {
    IXAudio2SourceVoice* m_voice;
    std::array<AudioRingBuffer, 3> m_buffers;  // triple-buffered
    uint64_t  m_current_sample;
    uint64_t  m_loop_start_sample;             // 0 = no loop
    uint64_t  m_loop_end_sample;
    bool      m_looping;
    OggVorbis_File m_vf;                       // or libmpg123 handle

    // Called from XAudio2 voice callback (audio thread):
    void on_buffer_end() {
        // Decode next chunk from compressed stream into next ring buffer slot
        // SubmitSourceBuffer for the next chunk
        // Handle loop: if current_sample >= loop_end_sample, seek to loop_start_sample
    }
};
```

**Gapless loop**: The loop point in OGG Vorbis must be stored as a comment tag (`LOOPSTART` and `LOOPLENGTH` in samples — the same convention used by RPG Maker and many game engines). The importer reads these tags and stores them in the `.uzaudio` header `Loop Point` field (already present in the spec but not described in detail for streaming).

**Pre-buffering**: The streaming thread decodes ahead by 500ms of audio. If the disk I/O stalls (NVMe seek), the pre-roll prevents the audible glitch. The triple-buffer scheme (one playing, one decoded, one being filled) is the standard XAudio2 streaming pattern.

**Seek points** in `.uzaudio` (streaming variant):

```
[streaming .uzaudio header extension]
seek_table_count: uint32_t
seek_table: [{sample_offset: uint64_t, byte_offset: uint64_t}]
```

The seek table allows seeking to the nearest seek point (granule) in O(log N) without decoding from the start.

### Impact if absent

Music cannot loop gaplessly. Seeking in long audio files requires decoding from the beginning. Large music files must be fully decoded into RAM (memory waste). Long loading stalls when importing multi-minute music tracks.

### Complexity: L
### Dependencies: `.uzaudio` format (Phase 2), OGG/MP3 decoder integration

---

## Gap 10 — Dialogue System and Voice Acting Pipeline

**Criticality**: HIGH for any narrative game

### Why it is critical

The `Voice` bus exists in the spec but there is no dialogue system. A dialogue system is not just playing a WAV file — it is an entire pipeline:

1. **Dialogue event with subtitle/caption data** (timing-tagged)
2. **Voice acting file naming convention** (localization-aware)
3. **Dialogue tree integration** (branching conversation flow)
4. **Playback priorities** (dialogue must interrupt or duck SFX)
5. **Lip sync data** (viseme timing for facial animation)
6. **Localization routing** (French locale plays French WAV, same event ID)

### Technical description

**Dialogue Line asset** (`.uzdialogue` — INI):

```ini
[dialogue_line id="npc_greeting_01"]
  speaker = "Guard_01"
  duration_ms = 2800
  priority = 180

  ; Localized audio paths (same event ID, different WAV per locale):
  [audio]
    en = "res://audio/dialogue/en/guard_greeting_01.wav"
    pt_br = "res://audio/dialogue/pt_br/guard_greeting_01.wav"
    fr = "res://audio/dialogue/fr/guard_greeting_01.wav"

  ; Subtitle timing (milliseconds from line start):
  [subtitles]
    en = "Hey, stop right there."
    pt_br = "Hey, para aí."

    [[cue]]
      time_ms = 0
      word_index_start = 0
      word_index_end = 0       ; "Hey"

    [[cue]]
      time_ms = 400
      word_index_start = 1
      word_index_end = 2       ; "stop right"

    [[cue]]
      time_ms = 900
      word_index_start = 3
      word_index_end = 4       ; "there."

  ; Lip sync viseme data (for Phase 4+ facial animation):
  [lipsync]
    data_path = "res://audio/dialogue/lipsync/guard_greeting_01.uzlipsync"
```

**DialoguePlayer** (C++):

```cpp
struct DialogueRequest {
    StringId line_id;
    StringId speaker_node_id;   // for 3D positional dialogue
    bool     spatial;
    float    priority;
};

// On trigger:
void DialoguePlayer::play(DialogueRequest req) {
    // 1. Resolve locale-specific audio path from line asset
    // 2. Acquire high-priority voice from pool (preempts low-priority SFX)
    // 3. Schedule subtitle cues via MusicClock timeline
    // 4. Fire on_dialogue_start(line_id, speaker_id) event to Lua
    // 5. On voice end callback: fire on_dialogue_end(line_id), advance dialogue tree
}
```

**Lua API**:

```lua
-- Play a dialogue line (handles locale, subtitle, priority, spatial):
ctx.audio:play_dialogue("npc_greeting_01", {
    speaker = guard_node,
    spatial = true,
})

-- Callbacks on dialogue state:
function M:on_dialogue_start(ctx, line_id, speaker_id)
    -- Trigger NPC animation
end

function M:on_dialogue_end(ctx, line_id, speaker_id)
    -- Advance dialogue tree, enable player response choices
end
```

**Subtitle integration**: The dialogue system fires subtitle display events to the UI system (`ctx.ui`) with timing-tagged word highlighting data. This is the canonical integration point; the UI system renders the actual caption.

**Ducking**: When a dialogue line plays, the `SFX` bus is automatically ducked (volume reduced) via an automated snapshot transition (see Gap 6). The duck depth and recovery time are configured per line or globally in `.uzproj`.

### Impact if absent

Narrative games, RPGs, and any game with voiced characters cannot be built. Localization of voice acting is impossible without the locale routing. Subtitle synchronization requires ad-hoc Lua timer hacks. Voice acting pipeline has no standard format.

### Complexity: L (basic dialogue player) to XL (full tree + lipsync)
### Dependencies: Gap 1 (event system), Gap 6 (snapshot for ducking), localization system, UI system

---

## Gap 11 — Random Variation and Asset Tagging System

**Criticality**: HIGH — the single most impactful quality-of-life gap

### Why it is critical

In any shipping game, SFX assets are grouped into **variation pools**. A footstep is not one file. It is 6–12 files, randomly selected (without immediate repeats) to avoid the "machine gun effect" — the jarring sound of the same audio sample repeating identically every frame. This is the number one audio complaint in poorly made indie games.

This is closely related to Gap 1 (event system) but specifically concerns the asset organization layer and the random selection algorithm.

### Technical description

**Random variation within events** (see Gap 1 for `.uzevent` format):

The event system handles `random_no_repeat` selection. The algorithm is:

```cpp
// Random-no-repeat: Fisher-Yates shuffle of indices, play through, reshuffle:
class RandomNoRepeatSelector {
    std::vector<int> m_shuffled;
    int m_cursor = 0;

    int next() {
        if (m_cursor >= m_shuffled.size()) {
            shuffle(m_shuffled);  // reshuffle, but ensure first element != last played
            m_cursor = 0;
        }
        return m_shuffled[m_cursor++];
    }
};
```

**Asset tagging** for parametric selection:

```ini
[audio_event name="impact_surface"]
  mode = parametric

  [[clip]]
    path = "res://audio/fx/impact_wood_01.wav"
    tags = { surface = "wood", intensity = "light" }
    weight = 1.0

  [[clip]]
    path = "res://audio/fx/impact_wood_heavy_01.wav"
    tags = { surface = "wood", intensity = "heavy" }
    weight = 1.0

  [[clip]]
    path = "res://audio/fx/impact_metal_01.wav"
    tags = { surface = "metal", intensity = "light" }
    weight = 1.2
```

```lua
-- Trigger selects by matching tags:
ctx.audio:trigger("impact_surface", { surface = "wood", intensity = "heavy" })
```

**Pitch and volume randomization ranges** (per clip, defined in `.uzevent`):

The random range is continuous, not discrete. On each trigger, pitch is sampled uniformly in `[pitch_min, pitch_max]` and volume in `[volume_min, volume_max]`. The LCG or xorshift RNG used must be seeded per event instance (not globally) to avoid correlated randomness when many enemies trigger the same event simultaneously.

### Impact if absent

SFX repetition is immediately noticeable and breaks immersion. Every game with repeated sound interactions (footsteps, gunshots, impacts, UI clicks) sounds amateurish. This is the single most commented-on audio quality issue in indie game reviews.

### Complexity: S (random variation) to M (parametric tag matching)
### Dependencies: Gap 1 (event system)

---

## Gap 12 — HRTF and Binaural Audio

**Criticality**: MEDIUM for desktop, CRITICAL for VR

### Why it is critical

X3DAudio provides stereo panning only. HRTF (Head-Related Transfer Function) simulates how the human outer ear (pinna) modifies sound coming from different directions, enabling height perception and front/back discrimination that is impossible with standard stereo panning.

For VR (OpenXR target mentioned in engine vision), binaural audio is not optional — it is the primary spatial audio mode. Without HRTF, VR audio sounds like a floating speaker rather than sounds in physical space.

### Technical description

**Windows Spatial Audio (ISpatialAudioClient)**: Windows 10 1703+ provides native HRTF via the Windows Spatial Sound platform (`ISpatialAudioClient` API), which is separate from XAudio2 but can run alongside it. Microsoft provides the **HoloLens HRTF** dataset for free.

Alternatively, **Steam Audio** (open-source, Valve) provides phonon-based HRTF + convolution-based environment rendering that integrates with any audio backend via a plugin layer.

**Architecture option for HRTF**:

```cpp
// Abstracted spatial audio backend:
class ISpatialBackend {
public:
    virtual void update_listener(const X3DAUDIO_LISTENER& listener) = 0;
    virtual void update_source(uint32_t voice_id,
                               const X3DAUDIO_EMITTER& emitter,
                               const SpatialAudioParams& params) = 0;
};

class X3DAudioBackend : public ISpatialBackend { /* standard panning */ };
class HRTFBackend     : public ISpatialBackend { /* Windows Spatial Audio */ };
class SteamAudioBackend : public ISpatialBackend { /* phonon HRTF + occlusion */ };
```

The backend is selected at runtime based on device capabilities and project settings.

**`.uzproj` config**:

```toml
[audio.spatial]
  backend = "x3daudio"     # x3daudio | hrtf_windows | steam_audio
  hrtf_dataset = "windows" # windows | mit_kemar | custom
```

### Impact if absent

Spatial audio is limited to stereo panning. VR support is functionally impossible. Games targeting premium headphones (Dolby Atmos, spatial audio headsets) lose hardware acceleration.

### Complexity: M (Windows Spatial Audio) to XL (Steam Audio full integration)
### Dependencies: 3D spatial audio (Phase 3), XAudio2 architecture

---

## Gap 13 — Audio LOD (Distance-Based Detail Reduction)

**Criticality**: MEDIUM — performance at scale

### Why it is critical

In an open-world scene with 200 sound sources, the naive approach — run X3DAudio DSP for every source every frame — is prohibitively expensive. AAA engines implement audio LOD:

- **Near zone** (within min_dist): Full quality, all DSP active, X3DAudio per-frame update
- **Mid zone** (min_dist to 2× max_dist): Reduced update frequency (every 3 frames), simplified DSP
- **Far zone** (2× max_dist): X3DAudio update once per second, no reverb DSP, distance attenuation only
- **Culled** (beyond max_dist): Voice is virtualized (see Gap 5), not playing

### Technical description

**AudioLODSystem**:

```cpp
enum class AudioLOD : uint8_t {
    LOD0_NEAR   = 0,  // full quality, per-frame DSP
    LOD1_MID    = 1,  // every 3 frames, no convolution
    LOD2_FAR    = 2,  // every 10 frames, attenuation only
    LOD3_CULLED = 3,  // virtual voice, no XAudio2 voice
};

struct CAudioSource {
    // ...existing...
    AudioLOD  current_lod;           // runtime, updated per-frame
    uint32_t  lod_update_counter;    // frame counter for staggered updates
};
```

**LOD distance thresholds** (configured per source or globally):

```cpp
// In AudioEngine::update_spatial_sources():
float dist = (source_pos - listener_pos).length();
float d0 = source.min_dist;
float d1 = source.max_dist;

AudioLOD lod;
if (dist < d0 * 2.0f)       lod = LOD0_NEAR;
else if (dist < d1)         lod = LOD1_MID;
else if (dist < d1 * 1.5f)  lod = LOD2_FAR;
else                        lod = LOD3_CULLED;
```

**Budget system**: Maximum N voices in LOD0, M voices in LOD1, etc. (configurable in `.uzproj`). When budget is exceeded, lowest-priority sources are bumped to a higher LOD number.

### Impact if absent

Open-world games with many simultaneous audio sources have poor performance. X3DAudio DSP calculated every frame for 200 sources is approximately 2–5ms of audio thread time — potentially exceeding the audio callback budget at 44100Hz/1024 samples (23ms window).

### Complexity: S
### Dependencies: 3D spatial audio (Phase 3), Gap 5 (virtual voices)

---

## Gap 14 — Audio Middleware Abstraction Layer

**Criticality**: MEDIUM-HIGH for long-term project viability

### Why it is critical

The current spec hard-codes XAudio2 throughout. There is no abstraction layer. Consequences:

1. **Platform expansion** (hypothetical future Linux/macOS target) requires replacing every XAudio2 call with platform-specific equivalents
2. **Professional studios** using Wwise or FMOD cannot swap the audio backend — they must use UzEngine's custom audio system or fork the engine
3. **Future API changes** (XAudio2 3.x, hypothetical) require invasive refactoring

Unreal Audio Engine uses an `IAudioDevice` abstraction. Every XAudio2 operation goes through an interface:

### Technical description

**IAudioEngine interface** (abstract C++ interface):

```cpp
class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    virtual bool init(const AudioEngineConfig& cfg) = 0;
    virtual void shutdown() = 0;

    // Voice management
    virtual AudioVoiceHandle acquire_voice(const VoiceParams& params) = 0;
    virtual void release_voice(AudioVoiceHandle handle) = 0;
    virtual void submit_buffer(AudioVoiceHandle handle, const AudioBuffer& buf) = 0;
    virtual void start_voice(AudioVoiceHandle handle) = 0;
    virtual void stop_voice(AudioVoiceHandle handle) = 0;

    // Bus/mixer
    virtual void set_bus_volume(BusIndex bus, float volume) = 0;
    virtual void set_voice_output(AudioVoiceHandle handle, BusIndex bus) = 0;

    // 3D spatial
    virtual void set_listener(const ListenerState& listener) = 0;
    virtual void set_voice_position(AudioVoiceHandle handle, const Vec3& pos) = 0;
};

// Concrete implementations:
class XAudio2Engine : public IAudioEngine { /* ... */ };
class NullAudioEngine : public IAudioEngine { /* graceful degradation */ };
// Future:
// class OpenALEngine  : public IAudioEngine { /* ... */ };
// class FMODEngine    : public IAudioEngine { /* ... */ };
```

The `NullAudioEngine` (all no-ops) is the graceful degradation implementation — removing the scattered `if (!m_initialized)` checks from the current spec. All callers call through the interface; the null object pattern handles the disabled case transparently.

**GameContext** exposes the abstraction, not XAudio2 types:

```cpp
// In game_api.h (C ABI boundary):
// No XAudio2 types cross the boundary — only engine-defined types
typedef uint32_t AudioVoiceHandle;
typedef uint8_t  AudioBusIndex;
```

### Impact if absent

XAudio2 leaks through every layer. Platform expansion is an invasive refactor. Professional studios cannot swap the audio middleware. No testability without XAudio2 hardware (mock requires the full XAudio2 COM interface).

### Complexity: M (interface extraction) — ideally done during Phase 2, not retrofitted later
### Dependencies: Phase 2 audio engine (must be designed before XAudio2 is deeply embedded)

---

## Gap 15 — Procedural Audio Foundation

**Criticality**: LOW for Phase 2-3, MEDIUM for open-world games

### Why it is critical

Procedural audio generates sound algorithmically rather than playing back pre-recorded samples. It is used for:

- **Engine sound synthesis**: RPM-based car engine sound that varies continuously
- **Rain procedural ambience**: Density, wind, and surface variation in real-time
- **Footstep synthesis**: Generated from surface material + character weight + speed
- **UI SFX synthesis**: Instant feedback without a WAV asset

In traditional engines, this requires an external tool (Wwise + AudioKinetic Spatial Audio SDK, or Max/MSP exported as an externally-run process). For an AI-native engine, procedural audio is compelling because it can be fully described in text and generated by AI without external tools.

### Technical description — DSP Graph (minimal)

A minimal DSP graph for procedural audio:

```ini
[procedural_sound name="engine_rumble"]
  [[node id=0 type=oscillator]]
    wave = sine
    frequency_hz = 80        ; base rumble frequency

  [[node id=1 type=oscillator]]
    wave = sawtooth
    frequency_hz_param = "rpm_ratio"   ; driven by game parameter

  [[node id=2 type=multiply]]
    input_a = 0
    input_b = 1              ; AM modulation

  [[node id=3 type=lowpass_filter]]
    input = 2
    cutoff_hz = 400

  [[node id=4 type=output]]
    input = 3
    bus = SFX
```

Game parameters drive the graph in real-time:

```lua
ctx.audio:set_procedural_param("engine_rumble", "rpm_ratio", self.engine_rpm / 8000.0)
```

The procedural audio system generates samples on the audio thread by evaluating the DSP graph at each output sample, consuming approximately 0.5–2% CPU per active procedural voice.

### Impact if absent

All audio is sample-based. Engine sounds, weather, water, and continuous environmental audio require large libraries of pre-recorded variations. Procedural audio is a medium-term differentiator for the engine's AI-native identity.

### Complexity: XL
### Dependencies: Gap 3 (DSP framework), audio thread model

---

## Gap 16 — Audio Profiler Integration

**Criticality**: MEDIUM — developer productivity

### Why it is critical

The current spec mentions testing coverage but has no audio profiler. In production, audio engineers need to see in real time:

- How many voices are active vs. virtual vs. pool capacity
- Which events are triggering most frequently
- CPU time consumed by DSP chains per bus
- Peak and average bus volume levels (for mix monitoring)
- Streaming buffer starvation events
- Voice stealing events (what got evicted and why)

Unreal Engine's **Audio Mixer Debugger** (`au.Debug.SoundVis`) and Wwise's **Wwise Profiler** provide all of this. FMOD Studio's profiler connects to a running game over a socket.

### Technical description

**AudioProfilerStats** struct (updated per frame):

```cpp
struct AudioProfilerStats {
    // Voice stats
    uint32_t voices_active;          // real XAudio2 voices playing
    uint32_t voices_virtual;         // virtual voices (CPU-tracked)
    uint32_t voices_pool_capacity;   // total pool size

    // Event stats
    struct EventStat {
        StringId event_id;
        uint32_t trigger_count_this_second;
        float    last_trigger_time;
    };
    std::array<EventStat, 64> event_stats;

    // Bus stats
    struct BusStat {
        float peak_level_db;         // peak since last reset
        float rms_level_db;          // RMS this frame
        float cpu_ms_dsp;            // DSP processing time this frame
    };
    std::array<BusStat, 4> bus_stats;

    // Streaming
    uint32_t stream_buffer_underruns_this_second;

    // Voice stealing
    struct StealEvent {
        StringId stolen_clip;
        StringId winning_clip;
        float    stolen_priority;
        float    winning_priority;
    };
    std::array<StealEvent, 8> steal_events_this_second;
};
```

**IPC handler** (editor integration):

```json
Request: { "command": "audio.get_profiler_stats" }

Response: {
    "voices_active": 14,
    "voices_virtual": 3,
    "voices_pool_capacity": 64,
    "buses": [
        { "name": "Master", "peak_db": -3.2, "rms_db": -12.1, "cpu_ms": 0.3 },
        { "name": "SFX",    "peak_db": -6.4, "rms_db": -18.2, "cpu_ms": 1.2 },
        { "name": "Music",  "peak_db": -8.1, "rms_db": -14.0, "cpu_ms": 0.1 },
        { "name": "Voice",  "peak_db": -1.2, "rms_db": -6.0,  "cpu_ms": 0.0 }
    ],
    "steal_events": [],
    "stream_underruns": 0
}
```

**VSCode extension panel** (Audio Monitor): Real-time bar meter per bus, voice count timeline, event frequency heat map.

**Tracy integration**: All `AudioProfilerStats` fields written to Tracy counters so they appear in the CPU timeline alongside rendering and physics.

### Impact if absent

Audio performance problems are invisible. Mix levels are unmonitored — the master bus can clip. Voice stealing is silent and non-deterministic from the developer's perspective. Audio budget overruns require binary search debugging.

### Complexity: M
### Dependencies: Debug/profiling system (Phase 1), audio engine core, IPC system

---

## Gap 17 — Audio Bus Routing Flexibility

**Criticality**: MEDIUM

### Why it is critical

The current spec defines exactly 4 fixed buses: Master, SFX, Music, Voice. The routing is hardcoded: all non-master buses route to Master, which routes to the mastering voice. This is insufficient for:

- **Ambient bus**: Separate from SFX (different reverb, different ducking behavior during dialogue)
- **UI SFX bus**: No spatialization, no reverb, sometimes routes to a separate output (headset vs. TV in console games)
- **Foley bus**: Separate from SFX for easier mix control in cinematic sequences
- **Send buses**: Auxiliary sends for shared effects (one reverb XAPO shared among multiple sources via send, not per-source)

Wwise uses an arbitrary graph of aux buses. FMOD uses groups within groups. Unreal allows up to 8 submix buses with arbitrary routing.

### Technical description

**Dynamic bus graph** (configured in `.uzproj`):

```toml
[audio.buses]
  [[bus]]
    name = "Master"
    parent = ""            # routes to mastering voice
    volume = 1.0

  [[bus]]
    name = "SFX"
    parent = "Master"
    volume = 0.8

  [[bus]]
    name = "Ambient"
    parent = "Master"
    volume = 0.6

  [[bus]]
    name = "Music"
    parent = "Master"
    volume = 0.8

  [[bus]]
    name = "Voice"
    parent = "Master"
    volume = 1.0

  [[bus]]
    name = "UI"
    parent = "Master"
    volume = 0.9

  [[bus]]
    name = "ReverbSend"   # aux send bus (sources send a copy here)
    parent = "Master"
    volume = 0.4
    is_send_only = true   # sources send a fraction; bus has reverb XAPO
```

The bus count and graph topology would be determined at engine init from `.uzproj`. XAudio2 `IXAudio2SubmixVoice` supports arbitrary routing graphs natively via `XAUDIO2_SEND_DESCRIPTOR`.

**Lua API extension**:

```lua
ctx.audio:set_bus_volume("Ambient", 0.3)
ctx.audio:set_bus_send("Ambient", "ReverbSend", 0.2)  -- 20% of ambient sent to reverb bus
```

### Impact if absent

Audio buses are not flexible enough for a shipping game's mix. Ambient and SFX share a bus, preventing independent reverb treatment. No aux sends means every source that needs reverb needs its own reverb XAPO (expensive). UI audio cannot be routed separately.

### Complexity: M
### Dependencies: Audio engine init, `.uzproj` format, bus system

---

## Gap 18 — Middleware Integration Layer (Wwise / FMOD)

**Criticality**: HIGH for studios, irrelevant for indie

### Why it is critical

Professional game studios do not build their own audio tools. They use Wwise (by far the most common in AAA, used in 70%+ of shipped titles) or FMOD Studio. These are authoring tools where audio directors create banks, events, parameters, and state machines without touching code. The engine integrates as a client of the middleware.

If UzEngine cannot integrate with Wwise or FMOD, it is disqualified from AAA studio adoption regardless of any other features.

### Technical description

The middleware integration requires the abstraction layer described in Gap 14 (`IAudioEngine`), plus:

**WwiseEngine : IAudioEngine**:
- Links against Wwise SDK (AK namespace)
- Wraps `AK::SoundEngine::Init`, `AK::SoundEngine::PostEvent`, `AK::SoundEngine::SetRTPCValue`
- Maps UzEngine's `BusIndex` → Wwise `AkGameObjectID`
- Maps UzEngine's `trigger(event_id)` → `AK::SoundEngine::PostEvent(event_name, game_object_id)`

**FMODEngine : IAudioEngine**:
- Links against FMOD Studio low-level + Studio APIs
- Wraps `FMOD::Studio::EventInstance::start()`, `FMOD::Studio::System::setParameterByName()`

**Plugin discovery** (similar to plugin system spec):

```toml
# .uzproj
[audio]
  backend = "wwise"
  wwise_project_path = "res://wwise/UzGameProject.wproj"
  wwise_banks_path   = "res://wwise/GeneratedSoundBanks"
```

**Game Object Registration**: Wwise and FMOD require game objects (equivalent to nodes with audio sources) to be registered before events can be posted on them. The `CAudioSource` component's `on_start` registers the node ID as a game object; `on_stop` unregisters it.

**Lua API compatibility**: The Lua API (`ctx.audio:trigger()`) remains identical regardless of backend. The middleware event IDs map 1:1 to the UzEngine event IDs. Switching from XAudio2 to Wwise requires only a `.uzproj` config change, not Lua script changes.

### Impact if absent

Professional studios cannot use UzEngine. Any project that needs Wwise (music, adaptive audio, professional mix pipeline) is blocked. Engine is limited to studios that accept the built-in audio system.

### Complexity: XL (per middleware SDK)
### Dependencies: Gap 14 (abstraction layer), plugin system (Phase 4)

---

## Summary Priority Matrix

| # | Gap | Criticality | Phase | Complexity |
|---|-----|-------------|-------|------------|
| 1 | Audio Event System | BLOCKER | Phase 2 | L |
| 5 | Virtual Voices | HIGH | Phase 2 | M |
| 16 | Audio Profiler | MEDIUM | Phase 2 | M |
| 17 | Bus Routing Flexibility | MEDIUM | Phase 2 | M |
| 14 | Middleware Abstraction Layer | HIGH | Phase 2 (design) | M |
| 4 | Audio Occlusion/Obstruction | HIGH | Phase 3 | M-L |
| 3 | DSP Effect Chains Per Bus | HIGH | Phase 3 | L-XL |
| 6 | Audio Snapshots | HIGH | Phase 3 | M |
| 7 | Reverb Zones | HIGH | Phase 3 | M |
| 9 | Audio Streaming + Seek | HIGH | Phase 3 | L |
| 11 | Random Variation System | HIGH | Phase 2 | S-M |
| 10 | Dialogue System | HIGH | Phase 3 | L-XL |
| 2 | Adaptive Music | HIGH | Phase 3-4 | XL |
| 13 | Audio LOD | MEDIUM | Phase 3 | S |
| 12 | HRTF / Binaural | MEDIUM | Phase 3-4 | M-XL |
| 18 | Middleware Integration | HIGH for AAA | Phase 4 | XL |
| 15 | Procedural Audio | MEDIUM | Phase 4 | XL |
| 8 | Convolution Reverb | MEDIUM | Phase 4 | XL |

---

## Phase 2 Immediate Actions (Before Implementation Starts)

The following must be addressed in the Phase 2 spec **before** writing any code, because retrofitting them later is a breaking change:

1. **Extend `.uzaudio` header** to include loop point samples (loop_start_sample, loop_end_sample), random variation seed, and a flags field for future streaming extensions. The current header does not have enough bits for all Phase 3 metadata.

2. **Design `IAudioEngine` interface** now, even if only `XAudio2Engine` implements it in Phase 2. Future middleware integration requires this boundary to exist from the start.

3. **Define the event system data format** (`.uzevent` INI) before Lua bindings are written. The Lua API must expose `ctx.audio:trigger()` as the primary playback API, with `ctx.audio:play_2d()` deprecated to a convenience wrapper that triggers a generated single-clip event.

4. **Add `priority` field to `CAudioSource`** before the voice pool is implemented. Virtual voice logic is much harder to add after the pool is already shipping.

5. **Add `bus` as a named string field** in `CAudioSource` that resolves at runtime from a bus registry, not a hardcoded enum. This enables Gap 17 (flexible bus routing) without changing the ECS schema.

---

*This review should be treated as a planning input document. The gaps are ordered by Phase-adjusted priority, not by severity in isolation. Architectural gaps (1, 14, 17) must be addressed during Phase 2 design, even if their full implementation is deferred. Deferring the design of these gaps risks breaking the Lua API and `.uzscene` format in Phase 3.*
