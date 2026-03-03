## 1. Interface Definition

- [ ] 1.1 Create `src/engine/audio/audio_engine.h` defining the `IAudioEngine` pure virtual C++ interface with methods: `create_source()`, `destroy_source()`, `play()`, `stop()`, `pause()`, `set_volume()`, `set_pitch()`, `set_position()`, `set_velocity()`, `set_listener_transform()`, `get_bus_config()`, `set_bus_volume()`, and `get_profiling_stats()`
- [ ] 1.2 Define `AudioSourceHandle` as a typed integer ID (`uint32_t`) in `src/engine/audio/audio_types.h` to uniquely identify live audio sources across backends
- [ ] 1.3 Define `AudioBusConfig` struct in `src/engine/audio/audio_types.h` with fields: `name` (string), `volume` (float), `parent_bus` (string), `output_device` (string), and `send_levels` (float array for reverb/DSP sends)
- [ ] 1.4 Define `AudioProfilingStats` struct in `src/engine/audio/audio_types.h` with fields: `active_sources`, `playing_sources`, `peak_voice_count`, `cpu_load_pct`, and `memory_used_bytes`
- [ ] 1.5 Define `AudioBackendType` enum in `src/engine/audio/audio_types.h` with values: `XAUDIO2`, `NULL_BACKEND`, `FMOD` (reserved), `WWISE` (reserved)

## 2. NullAudioEngine Implementation

- [ ] 2.1 Create `src/engine/audio/null_audio_engine.h` declaring `NullAudioEngine : public IAudioEngine` — a concrete implementation with no-op bodies for all interface methods
- [ ] 2.2 Implement `NullAudioEngine` in `src/engine/audio/null_audio_engine.cpp` tracking a monotonically incrementing `AudioSourceHandle` counter so `create_source()` returns unique handles without any hardware dependency
- [ ] 2.3 Implement minimal state tracking in `NullAudioEngine`: maintain an `std::unordered_map<AudioSourceHandle, bool>` for play/stop state so tests can assert `is_playing()` returns the correct value without audio hardware
- [ ] 2.4 Implement `NullAudioEngine::get_profiling_stats()` returning zeroed `AudioProfilingStats` populated only with the count of currently allocated source handles

## 3. Backend Factory and Startup Selection

- [ ] 3.1 Create `src/engine/audio/audio_engine_factory.h` and `src/engine/audio/audio_engine_factory.cpp` implementing `AudioEngineFactory::create(AudioBackendType)` returning `std::unique_ptr<IAudioEngine>`
- [ ] 3.2 Add `.uzproj` `[audio]` TOML section parsing in the project loader (`src/engine/core/project_loader.cpp`) that reads the `backend` key (e.g. `backend = "xaudio2"`) and maps it to `AudioBackendType`
- [ ] 3.3 When no audio device is detected at startup (HRESULT failure from XAudio2 creation), automatically fall back to `NullAudioEngine` and emit a warning via `UZ_LOG_WARN` instead of crashing
- [ ] 3.4 Expose the active backend type via `IAudioEngine::get_backend_type()` so IPC and profiler panels can display which backend is running

## 4. AudioBusConfig TOML Format

- [ ] 4.1 Define the `.uzproj` `[audio.buses]` TOML format spec in `docs/specs/audio/audio-abstraction-layer.md`: each bus is a TOML table with keys `volume`, `parent`, `output_device`, and `reverb_send`
- [ ] 4.2 Implement `AudioBusConfig` loading from TOML in `src/engine/audio/audio_bus_loader.cpp`, parsing the `[audio.buses]` array and constructing `AudioBusConfig` structs for each entry
- [ ] 4.3 Validate bus configs on load: detect cycles in the `parent` chain and emit `UZ_LOG_ERROR` with the cycle path if found; fall back to a single master bus in that case

## 5. Spec Documentation

- [ ] 5.1 Write `docs/specs/audio/audio-abstraction-layer.md` covering: interface rationale, `IAudioEngine` method signatures with parameter descriptions, `AudioBusConfig` TOML format with example, backend selection at startup, and testing strategy
- [ ] 5.2 Update `docs/specs/audio/audio-system.md` to reference `IAudioEngine` as the integration contract, noting that XAudio2 will be wrapped in `XAudio2AudioEngine` during Phase 2 audio system implementation

## 6. Testing

- [ ] 6.1 Create `tests/audio/test_null_audio_engine.cpp` with test fixture instantiating `NullAudioEngine` directly (no COM, no hardware)
- [ ] 6.2 Test `create_source()` returns a non-zero handle and each successive call returns a unique handle (verify 100 sequential calls produce 100 distinct handles)
- [ ] 6.3 Test `play()` followed by `is_playing()` returns `true`; `stop()` followed by `is_playing()` returns `false`
- [ ] 6.4 Test `destroy_source()` with a valid handle succeeds silently; test with an invalid handle does not crash or assert
- [ ] 6.5 Test `set_volume()` clamps values: input `-1.0f` stores `0.0f`; input `2.0f` stores `1.0f`; verify via `get_volume()`
- [ ] 6.6 Test `get_profiling_stats()` after creating 5 sources returns `active_sources == 5`; after destroying 2 returns `active_sources == 3`
- [ ] 6.7 Create `tests/audio/test_audio_engine_factory.cpp` testing `AudioEngineFactory::create(AudioBackendType::NULL_BACKEND)` returns a non-null pointer that satisfies the `IAudioEngine` interface
- [ ] 6.8 Test `AudioEngineFactory::create(AudioBackendType::NULL_BACKEND)` on a thread with no COM initialization succeeds (validates headless CI compatibility)
- [ ] 6.9 Create `tests/audio/test_audio_bus_loader.cpp` testing valid TOML parsing produces correct `AudioBusConfig` structs with expected field values
- [ ] 6.10 Test bus cycle detection: TOML with `busA.parent = "busB"` and `busB.parent = "busA"` triggers the cycle error log and falls back to master bus
- [ ] 6.11 Run coverage — all new files must reach >= 95% line and branch coverage before marking complete
