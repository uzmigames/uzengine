# Proposal: phase3-adaptive-music

## Why

Game music must react to gameplay state — switching from calm exploration to combat intensity, blending stems based on player health or tension, synchronizing transitions to the beat. A static music loop cannot provide this dynamic experience. An adaptive music system using stem-based audio — where individual instrument tracks (drums, bass, melody) are layered independently — gives designers precise control over musical tension without requiring custom code per game.

## What Changes

- **`src/engine/audio/music_track.h/cpp`** — `MusicTrack` data struct with stems, `MusicClock` BPM engine
- **`src/engine/audio/adaptive_music_system.h/cpp`** — manages active track, stem fades, quantized transitions
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.music:play(track)`, `ctx.music:set_stem(name, volume)`, `ctx.music:transition_to(track, quantize)`
- **Audio event integration** — trigger stems on/off via `AudioEventRegistry` patterns
- **Tests** — BPM clock accuracy, quantized transition timing, stem crossfade, Lua API

## Impact

- Affected specs: `docs/specs/audio-system.md` (adaptive music section)
- Dependencies: `phase2-audio-event-system` (stem trigger patterns), `phase2-audio-dsp-chains` (DSP on music bus)
- Breaking change: NO — additive; existing audio system unchanged
- User benefit: dynamic gameplay music without per-game audio programmer; AI can author music states from game events
