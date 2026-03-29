# Proposal: phase4-4.4.0-audio-core

## Why
An engine without audio cannot demo gameplay. Sound feedback is essential for immersion — footsteps, collisions, UI clicks, ambient music. XAudio2 is the Windows-native audio API with excellent performance and built-in spatial audio support via X3DAudio. This task covers the foundational audio pipeline: engine initialization, voice pool, asset import, 2D playback, and bus mixing.

## What Changes
- XAudio2 engine initialization with mastering voice and 64-voice pool
- CAudioSource ECS component for per-entity sound playback
- CAudioListener tag component (typically on camera/player entity)
- AudioClip asset format (.uzaudio) with PCM data and metadata
- WAV/OGG import pipeline via stb_vorbis
- 4-bus mixing hierarchy: Master → SFX / Music / Voice
- Fire-and-forget PlayAt() API for one-shot sounds
- Basic Lua audio API: uz.audio.play, uz.audio.stop, uz.audio.set_volume

## Impact
- New directory: `runtime/engine/audio/`
- New thirdparty: stb_vorbis (header-only, already in stb collection)
- Windows dependency: XAudio2 (system library, no vcpkg needed)
- Affected specs: audio-system.md, audio-abstraction-layer.md
- Breaking change: NO
- Performance target: <0.5ms audio update per frame with 64 active voices
