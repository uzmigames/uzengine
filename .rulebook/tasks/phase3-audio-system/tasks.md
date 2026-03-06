# Phase 2 — Audio System Tasks

## 1. Audio Engine Setup — XAudio2 Initialization

- [ ] 1.1 Create `src/engine/audio/audio_engine.h` — AudioEngine class interface (lifecycle, voice pool, bus management)
- [ ] 1.2 Create `src/engine/audio/audio_engine.cpp` — XAudio2 `IXAudio2` init, mastering voice setup
- [ ] 1.3 Implement voice pool: allocate/release 64 `IXAudio2SourceVoice` instances
- [ ] 1.4 Implement bus system: 4 `IXAudio2SubmixVoice` (Master, SFX, Music, Voice)
- [ ] 1.5 Implement `shutdown()` method (release all voices, SubmixVoices, IXAudio2 instance)
- [ ] 1.6 Audio engine lives in `uz_engine.lib`, not in render.exe or game.dll
- [ ] 1.7 Unit tests: engine init/shutdown, no memory leaks, graceful failure if XAudio2 unavailable

## 2. Audio Clip Asset Format & Decoding

- [ ] 2.1 Create `src/engine/audio/audio_clip.h` — AudioClip struct (PCM data, metadata: channels, sample_rate, sample_count, duration_ms, loop_point)
- [ ] 2.2 Create `src/engine/audio/audio_format.h` — format detection (WAV, MP3, OGG)
- [ ] 2.3 Create `src/engine/audio/audio_format.cpp` — WAV decoder (hand-written or stdlib)
- [ ] 2.4 Add `stb_vorbis.h` to project (header-only OGG decoder)
- [ ] 2.5 Add `libmpg123` to vcpkg.json (MP3 decoder)
- [ ] 2.6 Implement OGG decoder via stb_vorbis (decode → float PCM)
- [ ] 2.7 Implement MP3 decoder via libmpg123 (decode → int16 PCM, convert to float)
- [ ] 2.8 Implement WAV decoder (RIFF parse, support 16/24-bit PCM)
- [ ] 2.9 Unit tests: decode sample WAV/MP3/OGG files, verify PCM output is correct

## 3. Audio Import Pipeline & Asset Format

- [ ] 3.1 Define `.uzaudio` binary format: header (magic, version, format, channels, sample_rate, sample_count, streaming flag) + PCM data
- [ ] 3.2 Create `src/build/audio_importer.h/cpp` — import source formats → .uzaudio
- [ ] 3.3 Detect streaming vs decoded: files <2 MB decoded, >2 MB streaming (Phase 3; for now all decoded)
- [ ] 3.4 Create `.import.json` sidecar with metadata (path, format, channels, duration_ms, loop_point, streaming)
- [ ] 3.5 Add audio importer to asset registry (detect `.wav`, `.mp3`, `.ogg`)
- [ ] 3.6 Test import: create sample audio files, verify .uzaudio artifacts are created
- [ ] 3.7 Integration test: import → load → verify metadata + PCM matches source

## 4. ECS Components — Audio Source & Listener

- [ ] 4.1 Create `src/engine/audio/components.h` — CAudioSource struct (clip_id, volume, pitch, loop, play_on_start, spatial, min_dist, max_dist, bus_id, voice_id, is_playing)
- [ ] 4.2 Create listener component placeholder (CAudioListener — no fields, just tag)
- [ ] 4.3 Register CAudioSource in scene manager's component type registry
- [ ] 4.4 Register CAudioListener in scene manager's component type registry
- [ ] 4.5 Implement `.uzscene` property mapping (audio_source → clip_id, audio_volume → volume, etc.)
- [ ] 4.6 Unit tests: component serialization/deserialization, property defaults

## 5. Audio Playback System — Voice Management

