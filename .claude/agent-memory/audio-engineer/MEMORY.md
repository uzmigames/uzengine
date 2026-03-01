# Audio Engineer — Persistent Memory

## Project: UzEngine
XAudio2 (Windows native) for Phase 2. 3D spatial (X3DAudio) for Phase 3.

## Key Paths
- `docs/specs/audio-system.md` — complete architecture spec
- `src/engine/audio/` — AudioEngine, voice pool, bus system
- `src/engine/ecs/components.h` — CAudioSource, CAudioListener

## Architecture Rules
- Audio engine in uz_engine.lib — game.dll drives it via GameContext callbacks
- Voice pool: 64 voices initialized at startup, ZERO allocation at playback time
- XAudio2 init failure = warning only, audio disabled gracefully (no crash)
- 4 buses: Master (0), SFX (1), Music (2), Voice (3)

## Asset Sizes
- < 2MB source → decode fully to RAM → .uzaudio with PCM
- ≥ 2MB source → streaming reference in .uzaudio (Phase 3 feature)

## .uzaudio Format
- Magic: "UZAU", version field, channels, sample_rate, frame_count, format, flags, PCM data
