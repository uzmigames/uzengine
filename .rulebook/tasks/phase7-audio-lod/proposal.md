# Proposal: phase3-audio-lod

## Why
Distant sound sources (100 m or more away) are currently processed with the same full DSP quality as nearby sources — full spatial panning, reverb sends, high-quality resampling, and LPF chains. In scenes with many simultaneous sound sources (combat arenas, crowds, ambient environments) this wastes significant CPU time on voices the player can barely hear. Audio LOD scales down DSP complexity proportionally to listener distance, keeping CPU budgets predictable regardless of source count.

## What Changes
- Four LOD tiers defined for `CAudioSource`:
  - LOD 0 (0–20 m): full quality — X3DAudio panning, reverb send, full DSP chain, high-quality resampling
  - LOD 1 (20–50 m): reduced reverb send level, simplified DSP chain
  - LOD 2 (50–100 m): mono downmix, no DSP, basic distance attenuation only
  - LOD 3 (>100 m): virtual voice — source tracked in the scheduler but no XAudio2 voice slot allocated
- Distance thresholds configurable per `CAudioSource` component or overridden globally in `audio_config.uzini`
- LOD tier recomputed each frame for active sources; transition between tiers uses a short crossfade (50 ms) to avoid audible pops
- Virtual voice promotion: when a LOD 3 source moves within range it claims a voice slot on the next scheduler tick
- `debug.audio_lod_overlay` console command renders LOD tier color coding over scene sources in the editor viewport

## Impact
- Affected specs: `docs/specs/audio-system.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/audio/`, `src/components/audio_source.h`
- Breaking change: NO
- User benefit: Scenes with large numbers of simultaneous sound sources maintain stable CPU usage; developers gain a tunable budget control without manually silencing distant sources in Lua.
