## 1. AudioProfilerStats Struct
- [ ] 1.1 Define `AudioProfilerStats` struct in `src/audio/audio_profiler.h` with fields: `uint32_t active_voices`, `uint32_t virtual_voices`, `uint32_t max_voice_pool`, `double dsp_cpu_ms`, `uint32_t steal_events_per_sec`, `float bus_peak_levels[8]`, `float master_peak`
- [ ] 1.2 Initialize all fields to zero in a default constructor in `src/audio/audio_profiler.h`
- [ ] 1.3 Add a `uint64_t frame_index` field to `AudioProfilerStats` so callers can detect stale data

## 2. IAudioEngine Interface Extension
- [ ] 2.1 Add `get_stats(AudioProfilerStats* out_stats)` pure virtual method to `IAudioEngine` in `src/audio/audio_engine.h`
- [ ] 2.2 Add `reset_steal_counter()` method to `IAudioEngine` to reset `steal_events_per_sec` between measurement windows

## 3. XAudio2 Backend Implementation
- [ ] 3.1 Implement `AudioEngineXAudio2::get_stats()` in `src/audio/audio_engine_xaudio2.cpp` using `IXAudio2::GetPerformanceData()` to populate `active_voices`, `virtual_voices`, and `dsp_cpu_ms`
- [ ] 3.2 Populate `max_voice_pool` from the configured voice pool limit constant
- [ ] 3.3 Track voice steal events in the voice manager: increment an atomic counter each time a voice is stolen; read and reset it in `get_stats()` to produce `steal_events_per_sec`
- [ ] 3.4 Populate `bus_peak_levels[8]` by calling `IXAudio2SubmixVoice::GetOutputFilterParameters` or by tracking peak amplitude in the DSP callback for each bus (up to 8 buses)
- [ ] 3.5 Populate `master_peak` from the master bus peak level

## 4. IPC Handler
- [ ] 4.1 Add IPC handler `audio.get_profiler_stats` in `src/common/ipc_audio_handlers.cpp` (or existing IPC handler file) that calls `IAudioEngine::get_stats()` and returns a JSON object matching all `AudioProfilerStats` fields
- [ ] 4.2 Ensure the IPC handler returns within one frame (< 1ms) and does not allocate on the audio thread

## 5. VSCode Audio Panel
- [ ] 5.1 Create `vscode-uzengine/src/panels/AudioProfilerPanel.ts` with an `AudioProfilerPanel` class implementing a webview panel
- [ ] 5.2 Implement a 100ms polling loop that calls `audio.get_profiler_stats` via IPC and updates the panel with the latest `AudioProfilerStats`
- [ ] 5.3 Render a voice pool utilization bar: filled bar showing `active_voices / max_voice_pool` ratio with color threshold (green < 50%, yellow 50–80%, red > 80%)
- [ ] 5.4 Render per-bus VU meters for all 8 `bus_peak_levels` using a vertical bar chart with dBFS scale
- [ ] 5.5 Render a steal rate graph showing `steal_events_per_sec` over the last 60 seconds as a line chart
- [ ] 5.6 Display `dsp_cpu_ms` as a numeric readout with a colored threshold (green < 2ms, yellow 2–5ms, red > 5ms)
- [ ] 5.7 Register the panel as VSCode command `uzengine.openAudioProfiler` in `vscode-uzengine/package.json` `contributes.commands`

## 6. Tracy Integration
- [ ] 6.1 Add Tracy frame marker upload in `src/audio/audio_engine_xaudio2.cpp` at end-of-frame: call `TracyPlot("audio_active_voices", stats.active_voices)`, `TracyPlot("audio_dsp_ms", stats.dsp_cpu_ms)`, and `TracyPlot("audio_steals_per_sec", stats.steal_events_per_sec)`

## 7. Testing
- [ ] 7.1 Create `tests/core/test_audio_profiler.cpp` with Google Test suite `AudioProfilerTest`
- [ ] 7.2 Write test `StatsStructDefaultsToZero`: create a default `AudioProfilerStats`; assert all numeric fields are 0
- [ ] 7.3 Write test `GetStatsReturnsValidPoolSize`: call `get_stats()` on a freshly initialized engine; assert `max_voice_pool > 0`
- [ ] 7.4 Write test `ActiveVoicesIncreasesOnPlay`: call `play_sound()` to start a voice; call `get_stats()`; assert `active_voices >= 1`
- [ ] 7.5 Write test `StealCounterIncrements`: fill the voice pool to capacity; call `play_sound()` one more time; call `get_stats()`; assert `steal_events_per_sec >= 1`
- [ ] 7.6 Write test `ResetStealCounterClearsCount`: call `reset_steal_counter()`; call `get_stats()`; assert `steal_events_per_sec == 0`
- [ ] 7.7 Write test `MasterPeakIsNonZeroWhenPlaying`: start a voice; call `get_stats()`; assert `master_peak > 0.0f`
- [ ] 7.8 Create `vscode-uzengine/src/__tests__/AudioProfilerPanel.test.ts` with Jest test suite `AudioProfilerPanelTests`
- [ ] 7.9 Write test `PanelPollsEvery100ms`: mock IPC; assert `audio.get_profiler_stats` is called at least once per 100ms over a 250ms simulated interval
- [ ] 7.10 Write test `StatsJsonParsedToModel`: feed mock JSON `{active_voices:5, max_voice_pool:32, dsp_cpu_ms:1.5, master_peak:0.8}`; assert panel model fields match
- [ ] 7.11 Write test `CommandRegisteredInPackageJson`: parse `package.json` and assert `uzengine.openAudioProfiler` is in `contributes.commands`
- [ ] 7.12 Verify C++ line coverage >= 95% and branch coverage >= 95% for `audio_engine_xaudio2.cpp` stats paths
- [ ] 7.13 Verify Jest line coverage >= 95% for `AudioProfilerPanel.ts`

## 8. Documentation
- [ ] 8.1 Update `docs/specs/audio/audio-abstraction-layer.md` with the `AudioProfilerStats` struct, `IAudioEngine::get_stats()` API, and `audio.get_profiler_stats` IPC schema
- [ ] 8.2 Update `docs/specs/audio/audio-system.md` with the VSCode Audio Profiler panel workflow and how to interpret voice pool utilization and steal rate
