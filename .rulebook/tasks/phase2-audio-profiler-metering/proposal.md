# Proposal: phase2-audio-profiler-metering

## Why

Audio performance issues are invisible without profiling. There is currently no way to know how many voices are playing, what percentage of the voice pool is consumed, how much CPU the DSP graph is using, or whether voice stealing is occurring. Game audio engineers cannot diagnose issues like "why does music cut out in combat?" or "why does the audio lag spike every 2 seconds?"

This is table-stakes functionality for any production audio tool — every commercial game engine (Unreal, Unity, Wwise) provides real-time audio metering.

## What Changes

- `AudioProfilerStats` struct: active voices, virtual voices, max pool, DSP CPU ms, steal events/sec, 8 bus peak levels, master peak
- `IAudioEngine::get_stats()` method returning stats per-frame
- XAudio2 implementation reading `IXAudio2::GetPerformanceData()`
- VSCode extension Audio panel: voice pool bar, per-bus VU meters, steal rate graph
- IPC handler `audio.get_profiler_stats` → stats JSON
- Tracy integration: audio stats uploaded as frame markers

## Impact

- Affected specs: `docs/specs/audio/audio-abstraction-layer.md`, `docs/specs/audio/audio-system.md`
- Affected code: XAudio2 backend, IPC handlers, VSCode extension audio panel
- Breaking change: NO (additive — new stats API)
- User benefit: Real-time audio performance visibility; diagnose voice stealing, DSP overload, bus imbalances
