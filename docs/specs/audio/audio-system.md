# Spec: Audio System

> **Status**: Planned (Phase 2)
> **Owner**: `uz_engine.lib` (C++ audio engine) + `game.dll` (Lua bindings, GameContext integration)
> **Audio Library**: XAudio2 (Windows native, zero external dependencies, low-latency)
> **Thread Safety**: Audio thread isolated; game thread calls via callbacks in GameContext
> **Hot-reload**: Asset changes reload immediately (re-import + buffer swap, no restart)

**Related Technical Doc**: [Engine Audio System Architecture](../engine/audio-system.md) — Deep dive into XAudio2Engine, voice pool, DSP chains, X3DAudio integration

---

## Overview

The audio system provides dynamic sound playback with spatial 3D positioning, bus-based volume mixing, and hot-reload support. Audio is primarily driven from `game.dll` (via `GameContext` function pointers and Lua scripts), not directly from `UzEngine.exe`. This maintains the clean C ABI boundary and allows audio state to survive game DLL hot-reloads.

Three playback paths exist:

| Playback Path | Usage | API |
|---------------|-------|-----|
| **Lua scripts** | Node-attached SFX, music, ambient | `self:play_audio()`, `ctx.audio:play_at()` |
| **GameContext callbacks** | C++ game code (game.dll) | `game_ctx->audio_play_2d()`, `game_ctx->audio_play_3d()` |
| **IPC (editor only)** | Test playback in editor | `audio.play`, `audio.stop_all` |

The engine supports four **audio buses** with independent volume control:
- `Master` — global output volume
- `SFX` — sound effects and ambient
- `Music` — background music
- `Voice` — dialogue and announcements

---

## Architecture

```
UzEngine.exe
│  knows nothing about audio
│
game.dll  (C++, links XAudio2)
│  ├── XAudio2Engine lifecycle: IXAudio2 creation, mastering voice
│  ├── GameContext audio callbacks: play_2d, play_3d, stop, set_bus_volume
│  ├── Bus system: Master, SFX, Music, Voice submix voices
│  └── Voice pool: 64-128 voice pool (reusable XAudio2 voices)
│
src/engine/audio/  (C++ audio engine, compiled into uz_engine.lib)
│  ├── audio_engine.h/cpp     — XAudio2 initialization, voice pool, bus system
│  ├── audio_clip.h/cpp       — audio asset struct, decoded PCM data
│  ├── audio_source.h/cpp     — ECS component, playback state
│  ├── spatial_audio.h/cpp    — 3D positioning, X3DAudio DSP, attenuation
│  └── audio_format.h/cpp     — WAV/MP3/OGG decoding (Phase 2 basic)
│
res://audio/
   hit.wav                     → import → .import/audio/hit.uzaudio
   ambient_loop.ogg            → import → .import/audio/ambient_loop.uzaudio
   music_main.mp3              → import → .import/audio/music_main.uzaudio
```

### Why XAudio2?

- **Windows native**: Available on all Windows targets (XP+), zero external dependencies
- **Low latency**: Direct GPU/hardware audio path, minimal buffering
- **Spatial audio**: X3DAudio provides HRTF + attenuation without DirectSound
- **Simple API**: Fewer states and error cases than competing APIs
- **Straightforward hot-reload**: Replace voice buffers without recreating voices

For future cross-platform support, OpenAL Soft is an alternative implementation (API abstraction layer not shown here).

---

## Asset Format & Import Pipeline

### Source Formats

Supported input formats at import time:

| Format | Codec | Max Duration | Best For |
|--------|-------|--------------|----------|
| `.wav` | PCM (uncompressed) | Any | Master tracks, critical SFX |
| `.mp3` | MP3 (libmpg123) | Any | Music loops, voice dialogue |
| `.ogg` | Vorbis (stb_vorbis) | Any | Ambient, streaming music |

### Import Artifact: `.uzaudio`

All audio assets import to `.uzaudio` — a custom binary format with metadata:

