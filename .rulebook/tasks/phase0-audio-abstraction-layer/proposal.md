# Proposal: phase0-audio-abstraction-layer

## Why

XAudio2 is hard-coded throughout the audio system. Every audio call directly touches COM objects, requiring a real Windows audio device. This blocks three critical capabilities:
1. **Unit testing**: No test can run without a physical audio device or COM initialization
2. **CI/headless pipelines**: GitHub Actions containers have no audio hardware
3. **Middleware integration**: Switching to FMOD or Wwise requires rewriting every call site

Without this abstraction, the Phase 2 audio system will be permanently untestable and permanently Windows-locked. The abstraction layer must be designed before Phase 2 audio implementation begins, or all Phase 2 audio code will need to be rewritten.

## What Changes

- Define `IAudioEngine` pure virtual C++ interface with `create_source()`, `play()`, `stop()`, spatial audio, bus control, and profiling stats
- Implement `NullAudioEngine` — no-ops for all calls, minimal state tracking, usable in unit tests without any hardware
- Define `AudioBusConfig` struct and `.uzproj` `[audio.buses]` TOML format
- Design backend selection at engine startup from project config (`backend = "xaudio2"`)
- Existing XAudio2 code will be wrapped into `XAudio2AudioEngine` in Phase 2
- Full spec at `docs/specs/audio/audio-abstraction-layer.md`

## Impact

- Affected specs: `docs/specs/audio/audio-abstraction-layer.md`, `docs/specs/audio/audio-system.md`
- Affected code: Phase 2 audio system (will be implemented against `IAudioEngine`, not XAudio2 directly)
- Breaking change: NO (design only in Phase 0)
- User benefit: Unit-testable audio system; CI pipelines work without audio hardware; supports FMOD/Wwise in Phase 4
