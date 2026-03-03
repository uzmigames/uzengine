# Proposal: phase2-audio-bus-dynamic-graph

## Why

The current audio system has a static 4-bus layout (Master, Music, SFX, Voice) that is fixed at compile time. Adding a new bus requires code changes. Bus routing cannot be changed at runtime — a "Reverb Wet" send bus, a "Radio Filter" effect bus, or a "Cut Scene Music" bus cannot be added without recompiling the engine.

Professional audio design requires a flexible, data-driven bus graph where designers add buses in `.uzproj` without programmer involvement.

## What Changes

- Bus graph is a directed acyclic graph (DAG): any bus can send to any parent bus
- Bus configuration loaded from `.uzproj` `[[audio.buses]]` TOML array at startup
- `IAudioEngine::reload_config(AudioBusConfig)` reloads the bus graph without restarting voices
- New bus types: `submix` (sum of children), `send` (parallel copy), `aux` (side chain)
- Runtime bus creation/deletion via IPC: `audio.add_bus`, `audio.remove_bus`, `audio.route_bus`
- XAudio2 implementation uses `IXAudio2SubmixVoice` chain

## Impact

- Affected specs: `docs/specs/audio/audio-abstraction-layer.md`
- Affected code: XAudio2 backend, `AudioBusConfig`, `.uzproj` parser
- Breaking change: NO (backwards-compatible, existing 4-bus config still works)
- User benefit: Designers add/remove buses without code changes; complex audio routing (reverb sends, sidechain compression)