```
┌─ Header ────────────────────────────┐
│ Magic         4 bytes "UZAU"        │
│ Version       2 bytes (1)           │
│ Format        1 byte (0=PCM)        │
│ Channels      1 byte (1=mono, 2=st)│
│ Sample Rate   4 bytes (44100, etc) │
│ Sample Count  4 bytes (total)       │
│ Streaming     1 byte (0=decoded)    │
│ Reserved      padding to 64 bytes   │
├─ Metadata ─────────────────────────┤
│ Duration      4 bytes (ms)          │
│ Loop Point    4 bytes (sample)      │
│ Reserved      padding to 128 bytes  │
├─ Audio Data ───────────────────────┤
│ PCM frames    sample_count × bytes  │
│ (decoded mono/stereo interleaved)   │
└─────────────────────────────────────┘
```

### Storage Strategy

- **SFX** (<2 MB): Fully decoded to memory on import, played from RAM
- **Music** (>2 MB): **Phase 3** — streaming from disk with small ring buffer
- Detection: Auto-select based on duration or `.uzproj` config

### Import Workflow

```
res://audio/hit.wav
    ↓
engine import task (async)
    ↓ (decode, verify, wrap in .uzaudio header)
    ↓
.import/audio/hit.uzaudio (created)
    ↓
asset registry updated
    ↓ (on hot-reload)
game.dll swaps buffer in AudioClip
    ↓
playing voices switch to new buffer (next iteration)
```

---

## ECS Components

### `CAudioSource`

Represents a playable sound attached to a node. Only one clip per node; for multi-layered ambient use multiple child nodes.

