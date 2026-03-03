## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-audio-event-system` is complete — `AudioEventRegistry`, `trigger()` API work
- [ ] 1.2 Confirm `phase2-audio-dsp-chains` is complete — bus effect chains available for music bus
- [ ] 1.3 Read `docs/specs/audio-system.md` — understand bus architecture, voice pool, XAudio2 submix graph

## 2. MusicTrack Data Struct

- [ ] 2.1 Create `src/engine/audio/music_track.h`
- [ ] 2.2 Define `MusicStem` struct: `name: string`, `asset_path: string` (looping audio clip), `default_volume: float` (0–1)
- [ ] 2.3 Define `MusicTrack` struct: `name`, `bpm: float`, `bars_per_loop: int`, `stems: vector<MusicStem>`, `intro_path?: string`, `outro_path?: string`
- [ ] 2.4 Define `MusicTrackDesc` file format (INI-like via data-driven framework, or TOML subsection in `.uzproj`)

## 3. MusicClock

- [ ] 3.1 Create `MusicClock` class in `src/engine/audio/music_track.cpp`
- [ ] 3.2 Implement `MusicClock::start(bpm, bars_per_loop)` — initializes beat accumulator
- [ ] 3.3 Implement `MusicClock::update(dt)` — advance beat position: `beat_position += dt * (bpm / 60.0)`
- [ ] 3.4 Implement `MusicClock::get_beat_position() -> float` — current position within bar
- [ ] 3.5 Implement `MusicClock::get_next_bar_time_s() -> float` — seconds until next bar boundary
- [ ] 3.6 Implement `MusicClock::get_next_beat_time_s() -> float` — seconds until next beat
- [ ] 3.7 Implement quantize values: `OnBeat`, `OnBar`, `OnSection` (4 bars), `Immediate`

## 4. AdaptiveMusicSystem

- [ ] 4.1 Create `src/engine/audio/adaptive_music_system.h/cpp`
- [ ] 4.2 Implement `AdaptiveMusicSystem::play(track_name)`:
  - Load `MusicTrack` from registry
  - Create one XAudio2 voice per stem (loop-enabled)
  - Set all stems to their `default_volume`
  - Start `MusicClock`
- [ ] 4.3 Implement `AdaptiveMusicSystem::stop(fade_out_s)` — fade out all stem volumes, destroy voices after fade
- [ ] 4.4 Implement `AdaptiveMusicSystem::set_stem(stem_name, volume, fade_s)` — crossfade stem to target volume
- [ ] 4.5 Implement `AdaptiveMusicSystem::transition_to(track_name, quantize)`:
  - Wait until next quantize boundary (using `MusicClock`)
  - Start outro of current track (if defined)
  - On outro complete (or at boundary): crossfade to new track intro or loop
- [ ] 4.6 Implement `AdaptiveMusicSystem::update(dt)`:
  - Advance `MusicClock`
  - Process pending quantized transitions
  - Update in-progress stem fades
- [ ] 4.7 Call `update(dt)` from `AudioSystem::update(dt)`

## 5. Stem Crossfade

- [ ] 5.1 Implement `StemFade` struct: `stem_name`, `from_volume`, `to_volume`, `duration_s`, `elapsed_s`
- [ ] 5.2 Implement fade using `IXAudio2SourceVoice::SetVolume` per frame
- [ ] 5.3 Use linear fade (can extend to logarithmic for perceptually smooth transition)
- [ ] 5.4 Ensure simultaneous stem fades don't conflict (last set_stem wins per stem name)

## 6. Lua API

- [ ] 6.1 Implement `ctx.music:play(track_name)` — start playing a registered track
- [ ] 6.2 Implement `ctx.music:stop(fade_s?)` — stop with optional fade
- [ ] 6.3 Implement `ctx.music:transition_to(track_name, quantize?)` — quantize: `"beat"`/`"bar"`/`"section"`/`"immediate"`, default `"bar"`
- [ ] 6.4 Implement `ctx.music:set_stem(stem_name, volume, fade_s?)` — fade stem in/out
- [ ] 6.5 Implement `ctx.music:get_stem_volume(stem_name) -> float`
- [ ] 6.6 Implement `ctx.music:get_beat_position() -> float` — 0.0–bars_per_loop
- [ ] 6.7 Implement `ctx.music:on_bar(fn)` — callback fired every bar boundary (for sync effects)
- [ ] 6.8 Implement `ctx.music:on_beat(fn)` — callback fired every beat

## 7. Track Registry and Hot-Reload

- [ ] 7.1 Create `MusicTrackRegistry` singleton — `register(name, MusicTrack)`, `get(name)`
- [ ] 7.2 Load tracks from `.uzproj` `[music.tracks]` section at init
- [ ] 7.3 Hot-reload track definition: update registry; active track continues with existing voices

## 8. Testing

- [ ] 8.1 Unit test: `MusicClock::update` — 120 BPM, after 0.5s, beat_position ≈ 1.0 (one beat)
- [ ] 8.2 Unit test: `get_next_bar_time_s` — accurate within ±1ms for common BPM values
- [ ] 8.3 Unit test: `set_stem("drums", 0.0, 1.0)` — fade completes over 1 second of updates
- [ ] 8.4 Unit test: `transition_to(track, "bar")` — waits until bar boundary before switching
- [ ] 8.5 Unit test: `on_bar` callback fires at each bar for 4 bars
- [ ] 8.6 Integration test: play 2-stem track (melody + drums); mute melody via `set_stem("melody", 0.0)`; only drums audible

## 9. Documentation

- [ ] 9.1 Update `docs/specs/audio-system.md` — "Adaptive Music" section with track format, Lua API, quantize modes
- [ ] 9.2 Create `docs/specs/adaptive-music.md` — full system spec with stem diagram, transition state machine
- [ ] 9.3 Update `docs/guides/lua-scripting-guide.md` — "Adaptive Music" section with combat/exploration example