- [ ] 5.1 Create `src/engine/audio/audio_source.h/cpp` — playback system (update positions, create/release voices)
- [ ] 5.2 Implement `play_audio(CAudioSource&, AudioClip&)` — get voice from pool, submit buffer, start playback
- [ ] 5.3 Implement `stop_audio(CAudioSource&)` — stop voice, return to pool
- [ ] 5.4 Implement `update_audio_source(CAudioSource&, Transform3D world_pos)` — update voice position for 3D (Phase 3 implementation; Phase 2 stub)
- [ ] 5.5 Implement `set_volume(CAudioSource&, float)` — update voice volume in real-time
- [ ] 5.6 Implement `set_pitch(CAudioSource&, float)` — update voice frequency ratio
- [ ] 5.7 Implement `is_playing(const CAudioSource&)` → bool check via XAudio2 voice state
- [ ] 5.8 Implement `get_duration(const CAudioSource&)` → milliseconds
- [ ] 5.9 Voice pool recycling: on stop, return voice to available pool (zero-alloc)
- [ ] 5.10 Unit tests: play/stop/volume/pitch updates, voice pool exhaustion handling

## 6. Audio Bus System — Volume Control

- [ ] 6.1 Create `src/engine/audio/audio_bus.h/cpp` — bus struct (name, volume, IXAudio2SubmixVoice*)
- [ ] 6.2 Implement bus creation during AudioEngine init (Master, SFX, Music, Voice submix voices)
- [ ] 6.3 Implement `set_bus_volume(bus_id, volume)` — update submix voice volume
- [ ] 6.4 Implement voice routing: each voice sends to its assigned bus (via `voice->SetOutputVoices()`)
- [ ] 6.5 Voice defaults: CAudioSource.bus_id = "Master" (or configurable default in project)
- [ ] 6.6 Unit tests: set bus volume, verify change applies to all voices on that bus

## 7. GameContext Integration — C++ Audio API

- [ ] 7.1 Define GameContext audio callback pointers in `game_api_t` (C ABI boundary)
- [ ] 7.2 Implement `audio_play_2d(path, volume, pitch)` callback (decode if not cached, submit voice)
- [ ] 7.3 Implement `audio_play_3d(path, position, volume, pitch, spatial_params)` callback (Phase 2: position ignored, phase 3: use X3DAudio)
- [ ] 7.4 Implement `audio_stop(voice_id)` callback
- [ ] 7.5 Implement `audio_set_bus_volume(bus_id, volume)` callback
- [ ] 7.6 Implement `audio_stop_all()` callback
- [ ] 7.7 Update `game.dll` interface documentation with callback signatures
- [ ] 7.8 Integration test: call callbacks from test game DLL, verify audio plays

## 8. Lua Scripting API

- [ ] 8.1 Create `src/scripting/lua_audio.h/cpp` — Lua binding helpers for audio
- [ ] 8.2 Register Lua methods on nodes with CAudioSource (self:play_audio, self:stop_audio, etc.)
- [ ] 8.3 Implement `self:play_audio()` → calls GameContext callback
- [ ] 8.4 Implement `self:stop_audio()`
- [ ] 8.5 Implement `self:pause_audio()` / `self:resume_audio()` (if XAudio2 supports, else phase 3)
- [ ] 8.6 Implement `self:set_volume(volume)` → clamped to [0, 1]
- [ ] 8.7 Implement `self:set_pitch(pitch)` → clamped to [0.5, 2.0]
- [ ] 8.8 Implement `self:is_audio_playing()` → boolean
- [ ] 8.9 Implement `self:get_audio_duration()` → milliseconds
- [ ] 8.10 Implement context API: `ctx.audio:play_2d(path, {volume, pitch, loop})`
- [ ] 8.11 Implement context API: `ctx.audio:play_at(path, {position, volume, pitch, spatial, min_dist, max_dist})`
- [ ] 8.12 Implement context API: `ctx.audio:stop_all()`
- [ ] 8.13 Implement context API: `ctx.audio:set_bus_volume(bus_id, volume)`
- [ ] 8.14 Unit/integration tests: Lua script playback, volume changes, error handling (missing clip, invalid volume)

