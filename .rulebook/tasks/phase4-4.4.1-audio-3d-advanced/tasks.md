## 1. 3D Spatial Audio (X3DAudio)

- [ ] 1.1 Create `runtime/engine/audio/audio_3d.h/.cpp` — Audio3D: Init(channel_count), CalculateDSP(listener_pos, listener_forward, listener_up, emitter_pos, emitter_velocity) → X3DAUDIO_DSP_SETTINGS. Wraps X3DAudioInitialize + X3DAudioCalculate. Flags: X3DAUDIO_CALCULATE_MATRIX | DOPPLER | LPF_DIRECT.
- [ ] 1.2 Add listener tracking to AudioSubsystem — OnTick: find first entity with CAudioListener, read TransformComponent position/forward/up. Cache as current listener state.
- [ ] 1.3 Add spatial update to CAudioSource — For each playing spatial source: read entity TransformComponent → compute X3DAudio DSP → apply output matrix, volume, LPF to IXAudio2SourceVoice. Skip if distance > attenuation_max (voice culling).
- [ ] 1.4 Add attenuation parameters — CAudioSource: attenuation_min (1.0), attenuation_max (100.0), attenuation_model enum (InverseSquare, Linear, Custom). X3DAudio CurveDistance setup.
- [ ] 1.5 Add Doppler parameters — CAudioSource: doppler_factor (1.0). Track previous position for velocity calculation. Apply via X3DAudio Doppler ratio.

## 2. Audio Streaming

- [ ] 2.1 Create `runtime/engine/audio/audio_stream.h/.cpp` — AudioStream: ring buffer (64KB), background decode thread. Open(path), Read(buffer, size) → decoded PCM chunk. Double-buffered XAUDIO2_BUFFER submission via IXAudio2VoiceCallback::OnBufferEnd.
- [ ] 2.2 Add streaming flag to AudioClip — If decoded PCM > 2MB, auto-enable streaming. AudioClip::IsStreaming(). Streamed clips don't store full PCM in memory.
- [ ] 2.3 Create IXAudio2VoiceCallback — StreamCallback: OnBufferEnd triggers next chunk submission. OnStreamEnd for looping restart. OnVoiceError for error handling.

## 3. Audio Hot-Reload

- [ ] 3.1 Add .uzaudio to FileWatcher — When source WAV/OGG changes, re-import via AudioImporter. When .uzaudio changes, reload PCM data.
- [ ] 3.2 Implement hot-swap — Replace AudioClip PCM buffer in-place. Playing voices restart from current position with new data. No voice pool disruption.

## 4. Audio Occlusion

- [ ] 4.1 Create `runtime/engine/audio/audio_occlusion.h/.cpp` — AudioOcclusion: for each spatial source, cast ray from listener → source via PhysicsQueries::Raycast. If blocked, compute occlusion factor (0.0=clear, 1.0=fully occluded). Multi-ray for partial occlusion (3 rays: center + 2 offsets).
- [ ] 4.2 Apply occlusion to voice — XAPO LPF filter frequency scaled by occlusion factor. Volume reduced by occlusion * 0.7. Smooth transition over 100ms to avoid clicks.

## 5. Audio Events

- [ ] 5.1 Create `runtime/engine/audio/audio_event.h/.cpp` — AudioEvent: clips (vector<string>), pitch_range (Vec2, {0.9, 1.1}), volume_range (Vec2, {0.8, 1.0}), mode (Random, Sequential, Weighted). Play(): select clip, randomize pitch/volume, fire. No-repeat logic for Random mode.
- [ ] 5.2 Create .uzevent file format — JSON: { "clips": [...], "pitch_range": [0.9, 1.1], "volume_range": [0.8, 1.0], "mode": "random" }. Loaded by asset system.

## 6. Lua API Extensions

- [ ] 6.1 Add 3D audio Lua API — `uz.audio.play_at(path, position, opts)` with spatial=true. `uz.audio.set_listener(entity)`.
- [ ] 6.2 Add audio event Lua API — `uz.audio.play_event(event_path, position)`.
- [ ] 6.3 Add occlusion control — `uz.audio.set_occlusion_enabled(bool)`.

## 7. Testing

- [ ] 7.1 Unit tests for Audio3D — DSP calculation, attenuation curves, Doppler.
- [ ] 7.2 Unit tests for AudioStream — ring buffer fill/drain, double-buffer submission.
- [ ] 7.3 Unit tests for AudioEvent — random selection no-repeat, sequential cycling, pitch/volume range.
- [ ] 7.4 Unit tests for AudioOcclusion — occluded vs clear path, partial occlusion.
- [ ] 7.5 Integration test: 3D sound moves around listener, verify panning changes.

## 8. Demo

- [ ] 8.1 Extend `demos/audio_basic/` — Add 3D positioned sounds, moving sound source (orbit), listener follows camera. Occlusion through wall.

## 9. Documentation

- [ ] 9.1 Update `docs/engine/17-audio/` with 3D audio, streaming, occlusion, events.
- [ ] 9.2 Document .uzevent format and audio event authoring.

## 10. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 10.1 Update or create documentation covering the implementation
- [ ] 10.2 Write tests covering the new behavior
- [ ] 10.3 Run tests and confirm they pass
