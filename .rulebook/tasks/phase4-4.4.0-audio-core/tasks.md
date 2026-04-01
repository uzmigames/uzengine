## 1. XAudio2 Engine

- [ ] 1.1 Create `runtime/engine/audio/audio_engine.h` — AudioEngine class: Init(), Shutdown(), Update(float dt). Owns IXAudio2*, IXAudio2MasteringVoice*. Voice pool of 64 pre-allocated IXAudio2SourceVoice*. 4 submix buses: SFX, Music, Voice, Master. SetBusVolume(bus, volume), GetBusVolume(bus).
- [ ] 1.2 Create `runtime/engine/audio/audio_engine.cpp` — XAudio2Create(), CreateMasteringVoice(), 3 CreateSubmixVoice() routed to Master. Voice pool: pre-create 64 source voices with common format (44100Hz, 16-bit, stereo). AcquireVoice(priority) → VoiceHandle. ReleaseVoice(handle). Priority-based stealing on exhaustion.
- [ ] 1.3 Create `runtime/engine/audio/audio_subsystem.h/.cpp` — AudioSubsystem (WorldSubsystem): owns AudioEngine. Registered in SubsystemRegistry with TickGroup::PostUpdate. OnTick(dt): update active voices (check completion, release finished voices), process pending commands.

## 2. Audio Clip Asset

- [ ] 2.1 Create `runtime/engine/audio/audio_clip.h` — AudioClip: PCM data (vector<uint8_t>), metadata: sample_rate (uint32), channels (uint16), bit_depth (uint16), duration_seconds (float), frame_count (uint32), loop_start (uint32), loop_end (uint32). Load(path) from .uzaudio binary.
- [ ] 2.2 Create `runtime/engine/audio/audio_clip.cpp` — .uzaudio binary format: magic 'UZAU', version, metadata header (32 bytes), then raw PCM frames. Load reads header, validates, memory-maps or loads PCM data.
- [ ] 2.3 Create `runtime/engine/audio/audio_importer.h/.cpp` — AudioImporter: ImportWAV(path) → .uzaudio. ImportOGG(path) → .uzaudio via stb_vorbis decode. Auto-detect format by extension. Resample to 44100Hz if source differs. Register in asset pipeline.

## 3. Audio Source Component

- [ ] 3.1 Create `runtime/engine/audio/components/audio_source.h` — CAudioSource: clip_path (string), volume (1.0), pitch (1.0), loop (false), play_on_start (false), bus (SFX), priority (128, 0=lowest, 255=highest), spatial (false). Runtime: is_playing, voice_handle.
- [ ] 3.2 Create `runtime/engine/audio/components/audio_source.cpp` — Play(): acquire voice from pool, submit AudioClip PCM via XAUDIO2_BUFFER. Stop(): stop voice, release to pool. Pause()/Resume(). SetVolume/SetPitch at runtime.
- [ ] 3.3 Create `runtime/engine/audio/components/audio_listener.h` — CAudioListener: tag component (no data). Marks entity whose TransformComponent position is used as the listener position for 3D audio. First entity with CAudioListener is the active listener.

## 4. Fire-and-Forget API

- [ ] 4.1 Add AudioEngine::PlayOneShot(clip_path, volume, bus) — acquires voice, submits buffer, auto-releases on completion. No entity needed.
- [ ] 4.2 Add AudioEngine::PlayAt(clip_path, position, volume, bus) — same as PlayOneShot but with 3D position (spatial processing deferred to phase4-4.4.1).

## 5. Scene Serialization

- [ ] 5.1 Add CAudioSource to ComponentSerializerRegistry — JSON: clip_path, volume, pitch, loop, play_on_start, bus, priority, spatial.
- [ ] 5.2 Add CAudioListener serialization — simple tag component, JSON: { "audio_listener": true }.

## 6. Lua Audio API

- [ ] 6.1 Create `runtime/engine/scripting/api/lua_audio.h/.cpp` — Register `uz.audio` in LuaVM. `uz.audio.play(path, opts)` → fire-and-forget. `uz.audio.stop(handle)`. `uz.audio.set_bus_volume(bus, vol)`. `uz.audio.get_bus_volume(bus)`.
- [ ] 6.2 Add entity-based Lua audio — `uz.audio.play_on(entity, path, opts)`, `uz.audio.stop_on(entity)`, `uz.audio.is_playing(entity)`.

## 7. Testing

- [ ] 7.1 Unit tests for AudioEngine — init/shutdown, voice pool acquire/release, bus volume.
- [ ] 7.2 Unit tests for AudioClip — load .uzaudio, validate metadata, PCM data integrity.
- [ ] 7.3 Unit tests for AudioImporter — WAV import round-trip, OGG decode.
- [ ] 7.4 Unit tests for CAudioSource — play/stop/pause lifecycle, loop behavior.
- [ ] 7.5 Integration test: play sound, wait for completion, verify voice returned to pool.

## 8. Demo

- [ ] 8.1 Create `demos/audio_basic/` — scene with ambient music (looping, Music bus) + collision sound effects (SFX bus). Headless validation of voice pool and bus routing.

## 9. Documentation

- [ ] 9.1 Update `docs/engine/17-audio/` with XAudio2 engine, voice pool, bus architecture.
- [ ] 9.2 Add `uz.audio` to `docs/lua-api-reference.md`.
- [ ] 9.3 Document .uzaudio format and import pipeline.
