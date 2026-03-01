# Phase 2 — Audio System

## Context

UzEngine currently has no audio playback capability. Games require dynamic sound playback with volume control, spatial positioning, and hot-reload support. The engine is Windows-first with a stable C ABI boundary between `UzEngine.exe` and `game.dll` that must not be crossed by complex subsystems like audio.

## Goal

A running `UzEngine.exe` that:
- Plays 2D and 3D spatial audio via XAudio2 (Windows native API)
- Supports WAV, MP3, and OGG audio formats (decoded at import time)
- Provides playback control via Lua scripts (`self:play_audio()`, `ctx.audio:play_at()`)
- Provides playback control via C++ (`game.dll` via `GameContext` callbacks)
- Manages 4 audio buses (Master, SFX, Music, Voice) with independent volume control
- Hot-reloads audio assets without restart
- Exposes test playback via IPC handlers (editor-only)
- Maintains 95%+ test coverage

## Why XAudio2?

- **Windows native**: Available on all Windows targets (XP+), zero external dependencies beyond libc
- **Low latency**: Direct GPU/hardware audio path, minimal buffering — ideal for games
- **Spatial audio**: X3DAudio provides HRTF + attenuation without DirectSound
- **Simple API**: Fewer states and error cases than competing APIs (OpenAL, BASS, FMOD)
- **Hot-reload friendly**: Replace voice buffers without recreating voices
- **Stable**: Not deprecated (unlike DirectSound); used in millions of games

For cross-platform support in Phase 4+, a thin abstraction layer would allow OpenAL Soft as an alternative.

## What Changes

### New Files

**C++ Engine** (`src/engine/audio/`):
- `audio_engine.h/cpp` — XAudio2 initialization, voice pool, bus system, lifecycle
- `audio_clip.h/cpp` — audio asset struct, PCM data storage, metadata
- `audio_source.h/cpp` — ECS component definition
- `audio_listener.h/cpp` — ECS component definition (Phase 3 implementation)
- `spatial_audio.h/cpp` — X3DAudio DSP integration, 3D positioning (Phase 3)
- `audio_format.h/cpp` — WAV/MP3/OGG decoding via stb_vorbis, libmpg123

**Asset Import** (`src/build/audio_importer/`):
- `audio_importer.h/cpp` — decode source → .uzaudio format

**Asset Format**:
- `.uzaudio` — custom binary audio format (PCM + metadata)

**Documentation**:
- `docs/specs/audio-system.md` — full architecture, ECS components, Lua API, bus system (created)

**Rulebook Task**:
- `.rulebook/tasks/phase2-audio-system/` — this task

### Modified Files

**CMakeLists.txt**:
- Link `xaudio2.lib` in game.dll project
- Add `stb_vorbis.h`, `libmpg123` to vcpkg.json

**GameContext** (`game.dll` interface):
- Add audio callbacks: `audio_play_2d`, `audio_play_3d`, `audio_stop`, `audio_set_bus_volume`
- Callback signature: stable C ABI (void pointers, basic types only)

**`.uzscene` Format**:
- Add `audio_source`, `audio_volume`, `audio_pitch`, `audio_loop`, `audio_play_on_start`, `audio_spatial`, `audio_min_dist`, `audio_max_dist`, `audio_bus` properties
- Add `audio_listener` property (tag component)

**Scene Manager** (`src/engine/scene/`):
- Register `CAudioSource` and `CAudioListener` component types

**Lua Binding** (`game.dll`):
- Add `self:play_audio()`, `self:stop_audio()`, `self:set_volume()`, `self:set_pitch()`, `self:is_audio_playing()` methods
- Add `ctx.audio:play_2d()`, `ctx.audio:play_at()`, `ctx.audio:stop_all()`, `ctx.audio:set_bus_volume()` context API

**IPC Handlers** (`src/engine/ipc/`):
- Register `audio.play` — test play a clip in editor
- Register `audio.stop_all` — stop all playback
- Register `audio.set_bus_volume` — adjust bus volume for testing

### Asset Registry

- Audio import format detected as `.wav`, `.mp3`, `.ogg`
- Importer produces `.uzaudio` artifact in `.import/` directory
- Asset registry tracks duration, channels, sample rate, whether streaming or decoded

## Impact

### Non-Breaking

- Audio is **opt-in per node** — existing scenes without audio components continue unchanged
- XAudio2 initialization failure → graceful fallback (silent, warnings only)
- No changes to core engine interfaces (Node, Transform, ECS base)

### Performance

- Voice pool: 64-128 reusable voices (typical game needs <32 concurrent)
- Memory: Decoded SFX in RAM (2 MB typical), streaming music from disk (Phase 3)
- CPU: Minimal (XAudio2 does the work); Lua bindings negligible overhead
- GPU: None (audio is purely CPU/hardware)

### Dependencies

**New external libraries** (via vcpkg):
- `stb_vorbis` (OGG decoder, header-only)
- `libmpg123` (MP3 decoder, optional)
- `xaudio2` (already on Windows, part of DirectX SDK)

All audio libraries are public domain or MIT; no GPL/LGPL.

## Out of Scope

- **Streaming** (Phase 3): Large files (>2 MB) stream from disk; Phase 2 loads all audio into RAM
- **3D spatial audio** (Phase 3): X3DAudio integration, attenuation curves
- **Audio effects** (Phase 4): Reverb zones, filters, echo
- **Audio snapshots** (Phase 4): Pre-defined audio presets
- **Procedural audio** (Phase 4): Synthesis, tone generation
- **MIDI** (Phase 4): Sequencing, instrument banks
- **Networking** (Phase 5): Network audio sync

## Spec Reference

[Audio System Spec](../../../docs/specs/audio-system.md)

See the spec for detailed API signatures, `.uzaudio` format, `.uzscene` syntax, ECS component definitions, Lua API, bus architecture, and IPC handler schemas.