> **Definition**: `src/engine/audio/components.h` — see [Audio Abstraction Layer spec](audio-abstraction-layer.md#caudioaource-ecs-component) for the canonical struct definition. The struct uses `IAudioEngine`-compatible handles (`AudioSourceHandle`) instead of raw XAudio2 voice IDs, enabling headless testing and future backend swapping.

### `CAudioListener`

Marks a node as the audio listener. Only one listener per scene; typically the camera/player node. The listener's world position is used for 3D spatial calculations.

```cpp
// src/engine/audio/components.h
struct CAudioListener {
    // No properties — just a tag component
    // The node's Transform3D position defines listener location
    // The node's forward direction (from Transform3D rotation) defines listener "facing"
};
```

---

## `.uzscene` File Format

Audio components are added as properties to nodes in `.uzscene` INI format:

```ini
[uzscene version=1 name="Level01"]

; Node with audio source (SFX)
[node id=5 type=Node3D name="DoorSlam" parent=1]
position = [10.0, 0.0, 0.0]
audio_source = "res://audio/door_slam.wav"
audio_volume = 0.9
audio_loop = false
audio_play_on_start = false
audio_spatial = true
audio_min_dist = 1.0
audio_max_dist = 50.0
audio_bus = "SFX"

; Node with audio source (looped ambient)
[node id=6 type=Node3D name="AmbientWind" parent=1]
audio_source = "res://audio/wind_loop.ogg"
audio_volume = 0.5
audio_loop = true
audio_play_on_start = true
audio_spatial = false
audio_bus = "SFX"

; Node with audio source (music)
[node id=7 type=Node3D name="MusicPlayer" parent=1]
audio_source = "res://audio/boss_music.mp3"
audio_volume = 0.8
audio_loop = true
audio_play_on_start = true
audio_spatial = false
audio_bus = "Music"

; Audio listener (marks camera as listener for 3D audio)
[node id=2 type=Camera name="MainCamera" parent=1]
fov = 75.0
is_main = true
position = [0.0, 1.7, 5.0]
audio_listener = true
```

### Format Rules

- `audio_source` value is a `res://` path (required for playback)
- Numeric properties default to sensible values if omitted
- Boolean properties: `true` / `false`
- All properties optional except `audio_source` (if the component is present)

---

## Lua API

Per-node Lua scripts can control audio via the audio API. Each node with `CAudioSource` exposes these methods:

```lua
-- Node with CAudioSource component
-- self = node instance

-- Playback control
self:play_audio()                 -- Start/resume playback
self:stop_audio()                 -- Stop current playback
self:pause_audio()                -- Pause (resume with play_audio)

-- Volume and pitch
self:set_volume(0.8)              -- Set volume (0.0–1.0)
self:set_pitch(1.2)               -- Set pitch (0.5–2.0)

-- Query state
local is_playing = self:is_audio_playing()  -- boolean
local duration_ms = self:get_audio_duration() -- milliseconds
```

### Context API (One-Shot Playback)

For playback without an ECS component, use the global audio context:

```lua
-- One-shot 2D playback
ctx.audio:play_2d("res://audio/hit.wav", {
    volume = 0.9,
    pitch = 1.0,
    loop = false
})

-- One-shot 3D playback at position
ctx.audio:play_at("res://audio/explosion.wav", {
    position = Vec3(10.0, 0.0, 0.0),
    volume = 1.0,
    pitch = 1.0,
    loop = false,
    spatial = true,
    min_dist = 5.0,
    max_dist = 100.0
})

-- Stop all playback
ctx.audio:stop_all()

-- Bus volume control
ctx.audio:set_bus_volume("Music", 0.5)
ctx.audio:set_bus_volume("SFX", 0.8)
ctx.audio:set_bus_volume("Voice", 1.0)
```

### Error Handling

- Clip not found: Warning logged, no playback
- Spatial without listener: Falls back to 2D playback (warning logged)
- Invalid pitch/volume: Clamped to valid range (warning logged)

---

## Audio Bus System

Four buses provide independent volume mixing channels:

| Bus | Default Volume | Use Case | Runtime Controllable |
|-----|-----------------|----------|----------------------|
| `Master` | 1.0 (100%) | Global output | Yes (pause all) |
| `SFX` | 0.8 (80%) | Sound effects, ambient | Yes (mute options) |
| `Music` | 0.8 (80%) | Background music | Yes (fade in/out) |
| `Voice` | 1.0 (100%) | Dialogue, announcements | Yes (mono downmix option) |

### Bus Architecture

```
Voice input voices  ──┐
SFX input voices    ──┼─→ [Voice submix] ──┐
Master input voices ──┘                     ├─→ [Master submix] ──→ [Mastering Voice] ──→ Speaker
                          [Music submix] ───┤
                          [SFX submix]  ────┤
                          [Master direct] ──┘
```

Each bus is an XAudio2 `IXAudio2SubmixVoice` with independent volume control. Voices send to their assigned bus; buses can be routed hierarchically.

### Bus Volume at Runtime

Set via Lua or C++:

```lua
-- Lua: set Music bus volume to 50%
ctx.audio:set_bus_volume("Music", 0.5)

-- C++: (from game.dll or engine system)
game_ctx->audio_set_bus_volume("Music", 0.5f);
```

Volume changes apply to all voices on that bus immediately (no fade).

---

## 3D Spatial Audio (Phase 3)

Spatial audio uses **X3DAudio** (Windows audio DSP) to simulate 3D positioning:

- **Distance attenuation**: Inverse square law (volume decreases with distance)
- **Panning**: Stereo left/right based on listener-to-source angle
- **Doppler effect**: Optional pitch shift based on relative velocity

### Listener Position

The scene's single `CAudioListener` component (typically on the camera/player) defines:
- **Position**: World coordinates of listener's ears
- **Forward/Up vectors**: Listener orientation (used for panning)

If no listener exists, all spatial audio degrades to 2D (center pan).

### Source Position

Each `CAudioSource` with `spatial=true` uses its node's world Transform3D:
- **Position**: World coordinates of sound source
- **Attenuation curve**: `min_dist` to `max_dist` (inverse square)

### Attenuation Model

```
distance < min_dist  → volume = source_volume (no attenuation)
min_dist ≤ distance ≤ max_dist  → volume = source_volume * ((max_dist - distance) / (max_dist - min_dist))^2
distance > max_dist  → volume = 0.0 (inaudible)
```

**Implementation note**: X3DAudio handles calculations; the engine only updates listener/source positions each frame.

---

## IPC Handlers (Editor-Only)

For testing audio in the VSCode editor without a full game session:

### `audio.play`

Test play an audio clip in the editor.

```json
Request:
{
  "path": "res://audio/effect.wav",
  "volume": 0.8,
  "pitch": 1.0
}

Response:
{
  "ok": true,
  "voice_id": 12
}

Error (clip not found):
{
  "ok": false,
  "error": "Audio clip not found: res://audio/effect.wav"
}
```

### `audio.stop_all`

Stop all audio playback in editor.

```json
Request:
{}

Response:
{
  "ok": true,
  "stopped": 5
}
```

### `audio.set_bus_volume`

Adjust bus volume for testing.

```json
Request:
{
  "bus": "Music",
  "volume": 0.5
}

Response:
{
  "ok": true,
  "bus": "Music",
  "volume": 0.5
}

Error (invalid bus):
{
  "ok": false,
  "error": "Invalid bus: 'Oops' (valid: Master, SFX, Music, Voice)"
}
```

---

## Hot-Reload

When an audio asset in `res://audio/` is modified:

1. **Import watcher** detects change
2. **Re-import task** decodes → .uzaudio
3. **Asset registry** updates metadata
4. **Game.dll notified** (via GameContext callback)
5. **AudioClip buffers swapped** (in-place)
6. **Playing voices** fetch new buffer on next iteration

No restart needed. Voices playing the old buffer finish normally; new playbacks use the new buffer.

**Shader comparison**: Same hot-reload mechanism as shader hot-reload — asset identity unchanged, internals updated.

---

## Graceful Degradation

If audio initialization fails (XAudio2 not available, broken audio hardware):

- No crash; audio subsystem disabled with warnings
- All `play_audio()` calls become no-ops
- Game continues normally (silent)
- Warning in log: `[audio] XAudio2 init failed: ...`

---

## Phase Breakdown

### Phase 2 (Current)

- XAudio2 engine initialization and voice pool
- WAV/MP3/OGG decoding (via stb_vorbis / libmpg123)
- `.uzaudio` import format and asset registry
- `CAudioSource` component + Lua API (`play_audio`, `stop_audio`, `set_volume`, `set_pitch`)
- 2D playback (no spatial audio)
- 4 bus system (Master, SFX, Music, Voice)
- IPC handlers (audio.play, audio.stop_all, audio.set_bus_volume)
- Hot-reload (asset update → buffer swap)
- `.uzscene` format support for audio properties
- GameContext audio callbacks (C++ game code integration)

### Phase 3 (Future)

- **3D spatial audio**: X3DAudio integration, distance attenuation, panning, Doppler
- **Audio listener**: `CAudioListener` component, listener position updates
- **Streaming**: Large files (>2 MB) stream from disk instead of full decode
- **Audio effects**: Reverb zones, echo, filters
- **Audio snapshots**: Pre-defined audio presets (e.g., "underwater", "cave", "open field")

### Phase 4+ (Future)

- **Procedural audio**: Synthesis, modulation, tone generation
- **MIDI support**: Sequencing, instrument banks
- **Networking**: Network audio synchronization

---

## Defaults

If not specified in `.uzscene`, components use these defaults:

```ini
[node ... type=Node3D ... ]
audio_volume = 1.0
audio_pitch = 1.0
audio_loop = false
audio_play_on_start = false
audio_spatial = false
audio_min_dist = 1.0
audio_max_dist = 100.0
audio_bus = "Master"
```

---

## Thread Model

- **Main thread (Game Logic)**: Updates CAudioSource positions, calls play/stop
- **Audio thread (XAudio2)**: Decodes + plays (user callback driven)
- **Import thread (Async)**: Decodes source files → .uzaudio
- **Synchronization**: Voice buffer swap is atomic (interlocked); no locks

All game-facing API calls (play, stop, volume) are thread-safe via lock-free atomics or explicit locks where needed.

---

## Testing

Audio subsystem testing covers:

1. **Unit tests**: Component serialization, bus calculations, buffer management
2. **Integration tests**: Load scene with audio nodes, play via Lua, hot-reload asset
3. **IPC tests**: Editor audio.play handler, volume changes propagate to bus
4. **Manual tests**: Listen to audio output (subjective quality), check no crashes

Coverage target: **95%** of audio engine code.

---

## Audio Event System (Data-Driven)

Audio events decouple "what sound plays" from "when it plays". Events are defined in `.uzevent` assets:

```ini
[AudioEvent]
name = footstep_concrete
mode = random_no_repeat      ; random | random_no_repeat | sequential | weighted
clips[] = res://audio/sfx/footstep_concrete_01.uzaudio
clips[] = res://audio/sfx/footstep_concrete_02.uzaudio
clips[] = res://audio/sfx/footstep_concrete_03.uzaudio
pitch_min = 0.9
pitch_max = 1.1
volume_min = 0.8
volume_max = 1.0
cooldown_ms = 50
bus = sfx
```

Lua API:
```lua
ctx.audio:trigger("footstep_concrete")                     -- 2D
ctx.audio:trigger_at("footstep_concrete", position, 1.0)   -- 3D
ctx.audio:trigger_attached("footstep_concrete", entity_id) -- follows entity
```

The `AudioEventRegistry` resolves event names to `.uzevent` assets at startup.

---

## Audio Occlusion and Obstruction

Audio occlusion is computed via raycasts through Jolt Physics:

- **Occlusion**: Source → listener passes through solid geometry → full LPF filter + volume reduction
- **Obstruction**: Source → listener partially blocked → partial LPF + minor volume reduction
- Queries fire at `audio_occlusion_update_hz` (default: 10Hz) to amortize cost
- Smoothing: lerp over 100-200ms to avoid audible snapping

```toml
[audio]
occlusion_enabled = true
occlusion_update_hz = 10
occlusion_max_volume_reduction_db = -20.0
```

---

## DSP Effect Chains Per Bus

Each audio bus supports a chain of XAPO effects:

```toml
[audio.bus.music]
effects = [reverb, eq, limiter]

[audio.bus.music.reverb]
type = reverb
room_size = 0.6
damping = 0.5

[audio.bus.music.eq]
type = equalizer
low_gain_db = 2.0
mid_gain_db = 0.0
high_gain_db = -1.0

[audio.bus.music.limiter]
type = limiter
threshold_db = -3.0
```

Lua: `ctx.audio:set_bus_effect("music", "reverb", {room_size=0.8})`

---

## Virtual Voices

When voice count exceeds `max_voices` (default: 64), voices are virtualized:
- Virtual voices track playback position in CPU memory without consuming XAudio2 slots
- When a higher-priority sound ends, the highest-priority virtual voice resumes from its tracked position
- Priority is computed from: distance, `CAudioSource.priority`, bus importance, time since last play

---

## Adaptive Music System

Music layers (stems) play in sync and can be enabled/disabled at runtime:

```ini
[MusicTrack]
name = combat_theme
bpm = 120
stems[] = res://audio/music/combat_base.uzaudio
stems[] = res://audio/music/combat_percussion.uzaudio
stems[] = res://audio/music/combat_intensity.uzaudio
loop_bar = 8
transition_quantize = bar     ; beat | bar | phrase
```

Lua:
```lua
ctx.audio:play_music("combat_theme")
ctx.audio:set_stem_active("combat_theme", "combat_intensity", true)
ctx.audio:crossfade_music("combat_theme", "exploration_theme", 4.0)
```