## 9. Scene System Integration

- [ ] 9.1 Extend `.uzscene` parser to recognize audio properties (audio_source, audio_volume, audio_pitch, audio_loop, audio_play_on_start, audio_spatial, audio_min_dist, audio_max_dist, audio_bus, audio_listener)
- [ ] 9.2 Extend `.uzscene` serializer to write audio properties
- [ ] 9.3 Implement `SceneManager::load()` to instantiate CAudioSource components from nodes
- [ ] 9.4 Implement `SceneManager::load()` to find CAudioListener and register with audio engine
- [ ] 9.5 Implement scene reload: audio buffers swapped, voices continue playing (no interrupt)
- [ ] 9.6 Create sample `.uzscene` with audio nodes (SFX, music, ambient)
- [ ] 9.7 Unit tests: parse scene with audio properties, serialize back, round-trip matching

## 10. Hot-Reload

- [ ] 10.1 Audio asset file watcher: detect changes to `.wav`, `.mp3`, `.ogg`
- [ ] 10.2 On change: re-import to .uzaudio, update asset registry
- [ ] 10.3 On import complete: notify AudioEngine of buffer change (swap pointer in AudioClip)
- [ ] 10.4 Playing voices continue on old buffer (finish naturally), new playbacks use new buffer
- [ ] 10.5 Integration test: modify audio file, re-import, verify playback reflects change without restart

## 11. IPC Handlers — Editor Test Playback

- [ ] 11.1 Register `audio.play` handler — test play clip in editor (request: {path, volume, pitch}, response: {ok, voice_id})
- [ ] 11.2 Register `audio.stop_all` handler — stop all voices (request: {}, response: {ok, stopped_count})
- [ ] 11.3 Register `audio.set_bus_volume` handler — adjust bus volume (request: {bus, volume}, response: {ok, bus, volume})
- [ ] 11.4 Error handling: missing clip → `{ok: false, error: "..."}`, invalid bus → similar
- [ ] 11.5 Unit tests: MCP tool calls for audio handlers, verify response schemas

## 12. Quality & Testing

- [ ] 12.1 Unit tests for AudioEngine, voice pool, bus system, decoding, components
- [ ] 12.2 Integration tests: load scene, play audio, hot-reload, stop, set volume
- [ ] 12.3 Lua tests: self:play_audio(), ctx.audio:play_2d(), set_volume, is_playing
- [ ] 12.4 IPC tests: audio.play, audio.stop_all, audio.set_bus_volume handlers
- [ ] 12.5 Memory tests: allocations cleaned up on shutdown, voice pool bounds
- [ ] 12.6 Coverage report: target 95%+ of audio subsystem code
- [ ] 12.7 Performance test: 32 concurrent voices, CPU/memory usage
- [ ] 12.8 Graceful degradation: XAudio2 init fails → silent fallback (no crash)

## 13. Documentation

- [ ] 13.1 Verify `docs/specs/audio-system.md` matches implementation
- [ ] 13.2 Update `docs/README.md` specs table entry for audio-system (added)
- [ ] 13.3 Update `docs/README.md` Rulebook tasks table entry for phase2-audio-system (added)
- [ ] 13.4 Document GameContext audio callbacks in `docs/guides/` or engine docs
- [ ] 13.5 Document `.uzaudio` format in asset pipeline docs (or cross-reference)
- [ ] 13.6 Add audio section to `.uzscene` format documentation

---

## Notes

- All imports and asset processing are **async** (background task), not blocking main thread
- XAudio2 API calls must be on the game logic thread (or protected by locks)
- Voice lifetime is managed by audio engine; script code never directly owns a voice
- Error handling: missing clips → warning log + silent fallback; no exceptions thrown
- Audio state does NOT survive scene unload (voices released when scene ends)
